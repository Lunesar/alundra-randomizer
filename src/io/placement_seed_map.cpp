#include "placement_seed_map.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

#include "../constants/item_codes.hpp"
#include "../game/game_data.hpp"
#include "../model/item_source.hpp"
#include "../model/randomizer_world.hpp"
#include "../randomizer_options.hpp"
#include "../tools/exception.hpp"

namespace
{
    constexpr int format_number(placement_seed_map::PlacementFormat format)
    {
        return static_cast<int>(format);
    }

    bool parse_location_id(const std::string& key, uint16_t& location_id)
    {
        if(key.empty())
            return false;

        for(char character : key)
        {
            if(character < '0' || character > '9')
                return false;
        }

        uint16_t value = 0;
        const char* begin = key.data();
        const char* end = key.data() + key.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if(result.ec != std::errc() || result.ptr != end)
            return false;

        // Reject leading zeros so "01" and "1" cannot both appear.
        if(std::to_string(value) != key)
            return false;

        location_id = value;
        return true;
    }

    void require_remote_text(const std::string& text, const std::string& field, std::size_t limit,
                             const std::string& location_key)
    {
        if(text.empty() || text.size() > limit)
        {
            throw RandomizerException("Remote " + field + " for location " + location_key
                                      + " must be 1.." + std::to_string(limit) + " characters.");
        }

        bool non_space = false;
        for(unsigned char character : text)
        {
            if(character < 0x20 || character > 0x7E || character == '\\')
            {
                throw RandomizerException("Remote " + field + " for location " + location_key
                                          + " must be printable ASCII without backslash controls.");
            }
            if(character != ' ')
                non_space = true;
        }

        if(!non_space)
        {
            throw RandomizerException("Remote " + field + " for location " + location_key
                                      + " must contain a visible character.");
        }
    }

    RemoteClassification read_classification(const std::string& text, const std::string& location_key)
    {
        if(text == "progression")
            return RemoteClassification::Progression;
        if(text == "useful")
            return RemoteClassification::Useful;
        if(text == "filler")
            return RemoteClassification::Filler;
        if(text == "trap")
            return RemoteClassification::Trap;

        throw RandomizerException("Remote classification for location " + location_key
                                  + " must be progression, useful, filler, or trap.");
    }

    RemoteContents read_remote_object(const Json& value, const std::string& location_key)
    {
        if(!value.is_object())
        {
            throw RandomizerException("Placement for location " + location_key
                                      + " must be an item id or a remote item object.");
        }

        for(auto& [key, _] : value.items())
        {
            if(key == "player" || key == "item" || key == "classification")
                continue;
            throw RandomizerException("Unknown key '" + key + "' in remote item for location " + location_key + ".");
        }

        if(!value.contains("player") || !value.at("player").is_string()
           || !value.contains("item") || !value.at("item").is_string()
           || !value.contains("classification") || !value.at("classification").is_string())
        {
            throw RandomizerException("Remote item for location " + location_key
                                      + " needs string keys player, item, and classification.");
        }

        RemoteContents remote;
        remote.player = value.at("player").get<std::string>();
        remote.item = value.at("item").get<std::string>();
        require_remote_text(remote.player, "player", placement_seed_map::REMOTE_PLAYER_NAME_MAX, location_key);
        require_remote_text(remote.item, "item", placement_seed_map::REMOTE_ITEM_NAME_MAX, location_key);
        remote.classification = read_classification(value.at("classification").get<std::string>(), location_key);
        return remote;
    }

    placement_seed_map::Placement read_item_id(const Json& value, const std::string& location_key)
    {
        if(!value.is_number_integer())
        {
            throw RandomizerException("Placement item id for location " + location_key
                                      + " must be an integer item id.");
        }

        const int64_t raw_item_id = value.get<int64_t>();
        if(raw_item_id < 0 || raw_item_id >= ITEM_COUNT)
        {
            throw RandomizerException("Placement item id " + std::to_string(raw_item_id)
                                      + " for location " + location_key + " is not a valid item id.");
        }

        placement_seed_map::Placement placement;
        placement.item_id = static_cast<uint8_t>(raw_item_id);
        return placement;
    }

