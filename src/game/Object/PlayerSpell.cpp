/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
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

/**
 * @file PlayerSpell.cpp
 * @brief Cohesion split of Player.cpp.
 *        Re-applied onto MangosOne TBC 2.4.3; same class, pure code move,
 *        no behaviour change. CMake file(GLOB) picks this TU up automatically.
 */

#include "Player.h"
#include "Language.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Opcodes.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateMask.h"
#include "CinematicFlyover.h"
#include "SkillDiscovery.h"
#include "QuestDef.h"
#include "GossipDef.h"
#include "UpdateData.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "MapManager.h"
#include "MapPersistentStateMgr.h"
#include "InstanceData.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Formulas.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Pet.h"
#include "Util.h"
#include "Transports.h"
#include "Weather.h"
#include "BattleGround/BattleGround.h"
#include "BattleGround/BattleGroundMgr.h"
#include "BattleGround/BattleGroundAV.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "ArenaTeam.h"
#include "Chat.h"
#include "revision_data.h"
#include "Database/DatabaseImpl.h"
#include "Spell.h"
#include "ScriptMgr.h"
#include "SocialMgr.h"
#include "Mail.h"
#include "DBCStores.h"
#include "SQLStorages.h"
#include "DisableMgr.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

#ifdef ENABLE_PLAYERBOTS
#include "playerbot.h"
#endif

#include <cmath>

#define ZONE_UPDATE_INTERVAL (1*IN_MILLISECONDS)

#define PLAYER_SKILL_INDEX(x)       (PLAYER_SKILL_INFO_1_1 + ((x)*3))
#define PLAYER_SKILL_VALUE_INDEX(x) (PLAYER_SKILL_INDEX(x)+1)
#define PLAYER_SKILL_BONUS_INDEX(x) (PLAYER_SKILL_INDEX(x)+2)

#define SKILL_VALUE(x)         PAIR32_LOPART(x)
#define SKILL_MAX(x)           PAIR32_HIPART(x)
#define MAKE_SKILL_VALUE(v, m) MAKE_PAIR32(v,m)

#define SKILL_TEMP_BONUS(x)    int16(PAIR32_LOPART(x))
#define SKILL_PERM_BONUS(x)    int16(PAIR32_HIPART(x))
#define MAKE_SKILL_BONUS(t, p) MAKE_PAIR32(t,p)

/**
 * @brief Adds or updates a spell entry in the player's spellbook.
 *
 * @param spell_id The spell identifier to add.
 * @param active True if the spell should be active in the spellbook.
 * @param learning True if the spell is being learned now rather than loaded.
 * @param dependent True if the spell is learned as a dependency.
 * @param disabled True if the spell should remain disabled.
 * @return True if the spell should be reported to the client as newly learned.
 */
