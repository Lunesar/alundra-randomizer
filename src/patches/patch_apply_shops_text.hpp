#pragma once

#include "game_patch.hpp"
#include "../tools/byte_array.hpp"
#include "../game/room_strings.hpp"
#include "../constants/map_codes.hpp"
#include "../model/item_source.hpp"
#include "../tools/stringtools.hpp"
#include "../tools/textbox_formatter.hpp"

class PatchApplyShopsText : public GamePatch
{
public:
    void alter_datas_file(BinaryFile& data, const GameData& game_data, const RandomizerWorld& world) override
    {
        apply_naomi_shop_text(data, world);
        apply_lurvy_shop_text(data, world);

        freeze_merrick_items(data);
        skip_lurvy_shop_entrance_cutscene(data);
    }

private:
    // The item-id patch runs first and writes an Alundra item's gold into the
    // shop words. A remote item leaves the shop's own price in place.
    static std::string item_price(const BinaryFile& data, const ItemSource* source)
    {
        return std::to_string(data.get_word_le(source->price_addresses().front()));
    }

    static void apply_lurvy_shop_text(BinaryFile& data, const RandomizerWorld& world)
    {
        ItemSource* lurvy_1 = world.item_source("Lurvy's Shop: Item #1 (Herbs)");
        ItemSource* lurvy_2 = world.item_source("Lurvy's Shop: Item #2 (Strength Tonic)");
        ItemSource* lurvy_3 = world.item_source("Lurvy's Shop: Item #3 (Strength Elixyr)");
        ItemSource* lurvy_4 = world.item_source("Lurvy's Shop: Item #4 (Magic Elixyr)");
        ItemSource* lurvy_5_1 = world.item_source("Lurvy's Shop: Item #5 (Leather Armor)");
        ItemSource* lurvy_5_2 = world.item_source("Lurvy's Shop: Item #5 replacement when bought once (Life Vessel)");
        ItemSource* lurvy_5_3 = world.item_source("Lurvy's Shop: Item #5 replacement when bought twice (Wonder Essence)");

        RoomStrings strings(MAP_398, data);

        strings.string(7) = TextboxFormatter(
                R"(\CThat there )" + lurvy_1->contents_name() + R"('ll run you )" + item_price(data, lurvy_1) + R"( Gilder. Okay? \0100\Y)"
        ).format();

        strings.string(8) = TextboxFormatter(
                R"(\C)" + lurvy_2->contents_name() + R"('ll run ya )" + item_price(data, lurvy_2) + R"( Gilder these days. Soun' like a deal?\0100\Y)"
        ).format();

        strings.string(34) = TextboxFormatter(
                R"(\C)" + lurvy_3->contents_name() + R"( costs )" + item_price(data, lurvy_3) + R"( Gilder 'round here.\0100\Y)"
        ).format();

        strings.string(9) = TextboxFormatter(
                R"(\C)" + lurvy_4->contents_name() + R"( costs )" + item_price(data, lurvy_4) + R"( Gilder. Ya still in the market for one?\0100\Y)"
        ).format();

        strings.string(17) = TextboxFormatter(
                R"(\CThat there's a )" + lurvy_5_1->contents_name() + R"(. It's a steal of a deal, at only )" + item_price(data, lurvy_5_1) + R"( Gilder. Deal?\0100\Y)"
        ).format();

        strings.string(18) = TextboxFormatter(
                R"(\CI don't need that )" + lurvy_5_2->contents_name() + R"(. )" + item_price(data, lurvy_5_2) + R"( Gilder'n the thing's yurs. Want it?\0100\Y)"
        ).format();

        strings.string(19) = TextboxFormatter(
                R"(\C)" + lurvy_5_3->contents_name() + R"(, hm? How 'bout )" + item_price(data, lurvy_5_3) + R"( Gilder?\0100\Y)"
        ).format();

        strings.string(35) = R"(\BReceived )" + lurvy_5_1->contents_name() + ".";
        strings.string(36) = R"(\BReceived )" + lurvy_5_2->contents_name() + ".";

        strings.apply_on_data(data);
    }

    static void apply_naomi_shop_text(BinaryFile& data, const RandomizerWorld& world)
    {
        ItemSource* naomi_1 = world.item_source("Naomi's Shop: Item #1 (Herbs)");
        ItemSource* naomi_2 = world.item_source("Naomi's Shop: Item #2 (Strength Tonic)");
        ItemSource* naomi_3 = world.item_source("Naomi's Shop: Item #3 (Magic Elixyr)");
        ItemSource* naomi_4 = world.item_source("Naomi's Shop: Item #4 (Life Vessel)");

        RoomStrings strings(MAP_258, data);

        strings.string(1) = TextboxFormatter(
                R"(\DIf you'd like to purchase )" + naomi_1->contents_name() + R"(, the cost will be )" +
                item_price(data, naomi_1) + R"( Gilder. Is that satisfactory? \0100\Y)"
        ).format();

        strings.string(2) = TextboxFormatter(
                R"(\DAh, yes! )" + naomi_2->contents_name() + R"(! The prime choice of adventurers such as yourself! It costs, )" +
                item_price(data, naomi_2) + R"( Gilder, ok? \0100\Y)"
        ).format();

        strings.string(3) = TextboxFormatter(
                R"(\D)" + naomi_3->contents_name() + "! For just " + item_price(data, naomi_3)+
                R"( Gilder, it can be put to work to make all your dreams come true! Want it? \0100\Y)"
        ).format();

        strings.string(4) = TextboxFormatter(
                R"(\D)" + naomi_4->contents_name() + "! "
                "Hmm, pricey, but a prime choice for the especially discriminating. "
                "It's " + item_price(data, naomi_4) + R"( Gilder, ok? \0100\Y)"
        ).format();

        strings.string(57) = R"(\BReceived a )" + naomi_4->contents_name() + R"(.)";

        strings.apply_on_data(data);
    }

    /**
     * This function prevents Merrick items from being taken and thrown to remove the need for any text change
     * and remove any crash-prone interaction with the "open chest" slot
     */
    static void freeze_merrick_items(BinaryFile& data)
    {
        data.set_long(0x1EBF9D2, 0x00000000);
        data.set_byte(0x1EBF9E8, 0x00);
        data.set_byte(0x1EBF9FC, 0x00);
        data.set_byte(0x1EBFA10, 0x00);
    }

    static void skip_lurvy_shop_entrance_cutscene(BinaryFile& data)
    {
        // Remove camera pan
        data.set_bytes(0x56BFC9A, { 0x02, 0x08, 0x00 });

        // Remove waiting condition for camera pan & text
        data.set_bytes(0x56BFCB9, { 0x02, 0x0A, 0x00 });

        // Remove Alundra walking up inside the shop
        data.set_bytes(0x56BFCC7, { 0x02, 0x09, 0x00 });
        // Remove Alundra walking right inside the shop
        data.set_bytes(0x56BFCD0, { 0x02, 0x04, 0x00 });
    }
};

