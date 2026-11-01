#pragma once

#include "game_patch.hpp"
#include "../constants/flags.hpp"
#include "../tools/exception.hpp"

/**
 * Fairy Pond datas changes. Keeping the pond open after the reward belongs here too.
 */
class PatchFairyPond : public GamePatch
{
public:
    void alter_datas_file(BinaryFile& data, const GameData&, const RandomizerWorld&) override
    {
        give_reward_chest_a_collected_flag(data);
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
};