bool Player::addSpell(uint32 spell_id, bool active, bool learning, bool dependent, bool disabled)
{
    SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell_id);
    if (!spellInfo)
    {
        // do character spell book cleanup (all characters)
        if (!IsInWorld() && !learning)                      // spell load case
        {
            sLog.outError("Player::addSpell: nonexistent in SpellStore spell #%u request, deleting for all characters in `character_spell`.", spell_id);
            CharacterDatabase.PExecute("DELETE FROM `character_spell` WHERE `spell` = '%u'", spell_id);
        }
        else
        {
            sLog.outError("Player::addSpell: nonexistent in SpellStore spell #%u request.", spell_id);
        }

        return false;
    }

    if (!SpellMgr::IsSpellValid(spellInfo, this, false))
    {
        // do character spell book cleanup (all characters)
        if (!IsInWorld() && !learning)                      // spell load case
        {
            sLog.outError("Player::addSpell: Broken spell #%u learning not allowed, deleting for all characters in `character_spell`.", spell_id);
            CharacterDatabase.PExecute("DELETE FROM `character_spell` WHERE `spell` = '%u'", spell_id);
        }
        else
        {
            sLog.outError("Player::addSpell: Broken spell #%u learning not allowed.", spell_id);
        }

        return false;
    }

    PlayerSpellState state = learning ? PLAYERSPELL_NEW : PLAYERSPELL_UNCHANGED;

    bool disabled_case = false;

    PlayerSpellMap::iterator itr = m_spells.find(spell_id);
    if (itr != m_spells.end())
    {
        uint32 next_active_spell_id = 0;
        bool dependent_set = false;

        // fix activate state for non-stackable low rank (and find next spell for !active case)
        if (sSpellMgr.IsRankedSpellNonStackableInSpellBook(spellInfo))
        {
            SpellChainMapNext const& nextMap = sSpellMgr.GetSpellChainNext();
            for (SpellChainMapNext::const_iterator next_itr = nextMap.lower_bound(spell_id); next_itr != nextMap.upper_bound(spell_id); ++next_itr)
            {
                if (HasSpell(next_itr->second))
                {
                    // high rank already known so this must !active
                    active = false;
                    next_active_spell_id = next_itr->second;
                    break;
                }
            }
        }

        PlayerSpell& playerSpell = itr->second;

        // not do anything if already known in expected state
        if (playerSpell.state != PLAYERSPELL_REMOVED && playerSpell.active == active &&
                playerSpell.dependent == dependent && playerSpell.disabled == disabled)
        {
            if (!IsInWorld() && !learning)                  // explicitly load from DB and then exist in it already and set correctly
            {
                playerSpell.state = PLAYERSPELL_UNCHANGED;
            }

            return false;
        }

        // dependent spell known as not dependent, overwrite state
        if (playerSpell.state != PLAYERSPELL_REMOVED && !playerSpell.dependent && dependent)
        {
            playerSpell.dependent = dependent;
            if (playerSpell.state != PLAYERSPELL_NEW)
            {
                playerSpell.state = PLAYERSPELL_CHANGED;
            }
            dependent_set = true;
        }

        // update active state for known spell
        if (playerSpell.active != active && playerSpell.state != PLAYERSPELL_REMOVED && !playerSpell.disabled)
        {
            playerSpell.active = active;

            if (!IsInWorld() && !learning && !dependent_set)// explicitly load from DB and then exist in it already and set correctly
            {
                playerSpell.state = PLAYERSPELL_UNCHANGED;
            }
            else if (playerSpell.state != PLAYERSPELL_NEW)
            {
                playerSpell.state = PLAYERSPELL_CHANGED;
            }

            if (active)
            {
                if (IsNeedCastPassiveLikeSpellAtLearn(spellInfo))
                {
                    CastSpell(this, spell_id, true);
                }
            }
            else if (IsInWorld())
            {
                if (!next_active_spell_id)
                {
                    WorldPacket data(SMSG_REMOVED_SPELL, 4);
                    data << uint16(spell_id);
                    GetSession()->SendPacket(&data);
                }
            }

            return active;                                  // learn (show in spell book if active now)
        }

        if (playerSpell.disabled != disabled && playerSpell.state != PLAYERSPELL_REMOVED)
        {
            if (playerSpell.state != PLAYERSPELL_NEW)
            {
                playerSpell.state = PLAYERSPELL_CHANGED;
            }
            playerSpell.disabled = disabled;

            if (disabled)
            {
                return false;
            }

            disabled_case = true;
        }
        else switch (playerSpell.state)
            {
                case PLAYERSPELL_UNCHANGED:                 // known saved spell
                    return false;
                case PLAYERSPELL_REMOVED:                   // re-learning removed not saved spell
                {
                    m_spells.erase(itr);
                    state = PLAYERSPELL_CHANGED;
                    break;                                  // need re-add
                }
                default:                                    // known not saved yet spell (new or modified)
                {
                    // can be in case spell loading but learned at some previous spell loading
                    if (!IsInWorld() && !learning && !dependent_set)
                    {
                        playerSpell.state = PLAYERSPELL_UNCHANGED;
                    }

                    return false;
                }
            }
    }

    TalentSpellPos const* talentPos = GetTalentSpellPos(spell_id);
    bool canAddToSpellBook = true;

    if (!disabled_case) // skip new spell adding if spell already known (disabled spells case)
    {
        // talent: unlearn all other talent ranks (high and low)
        if (talentPos)
        {
            if (TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentPos->talent_id))
            {
                for (int i = 0; i < MAX_TALENT_RANK; ++i)
                {
                    // skip learning spell and no rank spell case
                    uint32 rankSpellId = talentInfo->RankID[i];
                    if (!rankSpellId || rankSpellId == spell_id)
                    {
                        continue;
                    }

                    removeSpell(rankSpellId, false, false);
                }
            }
        }
        // non talent spell: learn low ranks (recursive call)
        else if (uint32 prev_spell = sSpellMgr.GetPrevSpellInChain(spell_id))
        {
            if (!IsInWorld() || disabled)                   // at spells loading, no output, but allow save
            {
                addSpell(prev_spell, active, true, true, disabled);
            }
            else                                            // at normal learning
            {
                learnSpell(prev_spell, true);
            }
        }

        PlayerSpell newspell;
        newspell.state     = state;
        newspell.active    = active;
        newspell.dependent = dependent;
        newspell.disabled  = disabled;

        // replace spells in action bars and spellbook to bigger rank if only one spell rank must be accessible
        if (newspell.active && !newspell.disabled)
        {
            do
            {
                uint32 prev_spell_id = sSpellMgr.GetPrevSpellInChain(spell_id);  // get the previous spell in chain (if any)
                if (!prev_spell_id)  //spell_id does not have ranks or is the first spell in chain; must add in spellbook
                {
                    continue;
                }

                if ((m_spells.find(prev_spell_id) == m_spells.end()))
                {
                    continue;
                }

                PlayerSpell* lowerRank = &m_spells[prev_spell_id];
                if (lowerRank->state == PLAYERSPELL_REMOVED || !lowerRank->active)
                {
                    continue;
                }

                SpellEntry const *spell_old = sSpellStore.LookupEntry(prev_spell_id);
                SpellEntry const *spell_new = spellInfo;

                if (sSpellMgr.IsRankedSpellNonStackableInSpellBook(spell_old))
                {
                    if (IsInWorld())                // not send spell (re-/over-)learn packets at loading
                    {
                        WorldPacket data(SMSG_SUPERCEDED_SPELL, (4));
                        data << uint16(spell_old->Id);
                        data << uint16(spell_new->Id);
                        GetSession()->SendPacket(&data);
                    }

                    // mark lower rank disabled (SMSG_SUPERCEDED_SPELL replaced it in client by new)
                    lowerRank->active = false;
                    if (lowerRank->state != PLAYERSPELL_NEW)
                    {
                        lowerRank->state = PLAYERSPELL_CHANGED;
                    }

                    canAddToSpellBook = false;
                }
            } while(0);
        }

        m_spells[spell_id] = newspell;

        // return false if spell disabled or spell is non-stackable with lower-ranks
        if (newspell.disabled)
        {
            return false;
        }
    }

    if (talentPos)
    {
        // update used talent points count
        m_usedTalentCount += GetTalentSpellCost(talentPos);
        UpdateFreeTalentPoints(false);
    }

    // update free primary prof.points (if any, can be none in case GM .learn prof. learning)
    if (uint32 freeProfs = GetFreePrimaryProfessionPoints())
    {
        if (sSpellMgr.IsPrimaryProfessionFirstRankSpell(spell_id))
        {
            SetFreePrimaryProfessions(freeProfs - 1);
        }
    }

    // cast talents with SPELL_EFFECT_LEARN_SPELL (other dependent spells will learned later as not auto-learned)
    // note: all spells with SPELL_EFFECT_LEARN_SPELL isn't passive
    if (talentPos && IsSpellHaveEffect(spellInfo, SPELL_EFFECT_LEARN_SPELL))
    {
        // ignore stance requirement for talent learn spell (stance set for spell only for client spell description show)
        CastSpell(this, spell_id, true);
    }
    // also cast passive (and passive like) spells (including all talents without SPELL_EFFECT_LEARN_SPELL) with additional checks
    else if (IsNeedCastPassiveLikeSpellAtLearn(spellInfo))
    {
        CastSpell(this, spell_id, true);
    }
    else if (IsSpellHaveEffect(spellInfo, SPELL_EFFECT_SKILL_STEP))
    {
        CastSpell(this, spell_id, true);
        return false;
    }

    // add dependent skills
    uint16 maxskill = GetMaxSkillValueForLevel();

    SpellLearnSkillNode const* spellLearnSkill = sSpellMgr.GetSpellLearnSkill(spell_id);

    if (spellLearnSkill)
    {
        uint32 skill_value = GetPureSkillValue(spellLearnSkill->skill);
        uint32 skill_max_value = GetPureMaxSkillValue(spellLearnSkill->skill);

        if (skill_value < spellLearnSkill->value)
        {
            skill_value = spellLearnSkill->value;
        }

        uint32 new_skill_max_value = spellLearnSkill->maxvalue == 0 ? maxskill : spellLearnSkill->maxvalue;

        if (skill_max_value < new_skill_max_value)
        {
            skill_max_value =  new_skill_max_value;
        }

        SetSkill(spellLearnSkill->skill, skill_value, skill_max_value, spellLearnSkill->step);
    }
    else
    {
        // not ranked skills
        SkillLineAbilityMapBounds skill_bounds = sSpellMgr.GetSkillLineAbilityMapBounds(spell_id);

        for (SkillLineAbilityMap::const_iterator _spell_idx = skill_bounds.first; _spell_idx != skill_bounds.second; ++_spell_idx)
        {
            SkillLineAbilityEntry const* skillAbility = _spell_idx->second;
            SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skillAbility->skillId);
            if (!pSkill)
            {
                continue;
            }

            if (HasSkill(pSkill->id))
            {
                continue;
            }

            if (skillAbility->learnOnGetSkill == ABILITY_LEARNED_ON_GET_RACE_OR_CLASS_SKILL ||
                // poison special case, not have ABILITY_LEARNED_ON_GET_RACE_OR_CLASS_SKILL
                    (pSkill->id == SKILL_POISONS && skillAbility->max_value == 0) ||
                // lockpicking special case, not have ABILITY_LEARNED_ON_GET_RACE_OR_CLASS_SKILL
                    (pSkill->id == SKILL_LOCKPICKING && skillAbility->max_value == 0))
            {
                switch (GetSkillRangeType(pSkill, skillAbility->racemask != 0))
                {
                    case SKILL_RANGE_LANGUAGE:
                        SetSkill(pSkill->id, 300, 300);
                        break;
                    case SKILL_RANGE_LEVEL:
                        SetSkill(pSkill->id, 1, GetMaxSkillValueForLevel());
                        break;
                    case SKILL_RANGE_MONO:
                        SetSkill(pSkill->id, 1, 1);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    // learn dependent spells
    SpellLearnSpellMapBounds spell_bounds = sSpellMgr.GetSpellLearnSpellMapBounds(spell_id);

    for (SpellLearnSpellMap::const_iterator itr2 = spell_bounds.first; itr2 != spell_bounds.second; ++itr2)
    {
        SpellLearnSpellNode const& spellLearn = itr2->second;
        if (!spellLearn.autoLearned)
        {
            if (!IsInWorld() || !spellLearn.active)       // at spells loading, no output, but allow save
            {
                addSpell(spellLearn.spell, spellLearn.active, true, true, false);
            }
            else                                            // at normal learning
            {
                learnSpell(spellLearn.spell, true);
            }
        }
    }

    // return true (for send learn packet) only if spell active (in case ranked spells) and not replace old spell
    return active && !disabled && canAddToSpellBook;
}

/**
 * @brief Determines whether a learned spell must be cast immediately like a passive.
 *
 * @param spellInfo The spell being evaluated.
 * @return True if the spell should be cast on learn; otherwise, false.
 */
bool Player::IsNeedCastPassiveLikeSpellAtLearn(SpellEntry const* spellInfo) const
{
    ShapeshiftForm form = GetShapeshiftForm();

    if (IsNeedCastSpellAtFormApply(spellInfo, form))        // SPELL_ATTR_PASSIVE | SPELL_ATTR_HIDE_SPELL spells
    {
        return true;                                         // all stance req. cases, not have auarastate cases
    }

    if (!spellInfo->HasAttribute(SPELL_ATTR_PASSIVE))
    {
        return false;
    }

    // note: form passives activated with shapeshift spells be implemented by HandleShapeshiftBoosts instead of spell_learn_spell
    // talent dependent passives activated at form apply have proper stance data
    bool need_cast = !spellInfo->Stances || (!form && spellInfo->HasAttribute(SPELL_ATTR_EX2_NOT_NEED_SHAPESHIFT));

    // Check CasterAuraStates
    return need_cast && (!spellInfo->CasterAuraState || HasAuraState(AuraState(spellInfo->CasterAuraState)));
}

/**
 * @brief Learns a spell and notifies the client when appropriate.
 *
 * @param spell_id The spell identifier to learn.
 * @param dependent True if the spell is being learned as a dependency.
 */
void Player::learnSpell(uint32 spell_id, bool dependent)
{
    PlayerSpellMap::iterator itr = m_spells.find(spell_id);

    bool disabled = (itr != m_spells.end()) ? itr->second.disabled : false;
    bool active = disabled ? itr->second.active : true;

    bool learning = addSpell(spell_id, active, true, dependent, false);

    // prevent duplicated entires in spell book, also not send if not in world (loading)
    if (learning && IsInWorld())
    {
        WorldPacket data(SMSG_LEARNED_SPELL, 4);
        data << uint32(spell_id);
        GetSession()->SendPacket(&data);
    }

    // learn all disabled higher ranks (recursive)
    if (disabled)
    {
        SpellChainMapNext const& nextMap = sSpellMgr.GetSpellChainNext();
        for (SpellChainMapNext::const_iterator i = nextMap.lower_bound(spell_id); i != nextMap.upper_bound(spell_id); ++i)
        {
            PlayerSpellMap::iterator iter = m_spells.find(i->second);
            if (iter != m_spells.end() && iter->second.disabled)
            {
                learnSpell(i->second, false);
            }
        }
    }
}

/**
 * @brief Removes or disables a spell and updates dependent spell state.
 *
 * @param spell_id The spell identifier to remove.
 * @param disabled True to disable the spell instead of fully removing it.
 * @param learn_low_rank True to reactivate lower ranks when appropriate.
 */
void Player::removeSpell(uint32 spell_id, bool disabled, bool learn_low_rank, bool sendUpdate)
{
    PlayerSpellMap::iterator itr = m_spells.find(spell_id);
    if (itr == m_spells.end())
    {
        return;
    }

    PlayerSpell& playerSpell = itr->second;
    if (playerSpell.state == PLAYERSPELL_REMOVED || (disabled && playerSpell.disabled))
    {
        return;
    }

    // unlearn non talent higher ranks (recursive)
    SpellChainMapNext const& nextMap = sSpellMgr.GetSpellChainNext();
    for (SpellChainMapNext::const_iterator itr2 = nextMap.lower_bound(spell_id); itr2 != nextMap.upper_bound(spell_id); ++itr2)
    {
        if (HasSpell(itr2->second) && !GetTalentSpellPos(itr2->second))
        {
            removeSpell(itr2->second, disabled, false);
        }
    }

    // re-search, it can be corrupted in prev loop
    itr = m_spells.find(spell_id);
    if (itr == m_spells.end() || playerSpell.state == PLAYERSPELL_REMOVED)
    {
        return; // already unleared
    }

    bool cur_active = playerSpell.active;
    bool cur_dependent = playerSpell.dependent;

    if (disabled)
    {
        playerSpell.disabled = disabled;
        if (playerSpell.state != PLAYERSPELL_NEW)
        {
            playerSpell.state = PLAYERSPELL_CHANGED;
        }
    }
    else
    {
        if (playerSpell.state == PLAYERSPELL_NEW)
        {
            m_spells.erase(itr);
        }
        else
        {
            playerSpell.state = PLAYERSPELL_REMOVED;
        }
    }

    RemoveAurasDueToSpell(spell_id);

    // remove pet auras
    if (PetAura const* petSpell = sSpellMgr.GetPetAura(spell_id))
    {
        RemovePetAura(petSpell);
    }

    TalentSpellPos const* talentPos = GetTalentSpellPos(spell_id);
    if (talentPos)
    {
        // free talent points
        uint32 talentCosts = GetTalentSpellCost(talentPos);

        if (talentCosts < m_usedTalentCount)
        {
            m_usedTalentCount -= talentCosts;
        }
        else
        {
            m_usedTalentCount = 0;
        }

        UpdateFreeTalentPoints(false);
    }

    // update free primary prof.points (if not overflow setting, can be in case GM use before .learn prof. learning)
    if (sSpellMgr.IsPrimaryProfessionFirstRankSpell(spell_id))
    {
        uint32 freeProfs = GetFreePrimaryProfessionPoints() + 1;
        uint32 maxProfs = GetSession()->GetSecurity() < AccountTypes(sWorld.getConfig(CONFIG_UINT32_TRADE_SKILL_GMIGNORE_MAX_PRIMARY_COUNT)) ? sWorld.getConfig(CONFIG_UINT32_MAX_PRIMARY_TRADE_SKILL) : 10;
        if (freeProfs <= maxProfs)
        {
            SetFreePrimaryProfessions(freeProfs);
        }
    }

    // remove dependent skill
    SpellLearnSkillNode const* spellLearnSkill = sSpellMgr.GetSpellLearnSkill(spell_id);
    if (spellLearnSkill)
    {
        uint32 prev_spell = sSpellMgr.GetPrevSpellInChain(spell_id);
        if (!prev_spell)                                    // first rank, remove skill
        {
            SetSkill(spellLearnSkill->skill, 0, 0);
        }
        else
        {
            // search prev. skill setting by spell ranks chain
            SpellLearnSkillNode const* prevSkill = sSpellMgr.GetSpellLearnSkill(prev_spell);
            while (!prevSkill && prev_spell)
            {
                prev_spell = sSpellMgr.GetPrevSpellInChain(prev_spell);
                prevSkill = sSpellMgr.GetSpellLearnSkill(sSpellMgr.GetFirstSpellInChain(prev_spell));
            }

            if (!prevSkill)                                 // not found prev skill setting, remove skill
            {
                SetSkill(spellLearnSkill->skill, 0, 0);
            }
            else                                            // set to prev. skill setting values
            {
                uint32 skill_value = GetPureSkillValue(prevSkill->skill);
                uint32 skill_max_value = GetPureMaxSkillValue(prevSkill->skill);

                if (skill_value >  prevSkill->value)
                {
                    skill_value = prevSkill->value;
                }

                uint32 new_skill_max_value = prevSkill->maxvalue == 0 ? GetMaxSkillValueForLevel() : prevSkill->maxvalue;

                if (skill_max_value > new_skill_max_value)
                {
                    skill_max_value =  new_skill_max_value;
                }

                SetSkill(prevSkill->skill, skill_value, skill_max_value, prevSkill->step);
            }
        }
    }
    else
    {
        // not ranked skills
        SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBounds(spell_id);

        for (SkillLineAbilityMap::const_iterator _spell_idx = bounds.first; _spell_idx != bounds.second; ++_spell_idx)
        {
            SkillLineAbilityEntry const* skillAbility = _spell_idx->second;
            SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skillAbility->skillId);
            if (!pSkill)
            {
                continue;
            }

            if ((skillAbility->learnOnGetSkill == ABILITY_LEARNED_ON_GET_RACE_OR_CLASS_SKILL &&
                    pSkill->categoryId != SKILL_CATEGORY_CLASS) ||// not unlearn class skills (spellbook/talent pages)
                    // poisons/lockpicking special case, not have ABILITY_LEARNED_ON_GET_RACE_OR_CLASS_SKILL
                    ((pSkill->id == SKILL_POISONS || pSkill->id == SKILL_LOCKPICKING) && skillAbility->max_value == 0))
            {
                // not reset skills for professions and racial abilities
                if ((pSkill->categoryId == SKILL_CATEGORY_SECONDARY || pSkill->categoryId == SKILL_CATEGORY_PROFESSION) &&
                        (IsProfessionSkill(pSkill->id) || skillAbility->racemask != 0))
                {
                    continue;
                }

                SetSkill(pSkill->id, 0, 0);
            }
        }
    }

    // remove dependent spells
    SpellLearnSpellMapBounds spell_bounds = sSpellMgr.GetSpellLearnSpellMapBounds(spell_id);

    for (SpellLearnSpellMap::const_iterator itr2 = spell_bounds.first; itr2 != spell_bounds.second; ++itr2)
    {
        removeSpell(itr2->second.spell, disabled);
    }

    // activate lesser rank in spellbook/action bar, and cast it if need
    bool prev_activate = false;

    if (uint32 prev_id = sSpellMgr.GetPrevSpellInChain(spell_id))
    {
        SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell_id);

        // if talent then lesser rank also talent and need learn
        if (talentPos)
        {
            if (learn_low_rank)
            {
                learnSpell(prev_id, false);
            }
        }
        // if ranked non-stackable spell: need activate lesser rank and update dependence state
        else if (cur_active && sSpellMgr.IsRankedSpellNonStackableInSpellBook(spellInfo))
        {
            // need manually update dependence state (learn spell ignore like attempts)
            PlayerSpellMap::iterator prev_itr = m_spells.find(prev_id);
            if (prev_itr != m_spells.end())
            {
                PlayerSpell& spell = prev_itr->second;
                if (spell.dependent != cur_dependent)
                {
                    spell.dependent = cur_dependent;
                    if (spell.state != PLAYERSPELL_NEW)
                    {
                        spell.state = PLAYERSPELL_CHANGED;
                    }
                }

                // now re-learn if need re-activate
                if (cur_active && !spell.active && learn_low_rank)
                {
                    if (addSpell(prev_id, true, false, spell.dependent, spell.disabled))
                    {
                        // downgrade spell ranks in spellbook and action bar
                        WorldPacket data(SMSG_SUPERCEDED_SPELL, 4);
                        data << uint16(spell_id);
                        data << uint16(prev_id);
                        GetSession()->SendPacket(&data);
                        prev_activate = true;
                    }
                }
            }
        }
    }

    // for shaman Dual-wield
    if (CanDualWield())
    {
        SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell_id);

        if (IsSpellHaveEffect(spellInfo, SPELL_EFFECT_DUAL_WIELD))
        {
            SetCanDualWield(false);
        }
    }

    // for talents and normal spell unlearn that allow offhand use for some weapons
    if (sWorld.getConfig(CONFIG_BOOL_OFFHAND_CHECK_AT_TALENTS_RESET))
    {
        AutoUnequipOffhandIfNeed();
    }

    // remove from spell book if not replaced by lesser rank
    if (!prev_activate && sendUpdate)
    {
        WorldPacket data(SMSG_REMOVED_SPELL, 4);
        data << uint16(spell_id);
        GetSession()->SendPacket(&data);
    }
}

