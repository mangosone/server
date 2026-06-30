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
 * @file SpellAuraDummy.cpp
 * @brief Cohesion split of SpellAuras.cpp.
 *        Re-applied onto MangosOne TBC 2.4.3; same class, pure code move,
 *        no behaviour change. CMake file(GLOB) picks this TU up automatically.
 */

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "Unit.h"
#include "Spell.h"
#include "DynamicObject.h"
#include "Group.h"
#include "UpdateData.h"
#include "ObjectAccessor.h"
#include "Policies/Singleton.h"
#include "Totem.h"
#include "Creature.h"
#include "BattleGround/BattleGround.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "CreatureAI.h"
#include "ScriptMgr.h"
#include "Util.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "MapManager.h"

#define NULL_AURA_SLOT 0xFF

/**
 * An array with all the different handlers for taking care of
 * the various aura types that are defined in AuraType.
 */

void Aura::HandleAuraDummy(bool apply, bool Real)
{
    // spells required only Real aura add/remove
    if (!Real)
    {
        return;
    }

    Unit* target = GetTarget();

    // AT APPLY
    if (apply)
    {
        switch (GetSpellProto()->SpellFamilyName)
        {
            case SPELLFAMILY_GENERIC:
            {
                switch (GetId())
                {
                    case 1515:                              // Tame beast
                        // FIX_ME: this is 2.0.12 threat effect replaced in 2.1.x by dummy aura, must be checked for correctness
                        if (target->CanHaveThreatList())
                            if (Unit* caster = GetCaster())
                            {
                                target->AddThreat(caster, 10.0f, false, GetSpellSchoolMask(GetSpellProto()), GetSpellProto());
                            }
                        return;
                    case 7057:                              // Haunting Spirits
                        // expected to tick with 30 sec period (tick part see in Aura::PeriodicTick)
                        m_isPeriodic = true;
                        m_modifier.periodictime = 30 * IN_MILLISECONDS;
                        m_periodicTimer = m_modifier.periodictime;
                        return;
                    case 10255:                             // Stoned
                    {
                        if (Unit* caster = GetCaster())
                        {
                            if (caster->GetTypeId() != TYPEID_UNIT)
                            {
                                return;
                            }

                            caster->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                            caster->addUnitState(UNIT_STAT_ROOT);
                        }
                        return;
                    }
                    case 13139:                             // net-o-matic
                        // root to self part of (root_target->charge->root_self sequence
                        if (Unit* caster = GetCaster())
                        {
                            caster->CastSpell(caster, 13138, true, NULL, this);
                        }
                        return;
                    case 28832:                             // Mark of Korth'azz
                    case 28833:                             // Mark of Blaumeux
                    case 28834:                             // Mark of Rivendare
                    case 28835:                             // Mark of Zeliek
                    {
                        int32 damage = 0;

                        switch (GetStackAmount())
                        {
                            case 1:
                                return;
                            case 2: damage =   500; break;
                            case 3: damage =  1500; break;
                            case 4: damage =  4000; break;
                            case 5: damage = 12500; break;
                            default:
                                damage = 14000 + 1000 * GetStackAmount();
                                break;
                        }

                        if (Unit* caster = GetCaster())
                        {
                            caster->CastCustomSpell(target, 28836, &damage, NULL, NULL, true, NULL, this);
                        }
                        return;
                    }
                    case 31606:                             // Stormcrow Amulet
                    {
                        CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(17970);

                        // we must assume db or script set display id to native at ending flight (if not, target is stuck with this model)
                        if (cInfo)
                        {
                            target->SetDisplayId(Creature::ChooseDisplayId(cInfo));
                        }

                        return;
                    }
                    case 32045:                             // Soul Charge
                    case 32051:
                    case 32052:
                    {
                        // max duration is 2 minutes, but expected to be random duration
                        // real time randomness is unclear, using max 30 seconds here
                        // see further down for expire of this aura
                        GetHolder()->SetAuraDuration(urand(1, 30)*IN_MILLISECONDS);
                        return;
                    }
                    case 33326:                             // Stolen Soul Dispel
                    {
                        target->RemoveAurasDueToSpell(32346);
                        return;
                    }
                    case 36587:                             // Vision Guide
                    {
                        target->CastSpell(target, 36573, true, NULL, this);
                        return;
                    }
                    // Gender spells
                    case 38224:                             // Illidari Agent Illusion
                    case 37096:                             // Blood Elf Illusion
                    case 46354:                             // Blood Elf Illusion
                    {
                        uint8 gender = target->getGender();
                        uint32 spellId;
                        switch (GetId())
                        {
                            case 38224: spellId = (gender == GENDER_MALE ? 38225 : 38227); break;
                            case 37096: spellId = (gender == GENDER_MALE ? 37093 : 37095); break;
                            case 46354: spellId = (gender == GENDER_MALE ? 46355 : 46356); break;
                            default: return;
                        }
                        target->CastSpell(target, spellId, true, NULL, this);
                        return;
                    }
                    case 39850:                             // Rocket Blast
                        if (roll_chance_i(20))              // backfire stun
                        {
                            target->CastSpell(target, 51581, true, NULL, this);
                        }
                        return;
                    case 43873:                             // Headless Horseman Laugh
                        target->PlayDistanceSound(11965);
                        return;
                    case 46699:                             // Requires No Ammo
                        if (target->GetTypeId() == TYPEID_PLAYER)
                            // not use ammo and not allow use
                            ((Player*)target)->RemoveAmmo();
                        return;
                    case 48025:                             // Headless Horseman's Mount
                        Spell::SelectMountByAreaAndSkill(target, GetSpellProto(), 51621, 48024, 51617, 48023, 0);
                        return;
                }
                break;
            }
            case SPELLFAMILY_WARRIOR:
            {
                switch (GetId())
                {
                    case 41099:                             // Battle Stance
                    {
                        if (target->GetTypeId() != TYPEID_UNIT)
                        {
                            return;
                        }

                        // Stance Cooldown
                        target->CastSpell(target, 41102, true, NULL, this);

                        // Battle Aura
                        target->CastSpell(target, 41106, true, NULL, this);

                        // equipment
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_0, 32614);
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_1, 0);
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_2, 0);
                        return;
                    }
                    case 41100:                             // Berserker Stance
                    {
                        if (target->GetTypeId() != TYPEID_UNIT)
                        {
                            return;
                        }

                        // Stance Cooldown
                        target->CastSpell(target, 41102, true, NULL, this);

                        // Berserker Aura
                        target->CastSpell(target, 41107, true, NULL, this);

                        // equipment
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_0, 32614);
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_1, 0);
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_2, 0);
                        return;
                    }
                    case 41101:                             // Defensive Stance
                    {
                        if (target->GetTypeId() != TYPEID_UNIT)
                        {
                            return;
                        }

                        // Stance Cooldown
                        target->CastSpell(target, 41102, true, NULL, this);

                        // Defensive Aura
                        target->CastSpell(target, 41105, true, NULL, this);

                        // equipment
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_0, 32604);
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_1, 31467);
                        ((Creature*)target)->SetVirtualItem(VIRTUAL_ITEM_SLOT_2, 0);
                        return;
                    }
                }
                break;
            }
            case SPELLFAMILY_SHAMAN:
            {
                // Earth Shield
                if ((GetSpellProto()->SpellFamilyFlags & UI64LIT(0x40000000000)))
                {
                    // prevent double apply bonuses
                    if (target->GetTypeId() != TYPEID_PLAYER || !((Player*)target)->GetSession()->PlayerLoading())
                    {
                        if (Unit* caster = GetCaster())
                        {
                            m_modifier.m_amount = caster->SpellHealingBonusDone(target, GetSpellProto(), m_modifier.m_amount, SPELL_DIRECT_DAMAGE);
                            m_modifier.m_amount = target->SpellHealingBonusTaken(caster, GetSpellProto(), m_modifier.m_amount, SPELL_DIRECT_DAMAGE);
                        }
                    }
                    return;
                }
                break;
            }
        }
    }
    // AT REMOVE
    else
    {
        if (IsQuestTameSpell(GetId()) && target->IsAlive())
        {
            Unit* caster = GetCaster();
            if (!caster || !caster->IsAlive())
            {
                return;
            }

            uint32 finalSpellId = 0;
            switch (GetId())
            {
                case 19548: finalSpellId = 19597; break;
                case 19674: finalSpellId = 19677; break;
                case 19687: finalSpellId = 19676; break;
                case 19688: finalSpellId = 19678; break;
                case 19689: finalSpellId = 19679; break;
                case 19692: finalSpellId = 19680; break;
                case 19693: finalSpellId = 19684; break;
                case 19694: finalSpellId = 19681; break;
                case 19696: finalSpellId = 19682; break;
                case 19697: finalSpellId = 19683; break;
                case 19699: finalSpellId = 19685; break;
                case 19700: finalSpellId = 19686; break;
                case 30646: finalSpellId = 30647; break;
                case 30653: finalSpellId = 30648; break;
                case 30654: finalSpellId = 30652; break;
                case 30099: finalSpellId = 30100; break;
                case 30102: finalSpellId = 30103; break;
                case 30105: finalSpellId = 30104; break;
            }

            if (finalSpellId)
            {
                caster->CastSpell(target, finalSpellId, true, NULL, this);
            }

            return;
        }

        switch (GetId())
        {
            case 10255:                                     // Stoned
            {
                if (Unit* caster = GetCaster())
                {
                    if (caster->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    // see dummy effect of spell 10254 for removal of flags etc
                    caster->CastSpell(caster, 10254, true);
                }
                return;
            }
            case 12479:                                     // Hex of Jammal'an
                target->CastSpell(target, 12480, true, NULL, this);
                return;
            case 12774:                                     // (DND) Belnistrasz Idol Shutdown Visual
            {
                if (m_removeMode == AURA_REMOVE_BY_DEATH)
                {
                    return;
                }

                // Idom Rool Camera Shake <- wtf, don't drink while making spellnames?
                if (Unit* caster = GetCaster())
                {
                    caster->CastSpell(caster, 12816, true);
                }

                return;
            }
            case 28169:                                     // Mutating Injection
            {
                // Mutagen Explosion
                target->CastSpell(target, 28206, true, NULL, this);
                // Poison Cloud
                target->CastSpell(target, 28240, true, NULL, this);
                return;
            }
            case 32045:                                     // Soul Charge
            {
                if (m_removeMode == AURA_REMOVE_BY_EXPIRE)
                {
                    target->CastSpell(target, 32054, true, NULL, this);
                }

                return;
            }
            case 32051:                                     // Soul Charge
            {
                if (m_removeMode == AURA_REMOVE_BY_EXPIRE)
                {
                    target->CastSpell(target, 32057, true, NULL, this);
                }

                return;
            }
            case 32052:                                     // Soul Charge
            {
                if (m_removeMode == AURA_REMOVE_BY_EXPIRE)
                {
                    target->CastSpell(target, 32053, true, NULL, this);
                }

                return;
            }
            case 32286:                                     // Focus Target Visual
            {
                if (m_removeMode == AURA_REMOVE_BY_EXPIRE)
                {
                    target->CastSpell(target, 32301, true, NULL, this);
                }

                return;
            }
            case 35079:                                     // Misdirection, triggered buff
            {
                if (Unit* pCaster = GetCaster())
                {
                    pCaster->RemoveAurasDueToSpell(34477);
                }
                return;
            }
            case 36730:                                     // Flame Strike
            {
                target->CastSpell(target, 36731, true, NULL, this);
                return;
            }
            case 41099:                                     // Battle Stance
            {
                // Battle Aura
                target->RemoveAurasDueToSpell(41106);
                return;
            }
            case 41100:                                     // Berserker Stance
            {
                // Berserker Aura
                target->RemoveAurasDueToSpell(41107);
                return;
            }
            case 41101:                                     // Defensive Stance
            {
                // Defensive Aura
                target->RemoveAurasDueToSpell(41105);
                return;
            }
            case 42385:                                     // Alcaz Survey Aura
            {
                target->CastSpell(target, 42316, true, NULL, this);
                return;
            }
            case 42454:                                     // Captured Totem
            {
                if (m_removeMode == AURA_REMOVE_BY_DEFAULT)
                {
                    if (target->GetDeathState() != CORPSE)
                    {
                        return;
                    }

                    Unit* pCaster = GetCaster();

                    if (!pCaster)
                    {
                        return;
                    }

                    // Captured Totem Test Credit
                    if (Player* pPlayer = pCaster->GetCharmerOrOwnerPlayerOrPlayerItself())
                    {
                        pPlayer->CastSpell(pPlayer, 42455, true);
                    }
                }

                return;
            }
            case 42517:                                     // Beam to Zelfrax
            {
                // expecting target to be a dummy creature
                Creature* pSummon = target->SummonCreature(23864, 0.0f, 0.0f, 0.0f, target->GetOrientation(), TEMPSPAWN_DEAD_DESPAWN, 0);

                Unit* pCaster = GetCaster();

                if (pSummon && pCaster)
                {
                    pSummon->GetMotionMaster()->MovePoint(0, pCaster->GetPositionX(), pCaster->GetPositionY(), pCaster->GetPositionZ());
                }

                return;
            }
            case 44191:                                     // Flame Strike
            {
                if (target->GetMap()->IsDungeon())
                {
                    uint32 spellId = target->GetMap()->IsRegularDifficulty() ? 44190 : 46163;

                    target->CastSpell(target, spellId, true, NULL, this);
                }
                return;
            }
            case 45934:                                     // Dark Fiend
            {
                // Kill target if dispelled
                if (m_removeMode == AURA_REMOVE_BY_DISPEL)
                {
                    target->DealDamage(target, target->GetHealth(), NULL, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
                }
                return;
            }
            case 46308:                                     // Burning Winds
            {
                // casted only at creatures at spawn
                target->CastSpell(target, 47287, true, NULL, this);
                return;
            }
            case 46637:                                     // Break Ice
            {
                target->CastSpell(target, 46638, true);
                return;
            }
        }
    }

    // AT APPLY & REMOVE
    switch (GetSpellProto()->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (GetId())
            {
                case 6606:                                  // Self Visual - Sleep Until Cancelled (DND)
                {
                    if (apply)
                    {
                        target->SetStandState(UNIT_STAND_STATE_SLEEP);
                        target->addUnitState(UNIT_STAT_ROOT);
                    }
                    else
                    {
                        target->clearUnitState(UNIT_STAT_ROOT);
                        target->SetStandState(UNIT_STAND_STATE_STAND);
                    }

                    return;
                }
                case 24658:                                 // Unstable Power
                {
                    if (apply)
                    {
                        Unit* caster = GetCaster();
                        if (!caster)
                        {
                            return;
                        }

                        caster->CastSpell(target, 24659, true, NULL, NULL, GetCasterGuid());
                    }
                    else
                    {
                        target->RemoveAurasDueToSpell(24659);
                    }
                    return;
                }
                case 24661:                                 // Restless Strength
                {
                    if (apply)
                    {
                        Unit* caster = GetCaster();
                        if (!caster)
                        {
                            return;
                        }

                        caster->CastSpell(target, 24662, true, NULL, NULL, GetCasterGuid());
                    }
                    else
                    {
                        target->RemoveAurasDueToSpell(24662);
                    }
                    return;
                }
                case 29266:                                 // Permanent Feign Death
                case 31261:                                 // Permanent Feign Death (Root)
                case 37493:                                 // Feign Death
                {
                    // Unclear what the difference really is between them.
                    // Some has effect1 that makes the difference, however not all.
                    // Some appear to be used depending on creature location, in water, at solid ground, in air/suspended, etc
                    // For now, just handle all the same way
                    if (target->GetTypeId() == TYPEID_UNIT)
                    {
                        target->SetFeignDeath(apply);
                    }

                    return;
                }
                case 32216:                                 // Victorious
                    if (target->getClass() == CLASS_WARRIOR)
                    {
                        target->ModifyAuraState(AURA_STATE_WARRIOR_VICTORY_RUSH, apply);
                    }
                    return;
                case 35356:                                 // Spawn Feign Death
                case 35357:                                 // Spawn Feign Death
                {
                    if (target->GetTypeId() == TYPEID_UNIT)
                    {
                        // Flags not set like it's done in SetFeignDeath()
                        // UNIT_DYNFLAG_DEAD does not appear with these spells.
                        // All of the spells appear to be present at spawn and not used to feign in combat or similar.
                        if (apply)
                        {
                            target->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_29);
                            target->SetFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH);

                            target->addUnitState(UNIT_STAT_DIED);
                        }
                        else
                        {
                            target->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_29);
                            target->RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH);

                            target->clearUnitState(UNIT_STAT_DIED);
                        }
                    }
                    return;
                }
                case 40133:                                 // Summon Fire Elemental
                {
                    Unit* caster = GetCaster();
                    if (!caster)
                    {
                        return;
                    }

                    Unit* owner = caster->GetOwner();
                    if (owner && owner->GetTypeId() == TYPEID_PLAYER)
                    {
                        if (apply)
                        {
                            owner->CastSpell(owner, 8985, true);
                        }
                        else
                        {
                            ((Player*)owner)->RemovePet(PET_SAVE_REAGENTS);
                        }
                    }
                    return;
                }
                case 40132:                                 // Summon Earth Elemental
                {
                    Unit* caster = GetCaster();
                    if (!caster)
                    {
                        return;
                    }

                    Unit* owner = caster->GetOwner();
                    if (owner && owner->GetTypeId() == TYPEID_PLAYER)
                    {
                        if (apply)
                        {
                            owner->CastSpell(owner, 19704, true);
                        }
                        else
                        {
                            ((Player*)owner)->RemovePet(PET_SAVE_REAGENTS);
                        }
                    }
                    return;
                }
                case 40214:                                 // Dragonmaw Illusion
                {
                    if (apply)
                    {
                        target->CastSpell(target, 40216, true);
                        target->CastSpell(target, 42016, true);
                    }
                    else
                    {
                        target->RemoveAurasDueToSpell(40216);
                        target->RemoveAurasDueToSpell(42016);
                    }
                    return;
                }
                case 42515:                                 // Jarl Beam
                {
                    // aura animate dead (fainted) state for the duration, but we need to animate the death itself (correct way below?)
                    if (Unit* pCaster = GetCaster())
                    {
                        pCaster->ApplyModFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH, apply);
                    }

                    // Beam to Zelfrax at remove
                    if (!apply)
                    {
                        target->CastSpell(target, 42517, true);
                    }
                    return;
                }
                case 42583:                                 // Claw Rage
                {
                    Unit* caster = GetCaster();
                    if (!caster || target->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (apply)
                    {
                        caster->FixateTarget(target);
                    }
                    else if (target->GetObjectGuid() == caster->GetFixateTargetGuid())
                    {
                        caster->FixateTarget(NULL);
                    }

                    return;
                }
                case 27978:
                case 40131:
                    if (apply)
                    {
                        target->m_AuraFlags |= UNIT_AURAFLAG_ALIVE_INVISIBLE;
                    }
                    else
                    {
                        target->m_AuraFlags &= ~UNIT_AURAFLAG_ALIVE_INVISIBLE;
                    }
                    return;
            }
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            // Hypothermia
            if (GetId() == 41425)
            {
                target->ModifyAuraState(AURA_STATE_HYPOTHERMIA, apply);
                return;
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            switch (GetId())
            {
                case 34246:                                 // Idol of the Emerald Queen
                {
                    if (target->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (apply)
                        // dummy not have proper effectclassmask
                        m_spellmod  = new SpellModifier(SPELLMOD_DOT, SPELLMOD_FLAT, m_modifier.m_amount / 7, GetId(), UI64LIT(0x001000000000));

                    ((Player*)target)->AddSpellMod(m_spellmod, apply);
                    return;
                }
            }

            // Lifebloom
            if (GetSpellProto()->SpellFamilyFlags & UI64LIT(0x1000000000))
            {
                if (apply)
                {
                    if (Unit* caster = GetCaster())
                    {
                        // prevent double apply bonuses
                        if (target->GetTypeId() != TYPEID_PLAYER || !((Player*)target)->GetSession()->PlayerLoading())
                        {
                            // Lifebloom ignore stack amount
                            m_modifier.m_amount /= GetStackAmount();
                            m_modifier.m_amount = caster->SpellHealingBonusDone(target, GetSpellProto(), m_modifier.m_amount, SPELL_DIRECT_DAMAGE);
                            m_modifier.m_amount = target->SpellHealingBonusTaken(caster, GetSpellProto(), m_modifier.m_amount, SPELL_DIRECT_DAMAGE);
                        }
                    }
                }
                else
                {
                    // Final heal on duration end
                    if (m_removeMode != AURA_REMOVE_BY_EXPIRE)
                    {
                        return;
                    }

                    // final heal
                    if (target->IsInWorld() && GetStackAmount() > 0)
                    {
                        // Lifebloom dummy store single stack amount always
                        int32 amount = m_modifier.m_amount;
                        target->CastCustomSpell(target, 33778, &amount, NULL, NULL, true, NULL, this, GetCasterGuid());
                    }
                }
                return;
            }

            // Predatory Strikes
            if (target->GetTypeId() == TYPEID_PLAYER && GetSpellProto()->SpellIconID == 1563)
            {
                ((Player*)target)->UpdateAttackPowerAndDamage();
                return;
            }
            break;
        }
        case SPELLFAMILY_ROGUE:
        {
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            switch (GetId())
            {
                    // Improved Aspect of the Viper
                case 38390:
                {
                    if (target->GetTypeId() == TYPEID_PLAYER)
                    {
                        if (apply)
                            // + effect value for Aspect of the Viper
                            m_spellmod = new SpellModifier(SPELLMOD_ATTACK_POWER, SPELLMOD_FLAT, m_modifier.m_amount, GetId(), UI64LIT(0x4000000000000));

                        ((Player*)target)->AddSpellMod(m_spellmod, apply);
                    }
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            switch (GetId())
            {
                case 6495:                                  // Sentry Totem
                {
                    if (target->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    Totem* totem = target->GetTotem(TOTEM_SLOT_AIR);

                    if (totem && apply)
                    {
                        ((Player*)target)->GetCamera().SetView(totem);
                    }
                    else
                    {
                        ((Player*)target)->GetCamera().ResetView();
                    }

                    return;
                }
            }
            // Improved Weapon Totems
            if (GetSpellProto()->SpellIconID == 57 && target->GetTypeId() == TYPEID_PLAYER)
            {
                if (apply)
                {
                    switch (m_effIndex)
                    {
                        case 0:
                            // Windfury Totem
                            m_spellmod = new SpellModifier(SPELLMOD_ATTACK_POWER, SPELLMOD_PCT, m_modifier.m_amount, GetId(), UI64LIT(0x00200000000));
                            break;
                        case 1:
                            // Flametongue Totem
                            m_spellmod = new SpellModifier(SPELLMOD_ATTACK_POWER, SPELLMOD_PCT, m_modifier.m_amount, GetId(), UI64LIT(0x00400000000));
                            break;
                        default: return;
                    }
                }

                ((Player*)target)->AddSpellMod(m_spellmod, apply);
                return;
            }
            break;
        }
    }

    // pet auras
    if (PetAura const* petSpell = sSpellMgr.GetPetAura(GetId()))
    {
        if (apply)
        {
            target->AddPetAura(petSpell);
        }
        else
        {
            target->RemovePetAura(petSpell);
        }
        return;
    }

    if (target->GetTypeId() == TYPEID_PLAYER)
    {
        SpellAreaForAreaMapBounds saBounds = sSpellMgr.GetSpellAreaForAuraMapBounds(GetId());
        if (saBounds.first != saBounds.second)
        {
            uint32 zone, area;
            target->GetZoneAndAreaId(zone, area);

            for (SpellAreaForAreaMap::const_iterator itr = saBounds.first; itr != saBounds.second; ++itr)
            {
                itr->second->ApplyOrRemoveSpellIfCan((Player*)target, zone, area, false);
            }
        }
    }

    // script has to "handle with care", only use where data are not ok to use in the above code.
    if (target->GetTypeId() == TYPEID_UNIT)
    {
        sScriptMgr.OnAuraDummy(this, apply);
    }
}
