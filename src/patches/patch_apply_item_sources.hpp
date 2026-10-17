#pragma once

#include "game_patch.hpp"

#include <cstdint>
#include <string>
#include <vector>

/**
 * Writes each item source into DATAS.BIN and, when a source holds another
 * world's item, into the EXE name lookup described in the .cpp file.
 */
class PatchApplyItemSources : public GamePatch
{
public:
    void alter_datas_file(BinaryFile& data, const GameData& game_data, const RandomizerWorld& world) override;
    void alter_exe_file(PsxExeFile& exe, const GameData& game_data, const RandomizerWorld& world) override;

private:
    struct RemoteNote
    {
        std::string text;
        std::vector<uint16_t> flags;
        uint32_t merrick_offset = 0;
    };

    std::vector<RemoteNote> _remotes;

    static void install_remote_lookup(PsxExeFile& exe, const std::vector<RemoteNote>& notes);
};