/**
 * @brief Removes a cooldown entry for a specific spell.
 *
 * @param spell_id The spell identifier whose cooldown should be removed.
 * @param update True to notify the client that the cooldown was cleared.
 */
void Player::RemoveSpellCooldown(uint32 spell_id, bool update /* = false */)
{
    m_spellCooldowns.erase(spell_id);

    if (update)
    {
        SendClearCooldown(spell_id, this);
    }
}

/**
 * @brief Removes cooldowns for all spells in a cooldown category.
 *
 * @param cat The spell category identifier.
 * @param update True to notify the client for cleared cooldowns.
 */
void Player::RemoveSpellCategoryCooldown(uint32 cat, bool update /* = false */)
{
    SpellCategoryStore::const_iterator ct = sSpellCategoryStore.find(cat);
    if (ct == sSpellCategoryStore.end())
    {
        return;
    }

    const SpellCategorySet& ct_set = ct->second;
    for (SpellCooldowns::const_iterator i = m_spellCooldowns.begin(); i != m_spellCooldowns.end();)
    {
        if (ct_set.find(i->first) != ct_set.end())
        {
            RemoveSpellCooldown((i++)->first, update);
        }
        else
        {
            ++i;
        }
    }
}

void Player::RemoveArenaSpellCooldowns()
{
    // remove cooldowns on spells that has < 15 min CD
    SpellCooldowns::iterator itr, next;
    // iterate spell cooldowns
    for (itr = m_spellCooldowns.begin(); itr != m_spellCooldowns.end(); itr = next)
    {
        next = itr;
        ++next;
        SpellEntry const* entry = sSpellStore.LookupEntry(itr->first);
        // check if spellentry is present and if the cooldown is less than 15 mins
        if (entry &&
                entry->RecoveryTime <= 15 * MINUTE * IN_MILLISECONDS &&
                entry->CategoryRecoveryTime <= 15 * MINUTE * IN_MILLISECONDS)
        {
            // remove & notify
            RemoveSpellCooldown(itr->first, true);
        }
    }
}

