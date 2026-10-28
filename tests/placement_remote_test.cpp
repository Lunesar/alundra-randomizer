#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/constants/item_codes.hpp"
#include "../src/game/game_data.hpp"
#include "../src/io/placement_seed_map.hpp"
#include "../src/model/item_source.hpp"
#include "../src/model/randomizer_world.hpp"
#include "../src/patches/patch_apply_item_sources.hpp"
#include "../src/randomizer_options.hpp"
#include "../src/tools/binary_file.hpp"
#include "../src/tools/exception.hpp"
#include "../src/tools/json.hpp"
#include "../src/tools/psx_exe_file.hpp"

namespace
{
    constexpr uint32_t EXE_LOAD = 0x80020000;
    constexpr uint32_t EXE_SIZE = 0xB0000;
    constexpr uint32_t HEADER_SIZE = 0x800;

    constexpr uint32_t NAME_SITE_A = 0x5F21C;
    constexpr uint32_t NAME_SITE_B = 0x5F2E0;
    constexpr uint32_t NAME_SITE_C = 0x5F5BC;
    constexpr uint32_t SITE_A_PC = 0x8007EA1C;
    constexpr uint32_t SITE_B_PC = 0x8007EAE0;
    constexpr uint32_t SITE_C_PC = 0x8007EDBC;

    constexpr uint32_t SITE_A_WORDS[] = { 0x001380C0, 0x3C01800C, 0x00300821, 0x8C257DE4 };
    constexpr uint32_t SITE_B_WORDS[] = { 0x001310C0, 0x3C01800C, 0x00220821, 0x8C257DE4 };
    constexpr uint32_t SITE_C_WORDS[] = { 0x3C01800C, 0x00220821, 0x8C257DE4 };
    constexpr uint32_t SITE_C_NEXT = 0x3C108012;

    constexpr uint32_t SHOP_RECEIVE_CALL = 0x2117C;
    constexpr uint32_t SHOP_RECEIVE_JAL = 0x0C00D205;
    constexpr uint32_t SHOP_RECEIVE_DELAY = 0x00E08021;
    constexpr uint32_t SHOP_RECEIVE_PC = 0x8004097C;
    constexpr uint32_t CAN_RECEIVE = 0x80034814;

    constexpr uint32_t ITEM_INFO_2_FILE = 0xA85E4;
    constexpr uint32_t ITEM_QTY_FILE = 0x9C650;
    constexpr uint32_t SPRITE_TABLE = 0x830;
    constexpr uint32_t BOOK_OF_ELNA_SPRITE_RECORD = 0x0002A1B0;
    constexpr uint32_t BOOK_OF_ELNA_SPRITE_FILE = 0x800 + BOOK_OF_ELNA_SPRITE_RECORD;
    constexpr uint32_t REMOTE_SPRITE_HEADER = 0x387A0;
    constexpr uint32_t REMOTE_SPRITE_RECORD = REMOTE_SPRITE_HEADER - 0x800;
    constexpr uint32_t SPRITE_HEADER_SIZE = 0x20;
    constexpr uint32_t LOCAL_NAME_POINTER = 0x11112222;
    constexpr uint16_t CHEST_FLAG = 0x0680;
    constexpr uint16_t SHOP_PRICE = 4;
    constexpr uint32_t RETURN_SENTINEL = 0x80000000;
    constexpr uint32_t ENTITY_BASE = 0x80100000;
    constexpr uint32_t PARENT_BASE = 0x80200000;

    void check(bool condition, const std::string& message)
    {
        if(!condition)
            throw std::runtime_error(message);
    }

    std::string error_of(const std::function<void()>& action)
    {
        try
        {
            action();
        }
        catch(const std::exception& error)
        {
            return error.what();
        }
        return {};
    }

    void write_json(const std::filesystem::path& path, const Json& json)
    {
        std::ofstream output(path);
        check(static_cast<bool>(output), "Could not write " + path.string());
        output << json.dump(2);
    }

    ItemSource* source_by_id(RandomizerWorld& world, uint16_t id)
    {
        for(ItemSource* source : world.item_sources())
        {
            if(source->id() == id)
                return source;
        }
        throw std::runtime_error("Missing item source " + std::to_string(id));
    }

    uint32_t datas_limit(const RandomizerWorld& world)
    {
        uint32_t limit = std::max<uint32_t>(0x1EBF9CE, 0x1BA5D05);
        for(const ItemSource* source : world.item_sources())
        {
            for(uint32_t address : source->addresses())
                limit = std::max(limit, address);
            for(uint32_t address : source->sprite_addresses())
                limit = std::max(limit, address + 13);
            for(uint32_t address : source->price_addresses())
                limit = std::max(limit, address + 1);
        }
        return limit + 64;
    }

    void plant_hooks(BinaryFile& exe, uint8_t herbs_id)
    {
        exe.set_long_le(0x18, EXE_LOAD);
        for(size_t index = 0; index < 4; ++index)
        {
            exe.set_long_le(NAME_SITE_A + static_cast<uint32_t>(index * 4), SITE_A_WORDS[index]);
            exe.set_long_le(NAME_SITE_B + static_cast<uint32_t>(index * 4), SITE_B_WORDS[index]);
        }
        for(size_t index = 0; index < 3; ++index)
            exe.set_long_le(NAME_SITE_C + static_cast<uint32_t>(index * 4), SITE_C_WORDS[index]);
        exe.set_long_le(NAME_SITE_A + 16, 0x0C0211BA);
        exe.set_long_le(NAME_SITE_B + 16, 0x0C0211BA);
        exe.set_long_le(NAME_SITE_C + 12, SITE_C_NEXT);
        exe.set_long_le(SHOP_RECEIVE_CALL, SHOP_RECEIVE_JAL);
        exe.set_long_le(SHOP_RECEIVE_CALL + 4, SHOP_RECEIVE_DELAY);
        exe.set_word_le(ITEM_QTY_FILE + static_cast<uint32_t>(ITEM_REMOTE) * 10 + 6, 0xFFFF);
        exe.set_long_le(ITEM_INFO_2_FILE + static_cast<uint32_t>(herbs_id) * 8, LOCAL_NAME_POINTER);
    }

