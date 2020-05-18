/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2020 MaNGOS <https://getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "AccountMgr.h"
#include "PlayerDump.h"
#include "SpellMgr.h"
#include "Player.h"
#include "GameObject.h"
#include "Chat.h"
#include "Log.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "MassMailMgr.h"
#include "ScriptMgr.h"
#include "Language.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Weather.h"
#include "PointMovementGenerator.h"
#include "PathFinder.h"
#include "TargetedMovementGenerator.h"
#include "SkillDiscovery.h"
#include "SkillExtraItems.h"
#include "SystemConfig.h"
#include "Config/Config.h"
#include "Mail.h"
#include "Util.h"
#include "ItemEnchantmentMgr.h"
#include "BattleGround/BattleGroundMgr.h"
#include "MapPersistentStateMgr.h"
#include "InstanceData.h"
#include "DBCStores.h"
#include "CreatureEventAIMgr.h"
#include "AuctionHouseBot/AuctionHouseBot.h"
#include "SQLStorages.h"
#include "DisableMgr.h"

static uint32 ahbotQualityIds[MAX_AUCTION_QUALITY] =
{
    LANG_AHBOT_QUALITY_GREY, LANG_AHBOT_QUALITY_WHITE,
    LANG_AHBOT_QUALITY_GREEN, LANG_AHBOT_QUALITY_BLUE,
    LANG_AHBOT_QUALITY_PURPLE, LANG_AHBOT_QUALITY_ORANGE,
    LANG_AHBOT_QUALITY_YELLOW
};



// reload commands
bool ChatHandler::HandleReloadAllCommand(char* /*args*/)
{
    HandleReloadSkillFishingBaseLevelCommand((char*)"");

    HandleReloadAllAreaCommand((char*)"");
    HandleReloadAutoBroadcastCommand((char*)"");
    HandleReloadAllEventAICommand((char*)"");
    HandleReloadAllLootCommand((char*)"");
    HandleReloadAllNpcCommand((char*)"");
    HandleReloadAllQuestCommand((char*)"");
    HandleReloadAllSpellCommand((char*)"");
    HandleReloadAllItemCommand((char*)"");
    HandleReloadAllGossipsCommand((char*)"");
    HandleReloadAllLocalesCommand((char*)"");

    HandleReloadMailLevelRewardCommand((char*)"");
    HandleReloadCommandCommand((char*)"");
    HandleReloadReservedNameCommand((char*)"");
    HandleReloadMangosStringCommand((char*)"");
    HandleReloadGameTeleCommand((char*)"");
    HandleReloadBattleEventCommand((char*)"");
    return true;
}

bool ChatHandler::HandleReloadAllAreaCommand(char* /*args*/)
{
    // HandleReloadQuestAreaTriggersCommand((char*)""); -- reloaded in HandleReloadAllQuestCommand
    HandleReloadAreaTriggerTeleportCommand((char*)"");
    HandleReloadAreaTriggerTavernCommand((char*)"");
    HandleReloadGameGraveyardZoneCommand((char*)"");
    return true;
}

