#include "randomizer_world.hpp"

#include "../game/game_data.hpp"
#include "world_node.hpp"
#include "world_path.hpp"
#include "world_region.hpp"
#include "hint_source.hpp"
#include "../randomizer_options.hpp"

// Include headers automatically generated from model json files
#include "data/item_source.json.hxx"
#include "data/hint_source.json.hxx"
#include "data/world_node.json.hxx"
#include "data/world_path.json.hxx"
#include "data/world_region.json.hxx"

#include "../tools/exception.hpp"
#include "../tools/vectools.hpp"
#include "../constants/flags.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>

RandomizerWorld::RandomizerWorld(const GameData& game_data)
{
    this->init_nodes();
    this->init_regions();
    this->init_paths(game_data);
    this->init_item_sources(game_data);
    this->init_hint_sources();
}

RandomizerWorld::~RandomizerWorld()
{
    for (ItemSource* source : _item_sources)
        delete source;
    for (auto& [key, node] : _nodes)
        delete node;
    for (WorldPath* path : _paths)
        delete path;
    for (WorldRegion* region : _regions)
        delete region;
    for (auto& [source_id, source] : _hint_sources)
        delete source;
}

ItemSource* RandomizerWorld::item_source(const std::string& name) const
{
    for (ItemSource* source : _item_sources)
        if (source->pretty_name() == name)
            return source;

    throw std::out_of_range("No ItemSource with name '" + name + "'");
}

uint16_t RandomizerWorld::item_source_id(const ItemSource* source) const
{
    for(size_t i=0 ; i<_item_sources.size() ; ++i)
        if(source == _item_sources[i])
            return (uint16_t)i;

    throw std::out_of_range("ItemSource was not found when looking for its ID");
}

std::vector<ItemSource*> RandomizerWorld::item_sources_with_item(const Item* item) const
{
    std::vector<ItemSource*> sources_with_item;

    for (ItemSource* source : _item_sources)
        if (source->item() == item)
            sources_with_item.emplace_back(source);

    return sources_with_item;
}

std::vector<WorldPath*> RandomizerWorld::paths_starting_from_node(const WorldNode* node) const
{
    std::vector<WorldPath*> ret;
    for(WorldPath* path : _paths)
    {
        if(path->origin() == node || (path->is_two_way() && path->destination() == node))
            ret.emplace_back(path);
    }
    return ret;
}

WorldRegion* RandomizerWorld::region(const std::string& name) const
{
    for(WorldRegion* region : _regions)
        if(region->name() == name)
            return region;
    return nullptr;
}

void RandomizerWorld::init_item_sources(const GameData& game_data)
{
    Json item_sources_json = Json::parse(ITEM_SOURCES_JSON);
    for(const Json& source_json : item_sources_json)
    {
        _item_sources.emplace_back(ItemSource::from_json(source_json, game_data, *this));
    }

#ifdef DEBUG
    std::cout << _item_sources.size() << " item sources loaded." << std::endl;
#endif
}

void RandomizerWorld::init_nodes()
{
    Json nodes_json = Json::parse(WORLD_NODES_JSON);
    for(auto& [node_id, node_json] : nodes_json.items())
    {
        WorldNode* new_node = WorldNode::from_json(node_json, node_id);
        _nodes[node_id] = new_node;
    }

#ifdef DEBUG
    std::cout << _nodes.size() << " nodes loaded." << std::endl;
#endif
}

void RandomizerWorld::init_paths(const GameData& game_data)
{
    Json paths_json = Json::parse(WORLD_PATHS_JSON);
    for(const Json& path_json : paths_json)
    {
        WorldPath* path = WorldPath::from_json(path_json, _nodes, game_data);
        this->add_path(path);
    }

#ifdef DEBUG
    std::cout << _paths.size() << " paths loaded." << std::endl;
#endif
}

void RandomizerWorld::init_regions()
{
    Json regions_json = Json::parse(WORLD_REGIONS_JSON);
    for(const Json& region_json : regions_json)
        _regions.emplace_back(WorldRegion::from_json(region_json, *this));

#ifdef DEBUG
    std::cout << _regions.size() << " regions loaded." << std::endl;
#endif

    // Check for orphan nodes
    for(auto& [id, node] : _nodes)
        if(node->region() == nullptr)
            throw RandomizerException("Node '" + node->id() + "' doesn't belong to any region");
}

