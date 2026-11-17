#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/game/game_data.hpp"
#include "../src/model/randomizer_world.hpp"
#include "../src/patches/slot_block.hpp"
#include "../src/randomizer_options.hpp"
#include "../src/patches/patch_new_game.hpp"
#include "../src/tools/exception.hpp"

namespace
{
    constexpr uint32_t EXE_SIZE = 0xB0000;
    constexpr uint32_t HEADER_SIZE = 0x800;
    constexpr uint32_t LOAD_ADDRESS = slot_block::USA_LOAD;

    constexpr uint32_t NEW_GAME_CALL = 0x12534;
    constexpr uint32_t NEW_GAME_RETURN = 0x80031D3C;

    constexpr uint32_t TABLE = 0x800CC004;
    constexpr uint32_t TABLE_END = 0x800CC16C;
    constexpr uint32_t LIFE_FRAME = 0x800FEFB4;
    constexpr uint32_t LIFE_FRAME_NEXT = 0x800FEFFC;
    constexpr uint32_t ENTRY_COUNT = (TABLE_END - TABLE) / 4;
    constexpr uint32_t EMPTY_ENTRY = 10;

    void check(bool condition, const std::string& message)
    {
        if(!condition)
            throw std::runtime_error(message);
    }

    Json options_json()
    {
        Json json;
        json["randomizerSettings"]["startingInventory"] = std::vector<std::string>{
                "Dagger", "Cloth Armor", "Short Boots"
        };
        return json;
    }

    void plant_exe(const std::filesystem::path& path, uint32_t resume_word)
    {
        BinaryFile bytes(std::vector<uint8_t>(EXE_SIZE, 0));
        bytes.set_long_le(0x18, LOAD_ADDRESS);
        bytes.set_long_le(PatchNewGame::RESUME_JUMP, resume_word);
        bytes.set_long_le(PatchNewGame::RESUME_JUMP + 4, PatchNewGame::RESUME_DELAY_WORD);
        bytes.save_as(path);
    }

    uint32_t jump_target(uint32_t word, uint32_t pc)
    {
        return (pc & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2);
    }

    uint32_t file_offset(const PsxExeFile& exe, uint32_t va)
    {
        const uint32_t load = exe.get_long_le(0x18);
        return HEADER_SIZE + (va - load);
    }

    // Run the injected resume copy. Branch delays in this copy are nops.
    void run_resume(const PsxExeFile& exe, uint32_t start)
    {
        std::map<uint32_t, uint32_t> mem;
        mem[TABLE_END] = 0x80110000;
        for(uint32_t index = 0; index < ENTRY_COUNT; ++index)
        {
            const uint32_t pointer = (index == EMPTY_ENTRY) ? 0xFFFFFFFFu : 0x80100000u + index * 0x40u;
            mem[TABLE + index * 4] = pointer;
        }

        uint32_t reg[32] = {};
        reg[2] = 0x36;
        reg[16] = 0x11111111;
        reg[17] = 0x22222222;
        reg[18] = 0x33333333;
        uint32_t pc = start;

        for(int step = 0; step < 8000 && pc != PatchNewGame::RESUME_JOIN; ++step)
        {
            const uint32_t here = pc;
            const uint32_t word = exe.get_long_le(file_offset(exe, pc));
            pc += 4;

            const uint32_t op = word >> 26;
            const uint32_t rs = (word >> 21) & 31u;
            const uint32_t rt = (word >> 16) & 31u;
            const uint32_t rd = (word >> 11) & 31u;
            const uint32_t fn = word & 63u;
            const int32_t simm = static_cast<int16_t>(word & 0xFFFFu);
            bool taken = false;
            uint32_t target = 0;

            if(word == 0)
            {
            }
            else if(op == 0 && fn == 0x2A)
            {
                const int32_t left = static_cast<int32_t>(reg[rs]);
                const int32_t right = static_cast<int32_t>(reg[rt]);
                reg[rd] = left < right ? 1u : 0u;
            }
            else if(op == 2)
            {
                taken = true;
                target = jump_target(word, here);
            }
            else if(op == 4 || op == 5)
            {
                taken = op == 4 ? reg[rs] == reg[rt] : reg[rs] != reg[rt];
                target = here + 4u + static_cast<uint32_t>(simm * 4);
            }
            else if(op == 9)
            {
                reg[rt] = reg[rs] + static_cast<uint32_t>(simm);
            }
            else if(op == 15)
            {
                reg[rt] = (word & 0xFFFFu) << 16;
            }
            else if(op == 35)
            {
                const uint32_t addr = reg[rs] + static_cast<uint32_t>(simm);
                check(mem.count(addr) == 1, "Shop-throw copy loaded an unknown address");
                reg[rt] = mem[addr];
            }
            else if(op == 43)
            {
                const uint32_t addr = reg[rs] + static_cast<uint32_t>(simm);
                mem[addr] = reg[rt];
            }
            else
            {
                throw std::runtime_error("Unexpected shop-throw instruction");
            }

            reg[0] = 0;
            if(!taken)
                continue;
            check(exe.get_long_le(file_offset(exe, pc)) == 0, "Shop-throw branch delay is not a nop");
            pc = target;
        }

        check(pc == PatchNewGame::RESUME_JOIN, "Memory card resume did not rejoin retail code");
        check(reg[2] == 0x36, "Memory card resume changed v0");
        check(reg[16] == 0x11111111 && reg[17] == 0x22222222 && reg[18] == 0x33333333,
              "Memory card resume changed a saved register");
        check(mem[0x80110000] == 0 && mem[0x80110004] == 0, "Shop-throw copy wrote past sprite 0x78");

        for(uint32_t index = 0; index < ENTRY_COUNT; ++index)
        {
            if(index == EMPTY_ENTRY)
                continue;
            const uint32_t object = 0x80100000u + index * 0x40u;
            check(mem[object] == LIFE_FRAME && mem[object + 4] == LIFE_FRAME_NEXT,
                  "Shop-throw copy missed a sprite record");
        }
    }