    void plant_shop_price(BinaryFile& datas, const ItemSource* shop)
    {
        for(uint32_t address : shop->price_addresses())
            datas.set_word_le(address, SHOP_PRICE);
        datas.set_long_le(SPRITE_TABLE + static_cast<uint32_t>(ITEM_REMOTE + 0x1E) * 4, 0xFFFFFFFF);
        datas.set_long_le(SPRITE_TABLE + static_cast<uint32_t>(ITEM_BOOK_OF_ELNA + 0x1E) * 4,
                          BOOK_OF_ELNA_SPRITE_RECORD);
        for(uint32_t index = 0; index < SPRITE_HEADER_SIZE; ++index)
            datas.set_byte(BOOK_OF_ELNA_SPRITE_FILE + index, static_cast<uint8_t>(0xA0 + index));
    }

    void plant_chest_flag(BinaryFile& datas, const ItemSource* chest)
    {
        check(!chest->addresses().empty(), "Remote chest has no contents address");
        const uint32_t address = chest->addresses().front();
        check(address > 0x12, "Chest contents address is too small to hold an entity record");
        const uint32_t entity = address - 0x12;

        // One map, one entity, contents byte at the source address, flag 0x0680.
        datas.set_long_le(0x28 + 4, 0x2000);
        datas.set_long_le(0x2000 + 12, 0x100);
        datas.set_long_le(0x2100, entity - 0x2100);
        datas.set_byte(entity + 2, 0x33);
        datas.set_byte(entity + 3, 0x3B);
        datas.set_word_le(entity + 16, CHEST_FLAG);
    }

    uint32_t jal_target(uint32_t instruction, uint32_t pc)
    {
        return (pc & 0xF0000000u) | ((instruction & 0x03FFFFFFu) << 2);
    }

    uint32_t file_offset_for_va(uint32_t va, uint32_t load)
    {
        return HEADER_SIZE + (va - load);
    }

    std::string read_cstr(const BinaryFile& file, uint32_t va, uint32_t load)
    {
        uint32_t offset = file_offset_for_va(va, load);
        std::string text;
        while(text.size() < 80)
        {
            const uint8_t byte = file.get_byte(offset++);
            if(byte == 0)
                return text;
            text.push_back(static_cast<char>(byte));
        }
        throw std::runtime_error("String at " + std::to_string(va) + " is not terminated");
    }

    uint32_t find_cstr(const BinaryFile& file, const std::string& text)
    {
        const std::string pattern = text + '\0';
        const auto found = std::search(file.begin(), file.end(), pattern.begin(), pattern.end());
        check(found != file.end(), "Disc is missing display string \"" + text + "\"");
        return static_cast<uint32_t>(std::distance(file.begin(), found));
    }

    struct LookupMachine
    {
        const BinaryFile& memory;
        uint32_t load = 0;
        uint32_t flag_word = 0;
        uint32_t reg[32] {};
        bool older_active = false;
        int older_reg = 0;
        uint32_t older_value = 0;
        bool newer_active = false;
        int newer_reg = 0;
        uint32_t newer_value = 0;

        void begin_instruction()
        {
            if(older_active && older_reg != 0)
                reg[older_reg] = older_value;
            older_active = newer_active;
            older_reg = newer_reg;
            older_value = newer_value;
            newer_active = false;
            reg[0] = 0;
        }

        void write_reg(int index, uint32_t value)
        {
            if(older_active && older_reg == index)
                older_active = false;
            if(index != 0)
                reg[index] = value;
            reg[0] = 0;
        }

        void queue_load(int index, uint32_t value)
        {
            newer_active = index != 0;
            newer_reg = index;
            newer_value = value;
        }

        [[nodiscard]] uint32_t read_word(uint32_t va) const
        {
            if((va & 3u) != 0)
                throw std::runtime_error("Unaligned lookup load");
            // Chest-open stores the parent pointer at +0x280. Flag 0 is a null
            // parent, so the lookup must miss without reading parent+0x40.
            if(va == ENTITY_BASE + 0x280)
                return flag_word == 0 ? 0u : PARENT_BASE;
            if(va == PARENT_BASE + 0x40)
                return flag_word;

            if(va < load)
                throw std::runtime_error("Lookup read outside the executable");
            const uint32_t offset = file_offset_for_va(va, load);
            if(offset + 4 > memory.size())
                throw std::runtime_error("Lookup read past the executable");
            return memory.get_long_le(offset);
        }
    };

