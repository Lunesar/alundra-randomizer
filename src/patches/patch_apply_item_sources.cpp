#include "patch_apply_item_sources.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../constants/item_codes.hpp"
#include "../game/item.hpp"
#include "../model/item_source.hpp"
#include "../tools/exception.hpp"
#include "../tools/mips_tools.hpp"

namespace
{
    // Item-info-2 name pointer table. Word 0 of each 8-byte row is the name.
    constexpr uint32_t ITEM_INFO_2_FILE = 0xA85E4;
    constexpr uint32_t ITEM_INFO_2_VA = 0x800C7DE4;

    // Quantity / slot / tier table. Max quantity is the signed halfword at +6.
    constexpr uint32_t ITEM_QTY_FILE = 0x9C650;

    // DATAS.BIN sprite-record offset table. Slot 0x78 is empty in the retail disc.
    // A remote check keeps sprite index 0x78. Writing Book of Elna's own index
    // would make the pickup the book, and two slots must not share one record:
    // the loader adds the bank base to that record's four pointers once per slot.
    constexpr uint32_t SPRITE_TABLE = 0x830;
    constexpr uint32_t SPRITE_BANK_BASE = 0x800;
    constexpr uint8_t REMOTE_SPRITE = ITEM_REMOTE + 0x1E;
    constexpr uint8_t BOOK_OF_ELNA_SPRITE = ITEM_BOOK_OF_ELNA + 0x1E;
    // Tail of the sprite bank DATAS[0x800, 0x38800). USA 1.1 leaves these bytes zero.
    constexpr uint32_t REMOTE_SPRITE_HEADER = 0x387A0;
    constexpr uint32_t SPRITE_HEADER_SIZE = 0x20;

    // Chest popup name loads. Each sequence is replaced as a whole.
    constexpr uint32_t NAME_SITE_A = 0x5F21C; // 8007EA1C, must leave s0 = item_id * 8
    constexpr uint32_t NAME_SITE_B = 0x5F2E0; // 8007EAE0, s0 is the dialog buffer
    constexpr uint32_t NAME_SITE_C = 0x5F5BC; // 8007EDBC, inventory-full line

    constexpr uint32_t SITE_A_WORDS[] = { 0x001380C0, 0x3C01800C, 0x00300821, 0x8C257DE4 };
    constexpr uint32_t SITE_B_WORDS[] = { 0x001310C0, 0x3C01800C, 0x00220821, 0x8C257DE4 };
    constexpr uint32_t SITE_C_WORDS[] = { 0x3C01800C, 0x00220821, 0x8C257DE4 };

    // Chest-open spawn stores the parent chest here and sets state 2. The
    // collected flag is the word at parent+0x40, which is also what that
    // path passes to FlagOn. A null word is a different spawn and is a miss.
    // FlagOn itself is left alone.
    constexpr uint16_t PICKUP_PARENT_OFFSET = 0x280;
    constexpr uint16_t CHEST_FLAG_OFFSET = 0x40;

    constexpr const char* GENERIC_REMOTE_NAME = "AP Item";

    void expect_words(const PsxExeFile& exe, uint32_t address, const uint32_t* words, size_t count, const char* site)
    {
        for(size_t index = 0; index < count; ++index)
        {
            const uint32_t found = exe.get_long_le(address + static_cast<uint32_t>(index * 4));
            if(found != words[index])
            {
                throw RandomizerException(std::string("Remote item name hook at ") + site
                                          + " does not match this executable.");
            }
        }
    }