/**
 * @brief Clears all tracked spell cooldowns for the player.
 */
void Player::RemoveAllSpellCooldown()
{
    if (!m_spellCooldowns.empty())
    {
        for (SpellCooldowns::const_iterator itr = m_spellCooldowns.begin(); itr != m_spellCooldowns.end(); ++itr)
        {
            SendClearCooldown(itr->first, this);
        }

        m_spellCooldowns.clear();
    }
}

/**
 * @brief Loads persisted spell cooldowns from the database query result.
 *
 * @param result The query result containing cooldown rows.
 */
void Player::_LoadSpellCooldowns(QueryResult* result)
{
    // some cooldowns can be already set at aura loading...

    // QueryResult *result = CharacterDatabase.PQuery("SELECT `spell`,`item`,`time` FROM `character_spell_cooldown` WHERE `guid` = '%u'",GetGUIDLow());

    if (result)
    {
        time_t curTime = time(NULL);

        do
        {
            Field* fields = result->Fetch();

            uint32 spell_id = fields[0].GetUInt32();
            uint32 item_id  = fields[1].GetUInt32();
            time_t db_time  = (time_t)fields[2].GetUInt64();

            if (!sSpellStore.LookupEntry(spell_id))
            {
                sLog.outError("Player %u has unknown spell %u in `character_spell_cooldown`, skipping.", GetGUIDLow(), spell_id);
                continue;
            }

            // skip outdated cooldown
            if (db_time <= curTime)
            {
                continue;
            }

            AddSpellCooldown(spell_id, item_id, db_time);

            DEBUG_LOG("Player (GUID: %u) spell %u, item %u cooldown loaded (%u secs).", GetGUIDLow(), spell_id, item_id, uint32(db_time - curTime));
        }
        while (result->NextRow());

        delete result;
    }
}