    uint32_t run_lookup(const BinaryFile& memory, uint32_t load, uint32_t entry, uint32_t item_id, uint32_t flag_word)
    {
        LookupMachine cpu{ memory, load, flag_word, {} };
        cpu.reg[31] = RETURN_SENTINEL;
        cpu.reg[16] = 0x11111111;
        cpu.reg[17] = ENTITY_BASE;
        cpu.reg[18] = 0x22222222;
        cpu.reg[19] = item_id;
        for(int index = 20; index <= 23; ++index)
            cpu.reg[index] = 0x33330000u + static_cast<uint32_t>(index);
        const uint32_t saved_s[8] = {
            cpu.reg[16], cpu.reg[17], cpu.reg[18], cpu.reg[19],
            cpu.reg[20], cpu.reg[21], cpu.reg[22], cpu.reg[23]
        };

        uint32_t pc = entry;
        bool in_delay = false;
        bool taken = false;
        uint32_t dest = 0;
        for(int step = 0; step < 10000; ++step)
        {
            if(pc == RETURN_SENTINEL)
            {
                for(int index = 0; index < 8; ++index)
                    check(cpu.reg[16 + index] == saved_s[index], "Lookup clobbered a saved register");
                return cpu.reg[2];
            }

            cpu.begin_instruction();
            const uint32_t instr = cpu.read_word(pc);
            const uint32_t op = instr >> 26;
            const uint32_t rs = (instr >> 21) & 31u;
            const uint32_t rt = (instr >> 16) & 31u;
            const uint32_t rd = (instr >> 11) & 31u;
            const uint32_t sa = (instr >> 6) & 31u;
            const uint32_t funct = instr & 63u;
            const int32_t simm = static_cast<int16_t>(instr & 0xFFFFu);
            bool branch = false;
            bool this_taken = false;
            uint32_t this_dest = 0;

            auto arm = [&](bool condition, uint32_t target) {
                branch = true;
                this_taken = condition;
                this_dest = target;
            };

            if(op == 0)
            {
                if(funct == 0x00)
                    cpu.write_reg(static_cast<int>(rd), cpu.reg[rt] << sa);
                else if(funct == 0x08)
                    arm(true, cpu.reg[rs]);
                else if(funct == 0x20 || funct == 0x21)
                    cpu.write_reg(static_cast<int>(rd), cpu.reg[rs] + cpu.reg[rt]);
                else
                    throw std::runtime_error("Unsupported SPECIAL opcode in lookup");
            }
            else if(op == 0x01)
            {
                const bool condition = static_cast<int32_t>(cpu.reg[rs]) >= 0;
                const uint32_t target = pc + 4u + static_cast<uint32_t>(simm * 4);
                if(rt == 0x11)
                    cpu.write_reg(31, pc + 8);
                else if(rt != 0x01)
                    throw std::runtime_error("Unsupported REGIMM opcode in lookup");
                arm(condition, target);
            }
            else if(op == 0x04 || op == 0x05)
            {
                const bool equal = cpu.reg[rs] == cpu.reg[rt];
                arm(op == 0x04 ? equal : !equal, pc + 4u + static_cast<uint32_t>(simm * 4));
            }
            else if(op == 0x09)
            {
                cpu.write_reg(static_cast<int>(rt), cpu.reg[rs] + static_cast<uint32_t>(simm));
            }
            else if(op == 0x0F)
            {
                cpu.write_reg(static_cast<int>(rt), static_cast<uint32_t>(instr & 0xFFFFu) << 16);
            }
            else if(op == 0x23)
            {
                cpu.queue_load(static_cast<int>(rt), cpu.read_word(cpu.reg[rs] + static_cast<uint32_t>(simm)));
            }
            else if(instr != 0)
            {
                throw std::runtime_error("Unsupported lookup instruction");
            }

            check(!(branch && in_delay), "Lookup branches in a delay slot");
            if(in_delay)
            {
                pc = taken ? dest : pc + 4;
                in_delay = false;
            }
            else if(branch)
            {
                in_delay = true;
                taken = this_taken;
                dest = this_dest;
                pc += 4;
            }
            else
            {
                pc += 4;
            }
        }

        throw std::runtime_error("Lookup did not return");
    }

    // Shop opcode 0x82 calls this with the item id in a0. A remote id returns
    // 1 and must not reach the retail receive call. Any other id jumps there
    // with a0 unchanged. s0 belongs to the caller.
    uint32_t run_shop_receive(const BinaryFile& memory, uint32_t load, uint32_t entry, uint32_t item_id, bool& reached_retail)
    {
        uint32_t reg[32] {};
        reg[4] = item_id;
        reg[16] = 0x11111111;
        reg[31] = RETURN_SENTINEL;
        uint32_t pc = entry;
        bool in_delay = false;
        bool taken = false;
        uint32_t dest = 0;
        reached_retail = false;

        for(int step = 0; step < 32; ++step)
        {
            if(pc == CAN_RECEIVE || pc == RETURN_SENTINEL)
            {
                reached_retail = pc == CAN_RECEIVE;
                check(reg[4] == item_id, "Shop receive changed the item id");
                check(reg[16] == 0x11111111, "Shop receive clobbered s0");
                return reg[2];
            }

            const uint32_t instr = memory.get_long_le(file_offset_for_va(pc, load));
            const uint32_t op = instr >> 26;
            const uint32_t rs = (instr >> 21) & 31u;
            const uint32_t rt = (instr >> 16) & 31u;
            const int32_t simm = static_cast<int16_t>(instr & 0xFFFFu);
            bool branch = false;
            bool this_taken = false;
            uint32_t this_dest = 0;
            auto arm = [&](bool condition, uint32_t target) {
                branch = true;
                this_taken = condition;
                this_dest = target;
            };

            if(instr == 0)
            {
            }
            else if(op == 0 && (instr & 63u) == 0x08)
            {
                arm(true, reg[rs]);
            }
            else if(op == 0x02)
            {
                arm(true, (pc & 0xF0000000u) | ((instr & 0x03FFFFFFu) << 2));
            }
            else if(op == 0x04)
            {
                arm(reg[rs] == reg[rt], pc + 4u + static_cast<uint32_t>(simm * 4));
            }
            else if(op == 0x09)
            {
                if(rt != 0)
                    reg[rt] = reg[rs] + static_cast<uint32_t>(simm);
            }
            else
            {
                throw std::runtime_error("Unsupported shop-receive instruction");
            }

            check(!(branch && in_delay), "Shop receive branches in a delay slot");
            if(in_delay)
            {
                pc = taken ? dest : pc + 4;
                in_delay = false;
            }
            else if(branch)
            {
                in_delay = true;
                taken = this_taken;
                dest = this_dest;
                pc += 4;
            }
            else
            {
                pc += 4;
            }
        }

        throw std::runtime_error("Shop receive did not return");
    }

