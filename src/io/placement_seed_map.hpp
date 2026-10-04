#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "../tools/json.hpp"

class GameData;
class RandomizerOptions;
class RandomizerWorld;

/**
 * Placement seed map: the handoff between item randomization and disc patching.
 * An Archipelago world (or --placement-out) writes it; --placement-in consumes it
 * on another machine to patch the image and generate hints.
 *
 * Format 1 is a JSON object:
 * {
 *   "format": 1,
 *   "permalink": "a.../",            // seed + every setting; enough to patch elsewhere
 *   "hashSentence": "...",           // informational, ignored on read
 *   "seed": 1,                       // used only when "permalink" is absent
 *   "gameSettings": { ... },         // preset format; used only when "permalink" is absent
 *   "randomizerSettings": { ... },   // prePlacedCrests, when present, replaces the crest shuffle
 *   "placements": { "1": 57 }        // location id -> in-game item id
 * }
 *
 * A bare object { "1": 57, "2": 33 } is also accepted. Settings then come from
 * --preset or --permalink. Location ids are ItemSource ids. Item ids are the
 * in-game ids.
 */
namespace placement_seed_map
{
    struct Contents
    {
        std::string permalink;
        Json options_json = Json::object();
        bool has_options = false;
        std::map<uint16_t, uint8_t> placements;
    };

    Contents read(const std::filesystem::path& path);
    void write(const std::filesystem::path& path, const RandomizerOptions& options, const RandomizerWorld& world);
    void apply(RandomizerWorld& world, const GameData& game_data, const std::map<uint16_t, uint8_t>& placements);
}