bool ChatHandler::HandleReloadAllLootCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables...");
    LoadLootTables();
    SendGlobalSysMessage("DB tables `*_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadAllNpcCommand(char* args)
{
    HandleReloadNpcTrainerCommand((char*)"a");
    HandleReloadNpcVendorCommand((char*)"a");
    HandleReloadPointsOfInterestCommand((char*)"a");
    return true;
}

bool ChatHandler::HandleReloadAllQuestCommand(char* /*args*/)
{
    HandleReloadQuestAreaTriggersCommand((char*)"a");
    HandleReloadQuestTemplateCommand((char*)"a");

    sLog.outString("Re-Loading Quests Relations...");
    sObjectMgr.LoadQuestRelations();
    SendGlobalSysMessage("DB table `quest_relations` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadAllScriptsCommand(char* /*args*/)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        PSendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    sLog.outString("Re-Loading Scripts...");
    HandleReloadDBScriptsOnCreatureDeathCommand((char*)"a");
    HandleReloadDBScriptsOnGoUseCommand((char*)"a");
    HandleReloadDBScriptsOnGossipCommand((char*)"a");
    HandleReloadDBScriptsOnEventCommand((char*)"a");
    HandleReloadDBScriptsOnQuestEndCommand((char*)"a");
    HandleReloadDBScriptsOnQuestStartCommand((char*)"a");
    HandleReloadDBScriptsOnSpellCommand((char*)"a");
    SendGlobalSysMessage("DB tables `*_scripts` reloaded.", SEC_MODERATOR);
    HandleReloadDbScriptStringCommand((char*)"a");
    return true;
}

bool ChatHandler::HandleReloadAllEventAICommand(char* /*args*/)
{
    HandleReloadEventAITextsCommand((char*)"a");
    HandleReloadEventAISummonsCommand((char*)"a");
    HandleReloadEventAIScriptsCommand((char*)"a");
    return true;
}

bool ChatHandler::HandleReloadAllSpellCommand(char* /*args*/)
{
    HandleReloadSkillDiscoveryTemplateCommand((char*)"a");
    HandleReloadSkillExtraItemTemplateCommand((char*)"a");
    HandleReloadSpellAffectCommand((char*)"a");
    HandleReloadSpellAreaCommand((char*)"a");
    HandleReloadSpellChainCommand((char*)"a");
    HandleReloadSpellElixirCommand((char*)"a");
    HandleReloadSpellLearnSpellCommand((char*)"a");
    HandleReloadSpellProcEventCommand((char*)"a");
    HandleReloadSpellBonusesCommand((char*)"a");
    HandleReloadSpellProcItemEnchantCommand((char*)"a");
    HandleReloadSpellScriptTargetCommand((char*)"a");
    HandleReloadSpellTargetPositionCommand((char*)"a");
    HandleReloadSpellThreatsCommand((char*)"a");
    HandleReloadSpellPetAurasCommand((char*)"a");
    HandleReloadSpellLinkedCommand((char*)"a");
    return true;
}

bool ChatHandler::HandleReloadAllGossipsCommand(char* args)
{
    if (*args != 'a')                                       // already reload from all_scripts
    {
        HandleReloadDBScriptsOnGossipCommand((char*)"a");
    }
    HandleReloadGossipMenuCommand((char*)"a");
    HandleReloadPointsOfInterestCommand((char*)"a");
    return true;
}

bool ChatHandler::HandleReloadAllItemCommand(char* /*args*/)
{
    HandleReloadPageTextsCommand((char*)"a");
    HandleReloadItemEnchantementsCommand((char*)"a");
    HandleReloadItemRequiredTragetCommand((char*)"a");
    return true;
}

bool ChatHandler::HandleReloadAllLocalesCommand(char* /*args*/)
{
    HandleReloadLocalesCreatureCommand((char*)"a");
    HandleReloadLocalesGameobjectCommand((char*)"a");
    HandleReloadLocalesGossipMenuOptionCommand((char*)"a");
    HandleReloadLocalesItemCommand((char*)"a");
    HandleReloadLocalesNpcTextCommand((char*)"a");
    HandleReloadLocalesPageTextCommand((char*)"a");
    HandleReloadLocalesPointsOfInterestCommand((char*)"a");
    HandleReloadLocalesQuestCommand((char*)"a");
    return true;
}

bool ChatHandler::HandleReloadConfigCommand(char* /*args*/)
{
    sLog.outString("Re-Loading config settings...");
    sWorld.LoadConfigSettings(true);
    sMapMgr.InitializeVisibilityDistanceInfo();
    SendGlobalSysMessage("World config settings reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadAreaTriggerTavernCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Tavern Area Triggers...");
    sObjectMgr.LoadTavernAreaTriggers();
    SendGlobalSysMessage("DB table `areatrigger_tavern` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadAreaTriggerTeleportCommand(char* /*args*/)
{
    sLog.outString("Re-Loading AreaTrigger teleport definitions...");
    sObjectMgr.LoadAreaTriggerTeleports();
    SendGlobalSysMessage("DB table `areatrigger_teleport` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadAutoBroadcastCommand(char* /*args*/)
{
    sLog.outString("Re-Loading broadcast strings...");
    sWorld.LoadBroadcastStrings();
    SendGlobalSysMessage("Broadcast strings reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadCommandCommand(char* /*args*/)
{
    load_command_table = true;
    SendGlobalSysMessage("DB table `command` will be reloaded at next chat command use.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadCreatureQuestRelationsCommand(char* /*args*/)
{
    sLog.outString("Loading creature quest givers...");
    sObjectMgr.LoadCreatureQuestRelations();
    SendGlobalSysMessage("DB table `quest_relations` (creature quest givers) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadCreatureQuestInvRelationsCommand(char* /*args*/)
{
    sLog.outString("Loading creature quest takers...");
    sObjectMgr.LoadCreatureInvolvedRelations();
    SendGlobalSysMessage("DB table `quest_relations` (creature quest takers) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadConditionsCommand(char* /*args*/)
{
    sLog.outString("Re-Loading `conditions`... ");
    sObjectMgr.LoadConditions();
    SendGlobalSysMessage("DB table `conditions` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadCreaturesStatsCommand(char* /*args*/)
{
    sLog.outString("Re-Loading stats data...");
    sObjectMgr.LoadCreatureClassLvlStats();
    SendGlobalSysMessage("DB table `creature_template_classlevelstats` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadGossipMenuCommand(char* /*args*/)
{
    sObjectMgr.LoadGossipMenus();
    SendGlobalSysMessage("DB tables `gossip_menu` and `gossip_menu_option` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadGOQuestRelationsCommand(char* /*args*/)
{
    sLog.outString("Loading gameobject quest givers...");
    sObjectMgr.LoadGameobjectQuestRelations();
    SendGlobalSysMessage("DB table `quest_relations` (gameobject quest givers) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadGOQuestInvRelationsCommand(char* /*args*/)
{
    sLog.outString("Loading gameobject quest takers...");
    sObjectMgr.LoadGameobjectInvolvedRelations();
    SendGlobalSysMessage("DB table `quest_relations` (gameobject quest takers) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadQuestAreaTriggersCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Quest Area Triggers...");
    sObjectMgr.LoadQuestAreaTriggers();
    SendGlobalSysMessage("DB table `quest_relations` (quest area triggers) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadQuestTemplateCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Quest Templates...");
    sObjectMgr.LoadQuests();
    SendGlobalSysMessage("DB table `quest_template` (quest definitions) reloaded.", SEC_MODERATOR);

    /// dependent also from `gameobject` but this table not reloaded anyway
    sLog.outString("Re-Loading GameObjects for quests...");
    sObjectMgr.LoadGameObjectForQuests();
    SendGlobalSysMessage("Data GameObjects for quests reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesCreatureCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`creature_loot_template`)");
    LoadLootTemplates_Creature();
    LootTemplates_Creature.CheckLootRefs();
    SendGlobalSysMessage("DB table `creature_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesDisenchantCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`disenchant_loot_template`)");
    LoadLootTemplates_Disenchant();
    LootTemplates_Disenchant.CheckLootRefs();
    SendGlobalSysMessage("DB table `disenchant_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesFishingCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`fishing_loot_template`)");
    LoadLootTemplates_Fishing();
    LootTemplates_Fishing.CheckLootRefs();
    SendGlobalSysMessage("DB table `fishing_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesGameobjectCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`gameobject_loot_template`)");
    LoadLootTemplates_Gameobject();
    LootTemplates_Gameobject.CheckLootRefs();
    SendGlobalSysMessage("DB table `gameobject_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesItemCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`item_loot_template`)");
    LoadLootTemplates_Item();
    LootTemplates_Item.CheckLootRefs();
    SendGlobalSysMessage("DB table `item_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesPickpocketingCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`pickpocketing_loot_template`)");
    LoadLootTemplates_Pickpocketing();
    LootTemplates_Pickpocketing.CheckLootRefs();
    SendGlobalSysMessage("DB table `pickpocketing_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesProspectingCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`prospecting_loot_template`)");
    LoadLootTemplates_Prospecting();
    LootTemplates_Prospecting.CheckLootRefs();
    SendGlobalSysMessage("DB table `prospecting_loot_template` reloaded.");
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesMailCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`mail_loot_template`)");
    LoadLootTemplates_Mail();
    LootTemplates_Mail.CheckLootRefs();
    SendGlobalSysMessage("DB table `mail_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesReferenceCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`reference_loot_template`)");
    LoadLootTemplates_Reference();
    SendGlobalSysMessage("DB table `reference_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLootTemplatesSkinningCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Loot Tables... (`skinning_loot_template`)");
    LoadLootTemplates_Skinning();
    LootTemplates_Skinning.CheckLootRefs();
    SendGlobalSysMessage("DB table `skinning_loot_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadMangosStringCommand(char* /*args*/)
{
    sLog.outString("Re-Loading mangos_string Table!");
    sObjectMgr.LoadMangosStrings();
    SendGlobalSysMessage("DB table `mangos_string` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadNpcTextCommand(char* /*args*/)
{
    sLog.outString("Re-Loading `npc_text` Table!");
    sObjectMgr.LoadGossipText();
    SendGlobalSysMessage("DB table `npc_text` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadNpcTrainerCommand(char* /*args*/)
{
    sLog.outString("Re-Loading `npc_trainer_template` Table!");
    sObjectMgr.LoadTrainerTemplates();
    SendGlobalSysMessage("DB table `npc_trainer_template` reloaded.", SEC_MODERATOR);

    sLog.outString("Re-Loading `npc_trainer` Table!");
    sObjectMgr.LoadTrainers();
    SendGlobalSysMessage("DB table `npc_trainer` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadNpcVendorCommand(char* /*args*/)
{
    // not safe reload vendor template tables independent...
    sLog.outString("Re-Loading `npc_vendor_template` Table!");
    sObjectMgr.LoadVendorTemplates();
    SendGlobalSysMessage("DB table `npc_vendor_template` reloaded.", SEC_MODERATOR);

    sLog.outString("Re-Loading `npc_vendor` Table!");
    sObjectMgr.LoadVendors();
    SendGlobalSysMessage("DB table `npc_vendor` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadPointsOfInterestCommand(char* /*args*/)
{
    sLog.outString("Re-Loading `points_of_interest` Table!");
    sObjectMgr.LoadPointsOfInterest();
    SendGlobalSysMessage("DB table `points_of_interest` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadReservedNameCommand(char* /*args*/)
{
    sLog.outString("Loading ReservedNames... (`reserved_name`)");
    sObjectMgr.LoadReservedPlayersNames();
    SendGlobalSysMessage("DB table `reserved_name` (player reserved names) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadReputationRewardRateCommand(char* /*args*/)
{
    sLog.outString("Re-Loading `reputation_reward_rate` Table!");
    sObjectMgr.LoadReputationRewardRate();
    SendGlobalSysMessage("DB table `reputation_reward_rate` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadReputationSpilloverTemplateCommand(char* /*args*/)
{
    sLog.outString("Re-Loading `reputation_spillover_template` Table!");
    sObjectMgr.LoadReputationSpilloverTemplate();
    SendGlobalSysMessage("DB table `reputation_spillover_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSkillDiscoveryTemplateCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Skill Discovery Table...");
    LoadSkillDiscoveryTable();
    SendGlobalSysMessage("DB table `skill_discovery_template` (recipes discovered at crafting) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSkillExtraItemTemplateCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Skill Extra Item Table...");
    LoadSkillExtraItemTable();
    SendGlobalSysMessage("DB table `skill_extra_item_template` (extra item creation when crafting) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadScriptBindingCommand(char* /*args*/)
{
    sLog.outString("Trying to re-load `script_binding` Table!");
    if (sScriptMgr.ReloadScriptBinding())
    {
        SendGlobalSysMessage("DB table `script_binding` reloaded.", SEC_MODERATOR);
    }
    else
    {
        SendSysMessage("DENIED: DB table `script_binding` is reloadable only in Debug build.");
    }
    return true;
}

bool ChatHandler::HandleReloadSkillFishingBaseLevelCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Skill Fishing base level requirements...");
    sObjectMgr.LoadFishingBaseSkillLevel();
    SendGlobalSysMessage("DB table `skill_fishing_base_level` (fishing base level for zone/subzone) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellAffectCommand(char* /*args*/)
{
    sLog.outString("Re-Loading SpellAffect definitions...");
    sSpellMgr.LoadSpellAffects();
    SendGlobalSysMessage("DB table `spell_affect` (spell mods apply requirements) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellAreaCommand(char* /*args*/)
{
    sLog.outString("Re-Loading SpellArea Data...");
    sSpellMgr.LoadSpellAreas();
    SendGlobalSysMessage("DB table `spell_area` (spell dependences from area/quest/auras state) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellBonusesCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Spell Bonus Data...");
    sSpellMgr.LoadSpellBonuses();
    SendGlobalSysMessage("DB table `spell_bonus_data` (spell damage/healing coefficients) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellChainCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Spell Chain Data... ");
    sSpellMgr.LoadSpellChains();
    SendGlobalSysMessage("DB table `spell_chain` (spell ranks) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellElixirCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Spell Elixir types...");
    sSpellMgr.LoadSpellElixirs();
    SendGlobalSysMessage("DB table `spell_elixir` (spell elixir types) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellLearnSpellCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Spell Learn Spells...");
    sSpellMgr.LoadSpellLearnSpells();
    SendGlobalSysMessage("DB table `spell_learn_spell` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellProcEventCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Spell Proc Event conditions...");
    sSpellMgr.LoadSpellProcEvents();
    SendGlobalSysMessage("DB table `spell_proc_event` (spell proc trigger requirements) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellProcItemEnchantCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Spell Proc Item Enchant...");
    sSpellMgr.LoadSpellProcItemEnchant();
    SendGlobalSysMessage("DB table `spell_proc_item_enchant` (item enchantment ppm) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellScriptTargetCommand(char* /*args*/)
{
    sLog.outString("Re-Loading SpellsScriptTarget...");
    sSpellMgr.LoadSpellScriptTarget();
    SendGlobalSysMessage("DB table `spell_script_target` (spell targets selection in case specific creature/GO requirements) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellTargetPositionCommand(char* /*args*/)
{
    sLog.outString("Re-Loading spell target destination coordinates...");
    sSpellMgr.LoadSpellTargetPositions();
    SendGlobalSysMessage("DB table `spell_target_position` (destination coordinates for spell targets) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellThreatsCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Aggro Spells Definitions...");
    sSpellMgr.LoadSpellThreats();
    SendGlobalSysMessage("DB table `spell_threat` (spell aggro definitions) reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellPetAurasCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Spell pet auras...");
    sSpellMgr.LoadSpellPetAuras();
    SendGlobalSysMessage("DB table `spell_pet_auras` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadSpellLinkedCommand(char* /*arg*/)
{
    sLog.outString("Re-Loading spell linked table...");
    sSpellMgr.LoadSpellLinked();
    SendGlobalSysMessage("DB table `spell_linked` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadPageTextsCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Page Texts...");
    sObjectMgr.LoadPageTexts();
    SendGlobalSysMessage("DB table `page_texts` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadItemEnchantementsCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Item Random Enchantments Table...");
    LoadRandomEnchantmentsTable();
    SendGlobalSysMessage("DB table `item_enchantment_template` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadItemRequiredTragetCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Item Required Targets Table...");
    sObjectMgr.LoadItemRequiredTarget();
    SendGlobalSysMessage("DB table `item_required_target` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadBattleEventCommand(char* /*args*/)
{
    sLog.outString("Re-Loading BattleGround Eventindexes...");
    sBattleGroundMgr.LoadBattleEventIndexes();
    SendGlobalSysMessage("DB table `gameobject_battleground` and `creature_battleground` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadEventAITextsCommand(char* /*args*/)
{

    sLog.outString("Re-Loading Texts from `creature_ai_texts`...");
    sEventAIMgr.LoadCreatureEventAI_Texts(true);
    SendGlobalSysMessage("DB table `creature_ai_texts` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadEventAISummonsCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Summons from `creature_ai_summons`...");
    sEventAIMgr.LoadCreatureEventAI_Summons(true);
    SendGlobalSysMessage("DB table `creature_ai_summons` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadEventAIScriptsCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Scripts from `creature_ai_scripts`...");
    sEventAIMgr.LoadCreatureEventAI_Scripts();
    SendGlobalSysMessage("DB table `creature_ai_scripts` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadDbScriptStringCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Script strings from `db_script_string`...");
    sScriptMgr.LoadDbScriptStrings();
    SendGlobalSysMessage("DB table `db_script_string` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadDBScriptsOnGossipCommand(char* args)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        SendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    if (*args != 'a')
    {
        sLog.outString("Re-Loading Scripts from `db_scripts [type = DBS_ON_GOSSIP]`...");
    }

    sScriptMgr.LoadDbScripts(DBS_ON_GOSSIP);

    if (*args != 'a')
    {
        SendGlobalSysMessage("DB table `db_scripts [type = DBS_ON_GOSSIP]` reloaded.", SEC_MODERATOR);
    }

    return true;
}

bool ChatHandler::HandleReloadDBScriptsOnSpellCommand(char* args)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        SendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    if (*args != 'a')
    {
        sLog.outString("Re-Loading Scripts from `db_scripts [type = DBS_ON_SPELL]`...");
    }

    sScriptMgr.LoadDbScripts(DBS_ON_SPELL);

    if (*args != 'a')
    {
        SendGlobalSysMessage("DB table `db_scripts [type = DBS_ON_SPELL]` reloaded.", SEC_MODERATOR);
    }

    return true;
}

bool ChatHandler::HandleReloadDBScriptsOnQuestStartCommand(char* args)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        SendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    if (*args != 'a')
    {
        sLog.outString("Re-Loading Scripts from `db_scripts [type = DBS_ON_QUEST_START]`...");
    }

    sScriptMgr.LoadDbScripts(DBS_ON_QUEST_START);

    if (*args != 'a')
    {
        SendGlobalSysMessage("DB table `db_scripts [type = DBS_ON_QUEST_START]` reloaded.", SEC_MODERATOR);
    }

    return true;
}

bool ChatHandler::HandleReloadDBScriptsOnQuestEndCommand(char* args)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        SendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    if (*args != 'a')
    {
        sLog.outString("Re-Loading Scripts from `db_scripts [type = DBS_ON_QUEST_END]`...");
    }

    sScriptMgr.LoadDbScripts(DBS_ON_QUEST_END);

    if (*args != 'a')
    {
        SendGlobalSysMessage("DB table `db_scripts [type = DBS_ON_QUEST_END]` reloaded.", SEC_MODERATOR);
    }

    return true;
}

bool ChatHandler::HandleReloadDBScriptsOnEventCommand(char* args)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        SendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    if (*args != 'a')
    {
        sLog.outString("Re-Loading Scripts from `db_scripts [type = DBS_ON_EVENT]`...");
    }

    sScriptMgr.LoadDbScripts(DBS_ON_EVENT);

    if (*args != 'a')
    {
        SendGlobalSysMessage("DB table `db_scripts [type = DBS_ON_EVENT]` reloaded.", SEC_MODERATOR);
    }

    return true;
}

bool ChatHandler::HandleReloadDBScriptsOnGoUseCommand(char* args)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        SendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    if (*args != 'a')
    {
        sLog.outString("Re-Loading Scripts from `db_scripts [type = DBS_ON_GO[_TEMPLATE]_USE]`...");
    }

    sScriptMgr.LoadDbScripts(DBS_ON_GO_USE);
    sScriptMgr.LoadDbScripts(DBS_ON_GOT_USE);

    if (*args != 'a')
    {
        SendGlobalSysMessage("DB table `db_scripts [type = DBS_ON_GO[_TEMPLATE]_USE]` reloaded.", SEC_MODERATOR);
    }

    return true;
}

bool ChatHandler::HandleReloadDBScriptsOnCreatureDeathCommand(char* args)
{
    if (sScriptMgr.IsScriptScheduled())
    {
        SendSysMessage("DB scripts used currently, please attempt reload later.");
        SetSentErrorMessage(true);
        return false;
    }

    if (*args != 'a')
    {
        sLog.outString("Re-Loading Scripts from `db_scripts [type = DBS_ON_CREATURE_DEATH]`...");
    }

    sScriptMgr.LoadDbScripts(DBS_ON_CREATURE_DEATH);

    if (*args != 'a')
    {
        SendGlobalSysMessage("DB table `db_scripts [type = DBS_ON_CREATURE_DEATH]` reloaded.", SEC_MODERATOR);
    }

    return true;
}

bool ChatHandler::HandleReloadGameGraveyardZoneCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Graveyard-zone links...");

    sObjectMgr.LoadGraveyardZones();

    SendGlobalSysMessage("DB table `game_graveyard_zone` reloaded.", SEC_MODERATOR);

    return true;
}

bool ChatHandler::HandleReloadGameTeleCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Game Tele coordinates...");

    sObjectMgr.LoadGameTele();

    SendGlobalSysMessage("DB table `game_tele` reloaded.", SEC_MODERATOR);

    return true;
}

bool ChatHandler::HandleReloadLocalesCreatureCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales Creature ...");
    sObjectMgr.LoadCreatureLocales();
    SendGlobalSysMessage("DB table `locales_creature` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLocalesGameobjectCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales Gameobject ... ");
    sObjectMgr.LoadGameObjectLocales();
    SendGlobalSysMessage("DB table `locales_gameobject` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLocalesGossipMenuOptionCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales Gossip Menu Option ... ");
    sObjectMgr.LoadGossipMenuItemsLocales();
    SendGlobalSysMessage("DB table `locales_gossip_menu_option` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLocalesItemCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales Item ... ");
    sObjectMgr.LoadItemLocales();
    SendGlobalSysMessage("DB table `locales_item` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLocalesNpcTextCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales NPC Text ... ");
    sObjectMgr.LoadGossipTextLocales();
    SendGlobalSysMessage("DB table `locales_npc_text` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLocalesPageTextCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales Page Text ... ");
    sObjectMgr.LoadPageTextLocales();
    SendGlobalSysMessage("DB table `locales_page_text` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLocalesPointsOfInterestCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales Points Of Interest ... ");
    sObjectMgr.LoadPointOfInterestLocales();
    SendGlobalSysMessage("DB table `locales_points_of_interest` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadLocalesQuestCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Locales Quest ... ");
    sObjectMgr.LoadQuestLocales();
    SendGlobalSysMessage("DB table `locales_quest` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleReloadMailLevelRewardCommand(char* /*args*/)
{
    sLog.outString("Re-Loading Player level dependent mail rewards...");
    sObjectMgr.LoadMailLevelRewards();
    SendGlobalSysMessage("DB table `mail_level_reward` reloaded.");
    return true;
}

bool ChatHandler::HandleReloadDisablesCommand(char * /*args*/)
{
    sLog.outString("Re-loading Disables...");
    DisableMgr::LoadDisables();
    DisableMgr::CheckQuestDisables();
    SendGlobalSysMessage("DB table `disables` reloaded.", SEC_MODERATOR);
    return true;
}

bool ChatHandler::HandleMaxSkillCommand(char* /*args*/)
{
    Player* SelectedPlayer = getSelectedPlayer();
    if (!SelectedPlayer)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // each skills that have max skill value dependent from level seted to current level max skill value
    SelectedPlayer->UpdateSkillsToMaxSkillsForLevel();
    return true;
}

bool ChatHandler::HandleSetSkillCommand(char* args)
{
    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // number or [name] Shift-click form |color|Hskill:skill_id|h[name]|h|r
    char* skill_p = ExtractKeyFromLink(&args, "Hskill");
    if (!skill_p)
    {
        return false;
    }

    int32 skill;
    if (!ExtractInt32(&skill_p, skill))
    {
        return false;
    }

    int32 level;
    if (!ExtractInt32(&args, level))
    {
        return false;
    }

    int32 maxskill;
    if (!ExtractOptInt32(&args, maxskill, target->GetPureMaxSkillValue(skill)))
    {
        return false;
    }

    if (skill <= 0)
    {
        PSendSysMessage(LANG_INVALID_SKILL_ID, skill);
        SetSentErrorMessage(true);
        return false;
    }

    SkillLineEntry const* sl = sSkillLineStore.LookupEntry(skill);
    if (!sl)
    {
        PSendSysMessage(LANG_INVALID_SKILL_ID, skill);
        SetSentErrorMessage(true);
        return false;
    }

    std::string tNameLink = GetNameLink(target);

    if (!target->GetSkillValue(skill))
    {
        PSendSysMessage(LANG_SET_SKILL_ERROR, tNameLink.c_str(), skill, sl->name[GetSessionDbcLocale()]);
        SetSentErrorMessage(true);
        return false;
    }

    if (level <= 0 || level > maxskill || maxskill <= 0)
    {
        return false;
    }

    target->SetSkill(skill, level, maxskill);
    PSendSysMessage(LANG_SET_SKILL, skill, sl->name[GetSessionDbcLocale()], tNameLink.c_str(), level, maxskill);

    return true;
}

bool ChatHandler::HandleUnLearnCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r
    uint32 spell_id = ExtractSpellIdFromLink(&args);
    if (!spell_id)
    {
        return false;
    }

    bool allRanks = ExtractLiteralArg(&args, "all") != NULL;
    if (!allRanks && *args)                                 // can be fail also at syntax error
    {
        return false;
    }

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    if (allRanks)
    {
        spell_id = sSpellMgr.GetFirstSpellInChain(spell_id);
    }

    if (target->HasSpell(spell_id))
    {
        target->removeSpell(spell_id, false, !allRanks);
    }
    else
    {
        SendSysMessage(LANG_FORGET_SPELL);
    }

    return true;
}

bool ChatHandler::HandleCooldownCommand(char* args)
{
    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    std::string tNameLink = GetNameLink(target);

    if (!*args)
    {
        target->RemoveAllSpellCooldown();
        PSendSysMessage(LANG_REMOVEALL_COOLDOWN, tNameLink.c_str());
    }
    else
    {
        // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form
        uint32 spell_id = ExtractSpellIdFromLink(&args);
        if (!spell_id)
        {
            return false;
        }

        if (!sSpellStore.LookupEntry(spell_id))
        {
            PSendSysMessage(LANG_UNKNOWN_SPELL, target == m_session->GetPlayer() ? GetMangosString(LANG_YOU) : tNameLink.c_str());
            SetSentErrorMessage(true);
            return false;
        }

        target->RemoveSpellCooldown(spell_id, true);
        PSendSysMessage(LANG_REMOVE_COOLDOWN, spell_id, target == m_session->GetPlayer() ? GetMangosString(LANG_YOU) : tNameLink.c_str());
    }
    return true;
}

bool ChatHandler::HandleLearnAllCommand(char* /*args*/)
{
    static const char* allSpellList[] =
    {
        "3365",
        "6233",
        "6247",
        "6246",
        "6477",
        "6478",
        "22810",
        "8386",
        "21651",
        "21652",
        "522",
        "7266",
        "8597",
        "2479",
        "22027",
        "6603",
        "5019",
        "133",
        "168",
        "227",
        "5009",
        "9078",
        "668",
        "203",
        "20599",
        "20600",
        "81",
        "20597",
        "20598",
        "20864",
        "1459",
        "5504",
        "587",
        "5143",
        "118",
        "5505",
        "597",
        "604",
        "1449",
        "1460",
        "2855",
        "1008",
        "475",
        "5506",
        "1463",
        "12824",
        "8437",
        "990",
        "5145",
        "8450",
        "1461",
        "759",
        "8494",
        "8455",
        "8438",
        "6127",
        "8416",
        "6129",
        "8451",
        "8495",
        "8439",
        "3552",
        "8417",
        "10138",
        "12825",
        "10169",
        "10156",
        "10144",
        "10191",
        "10201",
        "10211",
        "10053",
        "10173",
        "10139",
        "10145",
        "10192",
        "10170",
        "10202",
        "10054",
        "10174",
        "10193",
        "12826",
        "2136",
        "143",
        "145",
        "2137",
        "2120",
        "3140",
        "543",
        "2138",
        "2948",
        "8400",
        "2121",
        "8444",
        "8412",
        "8457",
        "8401",
        "8422",
        "8445",
        "8402",
        "8413",
        "8458",
        "8423",
        "8446",
        "10148",
        "10197",
        "10205",
        "10149",
        "10215",
        "10223",
        "10206",
        "10199",
        "10150",
        "10216",
        "10207",
        "10225",
        "10151",
        "116",
        "205",
        "7300",
        "122",
        "837",
        "10",
        "7301",
        "7322",
        "6143",
        "120",
        "865",
        "8406",
        "6141",
        "7302",
        "8461",
        "8407",
        "8492",
        "8427",
        "8408",
        "6131",
        "7320",
        "10159",
        "8462",
        "10185",
        "10179",
        "10160",
        "10180",
        "10219",
        "10186",
        "10177",
        "10230",
        "10181",
        "10161",
        "10187",
        "10220",
        "2018",
        "2663",
        "12260",
        "2660",
        "3115",
        "3326",
        "2665",
        "3116",
        "2738",
        "3293",
        "2661",
        "3319",
        "2662",
        "9983",
        "8880",
        "2737",
        "2739",
        "7408",
        "3320",
        "2666",
        "3323",
        "3324",
        "3294",
        "22723",
        "23219",
        "23220",
        "23221",
        "23228",
        "23338",
        "10788",
        "10790",
        "5611",
        "5016",
        "5609",
        "2060",
        "10963",
        "10964",
        "10965",
        "22593",
        "22594",
        "596",
        "996",
        "499",
        "768",
        "17002",
        "1448",
        "1082",
        "16979",
        "1079",
        "5215",
        "20484",
        "5221",
        "15590",
        "17007",
        "6795",
        "6807",
        "5487",
        "1446",
        "1066",
        "5421",
        "3139",
        "779",
        "6811",
        "6808",
        "1445",
        "5216",
        "1737",
        "5222",
        "5217",
        "1432",
        "6812",
        "9492",
        "5210",
        "3030",
        "1441",
        "783",
        "6801",
        "20739",
        "8944",
        "9491",
        "22569",
        "5226",
        "6786",
        "1433",
        "8973",
        "1828",
        "9495",
        "9006",
        "6794",
        "8993",
        "5203",
        "16914",
        "6784",
        "9635",
        "22830",
        "20722",
        "9748",
        "6790",
        "9753",
        "9493",
        "9752",
        "9831",
        "9825",
        "9822",
        "5204",
        "5401",
        "22831",
        "6793",
        "9845",
        "17401",
        "9882",
        "9868",
        "20749",
        "9893",
        "9899",
        "9895",
        "9832",
        "9902",
        "9909",
        "22832",
        "9828",
        "9851",
        "9883",
        "9869",
        "17406",
        "17402",
        "9914",
        "20750",
        "9897",
        "9848",
        "3127",
        "107",
        "204",
        "9116",
        "2457",
        "78",
        "18848",
        "331",
        "403",
        "2098",
        "1752",
        "11278",
        "11288",
        "11284",
        "6461",
        "2344",
        "2345",
        "6463",
        "2346",
        "2352",
        "775",
        "1434",
        "1612",
        "71",
        "2468",
        "2458",
        "2467",
        "7164",
        "7178",
        "7367",
        "7376",
        "7381",
        "21156",
        "5209",
        "3029",
        "5201",
        "9849",
        "9850",
        "20719",
        "22568",
        "22827",
        "22828",
        "22829",
        "6809",
        "8972",
        "9005",
        "9823",
        "9827",
        "6783",
        "9913",
        "6785",
        "6787",
        "9866",
        "9867",
        "9894",
        "9896",
        "6800",
        "8992",
        "9829",
        "9830",
        "780",
        "769",
        "6749",
        "6750",
        "9755",
        "9754",
        "9908",
        "20745",
        "20742",
        "20747",
        "20748",
        "9746",
        "9745",
        "9880",
        "9881",
        "5391",
        "842",
        "3025",
        "3031",
        "3287",
        "3329",
        "1945",
        "3559",
        "4933",
        "4934",
        "4935",
        "4936",
        "5142",
        "5390",
        "5392",
        "5404",
        "5420",
        "6405",
        "7293",
        "7965",
        "8041",
        "8153",
        "9033",
        "9034",
        //"9036", problems with ghost state
        "16421",
        "21653",
        "22660",
        "5225",
        "9846",
        "2426",
        "5916",
        "6634",
        //"6718", phasing stealth, annoying for learn all case.
        "6719",
        "8822",
        "9591",
        "9590",
        "10032",
        "17746",
        "17747",
        "8203",
        "11392",
        "12495",
        "16380",
        "23452",
        "4079",
        "4996",
        "4997",
        "4998",
        "4999",
        "5000",
        "6348",
        "6349",
        "6481",
        "6482",
        "6483",
        "6484",
        "11362",
        "11410",
        "11409",
        "12510",
        "12509",
        "12885",
        "13142",
        "21463",
        "23460",
        "11421",
        "11416",
        "11418",
        "1851",
        "10059",
        "11423",
        "11417",
        "11422",
        "11419",
        "11424",
        "11420",
        "27",
        "31",
        "33",
        "34",
        "35",
        "15125",
        "21127",
        "22950",
        "1180",
        "201",
        "12593",
        "12842",
        "16770",
        "6057",
        "12051",
        "18468",
        "12606",
        "12605",
        "18466",
        "12502",
        "12043",
        "15060",
        "12042",
        "12341",
        "12848",
        "12344",
        "12353",
        "18460",
        "11366",
        "12350",
        "12352",
        "13043",
        "11368",
        "11113",
        "12400",
        "11129",
        "16766",
        "12573",
        "15053",
        "12580",
        "12475",
        "12472",
        "12953",
        "12488",
        "11189",
        "12985",
        "12519",
        "16758",
        "11958",
        "12490",
        "11426",
        "3565",
        "3562",
        "18960",
        "3567",
        "3561",
        "3566",
        "3563",
        "1953",
        "2139",
        "12505",
        "13018",
        "12522",
        "12523",
        "5146",
        "5144",
        "5148",
        "8419",
        "8418",
        "10213",
        "10212",
        "10157",
        "12524",
        "13019",
        "12525",
        "13020",
        "12526",
        "13021",
        "18809",
        "13031",
        "13032",
        "13033",
        "4036",
        "3920",
        "3919",
        "3918",
        "7430",
        "3922",
        "3923",
        "7411",
        "7418",
        "7421",
        "13262",
        "7412",
        "7415",
        "7413",
        "7416",
        "13920",
        "13921",
        "7745",
        "7779",
        "7428",
        "7457",
        "7857",
        "7748",
        "7426",
        "13421",
        "7454",
        "13378",
        "7788",
        "14807",
        "14293",
        "7795",
        "6296",
        "20608",
        "755",
        "444",
        "427",
        "428",
        "442",
        "447",
        "3578",
        "3581",
        "19027",
        "3580",
        "665",
        "3579",
        "3577",
        "6755",
        "3576",
        "2575",
        "2577",
        "2578",
        "2579",
        "2580",
        "2656",
        "2657",
        "2576",
        "3564",
        "10248",
        "8388",
        "2659",
        "14891",
        "3308",
        "3307",
        "10097",
        "2658",
        "3569",
        "16153",
        "3304",
        "10098",
        "4037",
        "3929",
        "3931",
        "3926",
        "3924",
        "3930",
        "3977",
        "3925",
        "136",
        "228",
        "5487",
        "43",
        "202",
        "0"
    };

    int loop = 0;
    while (strcmp(allSpellList[loop], "0"))
    {
        uint32 spell = atol((char*)allSpellList[loop++]);

        if (m_session->GetPlayer()->HasSpell(spell))
        {
            continue;
        }

        SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell);
        if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo, m_session->GetPlayer()))
        {
            PSendSysMessage(LANG_COMMAND_SPELL_BROKEN, spell);
            continue;
        }

        m_session->GetPlayer()->learnSpell(spell, false);
    }

    SendSysMessage(LANG_COMMAND_LEARN_MANY_SPELLS);

    return true;
}

bool ChatHandler::HandleLearnAllGMCommand(char* /*args*/)
{
    static const char* gmSpellList[] =
    {
        "24347",                                            // Become A Fish, No Breath Bar
        "35132",                                            // Visual Boom
        "38488",                                            // Attack 4000-8000 AOE
        "38795",                                            // Attack 2000 AOE + Slow Down 90%
        "15712",                                            // Attack 200
        "1852",                                             // GM Spell Silence
        "31899",                                            // Kill
        "31924",                                            // Kill
        "29878",                                            // Kill My Self
        "26644",                                            // More Kill

        "28550",                                            // Invisible 24
        "23452",                                            // Invisible + Target
        "0"
    };

    uint16 gmSpellIter = 0;
    while (strcmp(gmSpellList[gmSpellIter], "0"))
    {
        uint32 spell = atol((char*)gmSpellList[gmSpellIter++]);

        SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell);
        if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo, m_session->GetPlayer()))
        {
            PSendSysMessage(LANG_COMMAND_SPELL_BROKEN, spell);
            continue;
        }

        m_session->GetPlayer()->learnSpell(spell, false);
    }

    SendSysMessage(LANG_LEARNING_GM_SKILLS);
    return true;
}

bool ChatHandler::HandleLearnAllMyClassCommand(char* /*args*/)
{
    HandleLearnAllMySpellsCommand((char*)"");
    HandleLearnAllMyTalentsCommand((char*)"");
    return true;
}

bool ChatHandler::HandleLearnAllMySpellsCommand(char* /*args*/)
{
    Player* player = m_session->GetPlayer();
    ChrClassesEntry const* clsEntry = sChrClassesStore.LookupEntry(player->getClass());
    if (!clsEntry)
    {
        return true;
    }
    uint32 family = clsEntry->spellfamily;

    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        SkillLineAbilityEntry const* entry = sSkillLineAbilityStore.LookupEntry(i);
        if (!entry)
        {
            continue;
        }

        SpellEntry const* spellInfo = sSpellStore.LookupEntry(entry->spellId);
        if (!spellInfo)
        {
            continue;
        }

        // skip server-side/triggered spells
        if (spellInfo->spellLevel == 0)
        {
            continue;
        }

        // skip wrong class/race skills
        if (!player->IsSpellFitByClassAndRace(spellInfo->Id))
        {
            continue;
        }

        // skip other spell families
        if (spellInfo->SpellFamilyName != family)
        {
            continue;
        }

        // skip spells with first rank learned as talent (and all talents then also)
        uint32 first_rank = sSpellMgr.GetFirstSpellInChain(spellInfo->Id);
        if (GetTalentSpellCost(first_rank) > 0)
        {
            continue;
        }

        // skip broken spells
        if (!SpellMgr::IsSpellValid(spellInfo, player, false))
        {
            continue;
        }

        player->learnSpell(spellInfo->Id, false);
    }

    SendSysMessage(LANG_COMMAND_LEARN_CLASS_SPELLS);
    return true;
}

bool ChatHandler::HandleLearnAllMyTalentsCommand(char* /*args*/)
{
    Player* player = m_session->GetPlayer();
    uint32 classMask = player->getClassMask();

    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const* talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo)
        {
            continue;
        }

        TalentTabEntry const* talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo)
        {
            continue;
        }

        if ((classMask & talentTabInfo->ClassMask) == 0)
        {
            continue;
        }

        // search highest talent rank
        uint32 spellid = 0;

        for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
        {
            if (talentInfo->RankID[rank] != 0)
            {
                spellid = talentInfo->RankID[rank];
                break;
            }
        }

        if (!spellid)                                       // ??? none spells in talent
        {
            continue;
        }

        SpellEntry const* spellInfo = sSpellStore.LookupEntry(spellid);
        if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo, player, false))
        {
            continue;
        }

        // learn highest rank of talent and learn all non-talent spell ranks (recursive by tree)
        player->learnSpellHighRank(spellid);
    }

    SendSysMessage(LANG_COMMAND_LEARN_CLASS_TALENTS);
    return true;
}

bool ChatHandler::HandleLearnAllLangCommand(char* /*args*/)
{
    Player* player = m_session->GetPlayer();

    // skipping UNIVERSAL language (0)
    for (int i = 1; i < LANGUAGES_COUNT; ++i)
    {
        player->learnSpell(lang_description[i].spell_id, false);
    }

    SendSysMessage(LANG_COMMAND_LEARN_ALL_LANG);
    return true;
}

bool ChatHandler::HandleLearnAllDefaultCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    target->learnDefaultSpells();
    target->learnQuestRewardedSpells();

    PSendSysMessage(LANG_COMMAND_LEARN_ALL_DEFAULT_AND_QUEST, GetNameLink(target).c_str());
    return true;
}

bool ChatHandler::HandleLearnCommand(char* args)
{
    Player* player = m_session->GetPlayer();
    Player* targetPlayer = getSelectedPlayer();

    if (!targetPlayer)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form
    uint32 spell = ExtractSpellIdFromLink(&args);
    if (!spell || !sSpellStore.LookupEntry(spell))
    {
        return false;
    }

    bool allRanks = ExtractLiteralArg(&args, "all") != NULL;
    if (!allRanks && *args)                                 // can be fail also at syntax error
    {
        return false;
    }

    SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell);
    if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo, player))
    {
        PSendSysMessage(LANG_COMMAND_SPELL_BROKEN, spell);
        SetSentErrorMessage(true);
        return false;
    }

    if (!allRanks && targetPlayer->HasSpell(spell))
    {
        if (targetPlayer == player)
        {
            SendSysMessage(LANG_YOU_KNOWN_SPELL);
        }
        else
        {
            PSendSysMessage(LANG_TARGET_KNOWN_SPELL, targetPlayer->GetName());
        }
        SetSentErrorMessage(true);
        return false;
    }

    if (allRanks)
    {
        targetPlayer->learnSpellHighRank(spell);
    }
    else
    {
        targetPlayer->learnSpell(spell, false);
    }

    return true;
}

bool ChatHandler::HandleAddItemCommand(char* args)
{
    char* cId = ExtractKeyFromLink(&args, "Hitem");
    if (!cId)
    {
        return false;
    }

    uint32 itemId = 0;
    if (!ExtractUInt32(&cId, itemId))                       // [name] manual form
    {
        std::string itemName = cId;
        WorldDatabase.escape_string(itemName);
        QueryResult* result = WorldDatabase.PQuery("SELECT `entry` FROM `item_template` WHERE `name` = '%s'", itemName.c_str());
        if (!result)
        {
            PSendSysMessage(LANG_COMMAND_COULDNOTFIND, cId);
            SetSentErrorMessage(true);
            return false;
        }
        itemId = result->Fetch()->GetUInt16();
        delete result;
    }

    int32 count;
    if (!ExtractOptInt32(&args, count, 1))
    {
        return false;
    }

    Player* pl = m_session->GetPlayer();
    Player* plTarget = getSelectedPlayer();
    if (!plTarget)
    {
        plTarget = pl;
    }

    DETAIL_LOG(GetMangosString(LANG_ADDITEM), itemId, count);

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(itemId);
    if (!pProto)
    {
        PSendSysMessage(LANG_COMMAND_ITEMIDINVALID, itemId);
        SetSentErrorMessage(true);
        return false;
    }

    // Subtract
    if (count < 0)
    {
        plTarget->DestroyItemCount(itemId, -count, true, false);
        PSendSysMessage(LANG_REMOVEITEM, itemId, -count, GetNameLink(plTarget).c_str());
        return true;
    }

    // Adding items
    uint32 noSpaceForCount = 0;

    // check space and find places
    ItemPosCountVec dest;
    uint8 msg = plTarget->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count, &noSpaceForCount);
    if (msg != EQUIP_ERR_OK)                                // convert to possible store amount
    {
        count -= noSpaceForCount;
    }

    if (count == 0 || dest.empty())                         // can't add any
    {
        PSendSysMessage(LANG_ITEM_CANNOT_CREATE, itemId, noSpaceForCount);
        SetSentErrorMessage(true);
        return false;
    }

    Item* item = plTarget->StoreNewItem(dest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));

    // remove binding (let GM give it to another player later)
    if (pl == plTarget)
        for (ItemPosCountVec::const_iterator itr = dest.begin(); itr != dest.end(); ++itr)
            if (Item* item1 = pl->GetItemByPos(itr->pos))
            {
                item1->SetBinding(false);
            }

    if (count > 0 && item)
    {
        pl->SendNewItem(item, count, false, true);
        if (pl != plTarget)
        {
            plTarget->SendNewItem(item, count, true, false);
        }
    }

    if (noSpaceForCount > 0)
    {
        PSendSysMessage(LANG_ITEM_CANNOT_CREATE, itemId, noSpaceForCount);
    }

    return true;
}

bool ChatHandler::HandleAddItemSetCommand(char* args)
{
    uint32 itemsetId;
    if (!ExtractUint32KeyFromLink(&args, "Hitemset", itemsetId))
    {
        return false;
    }

    // prevent generation all items with itemset field value '0'
    if (itemsetId == 0)
    {
        PSendSysMessage(LANG_NO_ITEMS_FROM_ITEMSET_FOUND, itemsetId);
        SetSentErrorMessage(true);
        return false;
    }

    Player* pl = m_session->GetPlayer();
    Player* plTarget = getSelectedPlayer();
    if (!plTarget)
    {
        plTarget = pl;
    }

    DETAIL_LOG(GetMangosString(LANG_ADDITEMSET), itemsetId);

    bool found = false;
    for (uint32 id = 0; id < sItemStorage.GetMaxEntry(); ++id)
    {
        ItemPrototype const* pProto = sItemStorage.LookupEntry<ItemPrototype>(id);
        if (!pProto)
        {
            continue;
        }

        if (pProto->ItemSet == itemsetId)
        {
            found = true;
            ItemPosCountVec dest;
            InventoryResult msg = plTarget->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, pProto->ItemId, 1);
            if (msg == EQUIP_ERR_OK)
            {
                Item* item = plTarget->StoreNewItem(dest, pProto->ItemId, true);

                // remove binding (let GM give it to another player later)
                if (pl == plTarget)
                {
                    item->SetBinding(false);
                }

                pl->SendNewItem(item, 1, false, true);
                if (pl != plTarget)
                {
                    plTarget->SendNewItem(item, 1, true, false);
                }
            }
            else
            {
                pl->SendEquipError(msg, NULL, NULL, pProto->ItemId);
                PSendSysMessage(LANG_ITEM_CANNOT_CREATE, pProto->ItemId, 1);
            }
        }
    }

    if (!found)
    {
        PSendSysMessage(LANG_NO_ITEMS_FROM_ITEMSET_FOUND, itemsetId);

        SetSentErrorMessage(true);
        return false;
    }

    return true;
}


void ChatHandler::ShowSpellListHelper(Player* target, SpellEntry const* spellInfo, LocaleConstant loc)
{
    uint32 id = spellInfo->Id;

    bool known = target && target->HasSpell(id);
    bool learn = (spellInfo->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_LEARN_SPELL);

    uint32 talentCost = GetTalentSpellCost(id);

    bool talent = (talentCost > 0);
    bool passive = IsPassiveSpell(spellInfo);
    bool active = target && target->HasAura(id);

    // unit32 used to prevent interpreting uint8 as char at output
    // find rank of learned spell for learning spell, or talent rank
    uint32 rank = talentCost ? talentCost : sSpellMgr.GetSpellRank(learn ? spellInfo->EffectTriggerSpell[EFFECT_INDEX_0] : id);

    // send spell in "id - [name, rank N] [talent] [passive] [learn] [known]" format
    std::ostringstream ss;
    if (m_session)
    {
        ss << id << " - |cffffffff|Hspell:" << id << "|h[" << spellInfo->SpellName[loc];
    }
    else
    {
        ss << id << " - " << spellInfo->SpellName[loc];
    }

    // include rank in link name
    if (rank)
    {
        ss << GetMangosString(LANG_SPELL_RANK) << rank;
    }

    if (m_session)
    {
        ss << " " << localeNames[loc] << "]|h|r";
    }
    else
    {
        ss << " " << localeNames[loc];
    }

    if (talent)
    {
        ss << GetMangosString(LANG_TALENT);
    }
    if (passive)
    {
        ss << GetMangosString(LANG_PASSIVE);
    }
    if (learn)
    {
        ss << GetMangosString(LANG_LEARN);
    }
    if (known)
    {
        ss << GetMangosString(LANG_KNOWN);
    }
    if (active)
    {
        ss << GetMangosString(LANG_ACTIVE);
    }

    SendSysMessage(ss.str().c_str());
}



/** \brief GM command level 3 - Create a guild.
 *
 * This command allows a GM (level 3) to create a guild.
 *
 * The "args" parameter contains the name of the guild leader
 * and then the name of the guild.
 *
 */


bool ChatHandler::HandleGetDistanceCommand(char* args)
{
    WorldObject* obj = NULL;

    if (*args)
    {
        if (ObjectGuid guid = ExtractGuidFromLink(&args))
        {
            obj = (WorldObject*)m_session->GetPlayer()->GetObjectByTypeMask(guid, TYPEMASK_CREATURE_OR_GAMEOBJECT);
        }

        if (!obj)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        obj = getSelectedUnit();

        if (!obj)
        {
            SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            SetSentErrorMessage(true);
            return false;
        }
    }

    Player* player = m_session->GetPlayer();
    // Calculate point-to-point distance
    float dx, dy, dz;
    dx = player->GetPositionX() - obj->GetPositionX();
    dy = player->GetPositionY() - obj->GetPositionY();
    dz = player->GetPositionZ() - obj->GetPositionZ();

    PSendSysMessage(LANG_DISTANCE, player->GetDistance(obj), player->GetDistance2d(obj), sqrt(dx * dx + dy * dy + dz * dz));

    return true;
}

bool ChatHandler::HandleDieCommand(char* /*args*/)
{
    Player* player = m_session->GetPlayer();
    Unit* target = getSelectedUnit();

    if (!target || !player->GetSelectionGuid())
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (target->GetTypeId() == TYPEID_PLAYER)
    {
        if (HasLowerSecurity((Player*)target, ObjectGuid(), false))
        {
            return false;
        }
    }

    if (target->IsAlive())
    {
        player->DealDamage(target, target->GetHealth(), NULL, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
    }

    return true;
}

bool ChatHandler::HandleDamageCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Unit* target = getSelectedUnit();
    Player* player = m_session->GetPlayer();

    if (!target || !player->GetSelectionGuid())
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (!target->IsAlive())
    {
        return true;
    }

    int32 damage_int;
    if (!ExtractInt32(&args, damage_int))
    {
        return false;
    }

    if (damage_int <= 0)
    {
        return true;
    }

    uint32 damage = damage_int;

    // flat melee damage without resistance/etc reduction
    if (!*args)
    {
        player->DealDamage(target, damage, NULL, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
        if (target != player)
        {
            player->SendAttackStateUpdate(HITINFO_NORMALSWING2, target, SPELL_SCHOOL_MASK_NORMAL, damage, 0, 0, VICTIMSTATE_NORMAL, 0);
        }
        return true;
    }

    uint32 school;
    if (!ExtractUInt32(&args, school))
    {
        return false;
    }

    if (school >= MAX_SPELL_SCHOOL)
    {
        return false;
    }

    SpellSchoolMask schoolmask = SpellSchoolMask(1 << school);

    if (schoolmask & SPELL_SCHOOL_MASK_NORMAL)
    {
        damage = player->CalcArmorReducedDamage(target, damage);
    }

    // melee damage by specific school
    if (!*args)
    {
        uint32 absorb = 0;
        uint32 resist = 0;

        target->CalculateDamageAbsorbAndResist(player, schoolmask, SPELL_DIRECT_DAMAGE, damage, &absorb, &resist);

        if (damage <= absorb + resist)
        {
            return true;
        }

        damage -= absorb + resist;

        player->DealDamageMods(target, damage, &absorb);
        player->DealDamage(target, damage, NULL, DIRECT_DAMAGE, schoolmask, NULL, false);
        player->SendAttackStateUpdate(HITINFO_NORMALSWING2, target, schoolmask, damage, absorb, resist, VICTIMSTATE_NORMAL, 0);
        return true;
    }

    // non-melee damage

    // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form
    uint32 spellid = ExtractSpellIdFromLink(&args);
    if (!spellid || !sSpellStore.LookupEntry(spellid))
    {
        return false;
    }

    player->SpellNonMeleeDamageLog(target, spellid, damage);
    return true;
}

bool ChatHandler::HandleModifyArenaCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    int32 amount = (int32)atoi(args);

    target->ModifyArenaPoints(amount);

    PSendSysMessage(LANG_COMMAND_MODIFY_ARENA, GetNameLink(target).c_str(), target->GetArenaPoints());

    return true;
}

bool ChatHandler::HandleReviveCommand(char* args)
{
    Player* target;
    ObjectGuid target_guid;
    if (!ExtractPlayerTarget(&args, &target, &target_guid))
    {
        return false;
    }

    if (target)
    {
        target->ResurrectPlayer(0.5f);
        target->SpawnCorpseBones();
    }
    else // will resurrected at login without corpse
    {
        sObjectAccessor.ConvertCorpseForPlayer(target_guid);
    }

    return true;
}

bool ChatHandler::HandleLinkGraveCommand(char* args)
{
    uint32 g_id;
    if (!ExtractUInt32(&args, g_id))
    {
        return false;
    }

    char* teamStr = ExtractLiteralArg(&args);

    Team g_team;
    if (!teamStr)
    {
        g_team = TEAM_BOTH_ALLOWED;
    }
    else if (strncmp(teamStr, "horde", strlen(teamStr)) == 0)
    {
        g_team = HORDE;
    }
    else if (strncmp(teamStr, "alliance", strlen(teamStr)) == 0)
    {
        g_team = ALLIANCE;
    }
    else
    {
        return false;
    }

    WorldSafeLocsEntry const* graveyard = sWorldSafeLocsStore.LookupEntry(g_id);
    if (!graveyard)
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDNOEXIST, g_id);
        SetSentErrorMessage(true);
        return false;
    }

    Player* player = m_session->GetPlayer();

    uint32 zoneId = player->GetZoneId();

    AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(zoneId);
    if (!areaEntry || areaEntry->zone != 0)
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDWRONGZONE, g_id, zoneId);
        SetSentErrorMessage(true);
        return false;
    }

    if (sObjectMgr.AddGraveYardLink(g_id, zoneId, g_team))
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDLINKED, g_id, zoneId);
    }
    else
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDALRLINKED, g_id, zoneId);
    }

    return true;
}

bool ChatHandler::HandleNearGraveCommand(char* args)
{
    Team g_team;

    size_t argslen = strlen(args);

    if (!*args)
    {
        g_team = TEAM_BOTH_ALLOWED;
    }
    else if (strncmp(args, "horde", argslen) == 0)
    {
        g_team = HORDE;
    }
    else if (strncmp(args, "alliance", argslen) == 0)
    {
        g_team = ALLIANCE;
    }
    else
    {
        return false;
    }

    Player* player = m_session->GetPlayer();
    uint32 zone_id = player->GetZoneId();

    WorldSafeLocsEntry const* graveyard = sObjectMgr.GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), g_team);

    if (graveyard)
    {
        uint32 g_id = graveyard->ID;

        GraveYardData const* data = sObjectMgr.FindGraveYardData(g_id, zone_id);
        if (!data)
        {
            PSendSysMessage(LANG_COMMAND_GRAVEYARDERROR, g_id);
            SetSentErrorMessage(true);
            return false;
        }

        std::string team_name;

        if (data->team == TEAM_BOTH_ALLOWED)
        {
            team_name = GetMangosString(LANG_COMMAND_GRAVEYARD_ANY);
        }
        else if (data->team == HORDE)
        {
            team_name = GetMangosString(LANG_COMMAND_GRAVEYARD_HORDE);
        }
        else if (data->team == ALLIANCE)
        {
            team_name = GetMangosString(LANG_COMMAND_GRAVEYARD_ALLIANCE);
        }
        else                                                // Actually, this case can not happen
        {
            team_name = GetMangosString(LANG_COMMAND_GRAVEYARD_NOTEAM);
        }

        PSendSysMessage(LANG_COMMAND_GRAVEYARDNEAREST, g_id, team_name.c_str(), zone_id);
    }
    else
    {
        std::string team_name;

        if (g_team == TEAM_BOTH_ALLOWED)
        {
            team_name = GetMangosString(LANG_COMMAND_GRAVEYARD_ANY);
        }
        else if (g_team == HORDE)
        {
            team_name = GetMangosString(LANG_COMMAND_GRAVEYARD_HORDE);
        }
        else if (g_team == ALLIANCE)
        {
            team_name = GetMangosString(LANG_COMMAND_GRAVEYARD_ALLIANCE);
        }

        if (g_team == TEAM_BOTH_ALLOWED)
        {
            PSendSysMessage(LANG_COMMAND_ZONENOGRAVEYARDS, zone_id);
        }
        else
        {
            PSendSysMessage(LANG_COMMAND_ZONENOGRAFACTION, zone_id, team_name.c_str());
        }
    }

    return true;
}

//-----------------------Npc Commands-----------------------
bool ChatHandler::HandleExploreCheatCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    int flag = atoi(args);

    Player* chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    if (flag != 0)
    {
        PSendSysMessage(LANG_YOU_SET_EXPLORE_ALL, GetNameLink(chr).c_str());
        if (needReportToTarget(chr))
        {
            ChatHandler(chr).PSendSysMessage(LANG_YOURS_EXPLORE_SET_ALL, GetNameLink().c_str());
        }
    }
    else
    {
        PSendSysMessage(LANG_YOU_SET_EXPLORE_NOTHING, GetNameLink(chr).c_str());
        if (needReportToTarget(chr))
        {
            ChatHandler(chr).PSendSysMessage(LANG_YOURS_EXPLORE_SET_NOTHING, GetNameLink().c_str());
        }
    }

    for (uint8 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
    {
        if (flag != 0)
        {
            m_session->GetPlayer()->SetFlag(PLAYER_EXPLORED_ZONES_1 + i, 0xFFFFFFFF);
        }
        else
        {
            m_session->GetPlayer()->SetFlag(PLAYER_EXPLORED_ZONES_1 + i, 0);
        }
    }

    return true;
}

void ChatHandler::HandleCharacterLevel(Player* player, ObjectGuid player_guid, uint32 oldlevel, uint32 newlevel)
{
    if (player)
    {
        player->GiveLevel(newlevel);
        player->InitTalentForLevel();
        player->SetUInt32Value(PLAYER_XP, 0);

        if (needReportToTarget(player))
        {
            if (oldlevel == newlevel)
            {
                ChatHandler(player).PSendSysMessage(LANG_YOURS_LEVEL_PROGRESS_RESET, GetNameLink().c_str());
            }
            else if (oldlevel < newlevel)
            {
                ChatHandler(player).PSendSysMessage(LANG_YOURS_LEVEL_UP, GetNameLink().c_str(), newlevel);
            }
            else                                            // if(oldlevel > newlevel)
            {
                ChatHandler(player).PSendSysMessage(LANG_YOURS_LEVEL_DOWN, GetNameLink().c_str(), newlevel);
            }
        }
    }
    else
    {
        // update level and XP at level, all other will be updated at loading
        CharacterDatabase.PExecute("UPDATE `characters` SET `level` = '%u', `xp` = 0 WHERE `guid` = '%u'", newlevel, player_guid.GetCounter());
    }
}

bool ChatHandler::HandleCharacterLevelCommand(char* args)
{
    char* nameStr = ExtractOptNotLastArg(&args);

    int32 newlevel;
    bool nolevel = false;
    // exception opt second arg: .character level $name
    if (!ExtractInt32(&args, newlevel))
    {
        if (!nameStr)
        {
            nameStr = ExtractArg(&args);
            if (!nameStr)
            {
                return false;
            }

            nolevel = true;
        }
        else
        {
            return false;
        }
    }

    Player* target;
    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&nameStr, &target, &target_guid, &target_name))
    {
        return false;
    }

    int32 oldlevel = target ? target->getLevel() : Player::GetLevelFromDB(target_guid);
    if (nolevel)
    {
        newlevel = oldlevel;
    }

    if (newlevel < 1)
    {
        return false; // invalid level
    }

    if (newlevel > STRONG_MAX_LEVEL)                        // hardcoded maximum level
    {
        newlevel = STRONG_MAX_LEVEL;
    }

    HandleCharacterLevel(target, target_guid, oldlevel, newlevel);

    if (!m_session || m_session->GetPlayer() != target)     // including player==NULL
    {
        std::string nameLink = playerLink(target_name);
        PSendSysMessage(LANG_YOU_CHANGE_LVL, nameLink.c_str(), newlevel);
    }

    return true;
}

bool ChatHandler::HandleLevelUpCommand(char* args)
{
    int32 addlevel = 1;
    char* nameStr = NULL;

    if (*args)
    {
        nameStr = ExtractOptNotLastArg(&args);

        // exception opt second arg: .levelup $name
        if (!ExtractInt32(&args, addlevel))
        {
            if (!nameStr)
            {
                nameStr = ExtractArg(&args);
            }
            else
            {
                return false;
            }
        }
    }

    Player* target;
    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&nameStr, &target, &target_guid, &target_name))
    {
        return false;
    }

    int32 oldlevel = target ? target->getLevel() : Player::GetLevelFromDB(target_guid);
    int32 newlevel = oldlevel + addlevel;

    if (newlevel < 1)
    {
        newlevel = 1;
    }

    if (newlevel > STRONG_MAX_LEVEL)                        // hardcoded maximum level
    {
        newlevel = STRONG_MAX_LEVEL;
    }

    HandleCharacterLevel(target, target_guid, oldlevel, newlevel);

    if (!m_session || m_session->GetPlayer() != target)     // including chr==NULL
    {
        std::string nameLink = playerLink(target_name);
        PSendSysMessage(LANG_YOU_CHANGE_LVL, nameLink.c_str(), newlevel);
    }

    return true;
}

bool ChatHandler::HandleShowAreaCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    int area = GetAreaFlagByAreaID(atoi(args));
    int offset = area / 32;
    uint32 val = (uint32)(1 << (area % 32));

    if (area < 0 || offset >= PLAYER_EXPLORED_ZONES_SIZE)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 currFields = chr->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);
    chr->SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset, (uint32)(currFields | val));

    SendSysMessage(LANG_EXPLORE_AREA);
    return true;
}

bool ChatHandler::HandleHideAreaCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    int area = GetAreaFlagByAreaID(atoi(args));
    int offset = area / 32;
    uint32 val = (uint32)(1 << (area % 32));

    if (area < 0 || offset >= PLAYER_EXPLORED_ZONES_SIZE)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 currFields = chr->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);
    chr->SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset, (uint32)(currFields ^ val));

    SendSysMessage(LANG_UNEXPLORE_AREA);
    return true;
}

bool ChatHandler::HandleBankCommand(char* /*args*/)
{
    m_session->SendShowBank(m_session->GetPlayer()->GetObjectGuid());

    return true;
}

bool ChatHandler::HandleStableCommand(char* /*args*/)
{
    m_session->SendStablePet(m_session->GetPlayer()->GetObjectGuid());

    return true;
}
bool ChatHandler::HandleTeleAddCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* player = m_session->GetPlayer();
    if (!player)
    {
        return false;
    }

    std::string name = args;

    if (sObjectMgr.GetGameTele(name))
    {
        SendSysMessage(LANG_COMMAND_TP_ALREADYEXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameTele tele;
    tele.position_x  = player->GetPositionX();
    tele.position_y  = player->GetPositionY();
    tele.position_z  = player->GetPositionZ();
    tele.orientation = player->GetOrientation();
    tele.mapId       = player->GetMapId();
    tele.name        = name;

    if (sObjectMgr.AddGameTele(tele))
    {
        SendSysMessage(LANG_COMMAND_TP_ADDED);
    }
    else
    {
        SendSysMessage(LANG_COMMAND_TP_ADDEDERR);
        SetSentErrorMessage(true);
        return false;
    }

    return true;
}

bool ChatHandler::HandleTeleDelCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    std::string name = args;

    if (!sObjectMgr.DeleteGameTele(name))
    {
        SendSysMessage(LANG_COMMAND_TELE_NOTFOUND);
        SetSentErrorMessage(true);
        return false;
    }

    SendSysMessage(LANG_COMMAND_TP_DELETED);
    return true;
}

bool ChatHandler::HandleResetHonorCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    target->SetHonorPoints(0);
    target->SetUInt32Value(PLAYER_FIELD_KILLS, 0);
    target->SetUInt32Value(PLAYER_FIELD_LIFETIME_HONORBALE_KILLS, 0);
    target->SetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION, 0);
    target->SetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION, 0);
    return true;
}

static bool HandleResetStatsOrLevelHelper(Player* player)
{
    ChrClassesEntry const* cEntry = sChrClassesStore.LookupEntry(player->getClass());
    if (!cEntry)
    {
        sLog.outError("Class %u not found in DBC (Wrong DBC files?)", player->getClass());
        return false;
    }

    uint8 powertype = cEntry->powerType;

    // reset m_form if no aura
    if (!player->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
    {
        player->SetShapeshiftForm(FORM_NONE);
    }

    player->SetFloatValue(UNIT_FIELD_BOUNDINGRADIUS, DEFAULT_WORLD_OBJECT_SIZE);
    player->SetFloatValue(UNIT_FIELD_COMBATREACH, 1.5f);

    player->setFactionForRace(player->getRace());

    player->SetByteValue(UNIT_FIELD_BYTES_0, 3, powertype);

    // reset only if player not in some form;
    if (player->GetShapeshiftForm() == FORM_NONE)
    {
        player->InitDisplayIds();
    }

    player->SetByteValue(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_UNK3 | UNIT_BYTE2_FLAG_UNK5);

    player->SetUInt32Value(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);

    //-1 is default value
    player->SetInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, -1);

    // player->SetUInt32Value(PLAYER_FIELD_BYTES, 0xEEE00000 );
    return true;
}

bool ChatHandler::HandleResetLevelCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    if (!HandleResetStatsOrLevelHelper(target))
    {
        return false;
    }

    // set starting level
    uint32 start_level = sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL);

    target->SetLevel(start_level);
    target->InitStatsForLevel(true);
    target->InitTaxiNodesForLevel();
    target->InitTalentForLevel();
    target->SetUInt32Value(PLAYER_XP, 0);

    // reset level for pet
    if (Pet* pet = target->GetPet())
    {
        pet->SynchronizeLevelWithOwner();
    }

    return true;
}

bool ChatHandler::HandleResetStatsCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    if (!HandleResetStatsOrLevelHelper(target))
    {
        return false;
    }

    target->InitStatsForLevel(true);
    target->InitTaxiNodesForLevel();
    target->InitTalentForLevel();

    return true;
}

bool ChatHandler::HandleResetSpellsCommand(char* args)
{
    Player* target;
    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&args, &target, &target_guid, &target_name))
    {
        return false;
    }

    if (target)
    {
        target->resetSpells();

        ChatHandler(target).SendSysMessage(LANG_RESET_SPELLS);
        if (!m_session || m_session->GetPlayer() != target)
        {
            PSendSysMessage(LANG_RESET_SPELLS_ONLINE, GetNameLink(target).c_str());
        }
    }
    else
    {
        CharacterDatabase.PExecute("UPDATE `characters` SET `at_login` = `at_login` | '%u' WHERE `guid` = '%u'", uint32(AT_LOGIN_RESET_SPELLS), target_guid.GetCounter());
        PSendSysMessage(LANG_RESET_SPELLS_OFFLINE, target_name.c_str());
    }

    return true;
}