    void assert_load_delays(const BinaryFile& exe, uint32_t entry_offset, uint32_t code_end)
    {
        for(uint32_t offset = entry_offset; offset + 4 <= code_end; offset += 4)
        {
            const uint32_t instr = exe.get_long_le(offset);
            if((instr >> 26) != 0x23)
                continue;
            check(offset + 8 <= code_end, "Load is the last instruction of the lookup");
            check(exe.get_long_le(offset + 4) == 0, "Load is not followed by a delay nop");
        }
    }

    Json alundra_document(const RandomizerWorld& world, uint8_t herbs_id)
    {
        Json placements = Json::object();
        for(const ItemSource* source : world.item_sources())
            placements[std::to_string(source->id())] = static_cast<int>(herbs_id);

        Json json = Json::object();
        json["format"] = 1;
        json["seed"] = 7;
        json["gameSettings"] = Json::object({ { "progressiveItems", false } });
        json["randomizerSettings"] = Json::object({
            { "itemsDistribution", Json::object({ { "Herbs", world.item_sources().size() } }) },
            { "startingInventory", Json::array({ "Dagger", "Cloth Armor", "Short Boots" }) }
        });
        json["placements"] = std::move(placements);
        return json;
    }

    void test_rejections(const std::filesystem::path& dir, const GameData& game_data, RandomizerWorld& world)
    {
        const std::string remote_distribution = error_of([&] {
            Json json = Json::object({
                { "seed", 1 },
                { "randomizerSettings", Json::object({
                    { "itemsDistribution", Json::object({ { "Progressive Sword", 1 } }) },
                    { "startingInventory", Json::array({ "Dagger", "Cloth Armor", "Short Boots" }) }
                }) }
            });
            RandomizerOptions(json, game_data, world);
        });
        check(remote_distribution.find("Progressive Sword") != std::string::npos,
              "A remote name in itemsDistribution was accepted");

        const std::string remote_inventory = error_of([&] {
            Json json = Json::object({
                { "seed", 1 },
                { "randomizerSettings", Json::object({
                    { "startingInventory", Json::array({ "Dagger", "Cloth Armor", "Short Boots", "Hookshot" }) }
                }) }
            });
            RandomizerOptions(json, game_data, world);
        });
        check(remote_inventory.find("Hookshot") != std::string::npos,
              "A remote name in startingInventory was accepted");

        const auto remote_id_path = dir / "remote_id.json";
        write_json(remote_id_path, Json::object({
            { "format", 2 },
            { "placements", Json::object({ { "1", 0x5A } }) }
        }));
        const std::string remote_id = error_of([&] { placement_seed_map::read(remote_id_path); });
        check(remote_id.find("not a valid item id") != std::string::npos,
              "Item id 0x5A was accepted as a local placement");

        const auto alundra_object_path = dir / "alundra_object.json";
        write_json(alundra_object_path, Json::object({
            { "format", 1 },
            { "placements", Json::object({
                { "1", Json::object({
                    { "player", "Primus" },
                    { "item", "Sword" },
                    { "classification", "filler" }
                }) }
            }) }
        }));
        check(!error_of([&] { placement_seed_map::read(alundra_object_path); }).empty(),
              "The Alundra format accepted a remote object");

        const auto permalink_path = dir / "permalink_conflict.json";
        write_json(permalink_path, Json::object({
            { "format", 1 },
            { "permalink", "abc" },
            { "seed", 1 },
            { "placements", Json::object({ { "1", 1 } }) }
        }));
        check(!error_of([&] { placement_seed_map::read(permalink_path); }).empty(),
              "A permalink plus seed was accepted");

        const auto filler_path = dir / "filler.json";
        write_json(filler_path, Json::object({
            { "format", 2 },
            { "placements", Json::object({
                { "1", Json::object({
                    { "player", "Dee" },
                    { "item", "Map" },
                    { "classification", "filler" }
                }) }
            }) }
        }));
        const placement_seed_map::Contents filler = placement_seed_map::read(filler_path);
        check(filler.placements.at(1).remote, "Filler remote did not parse");
        check(filler.placements.at(1).remote_item.classification == RemoteClassification::Filler,
              "Filler classification did not round-trip");

        const auto bare_path = dir / "bare.json";
        write_json(bare_path, Json::object({ { "1", 36 } }));
        const placement_seed_map::Contents bare = placement_seed_map::read(bare_path);
        check(!bare.placements.at(1).remote, "A bare integer map was treated as remote");
        check(bare.placements.at(1).item_id == 36, "Bare Alundra format item id changed");

        const auto price_path = dir / "price.json";
        auto price_document = [](const Json& price) {
            return Json::object({
                { "format", 2 },
                { "placements", Json::object({
                    { "273", Json::object({
                        { "player", "Dee" },
                        { "item", "Map" },
                        { "classification", "filler" },
                        { "price", price }
                    }) }
                }) }
            });
        };
        write_json(price_path, price_document(0));
        const placement_seed_map::Contents free = placement_seed_map::read(price_path);
        check(free.placements.at(273).remote_item.price.has_value()
              && *free.placements.at(273).remote_item.price == 0,
              "A shop price of 0 was not read");
        write_json(price_path, price_document(9999));
        const placement_seed_map::Contents capped = placement_seed_map::read(price_path);
        check(capped.placements.at(273).remote_item.price.has_value()
              && *capped.placements.at(273).remote_item.price == 9999,
              "A shop price of 9999 was not read");
        write_json(price_path, price_document(10000));
        check(error_of([&] { placement_seed_map::read(price_path); }).find("0 to 9999") != std::string::npos,
              "A shop price above 9999 was accepted");
        write_json(price_path, price_document(-1));
        check(error_of([&] { placement_seed_map::read(price_path); }).find("0 to 9999") != std::string::npos,
              "A negative shop price was accepted");
        write_json(price_path, price_document("100"));
        check(error_of([&] { placement_seed_map::read(price_path); }).find("integer") != std::string::npos,
              "A string shop price was accepted");
        write_json(price_path, Json::object({
            { "format", 2 },
            { "placements", Json::object({
                { "273", Json::object({
                    { "player", "Dee" },
                    { "item", "Map" },
                    { "classification", "filler" },
                    { "price", 100 },
                    { "cost", 100 }
                }) }
            }) }
        }));
        check(error_of([&] { placement_seed_map::read(price_path); }).find("Unknown key") != std::string::npos,
              "An unknown remote key was accepted");
    }

