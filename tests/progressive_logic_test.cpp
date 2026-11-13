#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/game/game_data.hpp"
#include "../src/game/item.hpp"
#include "../src/model/item_source.hpp"
#include "../src/model/randomizer_world.hpp"
#include "../src/model/world_node.hpp"
#include "../src/model/world_path.hpp"
#include "../src/randomizer_options.hpp"
#include "../src/tools/json.hpp"

namespace
{
    struct Probe
    {
        const char* label;
        std::vector<std::string> before;
        std::vector<std::string> after;
    };

    void check(bool condition, const std::string& message)
    {
        if(!condition)
            throw std::runtime_error(message);
    }

    std::string listed(const std::vector<std::string>& names)
    {
        if(names.empty())
            return "(nothing)";
        std::string text;
        for(const std::string& name : names)
        {
            if(!text.empty())
                text += ", ";
            text += name;
        }
        return text;
    }

    std::vector<std::string> sorted_names(const std::vector<const Item*>& items)
    {
        std::vector<std::string> names;
        names.reserve(items.size());
        for(const Item* item : items)
            names.push_back(item->name());
        std::sort(names.begin(), names.end());
        return names;
    }

    void expect_items(const std::vector<const Item*>& items, std::vector<std::string> expected, const std::string& label)
    {
        std::sort(expected.begin(), expected.end());
        const std::vector<std::string> got = sorted_names(items);
        if(got != expected)
            throw std::runtime_error(label + " requires " + listed(got) + "; expected " + listed(expected));
    }

    WorldPath* add_probe(RandomizerWorld& world, const GameData& game_data, const std::vector<std::string>& items)
    {
        WorldPath* path = new WorldPath();
        path->origin(world.node("inoa"));
        path->destination(world.node("end"));
        for(const std::string& name : items)
            path->add_required_item(game_data.item(name));
        world.add_path(path);
        return path;
    }

    WorldPath* path_between(const RandomizerWorld& world, const std::string& from, const std::string& to)
    {
        WorldNode* origin = world.node(from);
        WorldNode* destination = world.node(to);
        WorldPath* found = nullptr;
        for(WorldPath* path : world.paths())
        {
            const bool forward = path->origin() == origin && path->destination() == destination;
            const bool backward = path->origin() == destination && path->destination() == origin;
            if(!forward && !backward)
                continue;
            if(found)
                throw std::runtime_error("More than one path between " + from + " and " + to);
            found = path;
        }
        if(!found)
            throw std::runtime_error("No path between " + from + " and " + to);
        return found;
    }

    void expect_forward(const WorldPath* path, const std::string& from, const std::string& to)
    {
        check(path->origin()->id() == from && path->destination()->id() == to,
                "Path direction is " + path->origin()->id() + " -> " + path->destination()->id());
    }

    void expect_distribution(const RandomizerWorld& world, const GameData& game_data, const char* name, uint16_t expected)
    {
        const uint16_t got = world.items_distribution()[game_data.item(name)->id()];
        if(got != expected)
            throw std::runtime_error(std::string(name) + " pool count is " + std::to_string(got)
                    + ", expected " + std::to_string(expected));
    }

    int highest_copies(const RandomizerWorld& world, const Item* item)
    {
        int highest = 0;
        for(const WorldPath* path : world.paths())
        {
            const int count = static_cast<int>(std::count(
                    path->required_items().begin(), path->required_items().end(), item));
            highest = std::max(highest, count);
        }
        return highest;
    }

    void expect_payable(const RandomizerWorld& world, const GameData& game_data, const char* name, int expected_max)
    {
        const Item* item = game_data.item(name);
        const int highest = highest_copies(world, item);
        const int pool = world.items_distribution()[item->id()];
        if(highest > pool)
            throw std::runtime_error(std::string(name) + " gate asks for " + std::to_string(highest)
                    + " and the pool has " + std::to_string(pool));
        if(highest != expected_max)
            throw std::runtime_error(std::string(name) + " highest gate is " + std::to_string(highest)
                    + ", expected " + std::to_string(expected_max));
    }

    void expect_source_node(const RandomizerWorld& world, uint16_t id, const std::string& node_id)
    {
        ItemSource* found = nullptr;
        for(ItemSource* source : world.item_sources())
        {
            if(source->id() != id)
                continue;
            if(found)
                throw std::runtime_error("Duplicate item source id " + std::to_string(id));
            found = source;
        }
        if(!found)
            throw std::runtime_error("Missing item source " + std::to_string(id));
        if(found->node()->id() != node_id)
            throw std::runtime_error("Item source " + std::to_string(id) + " is in " + found->node()->id()
                    + ", expected " + node_id);
    }