bool ChatHandler::HandleResetTalentsCommand(char* args)
{
    Player* target;
    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&args, &target, &target_guid, &target_name))
    {
        return false;
    }

    if (target)
    {
        target->resetTalents(true);

        ChatHandler(target).SendSysMessage(LANG_RESET_TALENTS);
        if (!m_session || m_session->GetPlayer() != target)
        {
            PSendSysMessage(LANG_RESET_TALENTS_ONLINE, GetNameLink(target).c_str());
        }
        return true;
    }
    else if (target_guid)
    {
        uint32 at_flags = AT_LOGIN_RESET_TALENTS;
        CharacterDatabase.PExecute("UPDATE `characters` SET `at_login` = `at_login` | '%u' WHERE `guid` = '%u'", at_flags, target_guid.GetCounter());
        std::string nameLink = playerLink(target_name);
        PSendSysMessage(LANG_RESET_TALENTS_OFFLINE, nameLink.c_str());
        return true;
    }

    SendSysMessage(LANG_NO_CHAR_SELECTED);
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleResetAllCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    std::string casename = args;

    AtLoginFlags atLogin;

    // Command specially created as single command to prevent using short case names
    if (casename == "spells")
    {
        atLogin = AT_LOGIN_RESET_SPELLS;
        sWorld.SendWorldText(LANG_RESETALL_SPELLS);
        if (!m_session)
        {
            SendSysMessage(LANG_RESETALL_SPELLS);
        }
    }
    else if (casename == "talents")
    {
        atLogin = AT_LOGIN_RESET_TALENTS;
        sWorld.SendWorldText(LANG_RESETALL_TALENTS);
        if (!m_session)
        {
            SendSysMessage(LANG_RESETALL_TALENTS);
        }
    }
    else
    {
        PSendSysMessage(LANG_RESETALL_UNKNOWN_CASE, args);
        SetSentErrorMessage(true);
        return false;
    }

    CharacterDatabase.PExecute("UPDATE `characters` SET `at_login` = `at_login` | '%u' WHERE (`at_login` & '%u') = '0'", atLogin, atLogin);
    sObjectAccessor.DoForAllPlayers([&atLogin](Player* plr){ plr->SetAtLoginFlag(atLogin); });
    return true;
}