void RandomizerWorld::init_hint_sources()
{
    Json hint_sources_json = Json::parse(HINT_SOURCES_JSON);
    for(auto& [hint_source_id, hint_source_json] : hint_sources_json.items())
    {
        _hint_sources[hint_source_id] = HintSource::from_json(hint_source_json, *this);
    }

#ifdef DEBUG
    std::cout << _hint_sources.size() << " hint sources loaded." << std::endl;
#endif
}

void RandomizerWorld::apply_options(const RandomizerOptions& options, const GameData& game_data)
{
    // Apply item sources which contents were set inside the preset configuration
    for(auto& [source_id, item_id] : options.fixed_item_sources())
        _item_sources[source_id]->item(game_data.item(item_id));

    // Mark item sources that cannot contain progression with a specific flag
    for(uint16_t source_id : options.item_sources_without_progression())
        _item_sources[source_id]->can_contain_progression(false);

    // Keep an editable copy of the items distribution from rando options
    _items_distribution = options.items_distribution();

    // Apply starting inventory
    for(uint8_t item_id : options.starting_inventory())
        _starting_inventory.emplace_back(game_data.item(item_id));

    // Handle progressive items in distribution
    if(options.progressive_items())
        this->handle_progressive_items(options, game_data);

    this->handle_crests(options, game_data);

    if(options.skip_last_dungeon())
    {
        WorldPath* lake_shrine_direct_path = new WorldPath();
        lake_shrine_direct_path->origin(this->node("lake_shrine_exterior"));
        lake_shrine_direct_path->destination(this->node("melzas_fight"));
        _paths.emplace_back(lake_shrine_direct_path);
    }
}

void RandomizerWorld::handle_crests(const RandomizerOptions& options, const GameData& game_data)
{
    const std::map<uint8_t, std::string> crest_nodes = {
            { ITEM_RUBY_CREST,      "crest_ruby" },
            { ITEM_SAPPHIRE_CREST,  "crest_sapphire" },
            { ITEM_TOPAZ_CREST,     "crest_topaz" },
            { ITEM_AGATE_CREST,     "crest_agate" },
            { ITEM_EMERALD_CREST,   "crest_emerald" },
            { ITEM_GARNET_CREST,    "crest_garnet" },
            { ITEM_DIAMOND_CREST,   "crest_diamond" }
    };

    std::vector<uint8_t> excluded_crests = {
            ITEM_RUBY_CREST, ITEM_SAPPHIRE_CREST, ITEM_TOPAZ_CREST, ITEM_AGATE_CREST,
            ITEM_EMERALD_CREST, ITEM_GARNET_CREST, ITEM_DIAMOND_CREST
    };

    const std::vector<uint8_t>& used_crests = game_data.used_crests();

    // Add randomly selected crests to item distribution
    for(uint8_t crest_id : used_crests)
    {
        _items_distribution[crest_id] += 1;
        vectools::erase_first(excluded_crests, crest_id);
    }

    WorldNode* origin = this->node("overworld_b1");
    WorldNode* destination = this->node("lake_shrine_exterior");
    WorldPath* path_to_lake_shrine = nullptr;
    for(WorldPath* path : _paths)
    {
        if(path->origin() != origin || path->destination() != destination)
            continue;

        path_to_lake_shrine = path;
        break;
    }

    if(!path_to_lake_shrine)
        throw RandomizerException("Could not find path to Lake Shrine on crests initialization");

    // Remove each excluded crest from the logic requirements to reach Lake Shrine
    for(uint8_t crest_id : excluded_crests)
    {
        WorldNode* crest_node_to_remove = this->node(crest_nodes.at(crest_id));
        vectools::erase_first(path_to_lake_shrine->required_nodes(), crest_node_to_remove);
    }
}

namespace
{
    // Tier stored on the "upgrade" item. Equipment tiers start at 0.
    constexpr uint16_t UPGRADE_TIER = 64;

    struct SlotTiers
    {
        const Item* upgrade = nullptr;
        std::vector<const Item*> items;
    };