/**
 * @brief Saves active spell cooldowns to the database.
 */
void Player::_SaveSpellCooldowns()
{
    static SqlStatementID deleteSpellCooldown ;
    static SqlStatementID insertSpellCooldown ;

    SqlStatement stmt = CharacterDatabase.CreateStatement(deleteSpellCooldown, "DELETE FROM `character_spell_cooldown` WHERE `guid` = ?");
    stmt.PExecute(GetGUIDLow());

    time_t curTime = time(NULL);
    time_t infTime = curTime + infinityCooldownDelayCheck;

    // remove outdated and save active
    for (SpellCooldowns::iterator itr = m_spellCooldowns.begin(); itr != m_spellCooldowns.end();)
    {
        if (itr->second.end <= curTime)
        {
            m_spellCooldowns.erase(itr++);
        }
        else if (itr->second.end <= infTime)                // not save locked cooldowns, it will be reset or set at reload
        {
            stmt = CharacterDatabase.CreateStatement(insertSpellCooldown, "INSERT INTO `character_spell_cooldown` (`guid`,`spell`,`item`,`time`) VALUES( ?, ?, ?, ?)");
            stmt.PExecute(GetGUIDLow(), itr->first, itr->second.itemid, uint64(itr->second.end));
            ++itr;
        }
        else
        {
            ++itr;
        }
    }
}