    void test_resume_copies_physics(const std::filesystem::path& dir, const GameData& game_data, RandomizerWorld& world)
    {
        const auto planted = dir / "resume.exe";
        plant_exe(planted, PatchNewGame::RESUME_JUMP_WORD);

        const RandomizerOptions options(options_json(), game_data, world);
        PsxExeFile exe(planted);
        PatchNewGame patch(options);
        patch.alter_exe_file(exe, game_data, world);
        const auto patched = dir / "resume-patched.exe";
        exe.save_as(patched);

        PsxExeFile saved(patched);
        const uint32_t expansion = LOAD_ADDRESS - saved.get_long_le(0x18);
        const uint32_t call = PatchNewGame::RESUME_JUMP + expansion;
        const uint32_t word = saved.get_long_le(call);
        check(saved.get_long_le(call + 4) == PatchNewGame::RESUME_DELAY_WORD, "Memory card resume delay slot changed");
        const uint32_t resume = jump_target(word, 0x80031D2C);
        check(resume != PatchNewGame::RESUME_JOIN, "Memory card resume still skips the shop-throw copy");

        bool saw_end = false;
        bool saw_store = false;
        bool saw_halfword_store = false;
        for(uint32_t va = resume; ; va += 4)
        {
            const uint32_t instruction = saved.get_long_le(file_offset(saved, va));
            if(instruction == 0x258CC16C)
                saw_end = true;
            if((instruction >> 26) == 43)
                saw_store = true;
            if((instruction >> 26) == 41)
                saw_halfword_store = true;
            if((instruction >> 26) == 2)
            {
                check(jump_target(instruction, va) == PatchNewGame::RESUME_JOIN, "Memory card resume joins the wrong address");
                break;
            }
            check(va - resume < 0x200, "Memory card resume copy did not join");
        }
        check(saw_end && saw_store, "Memory card resume copy does not write the Life Vessel words");
        check(!saw_halfword_store, "Memory card resume copy writes new-game flags");

        run_resume(saved, resume);

        const uint32_t new_game = jump_target(saved.get_long_le(NEW_GAME_CALL + expansion), 0x80031D34);
        bool new_game_copies = false;
        bool new_game_returns = false;
        for(uint32_t va = new_game; va - new_game < 0x2000; va += 4)
        {
            const uint32_t instruction = saved.get_long_le(file_offset(saved, va));
            if(instruction == 0x258CC16C)
                new_game_copies = true;
            if((instruction >> 26) == 2 && jump_target(instruction, va) == NEW_GAME_RETURN)
            {
                new_game_returns = true;
                break;
            }
        }
        check(new_game_copies && new_game_returns, "New game no longer copies shop-throw physics");
    }

    void test_resume_site_must_match(const std::filesystem::path& dir, const GameData& game_data, RandomizerWorld& world)
    {
        const auto planted = dir / "wrong-site.exe";
        plant_exe(planted, 0);

        const RandomizerOptions options(options_json(), game_data, world);
        PsxExeFile exe(planted);
        PatchNewGame patch(options);
        bool threw = false;
        try
        {
            patch.alter_exe_file(exe, game_data, world);
        }
        catch(const RandomizerException& error)
        {
            threw = std::string(error.what()).find("Memory card resume") != std::string::npos;
        }
        check(threw, "A changed memory-card resume jump was accepted");
    }
}

int main()
{
    try
    {
        const GameData game_data;
        RandomizerWorld world(game_data);
        const auto dir = std::filesystem::temp_directory_path() / "alundra-shop-throw-test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directory(dir);

        test_resume_copies_physics(dir, game_data, world);
        test_resume_site_must_match(dir, game_data, world);
        std::filesystem::remove_all(dir);
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    std::cout << "Shop throw tests passed" << std::endl;
    return 0;
}