bool ChatHandler::HandleQuestAddCommand(char* args)
{
    Player* player = getSelectedPlayer();
    if (!player)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // .addquest #entry'
    // number or [name] Shift-click form |color|Hquest:quest_id:quest_level|h[name]|h|r
    uint32 entry;
    if (!ExtractUint32KeyFromLink(&args, "Hquest", entry))
    {
        return false;
    }

    Quest const* pQuest = sObjectMgr.GetQuestTemplate(entry);
    if (!pQuest)
    {
        PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, entry);
        SetSentErrorMessage(true);
        return false;
    }

    // check item starting quest (it can work incorrectly if added without item in inventory)
    for (uint32 id = 0; id < sItemStorage.GetMaxEntry(); ++id)
    {
        ItemPrototype const* pProto = sItemStorage.LookupEntry<ItemPrototype>(id);
        if (!pProto)
        {
            continue;
        }

        if (pProto->StartQuest == entry)
        {
            PSendSysMessage(LANG_COMMAND_QUEST_STARTFROMITEM, entry, pProto->ItemId);
            SetSentErrorMessage(true);
            return false;
        }
    }

    // ok, normal (creature/GO starting) quest
    if (player->CanAddQuest(pQuest, true))
    {
        player->AddQuest(pQuest, NULL);

        if (player->CanCompleteQuest(entry))
        {
            player->CompleteQuest(entry);
        }
    }

    return true;
}