/**
 * @brief Calculates the current cost to reset the player's talents.
 *
 * @return The reset cost in copper.
 */
uint32 Player::resetTalentsCost() const
{
    // The first time reset costs 1 gold
    if (m_resetTalentsCost < 1 * GOLD)
    {
        return 1 * GOLD;
    }
    // then 5 gold
    else if (m_resetTalentsCost < 5 * GOLD)
    {
        return 5 * GOLD;
    }
    // After that it increases in increments of 5 gold
    else if (m_resetTalentsCost < 10 * GOLD)
    {
        return 10 * GOLD;
    }
    else
    {
        time_t months = (sWorld.GetGameTime() - m_resetTalentsTime) / MONTH;
        if (months > 0)
        {
            // This cost will be reduced by a rate of 5 gold per month
            int32 new_cost = int32((m_resetTalentsCost) - 5 * GOLD * months);
            // to a minimum of 10 gold.
            return uint32(new_cost < 10 * GOLD ? 10 * GOLD : new_cost);
        }
        else
        {
            // After that it increases in increments of 5 gold
            int32 new_cost = m_resetTalentsCost + 5 * GOLD;
            // until it hits a cap of 50 gold.
            if (new_cost > 50 * GOLD)
            {
                new_cost = 50 * GOLD;
            }
            return new_cost;
        }
    }
}