    void test_slot_identity(const std::filesystem::path& dir)
    {
        const auto path = dir / "slot.json";
        const Json remote = Json::object({
            { "player", "Primus" },
            { "item", "Sword" },
            { "classification", "filler" }
        });

        write_json(path, Json::object({
            { "format", 2 },
            { "apworldVersion", "0.1.5" },
            { "player", "N\u00E9stor\\Room" },
            { "seed", 7 },
            { "placements", Json::object({ { "1", remote } }) }
        }));
        const placement_seed_map::Contents kept = placement_seed_map::read(path);
        check(kept.apworld_version == "0.1.5", "apworldVersion was not kept");
        check(kept.player == "N\u00E9stor\\Room", "Slot player was not kept");
        check(kept.options_json.at("seed") == 7, "Slot identity displaced the seed");
        check(kept.placements.at(1).remote_item.player == "Primus",
              "The slot player replaced the remote disc name");

        write_json(path, Json::object({
            { "format", 2 },
            { "seed", 7 },
            { "placements", Json::object({ { "1", remote } }) }
        }));
        const placement_seed_map::Contents older = placement_seed_map::read(path);
        check(older.apworld_version.empty() && older.player.empty(),
              "An older Multiworld file invented a slot");

        write_json(path, Json::object({
            { "format", 1 },
            { "apworldVersion", "0.1.5" },
            { "player", "YourName" },
            { "placements", Json::object({ { "1", 36 } }) }
        }));
        check(error_of([&] { placement_seed_map::read(path); }).find("Multiworld format") != std::string::npos,
              "The Alundra format accepted slot identity");

        write_json(path, Json::object({
            { "format", 2 },
            { "apworldVersion", "0.1" },
            { "placements", Json::object({ { "1", remote } }) }
        }));
        check(error_of([&] { placement_seed_map::read(path); }).find("major.minor.patch") != std::string::npos,
              "A short apworldVersion was accepted");

        write_json(path, Json::object({
            { "format", 2 },
            { "apworldVersion", 1 },
            { "placements", Json::object({ { "1", remote } }) }
        }));
        check(error_of([&] { placement_seed_map::read(path); }).find("string") != std::string::npos,
              "A numeric apworldVersion was accepted");

        write_json(path, Json::object({
            { "format", 2 },
            { "player", "   " },
            { "placements", Json::object({ { "1", remote } }) }
        }));
        check(error_of([&] { placement_seed_map::read(path); }).find("login name") != std::string::npos,
              "A blank slot player was accepted");

        write_json(path, Json::object({
            { "format", 2 },
            { "player", "Your\nName" },
            { "placements", Json::object({ { "1", remote } }) }
        }));
        check(error_of([&] { placement_seed_map::read(path); }).find("control") != std::string::npos,
              "A slot player with a newline was accepted");

        write_json(path, Json::object({
            { "format", 2 },
            { "slot", "YourName" },
            { "placements", Json::object({ { "1", remote } }) }
        }));
        check(error_of([&] { placement_seed_map::read(path); }).find("Unknown key") != std::string::npos,
              "An unknown slot key was accepted");
    }

    void test_alundra_format(const std::filesystem::path& dir, const GameData& game_data, RandomizerWorld& world)
    {
        const uint8_t herbs_id = game_data.item("Herbs")->id();
        const auto path = dir / "alundra.json";
        write_json(path, alundra_document(world, herbs_id));

        const placement_seed_map::Contents placement = placement_seed_map::read(path);
        check(placement.has_options, "Alundra format settings were dropped");
        check(placement.permalink.empty(), "Alundra format invented a permalink");
        check(placement.placements.size() == world.item_sources().size(), "Alundra format placement count changed");
        for(const auto& [id, value] : placement.placements)
        {
            check(!value.remote, "Alundra format placement " + std::to_string(id) + " became remote");
            check(value.item_id == herbs_id, "Alundra format item id changed");
        }

        RandomizerOptions options(placement.options_json, game_data, world);
        check(options.seed() == 7, "Alundra format seed was not applied");
        check(!options.progressive_items(), "Alundra format progressiveItems was not applied");
        check(options.items_distribution()[herbs_id] == world.item_sources().size(),
              "Alundra format itemsDistribution was not applied");
        check(options.starting_inventory().size() == 3, "Alundra format starting inventory size changed");

        placement_seed_map::apply(world, game_data, placement.placements);

        BinaryFile datas(std::vector<uint8_t>(datas_limit(world), 0));
        const ItemSource* chest = source_by_id(world, 1);
        const ItemSource* shop = source_by_id(world, 262);
        plant_shop_price(datas, shop);

        BinaryFile exe_bytes(std::vector<uint8_t>(EXE_SIZE, 0));
        plant_hooks(exe_bytes, herbs_id);
        const auto exe_path = dir / "alundra_format.exe";
        exe_bytes.save_as(exe_path);

        PatchApplyItemSources patch;
        patch.alter_datas_file(datas, game_data, world);
        PsxExeFile exe(exe_path);
        patch.alter_exe_file(exe, game_data, world);

        check(datas.get_byte(chest->addresses().front()) == herbs_id, "Alundra format chest byte changed");
        check(exe.get_long_le(NAME_SITE_A) == SITE_A_WORDS[0], "Alundra format rewrote the chest name load");
        check(exe.get_long_le(NAME_SITE_B) == SITE_B_WORDS[0], "Alundra format rewrote the obtained name load");
        check(exe.get_long_le(NAME_SITE_C) == SITE_C_WORDS[0], "Alundra format rewrote the inventory-full name load");
        check(exe.get_long_le(SHOP_RECEIVE_CALL) == SHOP_RECEIVE_JAL,
              "Alundra format rewrote the shop receive call");
        check(exe.get_long_le(SHOP_RECEIVE_CALL + 4) == SHOP_RECEIVE_DELAY,
              "Alundra format rewrote the shop receive delay");
        check(exe.get_word_le(ITEM_QTY_FILE + static_cast<uint32_t>(ITEM_REMOTE) * 10 + 6) == 0xFFFF,
              "Alundra format wrote the remote quantity slot");
        check(exe.get_long_le(ITEM_INFO_2_FILE + static_cast<uint32_t>(herbs_id) * 8) == LOCAL_NAME_POINTER,
              "Alundra format wrote the local name pointer");
        check(datas.get_long_le(SPRITE_TABLE + static_cast<uint32_t>(ITEM_REMOTE + 0x1E) * 4) == 0xFFFFFFFF,
              "Alundra format claimed the remote sprite slot");
        check(datas.get_byte(REMOTE_SPRITE_HEADER) == 0,
              "Alundra format wrote the remote sprite header");
        for(uint32_t address : shop->price_addresses())
        {
            check(datas.get_word_le(address) == game_data.item("Herbs")->gold_value(),
                  "Alundra format shop price is not the Alundra item gold value");
        }
    }