    // Entity contents byte -> collected flag. The game's maybegetcontents
    // function reads the flag from record+0x10 and the item id from
    // record+0x12, which is the address stored on an item source. Script
    // bytes (shops, Merrick, NPC gifts) are not in an entity table, so
    // they have no flag and use the generic name in the chest popup.
    // Their own dialog carries the specific name.
    std::map<uint32_t, uint16_t> read_chest_flags(const BinaryFile& data)
    {
        std::map<uint32_t, uint16_t> flags;
        constexpr uint32_t MAP_TABLE = 0x28;
        constexpr int MAP_COUNT = 512;
        if(data.size() < MAP_TABLE + MAP_COUNT * 4)
            return flags;

        for(int map_id = 0; map_id < MAP_COUNT; ++map_id)
        {
            const uint32_t map_offset = data.get_long_le(MAP_TABLE + static_cast<uint32_t>(map_id * 4));
            if(map_offset == 0 || map_offset > data.size() || data.size() - map_offset < 28)
                continue;

            const int32_t sprites = static_cast<int32_t>(data.get_long_le(map_offset + 12));
            if(sprites < 0 || sprites > 0x200000)
                continue;

            const uint32_t sprite_info = map_offset + static_cast<uint32_t>(sprites);
            if(sprite_info > data.size() || data.size() - sprite_info < 48)
                continue;

            const int32_t entities_relative = static_cast<int32_t>(data.get_long_le(sprite_info));
            const int64_t entities_absolute = static_cast<int64_t>(sprite_info) + entities_relative;
            if(entities_absolute < 0 || static_cast<uint64_t>(entities_absolute) >= data.size())
                continue;

            uint32_t cursor = static_cast<uint32_t>(entities_absolute);
            for(int index = 0; index < 128; ++index)
            {
                if(cursor > data.size() || data.size() - cursor < 20)
                    break;
                if(data.get_byte(cursor + 2) == 0 && data.get_byte(cursor + 3) == 0)
                    break;

                flags.emplace(cursor + 18, data.get_word_le(cursor + 16));
                cursor += 20;
            }
        }

        return flags;
    }

    // The game clears a flag whose low 15 bits are >= 0x800 before storing it
    // on the chest. Those cannot key the side table. Flag 0 is also what a
    // scripted spawn stores, so it cannot name one chest.
    bool flag_is_lookup_key(uint16_t flag)
    {
        return flag != 0 && (flag & 0x7FFF) < 0x800;
    }

    void append_cstring(MipsCode& code, const std::string& text)
    {
        code.insert(code.end(), text.begin(), text.end());
        code.add_byte(0);
    }

    uint32_t emit_pc_relative(MipsCode& code, const MipsRegister& dest)
    {
        // MIPS bgezal $zero, 1 links ra to the addiu below and falls through.
        code.add_long_le(0x04110001);
        code.nop();
        const uint32_t addiu_offset = static_cast<uint32_t>(code.size());
        uint32_t opcode = 0x24000000;
        opcode |= reg_RA.code() << 21;
        opcode |= dest.code() << 16;
        code.add_long_le(opcode);
        return addiu_offset;
    }

    void patch_addiu_delta(MipsCode& code, uint32_t instruction_offset, uint32_t target_offset)
    {
        const int32_t delta = static_cast<int32_t>(target_offset) - static_cast<int32_t>(instruction_offset);
        if(delta < -32768 || delta > 32767)
            throw RandomizerException("Remote item names do not fit next to the pickup lookup.");
        code.set_word_le(instruction_offset, static_cast<uint16_t>(delta));
    }

    struct TableRow
    {
        uint16_t flag = 0;
        std::string text;
    };

    // Same fields the lookup needs from a remote note. The note type itself is
    // private to the patch, so the helper takes this copy.
    struct RemoteFlags
    {
        std::string text;
        std::vector<uint16_t> flags;
    };

    std::vector<TableRow> lookup_rows(const std::vector<RemoteFlags>& notes)
    {
        std::map<uint16_t, std::string> chosen;
        std::set<uint16_t> ambiguous;
        for(const auto& note : notes)
        {
            for(uint16_t flag : note.flags)
            {
                if(!flag_is_lookup_key(flag))
                    continue;

                const auto found = chosen.find(flag);
                if(found == chosen.end())
                    chosen.emplace(flag, note.text);
                else if(found->second != note.text)
                    ambiguous.insert(flag);
            }
        }

        for(uint16_t flag : ambiguous)
            chosen.erase(flag);

        std::vector<TableRow> rows;
        rows.reserve(chosen.size());
        for(const auto& [flag, text] : chosen)
            rows.push_back(TableRow{ flag, text });
        return rows;
    }