    Json options_json(bool split_boots)
    {
        Json json;
        json["gameSettings"]["progressiveItems"] = true;
        json["gameSettings"]["splitBootsEffects"] = split_boots;
        json["randomizerSettings"]["crestCount"] = 7;
        json["randomizerSettings"]["startingInventory"] = std::vector<std::string>{
                "Dagger", "Cloth Armor", "Short Boots"
        };

        // One of each tier. After the fold, the upgrade count is the number of
        // members, which is also the most a gate is allowed to ask for.
        Json& distribution = json["randomizerSettings"]["itemsDistribution"];
        const char* members[] = {
                "Long Boots", "Merman Boots", "Charm Boots",
                "Sword", "Fiend Blade", "Holy Sword", "Legend Sword",
                "Leather Armor", "Ancient Armor", "Silver Armor",
                "Hunter's Bow", "Willow Bow",
                "Iron Flail", "Steel Flail",
                "Earth Scroll", "Earth Book",
                "Water Scroll", "Water Book",
                "Fire Scroll", "Fire Book",
                "Wind Scroll", "Wind Book",
                "Fire Wand", "Ice Wand",
        };
        for(const char* name : members)
            distribution[name] = 1;
        return json;
    }

    void apply(GameData& game_data, RandomizerWorld& world, bool split_boots)
    {
        const RandomizerOptions options(options_json(split_boots), game_data, world);
        game_data.apply_options(options);
        world.apply_options(options, game_data);
    }

    void test_torla_sources(const RandomizerWorld& world)
    {
        for(uint16_t id : { 230, 231, 232, 233 })
            expect_source_node(world, id, "torla_mountain");
        for(uint16_t id : { 235, 236 })
            expect_source_node(world, id, "torla_mountain_behind_charm_boots");
        expect_source_node(world, 234, "torla_mountain_behind_spring");
    }

