#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../game/item.hpp"
#include "../constants/item_codes.hpp"

class GameData;
class RandomizerWorld;
class WorldNode;

/// How another world's item should be treated once a client exists.
/// The randomizer stores this and does not act on it. Trap has no effect.
enum class RemoteClassification : uint8_t
{
    Progression,
    Useful,
    Filler,
    Trap
};

/// An item granted by another game, shown when this source is collected.
struct RemoteContents
{
    std::string player;
    std::string item;
    RemoteClassification classification = RemoteClassification::Filler;
    /// Gilder this shelf charges. Unset leaves the price word already in the disc.
    std::optional<uint16_t> price;
};

class ItemSource
{
private:
    /// Stable unique identifier for this location (Archipelago location ID)
    uint16_t _id = 0;

    /// The name used to describe this ItemSource
    std::string _name;

    /// The item contained inside this ItemSource. Null when the source is empty
    /// or when it holds a remote item.
    const Item* _item = nullptr;

    /// Set when the source holds another world's item. _item stays null so the
    /// solver does not grant an Alundra item.
    std::optional<RemoteContents> _remote;

    /// The item contained inside this ItemSource in the base game
    const Item* _vanilla_item = nullptr;

    /// The logic node containing this ItemSource
    WorldNode* _node = nullptr;

    /// Addresses inside DATAS.BIN file where to put the contained item ID
    std::vector<uint32_t> _addresses;

    /// Addresses inside DATAS.BIN file where to put the contained item sprite ID
    std::vector<uint32_t> _sprite_addresses;

    /// Addresses inside DATAS.BIN file where to put the contained the price of the item as a word
    std::vector<uint32_t> _price_addresses;

    /// Address inside ALUND_CD.EXE file where to put the contained item ItemInfo address (0 if not handled)
    uint32_t _merrick_item_address = 0x0;

    /// A boolean indicating whether this item source can hold an item required for progression or not
    bool _can_contain_progression = true;

    /// If set to true, this ItemSource cannot contain "precious items" (items that would break the game if taken
    /// repeatedly). This is usually set for ItemSources that can be taken an infinite amount of times.
    bool _forbid_precious_items = false;

    /// An array of hints which can be used to subtly point at this ItemSource
    std::vector<std::string> _hints;

    /// FlagOn bit index for this source. Unset when the code is unknown.
    /// The patcher does not read it.
    std::optional<uint16_t> _event_code;

public:
    ItemSource() = default;
    virtual ~ItemSource() = default;

    [[nodiscard]] uint16_t id() const { return _id; }
    void id(uint16_t id) { _id = id; }

    [[nodiscard]] const std::string& name() const { return _name; }
    void name(const std::string& name) { _name = name; }
    [[nodiscard]] std::string pretty_name() const;

    [[nodiscard]] const Item* item() const { return _item; }
    virtual void item(const Item* item);

    [[nodiscard]] bool is_remote() const { return _remote.has_value(); }
    [[nodiscard]] const RemoteContents* remote() const { return _remote ? &*_remote : nullptr; }
    void remote(RemoteContents contents);

    /// Alundra item name, or "player - item" when the source is remote.
    [[nodiscard]] std::string contents_name() const;

    /// A source is empty if it contains no Alundra item and no remote item.
    /// ITEM_NONE is an item, so a source holding it is not empty.
    [[nodiscard]] bool is_empty() const { return _item == nullptr && !_remote.has_value(); }

    [[nodiscard]] const Item* vanilla_item() const { return _vanilla_item; }
    void vanilla_item(const Item* item) { _vanilla_item = item; }

    [[nodiscard]] WorldNode* node() const { return _node; }
    void node(WorldNode* node);

    [[nodiscard]] const std::vector<std::string>& hints() const { return _hints; }
    void add_hint(const std::string& hint) { _hints.emplace_back(hint); }

    [[nodiscard]] bool can_contain_progression() const { return _can_contain_progression; }
    void can_contain_progression(bool value) { _can_contain_progression = value; }

    [[nodiscard]] const std::vector<uint32_t>& addresses() const { return _addresses; }
    void addresses(const std::vector<uint32_t>& addresses) { _addresses = addresses; }
    void add_address(uint32_t address) { _addresses.emplace_back(address); }

    [[nodiscard]] const std::vector<uint32_t>& sprite_addresses() const { return _sprite_addresses; }
    void sprite_addresses(const std::vector<uint32_t>& addresses) { _sprite_addresses = addresses; }
    void add_sprite_address(uint32_t address) { _sprite_addresses.emplace_back(address); }

    [[nodiscard]] const std::vector<uint32_t>& price_addresses() const { return _price_addresses; }
    void price_addresses(const std::vector<uint32_t>& addresses) { _price_addresses = addresses; }
    void add_price_address(uint32_t address) { _price_addresses.emplace_back(address); }

    [[nodiscard]] uint32_t merrick_item_address() const { return _merrick_item_address; }
    void merrick_item_address(uint32_t addr) { _merrick_item_address = addr; }

    [[nodiscard]] bool forbid_precious_items() const { return _forbid_precious_items; }
    void forbid_precious_items(bool value) { _forbid_precious_items = value; }

    [[nodiscard]] std::optional<uint16_t> event_code() const { return _event_code; }
    void event_code(uint16_t code) { _event_code = code; }

    [[nodiscard]] int fill_priority() const;

    [[nodiscard]] virtual Json to_json() const;
    static ItemSource* from_json(const Json& json, const GameData& game_data, const RandomizerWorld& world);
};