    // s3 is the item id (sprite index - 0x1E). s1 is the pickup entity.
    // Returns the name pointer in v0. Does not write s0-s7: site A needs
    // s0 == s3 * 8 afterwards for the fanfare byte, and sites B and C own s0.
    //
    // A single generic item would fit in one table row, but the chest flag is
    // on the parent chest, so each chest can name its own remote item.
    // "AP Item" is the miss path: shops, scripted gifts, flag 0, and two
    // remotes that share one flag. Those checks still get a specific line
    // from the shop and NPC text patches. Merrick's menu reads a private
    // 8-byte info row instead of this table.
    MipsCode build_lookup(uint32_t& table_addiu, uint32_t& generic_addiu)
    {
        MipsCode code;
        code.set_(reg_T8, reg_RA);

        code.addiu(reg_T0, reg_ZERO, ITEM_REMOTE);
        code.bne(reg_S3, reg_T0, "local");

        code.lw(reg_T1, reg_S1, PICKUP_PARENT_OFFSET);
        code.beq(reg_T1, reg_ZERO, "miss");
        code.lw(reg_T1, reg_T1, CHEST_FLAG_OFFSET);
        table_addiu = emit_pc_relative(code, reg_T0);
        code.lw(reg_T2, reg_T0, 0);
        code.addiu(reg_T0, reg_T0, 4);

        code.label_("loop");
        code.beq(reg_T2, reg_ZERO, "miss");
        code.lw(reg_T3, reg_T0, 0);
        code.beq(reg_T3, reg_T1, "hit");
        code.addiu(reg_T0, reg_T0, 8);
        code.addiu(reg_T2, reg_T2, static_cast<uint16_t>(-1));
        code.bra_("loop");

        code.label_("hit");
        code.lw(reg_V0, reg_T0, 4);
        code.jr(reg_T8);

        code.label_("miss");
        generic_addiu = emit_pc_relative(code, reg_V0);
        code.jr(reg_T8);

        code.label_("local");
        code.add_long_le(0x001340C0); // sll t0, s3, 3
        code.lui(reg_AT, 0x800C);
        code.addu(reg_AT, reg_AT, reg_T0);
        code.lw(reg_V0, reg_AT, 0x7DE4);
        code.jr(reg_T8);
        return code;
    }

    // The sprite-bank loader adds its RAM base to a record once for every slot
    // that names it. Sharing Book of Elna's record would add that base again,
    // and the frame pointer would leave the bank. This header is a private
    // copy. Its four pointers still name the book's animation and frames,
    // which that loader does not relocate.
    void install_remote_sprite(BinaryFile& data)
    {
        const uint32_t book_offset = data.get_long_le(
            SPRITE_TABLE + static_cast<uint32_t>(BOOK_OF_ELNA_SPRITE) * 4);
        const uint32_t book_file = SPRITE_BANK_BASE + book_offset;
        const bool book_in_range = book_offset != 0 && book_offset != 0xFFFFFFFFu
            && book_file > book_offset
            && book_file + SPRITE_HEADER_SIZE > book_file
            && book_file + SPRITE_HEADER_SIZE <= data.size()
            && REMOTE_SPRITE_HEADER + SPRITE_HEADER_SIZE <= data.size();
        if(!book_in_range)
            throw RandomizerException("Book of Elna sprite record is outside the sprite bank.");

        for(uint32_t index = 0; index < SPRITE_HEADER_SIZE; ++index)
        {
            if(data.get_byte(REMOTE_SPRITE_HEADER + index) != 0)
                throw RandomizerException("Remote item sprite header would overwrite sprite-bank data.");
        }

        const ByteArray header = data.get_bytes(book_file, book_file + SPRITE_HEADER_SIZE);
        data.set_bytes(REMOTE_SPRITE_HEADER, header);
        data.set_long_le(SPRITE_TABLE + static_cast<uint32_t>(REMOTE_SPRITE) * 4,
                         REMOTE_SPRITE_HEADER - SPRITE_BANK_BASE);
    }
}

