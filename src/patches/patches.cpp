#include "patches.hpp"

#include "../randomizer_options.hpp"
#include "slot_block.hpp"

#include "patch_new_game.hpp"
#include "patch_fix_map_inconsistencies.hpp"
#include "patch_apply_item_sources.hpp"
#include "patch_original_game_balance.hpp"
#include "patch_neutralize_map_variant_changes.hpp"
#include "patch_neutralize_flags.hpp"
#include "patch_change_flag_checks.hpp"
#include "patch_allow_using_consumables_while_full.hpp"
#include "patch_remove_useless_cephas.hpp"
#include "patch_remove_cutscenes.hpp"
#include "patch_set_king_snow_death_count.hpp"
#include "patch_remove_items_fanfare.hpp"
#include "patch_fix_wrong_map_transitions.hpp"
#include "patch_fix_item_names_in_textboxes.hpp"
#include "patch_split_boots_effects.hpp"
#include "patch_apply_hint_text.hpp"
#include "patch_remove_nava_instances.hpp"
#include "patch_progressive_items.hpp"
#include "patch_remove_videos.hpp"
#include "patch_edit_credits.hpp"
#include "patch_apply_shops_text.hpp"
#include "patch_apply_items_info.hpp"
#include "patch_edit_global_strings.hpp"
#include "patch_adapt_blue_chest.hpp"
#include "patch_set_casino_win_count.hpp"

void execute_patches(const std::vector<std::unique_ptr<GamePatch>>& patches,
                     BinaryFile& data_file, PsxExeFile& exe_file,
                     GameData& game_data, RandomizerWorld& world)
{
    for(const std::unique_ptr<GamePatch>& patch : patches) patch->alter_game_data(game_data);
    for(const std::unique_ptr<GamePatch>& patch : patches) patch->alter_datas_file(data_file, game_data, world);
    for(const std::unique_ptr<GamePatch>& patch : patches) patch->alter_exe_file(exe_file, game_data, world);
}

void apply_randomizer_patches(BinaryFile& data, PsxExeFile& exe, 
                              GameData& game_data, RandomizerWorld& world,
                              const RandomizerOptions& options,
                              const slot_block::Identity* slot)
{
    std::vector<std::unique_ptr<GamePatch>> patches;

    patches.emplace_back(std::make_unique<PatchNewGame>(options));
    patches.emplace_back(std::make_unique<PatchFixMapInconsistencies>());
    patches.emplace_back(std::make_unique<PatchFixWrongMapTransitions>());
    patches.emplace_back(std::make_unique<PatchApplyItemSources>());
    patches.emplace_back(std::make_unique<PatchNeutralizeMapVariantChanges>());
    patches.emplace_back(std::make_unique<PatchNeutralizeFlags>());
    patches.emplace_back(std::make_unique<PatchChangeFlagChecks>());
    patches.emplace_back(std::make_unique<PatchAllowUsingConsumablesWhileFull>());
    patches.emplace_back(std::make_unique<PatchRemoveUselessCephas>());
    patches.emplace_back(std::make_unique<PatchRemoveCutscenes>());
    patches.emplace_back(std::make_unique<PatchSetKingSnowDeathCount>(options.king_snow_death_count()));
    patches.emplace_back(std::make_unique<PatchRemoveItemsFanfare>());
    patches.emplace_back(std::make_unique<PatchFixItemNamesInTextboxes>());
    patches.emplace_back(std::make_unique<PatchApplyHintText>());
    patches.emplace_back(std::make_unique<PatchRemoveNavaInstances>());
    patches.emplace_back(std::make_unique<PatchRemoveVideos>(options));
    patches.emplace_back(std::make_unique<PatchEditCredits>());
    patches.emplace_back(std::make_unique<PatchApplyShopsText>());
    patches.emplace_back(std::make_unique<PatchApplyItemsInfo>());
    patches.emplace_back(std::make_unique<PatchEditGlobalStrings>());
    patches.emplace_back(std::make_unique<PatchAdaptBlueChest>());
    patches.emplace_back(std::make_unique<PatchSetCasinoWinCount>(options.casino_win_count_for_last_reward()));

    if(options.split_boots_effects())
        patches.emplace_back(std::make_unique<PatchSplitBootsEffects>());
    if(options.progressive_items())
        patches.emplace_back(std::make_unique<PatchProgressiveItems>(!options.split_boots_effects()));
    if(options.original_game_balance())
        patches.emplace_back(std::make_unique<PatchOriginalGameBalance>());

    // First injection. Later patches land below this address and leave it put.
    if(slot != nullptr)
        slot_block::write(exe, *slot);

    execute_patches(patches, data, exe, game_data, world);
}