bool ChatHandler::HandleQuestRemoveCommand(char* args)
{
    Player* player = getSelectedPlayer();
    if (!player)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // .removequest #entry'
    // number or [name] Shift-click form |color|Hquest:quest_id:quest_level|h[name]|h|r
    uint32 entry;
    if (!ExtractUint32KeyFromLink(&args, "Hquest", entry))
    {
        return false;
    }

    Quest const* pQuest = sObjectMgr.GetQuestTemplate(entry);

    if (!pQuest)
    {
        PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, entry);
        SetSentErrorMessage(true);
        return false;
    }

    // remove all quest entries for 'entry' from quest log
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 quest = player->GetQuestSlotQuestId(slot);
        if (quest == entry)
        {
            player->SetQuestSlot(slot, 0);

            // we ignore unequippable quest items in this case, its' still be equipped
            player->TakeQuestSourceItem(quest, false);
        }
    }

    // set quest status to not started (will updated in DB at next save)
    player->SetQuestStatus(entry, QUEST_STATUS_NONE);

    // reset rewarded for restart repeatable quest
    player->getQuestStatusMap()[entry].m_rewarded = false;

    SendSysMessage(LANG_COMMAND_QUEST_REMOVED);
    return true;
}

bool ChatHandler::HandleQuestCompleteCommand(char* args)
{
    Player* player = getSelectedPlayer();
    if (!player)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // .quest complete #entry
    // number or [name] Shift-click form |color|Hquest:quest_id:quest_level|h[name]|h|r
    uint32 entry;
    if (!ExtractUint32KeyFromLink(&args, "Hquest", entry))
    {
        return false;
    }

    Quest const* pQuest = sObjectMgr.GetQuestTemplate(entry);

    // If player doesn't have the quest
    if (!pQuest || player->GetQuestStatus(entry) == QUEST_STATUS_NONE)
    {
        PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, entry);
        SetSentErrorMessage(true);
        return false;
    }

    // Add quest items for quests that require items
    for (uint8 x = 0; x < QUEST_ITEM_OBJECTIVES_COUNT; ++x)
    {
        uint32 id = pQuest->ReqItemId[x];
        uint32 count = pQuest->ReqItemCount[x];
        if (!id || !count)
        {
            continue;
        }

        uint32 curItemCount = player->GetItemCount(id, true);

        ItemPosCountVec dest;
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, id, count - curItemCount);
        if (msg == EQUIP_ERR_OK)
        {
            Item* item = player->StoreNewItem(dest, id, true);
            player->SendNewItem(item, count - curItemCount, true, false);
        }
    }

    // All creature/GO slain/casted (not required, but otherwise it will display "Creature slain 0/10")
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        int32 creature = pQuest->ReqCreatureOrGOId[i];
        uint32 creaturecount = pQuest->ReqCreatureOrGOCount[i];

        if (uint32 spell_id = pQuest->ReqSpell[i])
        {
            for (uint16 z = 0; z < creaturecount; ++z)
            {
                player->CastedCreatureOrGO(creature, ObjectGuid(), spell_id);
            }
        }
        else if (creature > 0)
        {
            if (CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(creature))
                for (uint16 z = 0; z < creaturecount; ++z)
                {
                    player->KilledMonster(cInfo, ObjectGuid());
                }
        }
        else if (creature < 0)
        {
            for (uint16 z = 0; z < creaturecount; ++z)
            {
                player->CastedCreatureOrGO(-creature, ObjectGuid(), 0);
            }
        }
    }

    // If the quest requires reputation to complete
    if (uint32 repFaction = pQuest->GetRepObjectiveFaction())
    {
        uint32 repValue = pQuest->GetRepObjectiveValue();
        uint32 curRep = player->GetReputationMgr().GetReputation(repFaction);
        if (curRep < repValue)
            if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
            {
                player->GetReputationMgr().SetReputation(factionEntry, repValue);
            }
    }

    // If the quest requires money
    int32 ReqOrRewMoney = pQuest->GetRewOrReqMoney();
    if (ReqOrRewMoney < 0)
    {
        player->ModifyMoney(-ReqOrRewMoney);
    }

    player->CompleteQuest(entry, QUEST_STATUS_FORCE_COMPLETE);
    return true;
}
bool ChatHandler::HandleMovegensCommand(char* /*args*/)
{
    Unit* unit = getSelectedUnit();
    if (!unit)
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_MOVEGENS_LIST, (unit->GetTypeId() == TYPEID_PLAYER ? "Player" : "Creature"), unit->GetGUIDLow());

    MotionMaster* mm = unit->GetMotionMaster();
    float x, y, z;
    mm->GetDestination(x, y, z);
    for (MotionMaster::const_iterator itr = mm->begin(); itr != mm->end(); ++itr)
    {
        switch ((*itr)->GetMovementGeneratorType())
        {
            case IDLE_MOTION_TYPE:          SendSysMessage(LANG_MOVEGENS_IDLE);          break;
            case RANDOM_MOTION_TYPE:        SendSysMessage(LANG_MOVEGENS_RANDOM);        break;
            case WAYPOINT_MOTION_TYPE:      SendSysMessage(LANG_MOVEGENS_WAYPOINT);      break;
            case CONFUSED_MOTION_TYPE:      SendSysMessage(LANG_MOVEGENS_CONFUSED);      break;

            case CHASE_MOTION_TYPE:
            {
                Unit* target = NULL;
                if (unit->GetTypeId() == TYPEID_PLAYER)
                {
                    target = static_cast<ChaseMovementGenerator<Player> const*>(*itr)->GetTarget();
                }
                else
                {
                    target = static_cast<ChaseMovementGenerator<Creature> const*>(*itr)->GetTarget();
                }

                if (!target)
                {
                    SendSysMessage(LANG_MOVEGENS_CHASE_NULL);
                }
                else if (target->GetTypeId() == TYPEID_PLAYER)
                {
                    PSendSysMessage(LANG_MOVEGENS_CHASE_PLAYER, target->GetName(), target->GetGUIDLow());
                }
                else
                {
                    PSendSysMessage(LANG_MOVEGENS_CHASE_CREATURE, target->GetName(), target->GetGUIDLow());
                }
                break;
            }
            case FOLLOW_MOTION_TYPE:
            {
                Unit* target = NULL;
                if (unit->GetTypeId() == TYPEID_PLAYER)
                {
                    target = static_cast<FollowMovementGenerator<Player> const*>(*itr)->GetTarget();
                }
                else
                {
                    target = static_cast<FollowMovementGenerator<Creature> const*>(*itr)->GetTarget();
                }

                if (!target)
                {
                    SendSysMessage(LANG_MOVEGENS_FOLLOW_NULL);
                }
                else if (target->GetTypeId() == TYPEID_PLAYER)
                {
                    PSendSysMessage(LANG_MOVEGENS_FOLLOW_PLAYER, target->GetName(), target->GetGUIDLow());
                }
                else
                {
                    PSendSysMessage(LANG_MOVEGENS_FOLLOW_CREATURE, target->GetName(), target->GetGUIDLow());
                }
                break;
            }
            case HOME_MOTION_TYPE:
                if (unit->GetTypeId() == TYPEID_UNIT)
                {
                    PSendSysMessage(LANG_MOVEGENS_HOME_CREATURE, x, y, z);
                }
                else
                {
                    SendSysMessage(LANG_MOVEGENS_HOME_PLAYER);
                }
                break;
            case FLIGHT_MOTION_TYPE:   SendSysMessage(LANG_MOVEGENS_FLIGHT);  break;
            case POINT_MOTION_TYPE:
            {
                PSendSysMessage(LANG_MOVEGENS_POINT, x, y, z);
                break;
            }
            case FLEEING_MOTION_TYPE:  SendSysMessage(LANG_MOVEGENS_FEAR);    break;
            case DISTRACT_MOTION_TYPE: SendSysMessage(LANG_MOVEGENS_DISTRACT);  break;
            case EFFECT_MOTION_TYPE: SendSysMessage(LANG_MOVEGENS_EFFECT);  break;
            default:
                PSendSysMessage(LANG_MOVEGENS_UNKNOWN, (*itr)->GetMovementGeneratorType());
                break;
        }
    }
    return true;
}

