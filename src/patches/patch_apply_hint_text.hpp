#pragma once

#include "game_patch.hpp"
#include "../tools/byte_array.hpp"
#include "../game/room_strings.hpp"
#include "../model/hint_source.hpp"
#include "../constants/flags.hpp"
#include "../constants/map_codes.hpp"
#include "../tools/exception.hpp"
#include "../tools/stringtools.hpp"
#include "../tools/textbox_formatter.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

class PatchApplyHintText : public GamePatch
{
public:
    void alter_datas_file(BinaryFile& data, const GameData& game_data, const RandomizerWorld& world) override
    {
        apply_lars_crypt_hint(data, world);
        apply_reptile_lair_hint(data, world);
        apply_nirudes_lair_hint(data, world);
        apply_coastal_cave_hint(data, world);
        apply_torla_mountain_hint(data, world);
        apply_magyscar_hint(data, world);
        apply_coal_mine_hint(data, world);
        apply_ancient_shrine_hint(data, world);
        apply_fairy_pond_hint(data, world);
        apply_great_tree_hint(data, world);
        apply_navas_keep_hint(data, world);

        apply_jess_hint(data, world);

        apply_coal_mine_sign_vanilla_hint(data);
        // The bits are written for every seed. A placeholder line can set one,
        // and the client has no row for a speaker who was not given a line.
        mark_speaker_lines(data);
        mark_casino_signs(data);
    }

private:
    static void apply_lars_crypt_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("lars_crypt")->text();

        RoomStrings strings(MAP_LARS_CRYPT_25, data);

        strings.string(35) = TextboxFormatter(
                R"(\CYou need to know... )" + hint_text + R"(.)"
        ).format();
        strings.string(37) = R"(\CGood luck on your quest.)";

