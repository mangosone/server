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



#include "Unit.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "SpellMgr.h"
#include "QuestDef.h"
#include "Player.h"
#include "Creature.h"
#include "Spell.h"
#include "Group.h"
#include "SpellAuras.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "CreatureAI.h"
#include "TemporarySummon.h"
#include "Formulas.h"
#include "Pet.h"
#include "Util.h"
#include "Totem.h"
#include "BattleGround/BattleGround.h"
#include "InstanceData.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "MapPersistentStateMgr.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "VMapFactory.h"
#include "MovementGenerator.h"
#include "movement/MoveSplineInit.h"
#include "movement/MoveSpline.h"
#include "CreatureLinkingMgr.h"
#include "GameTime.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */
#ifdef ENABLE_ELUNA
#include "ElunaConfig.h"
#endif /* ENABLE_ELUNA */
#ifdef ENABLE_ELUNA
#include "ElunaEventMgr.h"
#endif /* ENABLE_ELUNA */

/**
 * @brief Reduces physical damage by the victim's effective armor.
 *
 * @param pVictim The victim whose armor is used.
 * @param damage The incoming physical damage.
 * @return The reduced damage amount.
 */
uint32 Unit::CalcArmorReducedDamage(Unit* pVictim, const uint32 damage)
{
    uint32 newdamage = 0;
    float armor = (float)pVictim->GetArmor();

    // Ignore enemy armor by SPELL_AURA_MOD_TARGET_RESISTANCE aura
    armor += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, SPELL_SCHOOL_MASK_NORMAL);

    if (armor < 0.0f)
    {
        armor = 0.0f;
    }

    float levelModifier = (float)getLevel();
    if (levelModifier > 59)
    {
        levelModifier = levelModifier + (4.5f * (levelModifier - 59));
    }

    float tmpvalue = 0.1f * armor / (8.5f * levelModifier + 40);
    tmpvalue = tmpvalue / (1.0f + tmpvalue);

    if (tmpvalue < 0.0f)
    {
        tmpvalue = 0.0f;
    }
    if (tmpvalue > 0.75f)
    {
        tmpvalue = 0.75f;
    }

    newdamage = uint32(damage - (damage * tmpvalue));

    return (newdamage > 1) ? newdamage : 1;
}

/**
 * @brief Calculates resistance, absorbs, and split-damage effects for incoming damage.
 *
 * @param pCaster The attacking caster.
 * @param schoolMask The incoming damage school mask.
 * @param damagetype The damage effect type.
 * @param damage The incoming damage amount.
 * @param absorb Output absorbed amount.
 * @param resist Output resisted amount.
 * @param canReflect Unused reflection flag placeholder.
 */