void PatchApplyItemSources::install_remote_lookup(PsxExeFile& exe, const std::vector<RemoteNote>& notes)
{
        expect_words(exe, NAME_SITE_A, SITE_A_WORDS, 4, "chest line");
        expect_words(exe, NAME_SITE_B, SITE_B_WORDS, 4, "obtained line");
        expect_words(exe, NAME_SITE_C, SITE_C_WORDS, 3, "inventory-full line");

        uint32_t table_addiu = 0;
        uint32_t generic_addiu = 0;
        MipsCode code = build_lookup(table_addiu, generic_addiu);

        const uint32_t generic_offset = static_cast<uint32_t>(code.size());
        append_cstring(code, GENERIC_REMOTE_NAME);

        std::map<std::string, uint32_t> text_offsets;
        for(const auto& note : notes)
        {
            if(text_offsets.contains(note.text))
                continue;
            text_offsets.emplace(note.text, static_cast<uint32_t>(code.size()));
            append_cstring(code, note.text);
        }

        while(code.size() % 4 != 0)
            code.add_byte(0);

        const uint32_t table_offset = static_cast<uint32_t>(code.size());
        std::vector<RemoteFlags> row_notes;
        row_notes.reserve(notes.size());
        for(const RemoteNote& note : notes)
            row_notes.push_back(RemoteFlags{ note.text, note.flags });
        const std::vector<TableRow> rows = lookup_rows(row_notes);
        code.add_long_le(static_cast<uint32_t>(rows.size()));
        std::vector<uint32_t> pointer_offsets;
        pointer_offsets.reserve(rows.size());
        for(const TableRow& row : rows)
        {
            code.add_long_le(row.flag);
            pointer_offsets.push_back(static_cast<uint32_t>(code.size()));
            code.add_long_le(0);
        }

        struct MerrickRecord
        {
            uint32_t file_offset = 0;
            uint32_t blob_offset = 0;
            std::string text;
        };
        std::vector<MerrickRecord> merrick_records;
        for(const auto& note : notes)
        {
            if(note.merrick_offset == 0)
                continue;
            MerrickRecord record;
            record.file_offset = note.merrick_offset;
            record.blob_offset = static_cast<uint32_t>(code.size());
            record.text = note.text;
            code.add_long_le(0);
            code.add_long_le(0);
            merrick_records.push_back(std::move(record));
        }

        const uint32_t base = exe.next_inject_address(static_cast<uint32_t>(code.size()));
        patch_addiu_delta(code, table_addiu, table_offset);
        patch_addiu_delta(code, generic_addiu, generic_offset);

        for(size_t index = 0; index < rows.size(); ++index)
        {
            const uint32_t name_va = base + text_offsets.at(rows[index].text);
            code.set_long_le(pointer_offsets[index], name_va);
        }

        for(const MerrickRecord& record : merrick_records)
        {
            code.set_long_le(record.blob_offset, base + text_offsets.at(record.text));
            code.set_byte(record.blob_offset + 5, 0x34);
        }

        const uint32_t injected = exe.inject_code(code);
        if(injected != base)
            throw RandomizerException("Remote item lookup was injected at an unexpected address.");

        const uint32_t generic_va = base + generic_offset;
        const uint32_t info_address = ITEM_INFO_2_FILE + ITEM_REMOTE * 8;
        exe.set_long_le(info_address, generic_va);
        exe.set_byte(info_address + 5, 0x34);
        exe.set_word_le(ITEM_QTY_FILE + ITEM_REMOTE * 10 + 6, 0);

        for(const MerrickRecord& record : merrick_records)
            exe.set_long_le(record.file_offset, base + record.blob_offset);

        // Site A: the delay slot restores s0 = s3 * 8 before the call.
        MipsCode site_a;
        site_a.jal(injected, false);
        site_a.add_long_le(0x001380C0);
        site_a.addu(reg_A1, reg_V0, reg_ZERO);
        site_a.nop();
        exe.set_code(NAME_SITE_A, site_a);

        MipsCode site_b;
        site_b.jal(injected, false);
        site_b.nop();
        site_b.addu(reg_A1, reg_V0, reg_ZERO);
        site_b.nop();
        exe.set_code(NAME_SITE_B, site_b);

        MipsCode site_c;
        site_c.jal(injected, false);
        site_c.nop();
        site_c.addu(reg_A1, reg_V0, reg_ZERO);
        exe.set_code(NAME_SITE_C, site_c);
}