    std::vector<uint16_t> progressive_slot_ids(const GameData& game_data, bool include_boots)
    {
        std::vector<uint16_t> slots = {
                game_data.item(ITEM_SWORD_UPGRADE)->slot_id(),
                game_data.item(ITEM_BOW_UPGRADE)->slot_id(),
                game_data.item(ITEM_FLAIL_UPGRADE)->slot_id(),
                game_data.item(ITEM_ARMOR_UPGRADE)->slot_id(),
                game_data.item(ITEM_EARTH_MAGIC_UPGRADE)->slot_id(),
                game_data.item(ITEM_WATER_MAGIC_UPGRADE)->slot_id(),
                game_data.item(ITEM_FIRE_MAGIC_UPGRADE)->slot_id(),
                game_data.item(ITEM_WIND_MAGIC_UPGRADE)->slot_id()
        };
        // Boots are only made progressive if their effects are not split
        if(include_boots)
            slots.emplace_back(game_data.item(ITEM_BOOTS_UPGRADE)->slot_id());
        return slots;
    }

    SlotTiers tiers_in_slot(const GameData& game_data, uint16_t slot_id)
    {
        SlotTiers slot;
        for(uint8_t item_id = 1 ; item_id < ITEM_COUNT ; ++item_id)
        {
            const Item* item = game_data.item(item_id);
            if(item->slot_id() != slot_id)
                continue;
            if(item->tier() == UPGRADE_TIER)
                slot.upgrade = item;
            else
                slot.items.emplace_back(item);
        }
        if(!slot.upgrade)
            throw RandomizerException("Could not find a valid upgrade item for slot " + std::to_string(slot_id));
        return slot;
    }

    // -1 means the slot is empty. 0 is a real tier (Short Boots, Dagger, Cloth Armor),
    // so an empty slot cannot be stored as 0.
    int owned_tier_for_slot(const std::vector<const Item*>& starting_inventory, uint16_t slot_id)
    {
        int owned_tier = -1;
        for(const Item* owned : starting_inventory)
        {
            if(owned->slot_id() != slot_id || owned->tier() == UPGRADE_TIER)
                continue;
            owned_tier = std::max(owned_tier, static_cast<int>(owned->tier()));
        }
        return owned_tier;
    }

    // Empty slot: the first pickup grants tier 0, so tier T takes T + 1.
    // Owned slot: the next pickup grants owned_tier + 1, so tier T takes T - owned_tier.
    int upgrades_to_reach(uint16_t tier, int owned_tier)
    {
        if(owned_tier < 0)
            return static_cast<int>(tier) + 1;
        return static_cast<int>(tier) - owned_tier;
    }

    void replace_items_with_upgrades(std::array<uint16_t, ITEM_COUNT>& distribution, const SlotTiers& slot)
    {
        for(const Item* item : slot.items)
        {
            const uint16_t amount = distribution[item->id()];
            distribution[slot.upgrade->id()] += amount;
            distribution[item->id()] -= amount;
        }
    }

    void replace_tier_requirements(WorldPath* path, const SlotTiers& slot, int owned_tier)
    {
        std::vector<const Item*>& required_items = path->required_items();
        int highest = 0;
        for(const Item* item : slot.items)
        {
            const auto it = std::find(required_items.begin(), required_items.end(), item);
            if(it == required_items.end())
                continue;
            required_items.erase(it);
            highest = std::max(highest, upgrades_to_reach(item->tier(), owned_tier));
        }
        for(int i = 1 ; i <= highest ; ++i)
            required_items.emplace_back(slot.upgrade);
    }
}

void RandomizerWorld::handle_progressive_items(const RandomizerOptions& options, const GameData& game_data)
{
    const bool include_boots = !options.split_boots_effects();
    for(uint16_t slot_id : progressive_slot_ids(game_data, include_boots))
    {
        const SlotTiers slot = tiers_in_slot(game_data, slot_id);
        const int owned_tier = owned_tier_for_slot(_starting_inventory, slot_id);
        replace_items_with_upgrades(_items_distribution, slot);
        for(WorldPath* path : _paths)
            replace_tier_requirements(path, slot, owned_tier);
    }
}