void Unit::CalculateDamageAbsorbAndResist(Unit* pCaster, SpellSchoolMask schoolMask, DamageEffectType damagetype, const uint32 damage, uint32* absorb, uint32* resist, bool canReflect)
{
    if (!pCaster || !IsAlive() || !damage)
    {
        return;
    }

    // Magic damage, check for resists
    if ((schoolMask & SPELL_SCHOOL_MASK_NORMAL) == 0)
    {
        // Get base victim resistance for school
        float tmpvalue2 = (float)GetResistance(GetFirstSchoolInMask(schoolMask));
        // Ignore resistance by self SPELL_AURA_MOD_TARGET_RESISTANCE aura
        tmpvalue2 += (float)pCaster->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, schoolMask);

        tmpvalue2 *= (float)(0.15f / getLevel());
        if (tmpvalue2 < 0.0f)
        {
            tmpvalue2 = 0.0f;
        }
        if (tmpvalue2 > 0.75f)
        {
            tmpvalue2 = 0.75f;
        }
        uint32 ran = urand(0, 100);
        float faq[4] = {24.0f, 6.0f, 4.0f, 6.0f};
        uint8 m = 0;
        float Binom = 0.0f;
        for (uint8 i = 0; i < 4; ++i)
        {
            Binom += 2400 * (powf(tmpvalue2, float(i)) * powf((1 - tmpvalue2), float(4 - i))) / faq[i];
            if (ran > Binom)
            {
                ++m;
            }
            else
            {
                break;
            }
        }
        if (damagetype == DOT && m == 4)
        {
            *resist += uint32(damage - 1);
        }
        else
        {
            *resist += uint32(damage * m / 4);
        }
        if (*resist > damage)
        {
            *resist = damage;
        }
    }
    else
    {
        *resist = 0;
    }

    int32 RemainingDamage = damage - *resist;

    // Reflect damage spells (not cast any damage spell in aura lookup)
    uint32 reflectSpell = 0;
    int32  reflectDamage = 0;
    Aura*  reflectTriggeredBy = NULL;                       // expected as not expired at reflect as in current cases
    // Death Prevention Aura
    SpellEntry const*  preventDeathSpell = NULL;

    // full absorb cases (by chance)
    /* none cases, but preserve for better backporting conflict resolve
    AuraList const& vAbsorb = GetAurasByType(SPELL_AURA_SCHOOL_ABSORB);
    for(AuraList::const_iterator i = vAbsorb.begin(); i != vAbsorb.end() && RemainingDamage > 0; ++i)
    {
        // only work with proper school mask damage
        Modifier* i_mod = (*i)->GetModifier();
        if (!(i_mod->m_miscvalue & schoolMask))
        {
            continue;
        }

        SpellEntry const* i_spellProto = (*i)->GetSpellProto();
    }
    */

    // Need remove expired auras after
    bool existExpired = false;

    // absorb without mana cost
    AuraList const& vSchoolAbsorb = GetAurasByType(SPELL_AURA_SCHOOL_ABSORB);
    for (AuraList::const_iterator i = vSchoolAbsorb.begin(); i != vSchoolAbsorb.end() && RemainingDamage > 0; ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (!(mod->m_miscvalue & schoolMask))
        {
            continue;
        }

        SpellEntry const* spellProto = (*i)->GetSpellProto();

        // Max Amount can be absorbed by this aura
        int32  currentAbsorb = mod->m_amount;

        // Found empty aura (impossible but..)
        if (currentAbsorb <= 0)
        {
            existExpired = true;
            continue;
        }

        // Handle custom absorb auras
        // TODO: try find better way

        switch (spellProto->SpellClassSet)
        {
            case SPELLFAMILY_GENERIC:
            {
                // Reflective Shield (Lady Malande boss)
                if (spellProto->ID == 41475 && canReflect)
                {
                    if (RemainingDamage < currentAbsorb)
                    {
                        reflectDamage = RemainingDamage / 2;
                    }
                    else
                    {
                        reflectDamage = currentAbsorb / 2;
                    }
                    reflectSpell = 33619;
                    reflectTriggeredBy = *i;
                    reflectTriggeredBy->SetInUse(true);     // lock aura from final deletion until processing
                    break;
                }
                if (spellProto->ID == 39228)                // Argussian Compass
                {
                    // Max absorb stored in 1 dummy effect
                    int32 max_absorb = spellProto->CalculateSimpleValue(EFFECT_INDEX_1);
                    if (max_absorb < currentAbsorb)
                    {
                        currentAbsorb = max_absorb;
                    }
                    break;
                }
                break;
            }
            case SPELLFAMILY_ROGUE:
            {
                // Cheat Death
                if (spellProto->SpellIconID == 2109)
                {
                    if (!preventDeathSpell &&
                            GetTypeId() == TYPEID_PLAYER && // Only players
                            !((Player*)this)->HasSpellCooldown(31231) &&
                            // Only if no cooldown
                            roll_chance_i((*i)->GetModifier()->m_amount))
                        // Only if roll
                    {
                        preventDeathSpell = (*i)->GetSpellProto();
                    }
                    // always skip this spell in charge dropping, absorb amount calculation since it has chance as m_amount and doesn't need to absorb any damage
                    continue;
                }
                break;
            }
            case SPELLFAMILY_PRIEST:
            {
                // Reflective Shield
                if (spellProto->IsFitToFamilyMask(UI64LIT(0x0000000000000001)) && canReflect)
                {
                    if (pCaster == this)
                    {
                        break;
                    }
                    Unit* caster = (*i)->GetCaster();
                    if (!caster)
                    {
                        break;
                    }
                    AuraList const& vOverRideCS = caster->GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
                    for (AuraList::const_iterator k = vOverRideCS.begin(); k != vOverRideCS.end(); ++k)
                    {
                        switch ((*k)->GetModifier()->m_miscvalue)
                        {
                            case 5065:                      // Rank 1
                            case 5064:                      // Rank 2
                            case 5063:                      // Rank 3
                            case 5062:                      // Rank 4
                            case 5061:                      // Rank 5
                            {
                                if (RemainingDamage >= currentAbsorb)
                                {
                                    reflectDamage = (*k)->GetModifier()->m_amount * currentAbsorb / 100;
                                }
                                else
                                {
                                    reflectDamage = (*k)->GetModifier()->m_amount * RemainingDamage / 100;
                                }
                                reflectSpell = 33619;
                                reflectTriggeredBy = *i;
                                reflectTriggeredBy->SetInUse(true);// lock aura from final deletion until processing
                            } break;
                            default: break;
                        }
                    }
                    break;
                }
                break;
            }
            default:
                break;
        }

        // currentAbsorb - damage can be absorbed by shield
        // If need absorb less damage
        if (RemainingDamage < currentAbsorb)
        {
            currentAbsorb = RemainingDamage;
        }

        RemainingDamage -= currentAbsorb;

        // Reduce shield amount
        mod->m_amount -= currentAbsorb;
        if ((*i)->GetHolder()->DropAuraCharge())
        {
            mod->m_amount = 0;
        }
        // Need remove it later
        if (mod->m_amount <= 0)
        {
            existExpired = true;
        }
    }

    // Remove all expired absorb auras
    if (existExpired)
    {
        for (AuraList::const_iterator i = vSchoolAbsorb.begin(); i != vSchoolAbsorb.end();)
        {
            if ((*i)->GetModifier()->m_amount <= 0)
            {
                RemoveAurasDueToSpell((*i)->GetId(), NULL, AURA_REMOVE_BY_SHIELD_BREAK);
                i = vSchoolAbsorb.begin();
            }
            else
            {
                ++i;
            }
        }
    }

    // Cast back reflect damage spell
    if (canReflect && reflectSpell)
    {
        CastCustomSpell(pCaster, reflectSpell, &reflectDamage, NULL, NULL, true, NULL, reflectTriggeredBy);
        reflectTriggeredBy->SetInUse(false);                // free lock from deletion
    }

    // absorb by mana cost
    AuraList const& vManaShield = GetAurasByType(SPELL_AURA_MANA_SHIELD);
    for (AuraList::const_iterator i = vManaShield.begin(), next; i != vManaShield.end() && RemainingDamage > 0; i = next)
    {
        next = i; ++next;

        // check damage school mask
        if (((*i)->GetModifier()->m_miscvalue & schoolMask) == 0)
        {
            continue;
        }

        int32 currentAbsorb;
        if (RemainingDamage >= (*i)->GetModifier()->m_amount)
        {
            currentAbsorb = (*i)->GetModifier()->m_amount;
        }
        else
        {
            currentAbsorb = RemainingDamage;
        }

        if (float manaMultiplier = (*i)->GetSpellProto()->EffectAmplitude[(*i)->GetEffIndex()])
        {
            if (Player* modOwner = GetSpellModOwner())
            {
                modOwner->ApplySpellMod((*i)->GetId(), SPELLMOD_MULTIPLE_VALUE, manaMultiplier);
            }

            int32 maxAbsorb = int32(GetPower(POWER_MANA) / manaMultiplier);
            if (currentAbsorb > maxAbsorb)
            {
                currentAbsorb = maxAbsorb;
            }

            int32 manaReduction = int32(currentAbsorb * manaMultiplier);
            ApplyPowerMod(POWER_MANA, manaReduction, false);
        }

        (*i)->GetModifier()->m_amount -= currentAbsorb;
        if ((*i)->GetModifier()->m_amount <= 0)
        {
            RemoveAurasDueToSpell((*i)->GetId());
            next = vManaShield.begin();
        }

        RemainingDamage -= currentAbsorb;
    }

    // only split damage if not damaging yourself
    if (pCaster != this)
    {
        AuraList const& vSplitDamageFlat = GetAurasByType(SPELL_AURA_SPLIT_DAMAGE_FLAT);
        for (AuraList::const_iterator i = vSplitDamageFlat.begin(), next; i != vSplitDamageFlat.end() && RemainingDamage >= 0; i = next)
        {
            next = i; ++next;

            // check damage school mask
            if (((*i)->GetModifier()->m_miscvalue & schoolMask) == 0)
            {
                continue;
            }

            // Damage can be splitted only if aura has an alive caster
            Unit* caster = (*i)->GetCaster();
            if (!caster || caster == this || !caster->IsInWorld() || !caster->IsAlive())
            {
                continue;
            }

            int32 currentAbsorb;
            if (RemainingDamage >= (*i)->GetModifier()->m_amount)
            {
                currentAbsorb = (*i)->GetModifier()->m_amount;
            }
            else
            {
                currentAbsorb = RemainingDamage;
            }

            RemainingDamage -= currentAbsorb;


            uint32 splitted = currentAbsorb;
            uint32 splitted_absorb = 0;
            pCaster->DealDamageMods(caster, splitted, &splitted_absorb);

            pCaster->SendSpellNonMeleeDamageLog(caster, (*i)->GetSpellProto()->ID, splitted, schoolMask, splitted_absorb, 0, false, 0, false);

            CleanDamage cleanDamage = CleanDamage(splitted, BASE_ATTACK, MELEE_HIT_NORMAL);
            pCaster->DealDamage(caster, splitted, &cleanDamage, DIRECT_DAMAGE, schoolMask, (*i)->GetSpellProto(), false);
        }

        AuraList const& vSplitDamagePct = GetAurasByType(SPELL_AURA_SPLIT_DAMAGE_PCT);
        for (AuraList::const_iterator i = vSplitDamagePct.begin(), next; i != vSplitDamagePct.end() && RemainingDamage >= 0; i = next)
        {
            next = i; ++next;

            // check damage school mask
            if (((*i)->GetModifier()->m_miscvalue & schoolMask) == 0)
            {
                continue;
            }

            // Damage can be splitted only if aura has an alive caster
            Unit* caster = (*i)->GetCaster();
            if (!caster || caster == this || !caster->IsInWorld() || !caster->IsAlive())
            {
                continue;
            }

            uint32 splitted = uint32(RemainingDamage * (*i)->GetModifier()->m_amount / 100.0f);

            RemainingDamage -=  int32(splitted);

            uint32 split_absorb = 0;
            pCaster->DealDamageMods(caster, splitted, &split_absorb);

            pCaster->SendSpellNonMeleeDamageLog(caster, (*i)->GetSpellProto()->ID, splitted, schoolMask, split_absorb, 0, false, 0, false);

            CleanDamage cleanDamage = CleanDamage(splitted, BASE_ATTACK, MELEE_HIT_NORMAL);
            pCaster->DealDamage(caster, splitted, &cleanDamage, DIRECT_DAMAGE, schoolMask, (*i)->GetSpellProto(), false);

            // Break 'fear' and such - in MangosTwo but not MangosZero, was not originally in MangosOne. Leaving commented.
            // pCaster->ProcDamageAndSpellFor(true, this, PROC_FLAG_TAKEN_NEGATIVE_SPELL_HIT, PROC_EX_NORMAL_HIT, BASE_ATTACK, (*i)->GetSpellProto(), splitted);
        }
    }

    // Apply death prevention spells effects
    if (preventDeathSpell && RemainingDamage >= (int32)GetHealth())
    {
        switch (preventDeathSpell->SpellClassSet)
        {
                // Cheat Death
            case SPELLFAMILY_ROGUE:
            {
                // Cheat Death
                if (preventDeathSpell->SpellIconID == 2109)
                {
                    CastSpell(this, 31231, true);
                    ((Player*)this)->AddSpellCooldown(31231, 0, time(NULL) + 60);
                    // with health > 10% lost health until health==10%, in other case no losses
                    uint32 health10 = GetMaxHealth() / 10;
                    RemainingDamage = GetHealth() > health10 ? GetHealth() - health10 : 0;
                }
                break;
            }
        }
    }

    *absorb = damage - RemainingDamage - *resist;
}