void PatchApplyItemSources::alter_datas_file(BinaryFile& data, const GameData&, const RandomizerWorld& world)
{
    _remotes.clear();

    bool any_remote = false;
    for(ItemSource* source : world.item_sources())
    {
        if(source->is_remote())
            any_remote = true;
    }

    const std::map<uint32_t, uint16_t> chest_flags = any_remote ? read_chest_flags(data)
                                                                : std::map<uint32_t, uint16_t>{};

    for(ItemSource* source : world.item_sources())
    {
        const bool remote = source->is_remote();
        const uint8_t item_id = remote ? ITEM_REMOTE
                                       : (source->item() ? source->item()->id() : ITEM_NONE);

        for(uint32_t addr : source->addresses())
        {
#ifdef DEBUG
            if(source->vanilla_item() && data.get_byte(addr) != source->vanilla_item()->id())
                std::cerr << "ItemSource '" << source->name() << "' doesn't contain the expected vanilla item" << std::endl;
#endif
            data.set_byte(addr, item_id);
        }

        const uint8_t item_sprite_id = item_id + 0x1E;
        for(uint32_t addr : source->sprite_addresses())
        {
#ifdef DEBUG
            if(source->vanilla_item() && data.get_byte(addr) != source->vanilla_item()->id() + 0x1E
               && source->vanilla_item()->id() != ITEM_BOOK_OF_ELNA)
                std::cerr << "ItemSource '" << source->name() << "' doesn't contain the expected vanilla item" << std::endl;
#endif
            if(item_id == ITEM_NONE)
            {
                for(uint32_t sub_addr = addr + 4; sub_addr <= addr + 13; ++sub_addr)
                    data.set_byte(sub_addr, 0x00);
            }

            data.set_byte(addr, item_sprite_id);
        }

        // A shop's price words are the disc's own price. A remote item has no
        // gold value, so those words stay. An Alundra item still writes its gold.
        if(!source->price_addresses().empty() && !remote)
        {
            if(source->item() == nullptr)
            {
                throw RandomizerException("Item source '" + source->name()
                                          + "' has a shop price and no item.");
            }

            for(uint32_t addr : source->price_addresses())
                data.set_word_le(addr, source->item()->gold_value());
        }

        if(!remote)
            continue;

        RemoteNote note;
        note.text = source->contents_name();
        note.merrick_offset = source->merrick_item_address();
        for(uint32_t addr : source->addresses())
        {
            const auto found = chest_flags.find(addr);
            if(found != chest_flags.end())
                note.flags.push_back(found->second);
        }
        _remotes.push_back(std::move(note));
    }

    if(any_remote)
        install_remote_sprite(data);

    // Fix ground Book of Elna byte causing trouble
    data.set_byte(0x1BA5D05, 0x00);

    // Change the "repeating" slot at Merrick's shop visually into a open chest
    data.set_byte(0x1EBF9CE, 0x1E);
}

void PatchApplyItemSources::alter_exe_file(PsxExeFile& exe, const GameData&, const RandomizerWorld& world)
{
    for(ItemSource* source : world.item_sources())
    {
        const uint32_t merrick_item_address = source->merrick_item_address();
        if(merrick_item_address == 0 || source->is_remote())
            continue;

        const uint8_t item_id = source->item() ? source->item()->id() : ITEM_NONE;
        exe.set_long_le(merrick_item_address, ITEM_INFO_2_VA + 8 * item_id);
    }

    // GiveItem sends 0x5A past its 0x45..0x56 jump table into TryIncrease.
    // Max quantity 0 means that call adds nothing. The three name hooks
    // replace the pointer load; FlagOn still runs with the chest flag.
    if(!_remotes.empty())
        install_remote_lookup(exe, _remotes);
}
