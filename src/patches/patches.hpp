#pragma once

#include <memory>
#include <vector>

class RandomizerOptions;
class GamePatch;
class BinaryFile;
class PsxExeFile;
class GameData;
class RandomizerWorld;

namespace slot_block { struct Identity; }

void execute_patches(const std::vector<std::unique_ptr<GamePatch>>& patches,
                     BinaryFile& data_file, PsxExeFile& exe_file,
                     GameData& game_data, RandomizerWorld& world);

void apply_randomizer_patches(BinaryFile& data, PsxExeFile& exe,
                              GameData& game_data, RandomizerWorld& world,
                              const RandomizerOptions& options,
                              const slot_block::Identity* slot = nullptr);