    std::map<uint16_t, placement_seed_map::Placement> read_placements_object(const Json& object, placement_seed_map::PlacementFormat format)
    {
        if(!object.is_object())
        {
            throw RandomizerException("Placement seed map \"placements\" must be a JSON object "
                                      "mapping location ids to item ids.");
        }
        if(object.empty())
            throw RandomizerException("Placement seed map contains no locations.");

        std::map<uint16_t, placement_seed_map::Placement> placements;
        for(auto& [key, value] : object.items())
        {
            uint16_t location_id = 0;
            if(!parse_location_id(key, location_id))
            {
                throw RandomizerException("Placement location id '" + key
                                          + "' must be a decimal id with no leading zeros.");
            }

            placement_seed_map::Placement placement;
            if(format == placement_seed_map::PlacementFormat::Multiworld && value.is_object())
            {
                placement.remote = true;
                placement.remote_item = read_remote_object(value, key);
            }
            else
            {
                placement = read_item_id(value, key);
            }

            placements.emplace(location_id, std::move(placement));
        }

        return placements;
    }

    bool is_bare_placement_map(const Json& json)
    {
        if(!json.is_object() || json.empty())
            return false;

        for(auto& [key, _] : json.items())
        {
            uint16_t location_id = 0;
            if(!parse_location_id(key, location_id))
                return false;
        }

        return true;
    }

    void read_optional_string(const Json& json, const char* key, std::string& output)
    {
        if(!json.contains(key))
            return;

        if(!json.at(key).is_string())
            throw RandomizerException(std::string("Placement seed map \"") + key + "\" must be a string.");

        output = json.at(key).get<std::string>();
    }

    std::map<uint16_t, ItemSource*> index_item_sources(const RandomizerWorld& world)
    {
        std::map<uint16_t, ItemSource*> by_location_id;
        for(ItemSource* source : world.item_sources())
        {
            const uint16_t location_id = source->id();
            if(location_id == 0)
                throw RandomizerException("Item source '" + source->name() + "' has no location id.");

            if(by_location_id.contains(location_id))
            {
                throw RandomizerException("Duplicate location id " + std::to_string(location_id)
                                          + " on item source '" + source->name() + "'.");
            }

            by_location_id.emplace(location_id, source);
        }

        return by_location_id;
    }
}