/**
 * @brief Calculates block, absorb, and resist results for spell-based damage.
 *
 * @param pCaster The attacking caster.
 * @param damageInfo The mutable spell damage information.
 * @param spellProto The spell entry causing damage.
 * @param attType The associated attack type.
 */
void Unit::CalculateAbsorbResistBlock(Unit* pCaster, SpellNonMeleeDamage* damageInfo, SpellEntry const* spellProto, WeaponAttackType attType)
{
    bool blocked = false;
    // Get blocked status
    switch (spellProto->DefenseType)
    {
            // Melee and Ranged Spells
        case SPELL_DAMAGE_CLASS_RANGED:
        case SPELL_DAMAGE_CLASS_MELEE:
            blocked = IsSpellBlocked(pCaster, spellProto, attType);
            break;
        default:
            break;
    }

    if (blocked)
    {
        damageInfo->blocked = GetShieldBlockValue();
        if (damageInfo->damage < damageInfo->blocked)
        {
            damageInfo->blocked = damageInfo->damage;
        }
        damageInfo->damage -= damageInfo->blocked;
    }

    CalculateDamageAbsorbAndResist(pCaster, GetSpellSchoolMask(spellProto), SPELL_DIRECT_DAMAGE, damageInfo->damage, &damageInfo->absorb, &damageInfo->resist, !spellProto->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS));
    damageInfo->damage -= damageInfo->absorb + damageInfo->resist;
}