/**
 * @brief Resets all learned talents for the player's class.
 *
 * @param no_cost True to skip charging the reset fee.
 * @return True if talents were reset; otherwise, false.
 */
bool Player::resetTalents(bool no_cost)
{
    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnTalentsReset(this, no_cost);
    }
#endif /* ENABLE_ELUNA */

    // not need after this call
    if (HasAtLoginFlag(AT_LOGIN_RESET_TALENTS))
    {
        RemoveAtLoginFlag(AT_LOGIN_RESET_TALENTS, true);
    }

    if (m_usedTalentCount == 0)
    {
        UpdateFreeTalentPoints(false);                      // for fix if need counter
        return false;
    }

    uint32 cost = 0;

    if (!no_cost)
    {
        cost = resetTalentsCost();

        if (GetMoney() < cost)
        {
            SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, 0, 0, 0);
            return false;
        }
    }

    for (unsigned int i = 0; i < sTalentStore.GetNumRows(); ++i)
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

        // unlearn only talents for character class
        // some spell learned by one class as normal spells or know at creation but another class learn it as talent,
        // to prevent unexpected lost normal learned spell skip another class talents
        if ((getClassMask() & talentTabInfo->ClassMask) == 0)
        {
            continue;
        }

        for (int j = 0; j < MAX_TALENT_RANK; ++j)
        {
            if (talentInfo->RankID[j])
            {
                removeSpell(talentInfo->RankID[j], !IsPassiveSpell(talentInfo->RankID[j]), false);
            }
        }
    }

    UpdateFreeTalentPoints(false);

    if (!no_cost)
    {
        ModifyMoney(-(int32)cost);

        m_resetTalentsCost = cost;
        m_resetTalentsTime = time(NULL);
    }

    // FIXME: remove pet before or after unlearn spells? for now after unlearn to allow removing of talent related, pet affecting auras
    RemovePet(PET_SAVE_REAGENTS);
    return true;
}
