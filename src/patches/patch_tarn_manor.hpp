#pragma once

#include "game_patch.hpp"
#include "../constants/flags.hpp"
#include "../tools/exception.hpp"
#include "../tools/mips_tools.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

/**
 * Tarn's Manor table, the Book of Elna on the retail disc.
 *
 * Map 117 entity 20 is a ground sprite. B[1] shows it with ActivateEntity.
 * A placed item keeps the "{name} obtained!" popup. That popup is the entity's
 * own AI (sprite byte +0x14 selects it). On overlap it gives sprite-30 and
 * FlagOns the word at entity+640. Vanilla leaves the collected halfword 0, so
 * the ground-item tick stores -1 there and the check never sticks. The Book
 * of Elna sentence is a different path: touch sets 0x83E8, opcode 0x82 gives
 * the item, and the dialog is "Received the Book of Elna." A book-like sprite
 * still takes that path. An item sprite usually does not, which is why the
 * sentence never appears and the sprite comes back.
 *
 * The collected halfword is 0x03B9. The tick hides an entity whose flag is
 * set in the record but still clear in memory, so the tick keeps this one
 * visible and stores 0x03B9 at entity+640. The popup's existing FlagOn then
 * sets the check. The scene also sets it when the dialog path runs. 0x006C
 * stays the story gate and is not this check. Once the check is on, a later
 * visit ends the map event. The door back into the room is the follow-up
 * trigger, and that trigger takes control until temp flag 0x8006. Re-entering
 * already standing there never sets 0x8006, so the visit must not start it.
 * The book scene still reaches the trigger on the same visit, after control
 * is given back.
 *
 * The cave in this file is the empty space the new script is written into.
 * Map 117's D table has 24 fixed slots. Slots 1 and 23 hold scripts, and the
 * entities that use a D script point at those two. Slots 2 through 22 are
 * zero in vanilla and no entity selects them, so that space is empty. B[1]
 * cannot grow in place, because its jumps are halfword offsets, so the new
 * script is written into those 42 bytes and the original script branches
 * there and back.
 */
class PatchTarnManor : public GamePatch
{
public:
    void alter_datas_file(BinaryFile& data, const GameData&, const RandomizerWorld&) override
    {
        // B[1] entry. Vanilla is IfFlagOn 0x006C -> End, then Break, ActivateEntity 20.
        constexpr uint32_t SCRIPT_ENTRY = 0x1BA5720;
        // Opcode 0x82 argument. Item placement writes this byte on its own.
        constexpr uint32_t GIVE_ITEM = 0x1BA5730;
        // LoadBgm and the following 2-byte opcode. Both are replayed from the cave.
        constexpr uint32_t AFTER_GIVE = 0x1BA5731;
        constexpr uint32_t DIALOG = 0x1BA5735;
        // GetDialogChoice, WaitDialog, and the following 1-byte opcode.
        // Replayed from the cave so the flag can be set before DestroyEntity.
        constexpr uint32_t AFTER_CHOICE = 0x1BA573F;
        constexpr uint32_t DESTROY_ENTITY = 0x1BA5742;
        // Break, then ActivateEntity 20. The not-yet-taken path returns here.
        constexpr uint32_t SHOW_BOOK = 0x1BA5725;
        constexpr uint32_t SCRIPT_END = 0x1BA57A3;
        // D[2] through D[22] are unused script slots. D[1] and D[23] are not.
        constexpr uint32_t CAVE = 0x1BA56D6;
        constexpr uint32_t CAVE_BYTES = 42;

        expect_bytes(data, SCRIPT_ENTRY, { 0x30, 0x6C, 0x00, 0x83, 0x00 }, "B[1] entry");
        expect_bytes(data, GIVE_ITEM, { 0x58 }, "Book of Elna give");
        expect_bytes(data, AFTER_GIVE, { 0xA6, 0x00, 0xB9, 0x0B }, "bytes after the give");
        expect_bytes(data, AFTER_CHOICE, { 0x51, 0x39, 0xA5 }, "bytes before DestroyEntity");
        expect_bytes(data, SCRIPT_END, { 0xFF }, "B[1] end");
        for(uint32_t index = 0; index < CAVE_BYTES; ++index)
        {
            if(data.get_byte(CAVE + index) != 0)
                throw RandomizerException("Tarn's Manor table cave is not empty.");
        }

        const uint16_t taken = FLAG_TARNS_MANOR_TABLE.event_code();
        // Taken: end the map event. The door that comes back into this room is
        // the follow-up trigger, and starting it from that tile never returns
        // control. Not taken: vanilla Break and ActivateEntity.
        std::vector<uint8_t> cave;
        append_if_flag_on(cave, taken, distance(SCRIPT_END, CAVE));
        append_goto(cave, distance(SHOW_BOOK, CAVE + static_cast<uint32_t>(cave.size())));
        if(cave.size() != 8)
            throw RandomizerException("Tarn's Manor table entry cave changed size.");

        // The give has just run. Set the check, then play the music and dialog.
        // The same bit is set again after the dialog, directly before the
        // sprite is removed. The first playtest finished that scene with the
        // bit still clear, so the 0x82 result is not what gates it.
        const uint32_t give_cave = CAVE + static_cast<uint32_t>(cave.size());
        append_flag_on(cave, taken);
        cave.insert(cave.end(), { 0xA6, 0x00, 0xB9, 0x0B });
        append_goto(cave, distance(DIALOG, CAVE + static_cast<uint32_t>(cave.size())));

        const uint32_t after_choice = CAVE + static_cast<uint32_t>(cave.size());
        append_flag_on(cave, taken);
        cave.insert(cave.end(), { 0x51, 0x39, 0xA5 });
        append_goto(cave, distance(DESTROY_ENTITY, CAVE + static_cast<uint32_t>(cave.size())));

        if(cave.size() > CAVE_BYTES)
            throw RandomizerException("Tarn's Manor table cave does not fit in the unused script slots.");

        data.set_bytes(CAVE, cave);
        data.set_bytes(SCRIPT_ENTRY, branch_bytes(0x02, distance(CAVE, SCRIPT_ENTRY), 2));
        data.set_bytes(AFTER_GIVE, branch_bytes(0x02, distance(give_cave, AFTER_GIVE), 1));
        data.set_bytes(AFTER_CHOICE, branch_bytes(0x02, distance(after_choice, AFTER_CHOICE), 0));

        // Record+0x10. maybegetcontents copies it to entity+0x40. Vanilla is 0.
        // The tick below is what stops that from hiding the sprite on entry.
        constexpr uint32_t COLLECTED = 0x1BA5D10;
        expect_bytes(data, COLLECTED, { 0x00, 0x00 }, "table collected halfword");
        data.set_word_le(COLLECTED, taken);
    }