        strings.apply_on_data(data);
    }

    static void apply_reptile_lair_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("reptile_lair")->text();

        // Map 411 plays the same speech and sets the same flag. Its string
        // has to match, or that visit registers a hint the player did not hear.
        const std::string formatted = TextboxFormatter(
                R"(\CHear my words, Releaser. I have come to be known as Jeal. I am also one of the seven guardians\A)"
                R"(I was a metalsmith who sought to understand the mysteries of life.\N\A)"
                R"(To stop the evil present in the mortal realm, I sacrificed my human form and vanished into time.\A)"
                R"(I have given thought to trusting the light of your soul to save this weary land.\A)"
                R"(Here is a hint to help you in your journey: )" + hint_text + R"(.)"
        ).format();

        for(uint16_t map_id : { MAP_REPTILE_LAIR_84, MAP_411 })
        {
            RoomStrings strings(map_id, data);
            strings.string(59) = formatted;
            strings.apply_on_data(data);
        }
    }

    static void apply_nirudes_lair_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("nirudes_lair")->text();

        // The reward warp leaves the player on map 439, which has the same line.
        const std::string formatted = TextboxFormatter(
                R"(\CLord Nirude left a message for you with his dying breath: )" + hint_text + R"(.)"
        ).format();

        for(uint16_t map_id : { MAP_OVERWORLD_E1, MAP_OVERWORLD_E1_VARIANT })
        {
            RoomStrings strings(map_id, data);
            strings.string(37) = formatted;
            strings.apply_on_data(data);
        }
    }

    static void apply_coastal_cave_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("coastal_cave")->text();

        RoomStrings strings(MAP_COASTAL_CAVE_76, data);
        strings.string(11) = TextboxFormatter(
                R"(\CYou are the Releaser of which\NLars has spoken.\N\A)"
                R"(I sense the power that flows within you. I am Vul, one of the seven guardians.\A)"
                R"(I have seen your power, and sense that you may possess the strength to one day crush Melzas.\A)"
                R"(Listen to my words as they will help you to find what you seek: \A)"
                + hint_text + R"(.\N)"
                R"(Use it well, Releaser\W2you must recover the crests and beat Melzas!\N)"
        ).format();
        strings.apply_on_data(data);
    }

    static void apply_torla_mountain_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("torla_mountain")->text();

        RoomStrings strings(MAP_325, data);

        strings.string(10) = TextboxFormatter(
                R"(\FUngh\T\W2\T\W2 You humans have grown so strong, too strong, during my long rest.\A)"
                R"(I learnt this during my sleep: )" + hint_text + R"(.\A)"
                R"(I need to go now, Releaser.\AThis time,\W2for eternity...)"
        ).format();

        strings.apply_on_data(data);
    }

    static void apply_magyscar_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("magyscar")->text();

        RoomStrings strings(MAP_MAGYSCAR_95, data);

        strings.string(7) = TextboxFormatter(
                R"(\CI am Uma, Queen of Life and Death, and one of the seven Guardians.\A)"
                R"(The Murgg came before you and stole the Crest. My servant was so angry he mistakenly attacked you.\A)"
                R"(Please accept my apologies for his rash actions. The Murgg were talking about something...\A)"
                R"(What they said might help you: ")" + hint_text + R"("\A)"
                R"(Go forth now, Releaser. Return the stolen Crests and destroy Melzas.)"
        ).format();

        strings.apply_on_data(data);
    }

    static void apply_coal_mine_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("coal_mine")->text();

        RoomStrings strings(MAP_COAL_MINE_329, data);

        strings.string(46) = TextboxFormatter(
                R"(\FWhat? You haven't found a Crest here?\A)"
                R"(WUKIKI! You remember what Lord Melzas said? ")" + hint_text + R"("\A)"
                R"(GO and do not return without the remaining Crests! WUKIKI!")"
        ).format();

        strings.apply_on_data(data);
    }

    static void apply_ancient_shrine_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("ancient_shrine")->text();

        RoomStrings strings(MAP_ANCIENT_SHRINE_30, data);

        strings.string(14) = TextboxFormatter(
                R"(\CYou are truly worthy of the power you possess. Your courage shall not go unrewarded.\A)"
                R"(You deserve to know these ancestral words: ")" + hint_text + R"(")"
        ).format();

        strings.apply_on_data(data);
    }

    static void apply_fairy_pond_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("fairy_pond")->text();

        RoomStrings strings(MAP_FAIRY_POND_161, data);
        strings.string(95) = TextboxFormatter(
                R"(\DI am at once amazed and proud that you have progressed this far, Releaser.\A)"
                R"(I am Stenia, Queen of the waters. Your actions have been shown to my heart by the rivers and oceans.\A)"
                R"(I have been waiting for you to come, for I am able to help you in your quest.\A)"
                R"(Mark the words I am about to tell you: )" + hint_text + R"(.)"
        ).format();

        strings.apply_on_data(data);
    }

    static void apply_great_tree_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("great_tree")->text();

        // The line the player hears, and the flag set after it, are on map 154.
        const std::string formatted = TextboxFormatter(
                R"(\EAhh! Furless one, you have come so far, but this is the end of your sad journey of self delusion.\A)"
                R"(Since you are going to die, I can tell you the missing piece in the puzzle...\A)"
                + hint_text + R"(!\A)"
                R"(But it doesn't matter because now is the time where you DIE! WUKIKI!)"
        ).format();

        for(uint16_t map_id : { MAP_GREAT_TREE_153, MAP_GREAT_TREE_154 })
        {
            RoomStrings strings(map_id, data);
            strings.string(48) = formatted;
            strings.apply_on_data(data);
        }
    }

    static void apply_navas_keep_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("nava_keep")->text();

        RoomStrings strings(MAP_432, data);

        // Shorten Nava & Zorgia talk
        strings.string(32) = R"(\CIt's time you knew the truth about\NMelzas. He is a ravenous monster\Nfrom a distant star...)";
        strings.string(34) = TextboxFormatter(R"(\FAlundra, it's about time I get rid of you. I just hope it won't get too dirty...)").format();

        // Make post-fight Nava give a hint
        strings.string(36) = TextboxFormatter(
                R"(\CIt is I, Nava.\A)"
                R"(You were wonderful, Alundra. I've never witnessed such a passionate fight!\A)"
                R"(Now listen to me very well. There is a secret we've been sharing between guardians for centuries, and I think it is time for you to know about it.\A)"
                R"(You must know that )" + hint_text + R"(.\A)"
                R"(I hope you make good use of it.)"
        ).format();

        strings.apply_on_data(data);
    }

    static void apply_jess_hint(BinaryFile& data, const RandomizerWorld& world)
    {
        const std::string& hint_text = world.hint_source("jess")->text();

        std::vector<uint16_t> maps = { MAP_JESS_HOUSE_CHEST_OPENABLE, MAP_JESS_HOUSE_RANDOMIZER };

        for(uint16_t map_id : maps)
        {
            RoomStrings strings(map_id, data);

            strings.string(0) = TextboxFormatter(
                    R"(\CHello there, Alundra! This morning, I heard something in town.\A)"
                    R"(Apparently, )" + hint_text + R"(. Can you believe it?)"
            ).format();

            strings.apply_on_data(data);
        }
    }

    static void apply_coal_mine_sign_vanilla_hint(BinaryFile& data)
    {
        RoomStrings strings(MAP_COAL_MINE_61, data);

        strings.string(8) = R"(\B\HEngine Room to the right\N\HTo start the engine, do the following:\N\HLeft, left, right, left)";

        strings.apply_on_data(data);
    }

    // Lars, Jeal, Vul, Uma, and the Great Tree already set a flag after the
    // line and never clear it. The other speakers get one here. Jess does not:
    // map 260 C[1] has three bytes between the hint and a jump target, and the
    // next script starts on the byte after End. A flag there would play his
    // next sentence on every visit.
    static void mark_speaker_lines(BinaryFile& data)
    {
        mark_nirude(data, 0x547893);
        mark_nirude(data, 0x5F3C89F);
        mark_wilda(data);
        mark_coal_mine(data);
        mark_ancient_shrine(data);
        mark_stenia(data);
        mark_nava(data);
    }

    // Maps 17 and 439. The Wait 60 after the hint and one Break come out, and
    // FlagOn goes in, so the story FlagOff at the end of the span stays put.
    // PatchNeutralizeMapVariantChanges has already rewritten the SetMapVariant
    // just past that span, and that address has to stay.
    static void mark_nirude(BinaryFile& data, uint32_t wait)
    {
        expect_bytes(data, wait, { 0x37, 0x3C }, "Nirude's wait after the hint");
        expect_bytes(data, wait + 0x40, { 0x00 }, "Nirude's break before the story flags");
        expect_bytes(data, wait + 0x52, { 0xFF }, "Nirude's hint script end");
        splice_flag_on(data, wait, wait + 0x41, FLAG_SPEAKER_HINT_NIRUDE.event_code(),
                { { wait, wait + 2 }, { wait + 0x40, wait + 0x41 } }, {}, "Nirude");
    }

    // The 30-frame wait after Wilda's hint, before the spawn. Nothing branches
    // into those three bytes. The spawn runs as soon as the line is dismissed.
    static void mark_wilda(BinaryFile& data)
    {
        constexpr uint32_t WAIT = 0x48179AE;
        expect_bytes(data, WAIT, { 0x37, 0x1E, 0x00 }, "Wilda's wait after the hint");
        data.set_bytes(WAIT, flag_on_bytes(FLAG_SPEAKER_HINT_WILDA.event_code()));
    }

    // Zazan's Wait 40 and one later Wait 10 come out. FlagOn and one Break go
    // in, so End stays where B[3] and the skip from the boss flag both expect it.
    static void mark_coal_mine(BinaryFile& data)
    {
        constexpr uint32_t WAIT = 0x48DB66F;
        constexpr uint32_t SHORT_WAIT = 0x48DB691;
        constexpr uint32_t END = 0x48DB753;
        expect_bytes(data, WAIT, { 0x37, 0x28 }, "Coal Mine's wait after the hint");
        expect_bytes(data, SHORT_WAIT, { 0x37, 0x0A }, "Coal Mine's wait between spawns");
        expect_bytes(data, END, { 0xFF }, "Coal Mine's hint script end");
        splice_flag_on(data, WAIT, END, FLAG_SPEAKER_HINT_COAL_MINE.event_code(),
                { { WAIT, WAIT + 2 }, { SHORT_WAIT, SHORT_WAIT + 2 } }, { 0x00 }, "Coal Mine");
    }

    // The two 20-frame waits between the falling sprites come out. FlagOn and
    // one Break go in, so the music-load loop at 0x82EC94 stays the branch
    // target. PlayMusic 0x16 at 0x82EC7A is left in place. The bit is set
    // after the box closes, once the first sprite has been activated.
    static void mark_ancient_shrine(BinaryFile& data)
    {
        constexpr uint32_t MUSIC = 0x82EC7A;
        constexpr uint32_t FIRST_PAUSE = 0x82EC84;
        constexpr uint32_t SECOND_PAUSE = 0x82EC8C;
        constexpr uint32_t LOOP = 0x82EC94;
        expect_bytes(data, MUSIC, { 0xA7, 0x16, 0x01 }, "the priest's music");
        expect_bytes(data, FIRST_PAUSE, { 0x37, 0x14, 0x2D, 0x2B, 0x62, 0x2B, 0x00, 0x40 },
                "the priest's first pause");
        expect_bytes(data, SECOND_PAUSE, { 0x37, 0x14, 0x2D, 0x2C, 0x62, 0x2C, 0x00, 0x40 },
                "the priest's second pause");
        expect_bytes(data, LOOP, { 0x37, 0x01 }, "the priest's music-load wait");
        splice_flag_on(data, FIRST_PAUSE, LOOP, FLAG_SPEAKER_HINT_ANCIENT_SHRINE.event_code(),
                { { FIRST_PAUSE, FIRST_PAUSE + 2 }, { SECOND_PAUSE, SECOND_PAUSE + 2 } },
                { 0x00 }, "Ancient Shrine");
    }

    // PatchFairyPond has replaced FlagOn 0x03A3 with Goto +3, which falls
    // through to End. The same three bytes become Stenia's flag. 0x03A3 stays clear.
    static void mark_stenia(BinaryFile& data)
    {
        constexpr uint32_t SEAL = 0x24A56B6;
        expect_bytes(data, SEAL, { 0x02, 0x03, 0x00 }, "Fairy Pond's skipped seal flag");
        expect_bytes(data, SEAL + 3, { 0xFF }, "Stenia's hint script end");
        data.set_bytes(SEAL, flag_on_bytes(FLAG_SPEAKER_HINT_STENIA.event_code()));
    }

    // Revisit and the walk out of the keep are PatchRemoveNavaInstances on
    // map 144, and this function does not write that map. B[4] there still
    // becomes PlayerGainControl, End, so the stairs stay open and the warp
    // home does not freeze. Both warps below stay ChangeMap to map 144 at
    // (9, 10). B[2]'s End stays the address IfFlagOff 0x03D0 jumps to, so the
    // exit cutscene still waits until the fight has set 0x03D0.
    //
    // F[1] at 0x5D73940 is the ground talk: PlayerLoseControl, FlagOn 0x800A,
    // End. C[1] waits on that latch, gives the stone, and the fight on map 321
    // sets 0x03D0 and returns here. Those bytes are the way back to the exit,
    // so they stay. The flag replaces the one-second wait on B[2], and on the
    // talk path it replaces SetAnim 13 and the Break after the line.
    static void mark_nava(BinaryFile& data)
    {
        constexpr uint32_t DEPARTURE = 0x5D73856;
        constexpr uint32_t TALK = 0x5D738AE;
        constexpr uint32_t TALK_END = 0x5D738BA;
        constexpr uint32_t INTERACT = 0x5D73940;
        const uint16_t code = FLAG_SPEAKER_HINT_NAVA.event_code();

        expect_bytes(data, DEPARTURE,
                { 0x37, 0x3C, 0x53, 0x90, 0x00, 0x09, 0x0A, 0x01, 0x02, 0x00, 0x00, 0xFF },
                "Nava's departure");
        splice_flag_on(data, DEPARTURE, DEPARTURE + 12, code,
                { { DEPARTURE, DEPARTURE + 2 }, { DEPARTURE + 10, DEPARTURE + 11 } }, {}, "Nava");

        expect_bytes(data, TALK,
                { 0x1A, 0x0D, 0x53, 0x90, 0x00, 0x09, 0x0A, 0x01, 0x00, 0x02, 0x00, 0xFF },
                "Nava's post-diamond warp");
        splice_flag_on(data, TALK, TALK_END, code,
                { { TALK, TALK + 2 }, { TALK_END - 2, TALK_END - 1 } }, {}, "Nava's talk");

        expect_bytes(data, INTERACT, { 0x10, 0x05, 0x0A, 0x80, 0xFF }, "Nava's interact script");
    }

    // Map 394 B[2]. Each prize path is 14 bytes, and the four IfTrueGotos above
    // them land on the first byte. The 1-frame wait and the Break after Restart
    // come out. FlagOn goes in after WaitDialog, and the path stays 14 bytes.
    // The exit sign at 0x561160E remains unmodified.
    static void mark_casino_signs(BinaryFile& data)
    {
        mark_casino_sign(data, 0x56115E4, 0x96, FLAG_CASINO_SIGN_SLIME_SMASH.event_code(), "Slime Smash");
        mark_casino_sign(data, 0x56115F2, 0x97, FLAG_CASINO_SIGN_HIT_YOUR_MARK.event_code(), "Hit Your Mark");
        mark_casino_sign(data, 0x5611600, 0x98, FLAG_CASINO_SIGN_WHEEL_OF_FORTUNE.event_code(), "Wheel of Fortune");
        expect_bytes(data, 0x561160E,
                { 0x10, 0x5B, 0x80, 0x00, 0x41, 0x37, 0x01, 0x0D, 0x99, 0x01, 0x39, 0x11, 0x49, 0x00 },
                "the exit sign");
    }

    static void mark_casino_sign(BinaryFile& data, uint32_t address, uint8_t dialog,
                                 uint16_t code, const char* label)
    {
        const uint8_t retail[] = {
            0x10, 0x5B, 0x80, 0x00, 0x41, 0x37, 0x01, 0x0D, dialog, 0x01, 0x39, 0x11, 0x49, 0x00
        };
        for(uint32_t index = 0; index < sizeof(retail); ++index)
        {
            if(data.get_byte(address + index) != retail[index])
                throw RandomizerException(std::string("Casino sign flag did not find ") + label + ".");
        }

        const uint8_t patched[] = {
            0x10, 0x5B, 0x80, 0x00, 0x41,
            0x0D, dialog, 0x01,
            0x39,
            0x05, static_cast<uint8_t>(code & 0xFF), static_cast<uint8_t>(code >> 8),
            0x11,
            0x49
        };
        static_assert(sizeof(retail) == sizeof(patched));
        data.set_bytes(address, patched, sizeof(patched));
    }

    static std::vector<uint8_t> flag_on_bytes(uint16_t code)
    {
        return { 0x05, static_cast<uint8_t>(code & 0xFF), static_cast<uint8_t>(code >> 8) };
    }

    static void expect_bytes(const BinaryFile& data, uint32_t address,
                             std::initializer_list<uint8_t> expected, const char* label)
    {
        uint32_t index = 0;
        for(uint8_t byte : expected)
        {
            if(data.get_byte(address + index) != byte)
                throw RandomizerException(std::string("Speaker hint flag did not find ") + label + ".");
            ++index;
        }
    }

    // FlagOn replaces `drops` inside [start, end). The first drop starts at
    // `start`. `pad` makes the length match, so an End inside the span stays
    // on its address. A branch into the middle of the span would land on the
    // wrong instruction; the sites used here have none.
    static void splice_flag_on(BinaryFile& data, uint32_t start, uint32_t end, uint16_t code,
                               const std::vector<std::pair<uint32_t, uint32_t>>& drops,
                               const std::vector<uint8_t>& pad, const char* label)
    {
        if(drops.empty() || drops.front().first != start)
            throw RandomizerException(std::string(label) + " hint splice does not start on the bytes it removes.");

        std::vector<uint8_t> out = flag_on_bytes(code);
        uint32_t cursor = start;
        for(const std::pair<uint32_t, uint32_t>& drop : drops)
        {
            if(drop.first < cursor || drop.second < drop.first || drop.second > end)
                throw RandomizerException(std::string(label) + " hint splice is out of range.");
            for(uint32_t address = cursor; address < drop.first; ++address)
                out.push_back(data.get_byte(address));
            cursor = drop.second;
        }
        for(uint32_t address = cursor; address < end; ++address)
            out.push_back(data.get_byte(address));
        out.insert(out.end(), pad.begin(), pad.end());
        if(out.size() != static_cast<size_t>(end - start))
            throw RandomizerException(std::string(label) + " hint splice changed the script length.");
        data.set_bytes(start, out);
    }
};