/*
ComeToMe command REQUIRED for 3rd party scripting library to have access to PointMovementGenerator
Without this function 3rd party scripting library will get linking errors (unresolved external)
when attempting to use the PointMovementGenerator
*/

bool ChatHandler::ShowPlayerListHelper(QueryResult* result, uint32* limit, bool title, bool error)
{
    if (!result)
    {
        if (error)
        {
            PSendSysMessage(LANG_NO_PLAYERS_FOUND);
            SetSentErrorMessage(true);
        }
        return false;
    }

    if (!m_session && title)
    {
        SendSysMessage(LANG_CHARACTERS_LIST_BAR);
        SendSysMessage(LANG_CHARACTERS_LIST_HEADER);
        SendSysMessage(LANG_CHARACTERS_LIST_BAR);
    }

    if (result)
    {
        ///- Circle through them. Display username and GM level
        do
        {
            // check limit
            if (limit)
            {
                if (*limit == 0)
                {
                    break;
                }
                --*limit;
            }

            Field* fields = result->Fetch();
            uint32 guid      = fields[0].GetUInt32();
            std::string name = fields[1].GetCppString();
            uint8 race       = fields[2].GetUInt8();
            uint8 class_     = fields[3].GetUInt8();
            uint32 level     = fields[4].GetUInt32();

            ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
            ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(class_);

            char const* race_name = raceEntry   ? raceEntry->name[GetSessionDbcLocale()] : "<?>";
            char const* class_name = classEntry ? classEntry->name[GetSessionDbcLocale()] : "<?>";

            if (!m_session)
            {
                PSendSysMessage(LANG_CHARACTERS_LIST_LINE_CONSOLE, guid, name.c_str(), race_name, class_name, level);
            }
            else
            {
                PSendSysMessage(LANG_CHARACTERS_LIST_LINE_CHAT, guid, name.c_str(), name.c_str(), race_name, class_name, level);
            }
        }
        while (result->NextRow());

        delete result;
    }

    if (!m_session)
    {
        SendSysMessage(LANG_CHARACTERS_LIST_BAR);
    }

    return true;
}