    void alter_exe_file(PsxExeFile& exe, const GameData&, const RandomizerWorld&) override
    {
        // Ground-item tick, file offset of VA 0x80063ED0. Reached only when
        // entity+0x40 is nonzero and that flag is still clear. Vanilla then
        // removes the entity. jal 0x8003B0C8; the delay slot copies the
        // entity to a0 and has to stay.
        constexpr uint32_t HIDE_CALL = 0x446D0;
        constexpr uint32_t HIDE_ENTITY = 0x8003B0C8;
        constexpr uint32_t RETAIL_JAL = 0x0C00EC32;
        if(exe.get_long_le(HIDE_CALL) != RETAIL_JAL)
            throw RandomizerException("Tarn's Manor table did not find the ground-item hide call.");

        const uint16_t taken = FLAG_TARNS_MANOR_TABLE.event_code();
        MipsCode hide_unless_table_item;
        hide_unless_table_item.lw(reg_V0, reg_A0, 0x40);
        hide_unless_table_item.addiu(reg_V1, reg_ZERO, taken);
        hide_unless_table_item.bne(reg_V0, reg_V1, "hide");
        // Same fields as the tick's show path, except +640 keeps the flag.
        // +632 stays 0. A nonzero value there is a countdown, not a flag.
        hide_unless_table_item.sw(reg_ZERO, reg_A0, 636);
        hide_unless_table_item.sw(reg_V0, reg_A0, 640);
        hide_unless_table_item.sw(reg_ZERO, reg_A0, 644);
        hide_unless_table_item.sw(reg_ZERO, reg_A0, 632);
        hide_unless_table_item.jr(reg_RA);
        hide_unless_table_item.label_("hide");
        hide_unless_table_item.j(HIDE_ENTITY);

        const uint32_t injected = exe.inject_code(hide_unless_table_item);
        MipsCode site;
        site.jal(injected, false);
        exe.set_code(HIDE_CALL, site);
    }

private:
    static void expect_bytes(const BinaryFile& data, uint32_t address,
                             std::initializer_list<uint8_t> expected, const char* label)
    {
        uint32_t index = 0;
        for(uint8_t byte : expected)
        {
            if(data.get_byte(address + index) != byte)
                throw RandomizerException(std::string("Tarn's Manor table patch did not find ") + label + ".");
            ++index;
        }
    }

    static int32_t distance(uint32_t target, uint32_t origin)
    {
        return static_cast<int32_t>(target) - static_cast<int32_t>(origin);
    }

    static void append_u16(std::vector<uint8_t>& bytes, int32_t value)
    {
        if(value < -32768 || value > 32767)
            throw RandomizerException("Tarn's Manor table branch does not fit in a script offset.");
        const auto encoded = static_cast<uint16_t>(value);
        bytes.push_back(static_cast<uint8_t>(encoded & 0xFF));
        bytes.push_back(static_cast<uint8_t>(encoded >> 8));
    }

    static void append_branch(std::vector<uint8_t>& bytes, uint8_t opcode, int32_t relative)
    {
        bytes.push_back(opcode);
        append_u16(bytes, relative);
    }

    static void append_goto(std::vector<uint8_t>& bytes, int32_t relative)
    {
        append_branch(bytes, 0x02, relative);
    }

    static void append_if_flag_on(std::vector<uint8_t>& bytes, uint16_t flag, int32_t relative)
    {
        bytes.push_back(0x30);
        append_u16(bytes, flag);
        append_u16(bytes, relative);
    }

    static void append_flag_on(std::vector<uint8_t>& bytes, uint16_t flag)
    {
        bytes.push_back(0x05);
        append_u16(bytes, flag);
    }

    // Goto is 3 bytes. pad extra bytes sit after it and are not executed.
    static std::vector<uint8_t> branch_bytes(uint8_t opcode, int32_t relative, uint32_t pad)
    {
        std::vector<uint8_t> bytes;
        append_branch(bytes, opcode, relative);
        bytes.insert(bytes.end(), pad, 0x00);
        return bytes;
    }
};
