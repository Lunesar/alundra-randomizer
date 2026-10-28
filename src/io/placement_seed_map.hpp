#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "../model/item_source.hpp"
#include "../tools/json.hpp"

class GameData;
class RandomizerOptions;
class RandomizerWorld;

/**
 * Placement seed map: the handoff between item randomization and disc patching.
 * An Archipelago world (or --placement-out) writes it; --placement-in consumes it
 * on another machine to patch the image and generate hints.
 *
 * The Alundra format ("format": 1) is a JSON object:
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
 * A bare object { "1": 57, "2": 33 } is also accepted as the Alundra format.
 * Settings then come from --preset or --permalink. Location ids are ItemSource
 * ids, decimal strings with no leading zeros. Item ids are Alundra item ids in
 * [0, ITEM_COUNT).
 *
 * The Multiworld format is the same object with "format": 2. A placement value
 * is either an Alundra item id, as in the Alundra format, or a remote item from
 * another game:
 * {
 *   "format": 2,
 *   "apworldVersion": "0.1.5",   // optional; major.minor.patch of the world that wrote the file
 *   "player": "YourName",        // optional; this slot's login name, not a remote disc line
 *   "seed": 1,
 *   "gameSettings": { ... },
 *   "randomizerSettings": {
 *     "itemsDistribution": { "Herbs": 288 },   // Alundra items only; omit remote checks
 *     "startingInventory": ["Dagger", "Cloth Armor", "Short Boots"]
 *   },
 *   "placements": {
 *     "1": { "player": "Primus", "item": "Progressive Sword", "classification": "progression" },
 *     "5": 83,
 *     "262": { "player": "Secondus", "item": "Hookshot", "classification": "useful" },
 *     "273": { "player": "Quartus", "item": "Boss Key", "classification": "progression", "price": 100 }
 *   }
 * }
 *
 * A remote object has these keys:
 * - "player": the sending player's name, 1..16 printable ASCII characters, no '\\'
 * - "item": that player's item name, 1..28 printable ASCII characters, no '\\'
 * - "classification": "progression", "useful", "filler", or "trap"
 * - "price": optional integer, 0..9999 gilder. The patch writes that word on a
 *   source that has a price address. Omit it and the shelf keeps the price
 *   already in the disc. A price on a source with no price address is rejected.
 *
 * The top-level "player" is this slot's login name. It is not shortened to 16
 * characters and it may contain characters the disc line refuses. The top-level
 * "apworldVersion" is the world that wrote the file. Both are optional on read,
 * so an older file still patches, and both are rejected on the Alundra format.
 * When present, apworldVersion is major.minor.patch and player is a non-empty
 * string with no ASCII control characters. seed stays the unsigned 32-bit
 * randomizer seed. The reader keeps apworldVersion and player on Contents.
 *
 * The disc shows "player - item" on pickup (47 characters at most). classification
 * is stored and otherwise ignored. "trap" does not change the game. There is no
 * network client and no way for an Alundra item to be sent to another game:
 * --placement-out still writes the Alundra format ("format": 1) and refuses a
 * world that holds a remote item.
 *
 * Do not put a remote object in itemsDistribution or startingInventory. Those
 * fields stay lists of Alundra item names. A remote check is not an Alundra item
 * and must not add to any itemsDistribution count. gameSettings and
 * randomizerSettings keep the Alundra format's meaning.
 *
 * Emit the real player and item names. One generic stand-in is not this file's
 * format. The disc keeps a single reserved item id for every remote check and
 * looks the name up from the chest's collected flag. "AP Item" is only what the
 * chest popup says when that flag is missing (a shop, a scripted gift, or the
 * chest whose flag is 0). Shops and NPC lines still use the specific name.
 * A remote item has no gold value. Its shop price is the "price" field when
 * that field is present, and otherwise the price already in the disc.
 * See tests/fixtures/placement_multiworld_format.json.
 */
namespace placement_seed_map
{
    enum class PlacementFormat : uint8_t
    {
        Alundra = 1,
        Multiworld = 2,
    };

    inline constexpr std::size_t REMOTE_PLAYER_NAME_MAX = 16;
    inline constexpr std::size_t REMOTE_ITEM_NAME_MAX = 28;

    struct Placement
    {
        bool remote = false;
        uint8_t item_id = 0;
        RemoteContents remote_item;
    };

    struct Contents
    {
        std::string permalink;
        Json options_json = Json::object();
        bool has_options = false;
        // Empty when the Multiworld file omits them. The Alundra format leaves both empty.
        std::string apworld_version;
        std::string player;
        std::map<uint16_t, Placement> placements;
    };

    Contents read(const std::filesystem::path& path);
    void write(const std::filesystem::path& path, const RandomizerOptions& options, const RandomizerWorld& world);
    void apply(RandomizerWorld& world, const GameData& game_data, const std::map<uint16_t, Placement>& placements);
}