    void test_multiworld_format(const std::filesystem::path& dir, const GameData& game_data, RandomizerWorld& world)
    {
        const uint8_t herbs_id = game_data.item("Herbs")->id();
        const uint8_t life_id = game_data.item("Life Vessel")->id();
        check(herbs_id == 36 && life_id == 83, "Fixture item ids no longer match Herbs and Life Vessel");

        const placement_seed_map::Contents placement = placement_seed_map::read(PLACEMENT_MULTIWORLD_FORMAT_FIXTURE);
        check(placement.has_options && placement.permalink.empty(), "Multiworld format options were not kept");
        check(placement.apworld_version == "0.1.5", "Multiworld format dropped apworldVersion");
        check(placement.player == "YourName", "Multiworld format dropped the slot player");
        check(placement.placements.size() == world.item_sources().size(), "Multiworld format is missing a location");

        std::array<int, ITEM_COUNT> local_counts {};
        int remote_count = 0;
        for(const auto& [id, value] : placement.placements)
        {
            if(value.remote)
            {
                ++remote_count;
                continue;
            }
            local_counts[value.item_id] += 1;
            (void)id;
        }
        check(remote_count == 3, "Multiworld format fixture remote count changed");
        check(local_counts[herbs_id] == 287, "Multiworld format fixture Herbs count changed");
        check(local_counts[life_id] == 1, "Multiworld format fixture Life Vessel count changed");

        RandomizerOptions options(placement.options_json, game_data, world);
        check(options.seed() == 1, "Multiworld format seed was not applied");
        check(!options.progressive_items(), "Multiworld format progressiveItems was not applied");
        for(size_t item_id = 0; item_id < ITEM_COUNT; ++item_id)
        {
            check(options.items_distribution()[item_id] == local_counts[item_id],
                  "itemsDistribution counts a remote placement as an Alundra item");
        }
        const auto& inventory = options.starting_inventory();
        check(inventory.size() == 3, "Starting inventory size changed");
        check(game_data.item(inventory[0])->name() == "Dagger", "Starting inventory is not Alundra items");
        check(game_data.item(inventory[1])->name() == "Cloth Armor", "Starting inventory is not Alundra items");
        check(game_data.item(inventory[2])->name() == "Short Boots", "Starting inventory is not Alundra items");

        placement_seed_map::apply(world, game_data, placement.placements);

        ItemSource* chest = source_by_id(world, 1);
        ItemSource* other = source_by_id(world, 5);
        ItemSource* trap = source_by_id(world, 6);
        ItemSource* shop = source_by_id(world, 262);

        check(chest->is_remote() && chest->item() == nullptr && !chest->is_empty(),
              "Remote chest still grants an Alundra item");
        check(chest->contents_name() == "Primus - Progressive Sword", "Remote chest display name changed");
        check(chest->remote()->classification == RemoteClassification::Progression,
              "Progression classification did not round-trip");
        check(trap->is_remote() && trap->remote()->classification == RemoteClassification::Trap,
              "Trap classification did not round-trip");
        check(trap->contents_name() == "Tertius - Mouse Trap", "Trap display name changed");
        check(shop->is_remote() && shop->remote()->classification == RemoteClassification::Useful,
              "Useful classification did not round-trip");
        check(shop->contents_name() == "Secondus - Hookshot", "Shop display name changed");
        check(!other->is_remote() && other->item() != nullptr && other->item()->id() == life_id,
              "A local placement in the Multiworld file changed");

        const std::string refused = error_of([&] {
            placement_seed_map::write(dir / "refused.json", options, world);
        });
        check(refused.find("another world's item") != std::string::npos,
              "placement-out wrote a world that holds a remote item");

        BinaryFile datas(std::vector<uint8_t>(datas_limit(world), 0));
        plant_shop_price(datas, shop);
        plant_chest_flag(datas, chest);

        BinaryFile exe_bytes(std::vector<uint8_t>(EXE_SIZE, 0));
        plant_hooks(exe_bytes, herbs_id);
        const auto exe_path = dir / "multiworld_format.exe";
        exe_bytes.save_as(exe_path);

        PatchApplyItemSources patch;
        patch.alter_datas_file(datas, game_data, world);
        PsxExeFile exe(exe_path);
        const uint32_t size_before = static_cast<uint32_t>(exe.size());
        patch.alter_exe_file(exe, game_data, world);

        check(datas.get_byte(chest->addresses().front()) == ITEM_REMOTE, "Remote chest did not write the sentinel");
        check(datas.get_byte(trap->addresses().front()) == ITEM_REMOTE, "Remote trap chest did not write the sentinel");
        check(datas.get_byte(other->addresses().front()) == life_id, "Local chest byte changed");
        check(datas.get_byte(shop->addresses().front()) == ITEM_REMOTE, "Remote shop did not write the sentinel");
        for(uint32_t address : shop->price_addresses())
            check(datas.get_word_le(address) == SHOP_PRICE, "Remote shop price changed");
        check(datas.get_long_le(SPRITE_TABLE + static_cast<uint32_t>(ITEM_REMOTE + 0x1E) * 4) == REMOTE_SPRITE_RECORD,
              "Remote sprite slot does not name its own header");
        check(datas.get_long_le(SPRITE_TABLE + static_cast<uint32_t>(ITEM_BOOK_OF_ELNA + 0x1E) * 4) == BOOK_OF_ELNA_SPRITE_RECORD,
              "Book of Elna sprite record was overwritten");
        for(uint32_t index = 0; index < SPRITE_HEADER_SIZE; ++index)
        {
            const uint8_t planted = static_cast<uint8_t>(0xA0 + index);
            check(datas.get_byte(BOOK_OF_ELNA_SPRITE_FILE + index) == planted,
                  "Book of Elna sprite header was overwritten");
            check(datas.get_byte(REMOTE_SPRITE_HEADER + index) == planted,
                  "Remote sprite header is not Book of Elna's header");
        }
        check(!shop->sprite_addresses().empty(), "Remote shop has no sprite address");
        check(datas.get_byte(shop->sprite_addresses().front()) == ITEM_REMOTE + 0x1E,
              "Remote shop sprite index changed");
        check(exe.get_word_le(ITEM_QTY_FILE + static_cast<uint32_t>(ITEM_REMOTE) * 10 + 6) == 0,
              "Remote item can still be added to inventory");
        check(exe.get_long_le(SHOP_RECEIVE_CALL + 4) == SHOP_RECEIVE_DELAY,
              "Shop receive hook overwrote its delay slot");
        const uint32_t shop_jal = exe.get_long_le(SHOP_RECEIVE_CALL);
        check((shop_jal >> 26) == 3, "Shop receive call was not hooked");
        const uint32_t shop_stub = jal_target(shop_jal, SHOP_RECEIVE_PC);

        const uint32_t site_a = exe.get_long_le(NAME_SITE_A);
        const uint32_t target = jal_target(site_a, SITE_A_PC);
        check((site_a >> 26) == 3, "Chest name load was not hooked");
        check(exe.get_long_le(NAME_SITE_A + 4) == 0x001380C0, "Chest hook delay slot no longer sets s0");
        check(exe.get_long_le(NAME_SITE_A + 8) == 0x00402821, "Chest hook does not pass the looked-up name");
        check(exe.get_long_le(NAME_SITE_A + 12) == 0, "Chest hook is the wrong size");
        check(exe.get_long_le(NAME_SITE_A + 16) == 0x0C0211BA, "Chest hook overwrote the following call");
        check(exe.get_long_le(NAME_SITE_B + 16) == 0x0C0211BA, "Obtained-line hook overwrote the following call");
        check(exe.get_long_le(NAME_SITE_B + 4) == 0, "Obtained-line hook writes s0");
        check(jal_target(exe.get_long_le(NAME_SITE_B), SITE_B_PC) == target, "Obtained line uses a different lookup");
        check(jal_target(exe.get_long_le(NAME_SITE_C), SITE_C_PC) == target, "Inventory-full line uses a different lookup");
        check(exe.get_long_le(NAME_SITE_C + 12) == SITE_C_NEXT, "Inventory-full hook overwrote the next instruction");
        check(target >= 0x80000000 && target < EXE_LOAD, "Lookup was injected outside the executable");

        const auto saved_path = dir / "multiworld_format.saved.exe";
        exe.save_as(saved_path);
        const uint32_t expansion = static_cast<uint32_t>(exe.size()) - size_before;
        const uint32_t load = exe.get_long_le(0x18);
        check(load == EXE_LOAD - expansion, "Saved executable load address does not match the expansion");

        const uint32_t entry_offset = file_offset_for_va(target, load);
        check(exe.get_long_le(entry_offset) == 0x03E0C020, "Lookup does not save ra before bgezal");

        const uint32_t primus_offset = find_cstr(exe, "Primus - Progressive Sword");
        const uint32_t primus_va = load + (primus_offset - HEADER_SIZE);
        const uint32_t generic_offset = find_cstr(exe, "AP Item");
        check(find_cstr(exe, "Tertius - Mouse Trap") > 0, "Trap display string was not written");
        check(find_cstr(exe, "Secondus - Hookshot") > 0, "Shop display string was not written");
        assert_load_delays(exe, entry_offset, generic_offset);

        bool paired = false;
        for(uint32_t offset = 0; offset + 8 <= exe.size(); offset += 4)
        {
            if(exe.get_long_le(offset) == CHEST_FLAG && exe.get_long_le(offset + 4) == primus_va)
                paired = true;
        }
        check(paired, "Side table does not pair flag 0x0680 with Primus' item name");

        const uint32_t info_offset = ITEM_INFO_2_FILE + static_cast<uint32_t>(ITEM_REMOTE) * 8 + expansion;
        const uint32_t generic_va = exe.get_long_le(info_offset);
        check(read_cstr(exe, generic_va, load) == "AP Item", "Menu name for the sentinel is not AP Item");
        check(exe.get_byte(info_offset + 5) == 0x34, "Sentinel fanfare byte was not set");

        const uint32_t primus_pointer = run_lookup(exe, load, target, ITEM_REMOTE, CHEST_FLAG);
        check(read_cstr(exe, primus_pointer, load) == "Primus - Progressive Sword",
              "Flagged chest lookup did not return Primus' item");
        const uint32_t missing_pointer = run_lookup(exe, load, target, ITEM_REMOTE, 0);
        check(read_cstr(exe, missing_pointer, load) == "AP Item",
              "A chest with no flag did not fall back to AP Item");
        const uint32_t local_pointer = run_lookup(exe, load, target, herbs_id, CHEST_FLAG);
        check(local_pointer == LOCAL_NAME_POINTER, "Local item lookup no longer uses the item-name table");

        bool reached_retail = true;
        const uint32_t remote_receive = run_shop_receive(exe, load, shop_stub, ITEM_REMOTE, reached_retail);
        check(!reached_retail && remote_receive == 1, "Remote shop purchase is still refused");
        reached_retail = false;
        run_shop_receive(exe, load, shop_stub, herbs_id, reached_retail);
        check(reached_retail, "Alundra shop item no longer uses the retail receive check");
    }