bool ChatHandler::HandleFlushArenaPointsCommand(char* /*args*/)
{
    sBattleGroundMgr.DistributeArenaPoints();
    return true;
}

bool ChatHandler::HandleModifyGenderCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* player = getSelectedPlayer();

    if (!player)
    {
        PSendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(player->getRace(), player->getClass());
    if (!info)
    {
        return false;
    }

    char* gender_str = args;
    int gender_len = strlen(gender_str);

    Gender gender;

    if (!strncmp(gender_str, "male", gender_len))           // MALE
    {
        if (player->getGender() == GENDER_MALE)
        {
            return true;
        }

        gender = GENDER_MALE;
    }
    else if (!strncmp(gender_str, "female", gender_len))    // FEMALE
    {
        if (player->getGender() == GENDER_FEMALE)
        {
            return true;
        }

        gender = GENDER_FEMALE;
    }
    else
    {
        SendSysMessage(LANG_MUST_MALE_OR_FEMALE);
        SetSentErrorMessage(true);
        return false;
    }

    // Set gender
    player->SetByteValue(UNIT_FIELD_BYTES_0, 2, gender);
    player->SetUInt16Value(PLAYER_BYTES_3, 0, uint16(gender) | (player->GetDrunkValue() & 0xFFFE));

    // Change display ID
    player->InitDisplayIds();

    char const* gender_full = gender ? "female" : "male";

    PSendSysMessage(LANG_YOU_CHANGE_GENDER, player->GetName(), gender_full);

    if (needReportToTarget(player))
    {
        ChatHandler(player).PSendSysMessage(LANG_YOUR_GENDER_CHANGED, gender_full, GetNameLink().c_str());
    }

    return true;
}