placement_seed_map::Contents placement_seed_map::read(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if(!input)
        throw RandomizerException("Could not open placement seed map '" + path.string() + "'.");

    std::stringstream buffer;
    buffer << input.rdbuf();

    Json json;
    try
    {
        json = Json::parse(buffer.str(), nullptr, true, true);
    }
    catch(const Json::parse_error& error)
    {
        throw RandomizerException("Malformed placement seed map '" + path.string() + "': " + error.what());
    }

    if(!json.is_object())
        throw RandomizerException("Placement seed map '" + path.string() + "' must be a JSON object.");

    Contents contents;
    if(is_bare_placement_map(json))
    {
        contents.placements = read_placements_object(json, placement_seed_map::PlacementFormat::Alundra);
        return contents;
    }

    placement_seed_map::PlacementFormat format = placement_seed_map::PlacementFormat::Alundra;
    if(json.contains("format"))
    {
        if(!json.at("format").is_number_integer())
        {
            throw RandomizerException("Unsupported placement seed map format in '" + path.string()
                                      + "'. This build reads Alundra (1) and Multiworld (2).");
        }

        const int format_value = json.at("format").get<int>();
        if(format_value == format_number(placement_seed_map::PlacementFormat::Alundra))
            format = placement_seed_map::PlacementFormat::Alundra;
        else if(format_value == format_number(placement_seed_map::PlacementFormat::Multiworld))
            format = placement_seed_map::PlacementFormat::Multiworld;
        else
        {
            throw RandomizerException("Unsupported placement seed map format in '" + path.string()
                                      + "'. This build reads Alundra (1) and Multiworld (2).");
        }
    }

    read_optional_string(json, "permalink", contents.permalink);
    if(json.contains("permalink") && contents.permalink.empty())
        throw RandomizerException("Placement seed map permalink is empty.");

    std::string hash_sentence;
    read_optional_string(json, "hashSentence", hash_sentence);
    (void)hash_sentence;

    if(!contents.permalink.empty())
    {
        if(json.contains("seed") || json.contains("gameSettings") || json.contains("randomizerSettings"))
        {
            throw RandomizerException("Placement seed map '" + path.string()
                                      + "' contains a permalink and also seed or settings. Keep only one.");
        }

        contents.has_options = true;
        contents.options_json["permalink"] = contents.permalink;
    }
    else
    {
        if(json.contains("seed"))
        {
            if(!json.at("seed").is_number_integer())
                throw RandomizerException("Placement seed map \"seed\" must be an integer.");

            const int64_t seed = json.at("seed").get<int64_t>();
            if(seed < 0 || seed > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
                throw RandomizerException("Placement seed map \"seed\" does not fit in an unsigned 32-bit value.");

            contents.options_json["seed"] = seed;
        }
        if(json.contains("gameSettings"))
        {
            if(!json.at("gameSettings").is_object())
                throw RandomizerException("Placement seed map \"gameSettings\" must be a JSON object.");
            contents.options_json["gameSettings"] = json.at("gameSettings");
        }
        if(json.contains("randomizerSettings"))
        {
            if(!json.at("randomizerSettings").is_object())
                throw RandomizerException("Placement seed map \"randomizerSettings\" must be a JSON object.");
            contents.options_json["randomizerSettings"] = json.at("randomizerSettings");
        }

        contents.has_options = !contents.options_json.empty();
    }

    if(!json.contains("placements"))
    {
        throw RandomizerException("Placement seed map '" + path.string()
                                  + "' is missing \"placements\" (location id to item id).");
    }

    contents.placements = read_placements_object(json.at("placements"), format);

    for(auto& [key, _] : json.items())
    {
        if(key == "format" || key == "permalink" || key == "hashSentence" || key == "seed"
           || key == "gameSettings" || key == "randomizerSettings" || key == "placements")
            continue;

        throw RandomizerException("Unknown key '" + key + "' in placement seed map '" + path.string()
                                  + "'. Location ids belong inside \"placements\".");
    }

    return contents;
}

void placement_seed_map::write(const std::filesystem::path& path, const RandomizerOptions& options, const RandomizerWorld& world)
{
    const std::map<uint16_t, ItemSource*> by_location_id = index_item_sources(world);

    Json placements = Json::object();
    for(const auto& [location_id, source] : by_location_id)
    {
        if(source->is_remote())
        {
            throw RandomizerException("Location " + std::to_string(location_id) + " (" + source->name()
                                      + ") holds another world's item. This program writes the Alundra format and cannot "
                                        "send an Alundra item out, or record a remote item.");
        }

        if(source->item() == nullptr)
        {
            throw RandomizerException("Location " + std::to_string(location_id) + " (" + source->name()
                                      + ") has no item to record in the placement seed map.");
        }

        placements[std::to_string(location_id)] = static_cast<int>(source->item()->id());
    }

    Json json = Json::object();
    json["format"] = format_number(placement_seed_map::PlacementFormat::Alundra);
    json["permalink"] = options.permalink();
    json["hashSentence"] = options.hash_sentence();
    json["placements"] = std::move(placements);

    std::ofstream output(path);
    if(!output)
        throw RandomizerException("Could not open placement seed map for writing at path '" + path.string() + "'.");

    output << json.dump(4);
    if(!output)
        throw RandomizerException("Could not write placement seed map at path '" + path.string() + "'.");
}

void placement_seed_map::apply(RandomizerWorld& world, const GameData& game_data,
                               const std::map<uint16_t, Placement>& placements)
{
    const std::map<uint16_t, ItemSource*> by_location_id = index_item_sources(world);

    for(const auto& [location_id, placement] : placements)
    {
        const auto found = by_location_id.find(location_id);
        if(found == by_location_id.end())
        {
            throw RandomizerException("Placement seed map refers to unknown location id "
                                      + std::to_string(location_id) + ".");
        }

        if(placement.remote)
        {
            found->second->remote(placement.remote_item);
            continue;
        }

        if(placement.item_id >= ITEM_COUNT)
        {
            throw RandomizerException("Placement item id " + std::to_string(placement.item_id) + " for location "
                                      + std::to_string(location_id) + " is not a valid item id.");
        }

        found->second->item(game_data.item(placement.item_id));
    }

    for(const auto& [location_id, source] : by_location_id)
    {
        if(!placements.contains(location_id))
        {
            throw RandomizerException("Placement seed map is missing location id " + std::to_string(location_id)
                                      + " (" + source->name() + ").");
        }
    }
}