    Json load_json(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        check(static_cast<bool>(input), "Could not open " + path.string());
        std::stringstream buffer;
        buffer << input.rdbuf();
        return Json::parse(buffer.str(), nullptr, true, true);
    }

    void expect_price(const BinaryFile& datas, const ItemSource* shop, uint16_t price, const std::string& message)
    {
        check(!shop->price_addresses().empty(), message + " has no price address");
        for(uint32_t address : shop->price_addresses())
            check(datas.get_word_le(address) == price, message);
    }

    void test_remote_shop_price(const std::filesystem::path& dir, const GameData& game_data, RandomizerWorld& world)
    {
        Json json = load_json(PLACEMENT_MULTIWORLD_FORMAT_FIXTURE);
        json["placements"]["273"] = Json::object({
            { "player", "Primus" },
            { "item", "Boss Key" },
            { "classification", "progression" },
            { "price", 100 }
        });
        json["placements"]["266"] = Json::object({
            { "player", "Secondus" },
            { "item", "Hookshot" },
            { "classification", "useful" },
            { "price", 250 }
        });
        json["placements"]["267"] = Json::object({
            { "player", "Quartus" },
            { "item", "Rupee" },
            { "classification", "filler" },
            { "price", 100 }
        });

        const auto path = dir / "shop_prices.json";
        write_json(path, json);
        const placement_seed_map::Contents placement = placement_seed_map::read(path);
        check(placement.placements.at(273).remote_item.price.has_value()
              && *placement.placements.at(273).remote_item.price == 100,
              "Naomi price was not read");
        check(placement.placements.at(266).remote_item.price.has_value()
              && *placement.placements.at(266).remote_item.price == 250,
              "Lurvy unique price was not read");
        check(!placement.placements.at(262).remote_item.price.has_value(),
              "A remote shelf without a price invented one");
        check(placement.placements.at(1).remote && !placement.placements.at(1).remote_item.price.has_value(),
              "A chest price was invented");

        placement_seed_map::apply(world, game_data, placement.placements);

        ItemSource* naomi = source_by_id(world, 273);
        ItemSource* lurvy_unique = source_by_id(world, 266);
        ItemSource* lurvy_replacement = source_by_id(world, 267);
        ItemSource* lurvy_restock = source_by_id(world, 262);
        ItemSource* naomi_herbs = source_by_id(world, 270);
        check(naomi->is_remote() && naomi->remote()->price.has_value() && *naomi->remote()->price == 100,
              "Naomi price was not applied");
        check(lurvy_unique->remote()->classification == RemoteClassification::Useful,
              "Lurvy unique classification changed");
        check(!lurvy_restock->remote()->price.has_value(), "Restocking shelf gained a price");
        check(!naomi_herbs->is_remote() && naomi_herbs->item() != nullptr, "Naomi herbs became remote");

        BinaryFile datas(std::vector<uint8_t>(datas_limit(world), 0));
        plant_shop_price(datas, lurvy_restock);
        for(const ItemSource* shop : { naomi, lurvy_unique, lurvy_replacement })
        {
            for(uint32_t address : shop->price_addresses())
                datas.set_word_le(address, SHOP_PRICE);
        }

        PatchApplyItemSources patch;
        patch.alter_datas_file(datas, game_data, world);

        expect_price(datas, naomi, 100, "Naomi remote price was not written");
        expect_price(datas, lurvy_unique, 250, "Lurvy unique remote price was not written");
        expect_price(datas, lurvy_replacement, 100, "Lurvy replacement remote price was not written");
        expect_price(datas, lurvy_restock, SHOP_PRICE, "Remote shelf without a price changed the disc price");
        expect_price(datas, naomi_herbs, game_data.item("Herbs")->gold_value(),
                     "Alundra shop price is not the item gold value");

        json["placements"]["1"]["price"] = 100;
        const auto chest_price_path = dir / "chest_price.json";
        write_json(chest_price_path, json);
        const placement_seed_map::Contents chest_price = placement_seed_map::read(chest_price_path);
        RandomizerWorld reject_world(game_data);
        const std::string rejected = error_of([&] {
            placement_seed_map::apply(reject_world, game_data, chest_price.placements);
        });
        check(rejected.find("shop price") != std::string::npos, "A price on a chest was accepted");
    }
}

int main()
{
    try
    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "alundra-placement-remote";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        GameData game_data;
        RandomizerWorld rejection_world(game_data);
        test_rejections(dir, game_data, rejection_world);
        test_slot_identity(dir);

        RandomizerWorld alundra_world(game_data);
        test_alundra_format(dir, game_data, alundra_world);

        RandomizerWorld multiworld_world(game_data);
        test_multiworld_format(dir, game_data, multiworld_world);

        RandomizerWorld shop_price_world(game_data);
        test_remote_shop_price(dir, game_data, shop_price_world);

        std::filesystem::remove_all(dir);
        std::cout << "placement_remote_test passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
