#pragma once

#include "game_patch.hpp"
#include "../constants/flags.hpp"
#include "../tools/exception.hpp"

/**
 * Fairy Pond datas changes. The reward chest gets a collected flag, and the
 * scene no longer seals the pond.
 */
class PatchFairyPond : public GamePatch
{
public:
    void alter_datas_file(BinaryFile& data, const GameData&, const RandomizerWorld&) override
    {
        give_reward_chest_a_collected_flag(data);
        keep_the_pond_open(data);
    }

private:
    /**
     * Map 161 entity 6 stores collected flag 0. FlagOn(0) sets nothing, so the
     * chest is full on every visit. The open path already calls FlagOn with the
     * halfword copied onto the chest entity. Item placement overwrites the
     * contents byte at 0x24A57C6 on its own.
     */
    static void give_reward_chest_a_collected_flag(BinaryFile& data)
    {
        constexpr uint32_t FLAG_ADDRESS = 0x24A57C4;

        if (data.get_word_le(FLAG_ADDRESS) != 0)
            throw RandomizerException("Fairy Pond reward chest already has a collected flag.");

        data.set_word_le(FLAG_ADDRESS, FLAG_FAIRY_POND_REWARD_CHEST.event_code());
    }

    /**
     * Map 161 B[1] sets 0x03A3 at the end of Stenia's scene. That bit closes
     * the way back, removes the whirlpool, and arms the only exit. Maps 158,
     * 159, and 160 wait on it, then spawn the blocks. Map 7 B[3] removes the
     * whirlpool when it is on. Map 158 B[4] is the entrance pad at
     * (5,54)-(6,55). It waits on the same bit, then warps to map 7 at (2, 29).
     *
     * Both of those instructions are 3 bytes. Goto +3 falls into the End after
     * the seal, and into the ChangeMap after the pad's wait, so the pad works
     * before the scene. Opcode 0x01 returns an advance of 0 and would stall
     * the script on that byte.
     *
     * Map 7 B[3] sends anyone on tiles x=1-3, y=28-30 into the pond. (2, 29)
     * is the middle of that block, so the vanilla landing steps straight back
     * in. (2, 27) is the walkable tile north of it. The whirlpool's own
     * ChangeMap lands on the pad, at (5, 55). An always-live pad would warp
     * that spawn straight back out. (5, 53) is the floor tile north of it.
     * 0x03A3 stays clear, so the blocks never spawn and the whirlpool stays.
     */
    static void keep_the_pond_open(BinaryFile& data)
    {
        constexpr uint32_t SEAL_FLAG_ON = 0x24A56B6;
        constexpr uint32_t EXIT_PAD_WAIT = 0x23F4EB0;
        constexpr uint32_t EXIT_LANDING_Y = 0x23F4EB7;
        constexpr uint32_t WHIRLPOOL_ENTRY_Y = 0x29B09F;

        // Goto +3. The handler returns the signed halfword as the advance.
        data.set_bytes(SEAL_FLAG_ON, { 0x02, 0x03, 0x00 });
        data.set_bytes(EXIT_PAD_WAIT, { 0x02, 0x03, 0x00 });
        data.set_byte(EXIT_LANDING_Y, 27);
        data.set_byte(WHIRLPOOL_ENTRY_Y, 53);
    }
};