    void test_folded_requirements()
    {
        GameData game_data;
        RandomizerWorld world(game_data);

        // Tiers that no shipped edge names. The same rewrite has to leave them
        // payable, or the next edge that names one fills its checks with filler.
        const Probe probes[] = {
                { "Short Boots", { "Short Boots" }, {} },
                { "Dagger", { "Dagger" }, {} },
                { "Cloth Armor", { "Cloth Armor" }, {} },
                { "Silver Armor", { "Silver Armor" }, { "Armor upgrade", "Armor upgrade", "Armor upgrade" } },
                { "Legend Sword", { "Legend Sword" }, { "Sword upgrade", "Sword upgrade", "Sword upgrade", "Sword upgrade" } },
                { "Holy Sword", { "Holy Sword" }, { "Sword upgrade", "Sword upgrade", "Sword upgrade" } },
                { "Willow Bow", { "Willow Bow" }, { "Bow upgrade", "Bow upgrade" } },
                { "Hunter's Bow", { "Hunter's Bow" }, { "Bow upgrade" } },
                { "Steel Flail", { "Steel Flail" }, { "Flail upgrade", "Flail upgrade" } },
                { "Iron Flail", { "Iron Flail" }, { "Flail upgrade" } },
                { "Earth Scroll", { "Earth Scroll" }, { "Earth Magic upgrade" } },
                { "Earth Book", { "Earth Book" }, { "Earth Magic upgrade", "Earth Magic upgrade" } },
                { "Water Book", { "Water Book" }, { "Water Magic upgrade", "Water Magic upgrade" } },
                { "Fire Book", { "Fire Book" }, { "Fire Magic upgrade", "Fire Magic upgrade" } },
                { "Wind Book", { "Wind Book" }, { "Wind Magic upgrade", "Wind Magic upgrade" } },
                { "Long and Merman Boots", { "Long Boots", "Merman Boots" }, { "Boots upgrade", "Boots upgrade" } },
                { "Fire Wand", { "Fire Wand" }, { "Fire Wand" } },
                { "Ice Wand", { "Ice Wand" }, { "Ice Wand" } },
        };
        std::vector<std::pair<const Probe*, WorldPath*>> added;
        for(const Probe& probe : probes)
            added.emplace_back(&probe, add_probe(world, game_data, probe.before));

        apply(game_data, world, false);

        for(const auto& [probe, path] : added)
            expect_items(path->required_items(), probe->after, probe->label);

        WorldPath* entrance = path_between(world, "overworld_d1", "torla_mountain");
        expect_forward(entrance, "overworld_d1", "torla_mountain");
        check(entrance->is_two_way(), "The Torla entrance is walked both ways");
        expect_items(entrance->required_items(), { "Fire Wand", "Ice Wand", "Flail upgrade" }, "Torla entrance");

        WorldPath* boots = path_between(world, "torla_mountain", "torla_mountain_behind_charm_boots");
        expect_forward(boots, "torla_mountain", "torla_mountain_behind_charm_boots");
        check(boots->is_two_way(), "The Charm Boots gate is walked both ways");
        expect_items(boots->required_items(), { "Boots upgrade", "Boots upgrade", "Boots upgrade" }, "Charm Boots gate");

        WorldPath* spring = path_between(world, "torla_mountain_behind_charm_boots", "torla_mountain_behind_spring");
        expect_forward(spring, "torla_mountain_behind_charm_boots", "torla_mountain_behind_spring");
        check(spring->is_two_way(), "The Spring Bean gate is walked both ways");
        expect_items(spring->required_items(), { "Spring Bean" }, "Spring Bean gate");

        WorldPath* lair = path_between(world, "overworld_d4", "reptile_lair");
        expect_forward(lair, "overworld_d4", "reptile_lair");
        expect_items(lair->required_items(), { "Bomb", "Bow upgrade", "Boots upgrade", "Boots upgrade" },
                "Reptile Lair entrance");

        expect_distribution(world, game_data, "Boots upgrade", 3);
        expect_distribution(world, game_data, "Sword upgrade", 4);
        expect_distribution(world, game_data, "Armor upgrade", 3);
        expect_distribution(world, game_data, "Bow upgrade", 2);
        expect_distribution(world, game_data, "Flail upgrade", 2);
        expect_distribution(world, game_data, "Earth Magic upgrade", 2);
        expect_distribution(world, game_data, "Water Magic upgrade", 2);
        expect_distribution(world, game_data, "Fire Magic upgrade", 2);
        expect_distribution(world, game_data, "Wind Magic upgrade", 2);
        expect_distribution(world, game_data, "Long Boots", 0);
        expect_distribution(world, game_data, "Merman Boots", 0);
        expect_distribution(world, game_data, "Charm Boots", 0);
        expect_distribution(world, game_data, "Short Boots", 0);
        expect_distribution(world, game_data, "Silver Armor", 0);
        expect_distribution(world, game_data, "Legend Sword", 0);
        expect_distribution(world, game_data, "Fire Wand", 1);
        expect_distribution(world, game_data, "Ice Wand", 1);

        expect_payable(world, game_data, "Boots upgrade", 3);
        expect_payable(world, game_data, "Armor upgrade", 3);
        expect_payable(world, game_data, "Sword upgrade", 4);
        expect_payable(world, game_data, "Bow upgrade", 2);
        expect_payable(world, game_data, "Flail upgrade", 2);
        expect_payable(world, game_data, "Earth Magic upgrade", 2);
        expect_payable(world, game_data, "Charm Boots", 0);
        expect_payable(world, game_data, "Fire Wand", 1);
        expect_payable(world, game_data, "Ice Wand", 1);

        test_torla_sources(world);
    }

    void test_split_boots_keep_the_named_item()
    {
        GameData game_data;
        RandomizerWorld world(game_data);
        apply(game_data, world, true);

        WorldPath* boots = path_between(world, "torla_mountain", "torla_mountain_behind_charm_boots");
        expect_forward(boots, "torla_mountain", "torla_mountain_behind_charm_boots");
        check(boots->is_two_way(), "The Charm Boots gate is walked both ways");
        expect_items(boots->required_items(), { "Charm Boots" }, "Split Charm Boots gate");

        expect_distribution(world, game_data, "Boots upgrade", 0);
        expect_distribution(world, game_data, "Long Boots", 1);
        expect_distribution(world, game_data, "Merman Boots", 1);
        expect_distribution(world, game_data, "Charm Boots", 1);
        expect_distribution(world, game_data, "Sword upgrade", 4);
        expect_payable(world, game_data, "Boots upgrade", 0);
        expect_payable(world, game_data, "Charm Boots", 1);
    }
}

int main()
{
    try
    {
        test_folded_requirements();
        test_split_boots_keep_the_named_item();
        std::cout << "progressive_logic_test passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
