#include "item_source.hpp"

#include <utility>
#include "../tools/exception.hpp"
#include "../tools/json_tools.hpp"
#include "../game/game_data.hpp"
#include "world_node.hpp"
#include "randomizer_world.hpp"

std::string ItemSource::pretty_name() const
{
    std::string pretty_name = _name;
    if(_vanilla_item)
        pretty_name += " (" + _vanilla_item->name() + ")";
    return pretty_name;
}

void ItemSource::item(const Item* item)
{
    _item = item;
    _remote.reset();
}

void ItemSource::remote(RemoteContents contents)
{
    _item = nullptr;
    _remote = std::move(contents);
}

std::string ItemSource::contents_name() const
{
    if(_remote)
        return _remote->player + " - " + _remote->item;
    if(_item)
        return _item->name();
    return "Nothing";
}

int ItemSource::fill_priority() const
{
    int priority = 0;
    if(_node->should_have_unique_items())
        priority += 2;
    if(_forbid_precious_items)
        priority += 1;

    return priority;
}

void ItemSource::node(WorldNode* node)
{
    if(_node)
        _node->remove_item_source(this);
    _node = node;
    _node->add_item_source(this);
}

Json ItemSource::to_json() const
{
    Json json;

    json["id"] = _id;
    json["name"] = _name;
    json["nodeId"] = _node->id();
    if(!_addresses.empty())
        json["address"] = addresses_to_json(_addresses);
    if(!_sprite_addresses.empty())
        json["spriteAddress"] = addresses_to_json(_sprite_addresses);
    if(_merrick_item_address)
        json["merrickItemAddress"] = _merrick_item_address;
    if(!_hints.empty())
        json["hints"] = _hints;
    if(_vanilla_item)
        json["vanillaItem"] = _vanilla_item->name();
    if(_forbid_precious_items)
        json["forbidPreciousItems"] = _forbid_precious_items;

    return json;
}

ItemSource* ItemSource::from_json(const Json& json, const GameData& game_data, const RandomizerWorld& world)
{
    ItemSource* source = new ItemSource();

    for(auto& [key, value] : json.items())
    {
        if(key == "id")
            source->id(value);
        else if(key == "name")
            source->name(value);
        else if(key == "nodeId")
            source->node(world.node(value));
        else if(key == "address")
            source->addresses(parse_addresses_from_json(value));
        else if(key == "spriteAddress")
            source->sprite_addresses(parse_addresses_from_json(value));
        else if(key == "priceAddress")
            source->price_addresses(parse_addresses_from_json(value));
        else if(key == "merrickItemAddress")
            source->merrick_item_address(parse_addresses_from_json(value)[0]);
        else if(key == "hints")
            value.get_to(source->_hints);
        else if(key == "vanillaItem")
            source->vanilla_item(game_data.item(std::string(value)));
        else if(key == "forbidPreciousItems")
            source->forbid_precious_items(value);
        else
            throw RandomizerException("Unknown key '" + key + "' in ItemSource JSON");
    }

    if(source->id() == 0)
        throw RandomizerException("ItemSource '" + source->name() + "' is missing a unique id");

    return source;
}
