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



#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "Log.h"
#include "UpdateMask.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "SkillExtraItems.h"
#include "Unit.h"
#include "Spell.h"
#include "DynamicObject.h"
#include "SpellAuras.h"
#include "Group.h"
#include "UpdateData.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "SharedDefines.h"
#include "Pet.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "Creature.h"
#include "Totem.h"
#include "CreatureAI.h"
#include "BattleGround/BattleGroundMgr.h"
#include "BattleGround/BattleGround.h"
#include "BattleGround/BattleGroundEY.h"
#include "BattleGround/BattleGroundWS.h"
#include "Language.h"
#include "SocialMgr.h"
#include "Util.h"
#include "TemporarySummon.h"
#include "ScriptMgr.h"
#include "SkillDiscovery.h"
#include "Formulas.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "terrain/Geometry/Vector3.h"
#include <random>
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

/**
 * @brief Executes spell-specific dummy effect behavior.
 *
 * @param eff_idx The dummy effect index.
 */
void Spell::EffectDummy(SpellEffectIndex eff_idx)
{
    if (!unitTarget && !gameObjTarget && !itemTarget)
    {
        return;
    }

    // selection by spell family
    switch (m_spellInfo->SpellClassSet)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (m_spellInfo->ID)
            {
                case 3360:                                  // Curse of the Eye
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 spell_id = (unitTarget->getGender() == GENDER_MALE) ? 10651 : 10653;

                    m_caster->CastSpell(unitTarget, spell_id, true);
                    return;
                }
                case 7671:                                  // Transformation (human<->worgen)
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Transform Visual
                    unitTarget->CastSpell(unitTarget, 24085, true);
                    return;
                }
                case 8063:                                  // Deviate Fish
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = 0;
                    switch (urand(1, 5))
                    {
                        case 1: spell_id = 8064; break;     // Sleepy
                        case 2: spell_id = 8065; break;     // Invigorate
                        case 3: spell_id = 8066; break;     // Shrink
                        case 4: spell_id = 8067; break;     // Party Time!
                        case 5: spell_id = 8068; break;     // Healthy Spirit
                    }
                    m_caster->CastSpell(m_caster, spell_id, true, NULL);
                    return;
                }
                case 8213:                                  // Savory Deviate Delight
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = 0;
                    switch (urand(1, 2))
                    {
                            // Flip Out - ninja
                        case 1: spell_id = (m_caster->getGender() == GENDER_MALE ? 8219 : 8220); break;
                            // Yaaarrrr - pirate
                        case 2: spell_id = (m_caster->getGender() == GENDER_MALE ? 8221 : 8222); break;
                    }

                    m_caster->CastSpell(m_caster, spell_id, true, NULL);
                    return;
                }
                case 9976:                                  // Polly Eats the E.C.A.C.
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    // Summon Polly Jr.
                    unitTarget->CastSpell(unitTarget, 9998, true);

                    ((Creature*)unitTarget)->ForcedDespawn(100);
                    return;
                }
                case 10254:                                 // Stone Dwarf Awaken Visual
                {
                    if (m_caster->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    // see spell 10255 (aura dummy)
                    m_caster->clearUnitState(UNIT_STAT_ROOT);
                    m_caster->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                    return;
                }
                case 12975:                                 // Last Stand
                {
                    int32 healthModSpellBasePoints0 = int32(m_caster->GetMaxHealth() * 0.3);
                    m_caster->CastCustomSpell(m_caster, 12976, &healthModSpellBasePoints0, NULL, NULL, true, NULL);
                    return;
                }
                case 13120:                                 // net-o-matic
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 spell_id = 0;

                    uint32 roll = urand(0, 99);

                    if (roll < 2)                           // 2% for 30 sec self root (off-like chance unknown)
                    {
                        spell_id = 16566;
                    }
                    else if (roll < 4)                      // 2% for 20 sec root, charge to target (off-like chance unknown)
                    {
                        spell_id = 13119;
                    }
                    else                                    // normal root
                    {
                        spell_id = 13099;
                    }

                    m_caster->CastSpell(unitTarget, spell_id, true, NULL);
                    return;
                }
                case 13489:
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 14744, true);
                    return;
                }
                case 13567:                                 // Dummy Trigger
                {
                    // can be used for different aura triggering, so select by aura
                    if (!m_triggeredByAuraSpell || !unitTarget)
                    {
                        return;
                    }

                    switch (m_triggeredByAuraSpell->ID)
                    {
                        case 26467:                         // Persistent Shield
                            m_caster->CastCustomSpell(unitTarget, 26470, &damage, NULL, NULL, true);
                            break;
                        default:
                            sLog.outError("EffectDummy: Non-handled case for spell 13567 for triggered aura %u", m_triggeredByAuraSpell->ID);
                            break;
                    }
                    return;
                }
                case 14185:                                 // Preparation Rogue
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // immediately finishes the cooldown on certain Rogue abilities
                    const SpellCooldowns& cm = ((Player*)m_caster)->GetSpellCooldownMap();
                    for (SpellCooldowns::const_iterator itr = cm.begin(); itr != cm.end();)
                    {
                        SpellEntry const* spellInfo = sSpellStore.LookupEntry(itr->first);

                        if (spellInfo->SpellClassSet == SPELLFAMILY_ROGUE && (spellInfo->SpellClassMask & UI64LIT(0x0000026000000860)))
                        {
                            ((Player*)m_caster)->RemoveSpellCooldown((itr++)->first, true);
                        }
                        else
                        {
                            ++itr;
                        }
                    }
                    return;
                }
                case 14537:                                 // Six Demon Bag
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    Unit* newTarget = unitTarget;
                    uint32 spell_id = 0;
                    uint32 roll = urand(0, 99);
                    if (roll < 25)                          // Fireball (25% chance)
                    {
                        spell_id = 15662;
                    }
                    else if (roll < 50)                     // Frostbolt (25% chance)
                    {
                        spell_id = 11538;
                    }
                    else if (roll < 70)                     // Chain Lighting (20% chance)
                    {
                        spell_id = 21179;
                    }
                    else if (roll < 77)                     // Polymorph (10% chance, 7% to target)
                    {
                        spell_id = 14621;
                    }
                    else if (roll < 80)                     // Polymorph (10% chance, 3% to self, backfire)
                    {
                        spell_id = 14621;
                        newTarget = m_caster;
                    }
                    else if (roll < 95)                     // Enveloping Winds (15% chance)
                    {
                        spell_id = 25189;
                    }
                    else                                    // Summon Felhund minion (5% chance)
                    {
                        spell_id = 14642;
                        newTarget = m_caster;
                    }

                    m_caster->CastSpell(newTarget, spell_id, true, m_CastItem);
                    return;
                }
                case 15998:                                 // Capture Worg Pup
                case 29435:                                 // Capture Female Kaliri Hatchling
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    Creature* creatureTarget = (Creature*)unitTarget;

                    creatureTarget->ForcedDespawn();
                    return;
                }
                case 16589:                                 // Noggenfogger Elixir
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = 0;
                    switch (urand(1, 3))
                    {
                        case 1: spell_id = 16595; break;
                        case 2: spell_id = 16593; break;
                        default: spell_id = 16591; break;
                    }

                    m_caster->CastSpell(m_caster, spell_id, true, NULL);
                    return;
                }
                case 17009:                                 // Voodoo
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = 0;
                    switch (urand(0, 6))
                    {
                        case 0: spell_id = 16707; break;    // Hex
                        case 1: spell_id = 16708; break;    // Hex
                        case 2: spell_id = 16709; break;    // Hex
                        case 3: spell_id = 16711; break;    // Grow
                        case 4: spell_id = 16712; break;    // Special Brew
                        case 5: spell_id = 16713; break;    // Ghostly
                        case 6: spell_id = 16716; break;    // Launch
                    }

                    m_caster->CastSpell(unitTarget, spell_id, true, NULL, NULL, m_originalCasterGUID, m_spellInfo);
                    return;
                }
                case 17251:                                 // Spirit Healer Res
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    Unit* caster = GetAffectiveCaster();

                    if (caster && caster->GetTypeId() == TYPEID_PLAYER)
                    {
                        WorldPacket data(SMSG_SPIRIT_HEALER_CONFIRM, 8);
                        data << unitTarget->GetObjectGuid();
                        ((Player*)caster)->GetSession()->SendPacket(&data);
                    }
                    return;
                }
                case 17271:                                 // Test Fetid Skull
                {
                    if (!itemTarget && m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = urand(0, 1)
                                      ? 17269               // Create Resonating Skull
                                      : 17270;              // Create Bone Dust

                    m_caster->CastSpell(m_caster, spell_id, true, NULL);
                    return;
                }
                case 17770:                                 // Wolfshead Helm Energy
                {
                    m_caster->CastSpell(m_caster, 29940, true, NULL);
                    return;
                }
                case 17950:                                 // Shadow Portal
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Shadow Portal
                    const uint32 spell_list[6] = {17863, 17939, 17943, 17944, 17946, 17948};

                    m_caster->CastSpell(unitTarget, spell_list[urand(0, 5)], true);
                    return;
                }
                case 19395:                                 // Gordunni Trap
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, urand(0, 1) ? 19394 : 11756, true);
                    return;
                }
                case 19411:                                 // Lava Bomb
                case 20474:                                 // Lava Bomb
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Hack alert!
                    // This dummy are expected to cast spell 20494 to summon GO entry 177704
                    // Spell does not exist client side, so we have to make a hack, creating the GO (SPELL_EFFECT_SUMMON_OBJECT_WILD)
                    // Spell should appear in both SMSG_SPELL_START/GO and SMSG_SPELLLOGEXECUTE

                    // For later, creating custom spell
                    // _START: packguid: target, cast flags: 0xB, TARGET_FLAG_SELF
                    // _GO: packGuid: target, cast flags: 0x4309, TARGET_FLAG_DEST_LOCATION
                    // LOG: spell: 20494, effect, pguid: goguid

                    GameObject* pGameObj = new GameObject;

                    Map* map = unitTarget->GetMap();

                    if (!pGameObj->Create(map->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT), 177704,
                                          map,
                                          unitTarget->GetPositionX(), unitTarget->GetPositionY(), unitTarget->GetPositionZ(),
                                          unitTarget->GetOrientation()))
                    {
                        delete pGameObj;
                        return;
                    }

                    DEBUG_LOG("Gameobject, create custom in SpellEffects.cpp EffectDummy");

                    // Expect created without owner, but with level from _template
                    pGameObj->SetRespawnTime(MINUTE / 2);
                    pGameObj->SetUInt32Value(GAMEOBJECT_LEVEL, pGameObj->GetGOInfo()->trap.level);
                    pGameObj->SetSpellId(m_spellInfo->ID);

                    map->Add(pGameObj);
                    pGameObj->AIM_Initialize();

                    return;
                }
                case 19869:                                 // Dragon Orb
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER || unitTarget->HasAura(23958))
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 19832, true);
                    return;
                }
                case 20037:                                 // Explode Orb Effect
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 20038, true);
                    return;
                }
                case 20577:                                 // Cannibalize
                {
                    if (unitTarget)
                    {
                        m_caster->CastSpell(m_caster, 20578, false, NULL);
                    }
                    return;
                }
                case 21147:                                 // Arcane Vacuum
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Spell used by Azuregos to teleport all the players to him
                    // This also resets the target threat
                    if (m_caster->GetThreatManager().getThreat(unitTarget))
                    {
                        m_caster->GetThreatManager().modifyThreatPercent(unitTarget, -100);
                    }

                    // cast summon player
                    m_caster->CastSpell(unitTarget, 21150, true);

                    return;
                }
                case 23019:                                 // Crystal Prison Dummy DND
                {
                    if (!unitTarget || !unitTarget->IsAlive() || unitTarget->GetTypeId() != TYPEID_UNIT || ((Creature*)unitTarget)->IsPet())
                    {
                        return;
                    }

                    Creature* creatureTarget = (Creature*)unitTarget;
                    if (creatureTarget->IsPet())
                    {
                        return;
                    }

                    GameObject* pGameObj = new GameObject;

                    Map* map = creatureTarget->GetMap();

                    // create before death for get proper coordinates
                    if (!pGameObj->Create(map->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT), 179644, map,
                                          creatureTarget->GetPositionX(), creatureTarget->GetPositionY(), creatureTarget->GetPositionZ(),
                                          creatureTarget->GetOrientation()))
                    {
                        delete pGameObj;
                        return;
                    }

                    pGameObj->SetRespawnTime(creatureTarget->GetRespawnTime() - time(NULL));
                    pGameObj->SetOwnerGuid(m_caster->GetObjectGuid());
                    pGameObj->SetUInt32Value(GAMEOBJECT_LEVEL, m_caster->getLevel());
                    pGameObj->SetSpellId(m_spellInfo->ID);

                    creatureTarget->ForcedDespawn();

                    DEBUG_LOG("AddObject at SpellEfects.cpp EffectDummy");
                    map->Add(pGameObj);
                    pGameObj->AIM_Initialize();

                    WorldPacket data(SMSG_GAMEOBJECT_SPAWN_ANIM_OBSOLETE, 8);
                    data << ObjectGuid(pGameObj->GetObjectGuid());
                    m_caster->SendMessageToSet(&data, true);

                    return;
                }
                case 23074:                                 // Arcanite Dragonling
                {
                    if (!m_CastItem)
                    {
                        return;
                    }

                    m_caster->CastSpell(m_caster, 19804, true, m_CastItem);
                    return;
                }
                case 23075:                                 // Mithril Mechanical Dragonling
                {
                    if (!m_CastItem)
                    {
                        return;
                    }

                    m_caster->CastSpell(m_caster, 12749, true, m_CastItem);
                    return;
                }
                case 23076:                                 // Mechanical Dragonling
                {
                    if (!m_CastItem)
                    {
                        return;
                    }

                    m_caster->CastSpell(m_caster, 4073, true, m_CastItem);
                    return;
                }
                case 23133:                                 // Gnomish Battle Chicken
                {
                    if (!m_CastItem)
                    {
                        return;
                    }

                    m_caster->CastSpell(m_caster, 13166, true, m_CastItem);
                    return;
                }
                case 23138:                                 // Gate of Shazzrah
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Effect probably include a threat change, but it is unclear if fully
                    // reset or just forced upon target for teleport (SMSG_HIGHEST_THREAT_UPDATE)

                    // Gate of Shazzrah
                    m_caster->CastSpell(unitTarget, 23139, true);
                    return;
                }
                case 23448:                                 // Transporter Arrival - Ultrasafe Transporter: Gadgetzan - backfires
                {
                    int32 r = irand(0, 119);
                    if (r < 20)                             // Transporter Malfunction - 1/6 polymorph
                    {
                        m_caster->CastSpell(m_caster, 23444, true);
                    }
                    else if (r < 100)                       // Evil Twin               - 4/6 evil twin
                    {
                        m_caster->CastSpell(m_caster, 23445, true);
                    }
                    else                                    // Transporter Malfunction - 1/6 miss the target
                    {
                        m_caster->CastSpell(m_caster, 36902, true);
                    }

                    return;
                }
                case 23453:                                 // Gnomish Transporter - Ultrasafe Transporter: Gadgetzan
                {
                    if (roll_chance_i(50))                  // Gadgetzan Transporter         - success
                    {
                        m_caster->CastSpell(m_caster, 23441, true);
                    }
                    else                                    // Gadgetzan Transporter Failure - failure
                    {
                        m_caster->CastSpell(m_caster, 23446, true);
                    }

                    return;
                }
                case 23645:                                 // Hourglass Sand
                    m_caster->RemoveAurasDueToSpell(23170); // Brood Affliction: Bronze
                    return;
                case 23725:                                 // Gift of Life (warrior bwl trinket)
                    m_caster->CastSpell(m_caster, 23782, true);
                    m_caster->CastSpell(m_caster, 23783, true);
                    return;
                case 24930:                                 // Hallow's End Treat
                {
                    uint32 spell_id = 0;

                    switch (urand(1, 4))
                    {
                        case 1: spell_id = 24924; break;    // Larger and Orange
                        case 2: spell_id = 24925; break;    // Skeleton
                        case 3: spell_id = 24926; break;    // Pirate
                        case 4: spell_id = 24927; break;    // Ghost
                    }

                    m_caster->CastSpell(m_caster, spell_id, true);
                    return;
                }
                case 25860:                                 // Reindeer Transformation
                {
                    if (!m_caster->HasAuraType(SPELL_AURA_MOUNTED))
                    {
                        return;
                    }

                    float flyspeed = m_caster->GetSpeedRate(MOVE_FLIGHT);
                    float speed = m_caster->GetSpeedRate(MOVE_RUN);

                    m_caster->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

                    // 5 different spells used depending on mounted speed and if mount can fly or not
                    if (flyspeed >= 4.1f)
                        // Flying Reindeer
                        m_caster->CastSpell(m_caster, 44827, true); // 310% flying Reindeer
                    else if (flyspeed >= 3.8f)
                        // Flying Reindeer
                        m_caster->CastSpell(m_caster, 44825, true); // 280% flying Reindeer
                    else if (flyspeed >= 1.6f)
                        // Flying Reindeer
                        m_caster->CastSpell(m_caster, 44824, true); // 60% flying Reindeer
                    else if (speed >= 2.0f)
                        // Reindeer
                    {
                        m_caster->CastSpell(m_caster, 25859, true);  // 100% ground Reindeer
                    }
                    else
                        // Reindeer
                    {
                        m_caster->CastSpell(m_caster, 25858, true);  // 60% ground Reindeer
                    }

                    return;
                }
                case 26074:                                 // Holiday Cheer
                    // implemented at client side
                    return;
                case 28006:                                 // Arcane Cloaking
                {
                    if (unitTarget && unitTarget->GetTypeId() == TYPEID_PLAYER)
                        // Naxxramas Entry Flag Effect DND
                        m_caster->CastSpell(unitTarget, 29294, true);

                    return;
                }
                case 28730:                                 // Arcane Torrent (Mana)
                {
                    Aura* dummy = m_caster->GetDummyAura(28734);
                    if (dummy)
                    {
                        int32 bp = damage * dummy->GetStackAmount();
                        m_caster->CastCustomSpell(m_caster, 28733, &bp, NULL, NULL, true);
                        m_caster->RemoveAurasDueToSpell(28734);
                    }
                    return;
                }
                case 29200:                                 // Purify Helboar Meat
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = roll_chance_i(50)
                                      ? 29277               // Summon Purified Helboar Meat
                                      : 29278;              // Summon Toxic Helboar Meat

                    m_caster->CastSpell(m_caster, spell_id, true, NULL);
                    return;
                }
                case 29858:                                 // Soulshatter
                {
                    if (unitTarget && unitTarget->GetTypeId() == TYPEID_UNIT && unitTarget->IsHostileTo(m_caster))
                    {
                        m_caster->CastSpell(unitTarget, 32835, true);
                    }

                    return;
                }
                case 29969:                                 // Summon Blizzard
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 29952, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 29979:                                 // Massive Magnetic Pull
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 30010, true);
                    return;
                }
                case 30004:                                 // Flame Wreath
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 29946, true);
                    return;
                }
                case 30458:                                 // Nigh Invulnerability
                {
                    if (!m_CastItem)
                    {
                        return;
                    }

                    if (roll_chance_i(86))                  // Nigh-Invulnerability   - success
                    {
                        m_caster->CastSpell(m_caster, 30456, true, m_CastItem);
                    }
                    else                                    // Complete Vulnerability - backfire in 14% casts
                    {
                        m_caster->CastSpell(m_caster, 30457, true, m_CastItem);
                    }

                    return;
                }
                case 30507:                                 // Poultryizer
                {
                    if (!m_CastItem)
                    {
                        return;
                    }

                    if (roll_chance_i(80))                  // Poultryized! - success
                    {
                        m_caster->CastSpell(unitTarget, 30501, true, m_CastItem);
                    }
                    else                                    // Poultryized! - backfire 20%
                    {
                        m_caster->CastSpell(unitTarget, 30504, true, m_CastItem);
                    }

                    return;
                }
                case 32146:                                 // Liquid Fire
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT || m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    ((Player*)m_caster)->KilledMonsterCredit(unitTarget->GetEntry(), unitTarget->GetObjectGuid());
                    ((Creature*)unitTarget)->ForcedDespawn();
                    return;
                }
                case 32300:                                 // Focus Fire
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, unitTarget->GetMap()->IsRegularDifficulty() ? 32302 : 38382, true);
                    return;
                }
                case 32312:                                 // Move 1 (Chess event AI short distance move)
                case 37388:                                 // Move 2 (Chess event AI long distance move)
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    // cast generic move spell
                    m_caster->CastSpell(unitTarget, 30012, true);
                    return;
                }
                case 33060:                                 // Make a Wish
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = 0;

                    switch (urand(1, 5))
                    {
                        case 1: spell_id = 33053; break;    // Mr Pinchy's Blessing
                        case 2: spell_id = 33057; break;    // Summon Mighty Mr. Pinchy
                        case 3: spell_id = 33059; break;    // Summon Furious Mr. Pinchy
                        case 4: spell_id = 33062; break;    // Tiny Magical Crawdad
                        case 5: spell_id = 33064; break;    // Mr. Pinchy's Gift
                    }

                    m_caster->CastSpell(m_caster, spell_id, true, NULL);
                    return;
                }
                case 34803:                                 // Summon Reinforcements
                {
                    m_caster->CastSpell(m_caster, 34810, true); // Summon 20083 behind of the caster
                    m_caster->CastSpell(m_caster, 34817, true); // Summon 20078 right of the caster
                    m_caster->CastSpell(m_caster, 34818, true); // Summon 20078 left of the caster
                    m_caster->CastSpell(m_caster, 34819, true); // Summon 20078 front of the caster
                    return;
                }
                case 36677:                                 // Chaos Breath
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 possibleSpells[] = {36693, 36694, 36695, 36696, 36697, 36698, 36699, 36700} ;
                    std::vector<uint32> spellPool(possibleSpells, possibleSpells + countof(possibleSpells));

                    //std::random_shuffle(spellPool.begin(), spellPool.end());
                    std::mt19937 rng(std::time(nullptr));
                    std::shuffle(spellPool.begin(), spellPool.end(), rng);

                    for (uint8 i = 0; i < (m_caster->GetMap()->IsRegularDifficulty() ? 2 : 4); ++i)
                    {
                        m_caster->CastSpell(m_caster, spellPool[i], true);
                    }

                    return;
                }
                case 33923:                                 // Sonic Boom
                case 38796:                                 // Sonic Boom (heroic)
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, m_spellInfo->ID == 33923 ? 33666 : 38795, true);
                    return;
                }
                case 35745:                                 // Socrethar's Stone
                {
                    uint32 spell_id;
                    switch (m_caster->GetAreaId())
                    {
                        case 3900: spell_id = 35743; break; // Socrethar Portal
                        case 3742: spell_id = 35744; break; // Socrethar Portal
                        default: return;
                    }

                    m_caster->CastSpell(m_caster, spell_id, true);
                    return;
                }
                case 37573:                                 // Temporal Phase Modulator
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    TemporarySummon* tempSummon = dynamic_cast<TemporarySummon*>(unitTarget);
                    if (!tempSummon)
                    {
                        return;
                    }

                    uint32 health = tempSummon->GetHealth();
                    const uint32 entry_list[6] = {21821, 21820, 21817};

                    float x = tempSummon->GetPositionX();
                    float y = tempSummon->GetPositionY();
                    float z = tempSummon->GetPositionZ();
                    float o = tempSummon->GetOrientation();

                    tempSummon->UnSummon();

                    Creature* pCreature = m_caster->SummonCreature(entry_list[urand(0, 2)], x, y, z, o, TEMPSPAWN_TIMED_OR_DEAD_DESPAWN, 180000);
                    if (!pCreature)
                    {
                        return;
                    }

                    pCreature->SetHealth(health);

                    if (pCreature->AI())
                    {
                        pCreature->AI()->AttackStart(m_caster);
                    }

                    return;
                }
                case 37674:                                 // Chaos Blast
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    int32 basepoints0 = 100;
                    m_caster->CastCustomSpell(unitTarget, 37675, &basepoints0, NULL, NULL, true);
                    return;
                }
                case 39189:                                 // Sha'tari Torch
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT || m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Flames
                    if (unitTarget->HasAura(39199))
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 39199, true);
                    ((Player*)m_caster)->KilledMonsterCredit(unitTarget->GetEntry(), unitTarget->GetObjectGuid());
                    ((Creature*)unitTarget)->ForcedDespawn(10000);
                    return;
                }
                case 39635:                                 // Throw Glaive (first)
                case 39849:                                 // Throw Glaive (second)
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 41466, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 40802:                                 // Mingo's Fortune Generator (Mingo's Fortune Giblets)
                {
                    // selecting one from Bloodstained Fortune item
                    uint32 newitemid;
                    switch (urand(1, 20))
                    {
                        case 1:  newitemid = 32688; break;
                        case 2:  newitemid = 32689; break;
                        case 3:  newitemid = 32690; break;
                        case 4:  newitemid = 32691; break;
                        case 5:  newitemid = 32692; break;
                        case 6:  newitemid = 32693; break;
                        case 7:  newitemid = 32700; break;
                        case 8:  newitemid = 32701; break;
                        case 9:  newitemid = 32702; break;
                        case 10: newitemid = 32703; break;
                        case 11: newitemid = 32704; break;
                        case 12: newitemid = 32705; break;
                        case 13: newitemid = 32706; break;
                        case 14: newitemid = 32707; break;
                        case 15: newitemid = 32708; break;
                        case 16: newitemid = 32709; break;
                        case 17: newitemid = 32710; break;
                        case 18: newitemid = 32711; break;
                        case 19: newitemid = 32712; break;
                        case 20: newitemid = 32713; break;
                        default:
                            return;
                    }

                    DoCreateItem(eff_idx, newitemid);
                    return;
                }
                case 40834:                                 // Agonizing Flames
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 40932, true);
                    return;
                }
                case 40869:                                 // Fatal Attraction
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 41001, true);
                    return;
                }
                case 40962:                                 // Blade's Edge Terrace Demon Boss Summon Branch
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = 0;
                    switch (urand(1, 4))
                    {
                        case 1: spell_id = 40957; break;    // Blade's Edge Terrace Demon Boss Summon 1
                        case 2: spell_id = 40959; break;    // Blade's Edge Terrace Demon Boss Summon 2
                        case 3: spell_id = 40960; break;    // Blade's Edge Terrace Demon Boss Summon 3
                        case 4: spell_id = 40961; break;    // Blade's Edge Terrace Demon Boss Summon 4
                    }
                    unitTarget->CastSpell(unitTarget, spell_id, true);
                    return;
                }
                case 41283:                                 // Abyssal Toss
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->SummonCreature(23416, unitTarget->GetPositionX(), unitTarget->GetPositionY(), unitTarget->GetPositionZ(), 0, TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN, 30000);
                    return;
                }
                case 41333:                                 // Empyreal Equivalency
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Equilize the health of all targets based on the corresponding health percent
                    float health_diff = (float)unitTarget->GetMaxHealth() / (float)m_caster->GetMaxHealth();
                    unitTarget->SetHealth(m_caster->GetHealth() * health_diff);
                    return;
                }
                case 42287:                                 // Salvage Wreckage
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (roll_chance_i(66))
                    {
                        m_caster->CastSpell(m_caster, 42289, true, m_CastItem);
                    }
                    else
                    {
                        m_caster->CastSpell(m_caster, 42288, true);
                    }

                    return;
                }
                 case 42628:                                 // Fire Bomb (throw)
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 42629, true);
                    return;
                }
                case 42631:                                 // Fire Bomb (explode)
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    unitTarget->RemoveAurasDueToSpell(42629);
                    unitTarget->CastSpell(unitTarget, 42630, true);

                    // despawn the bomb after exploding
                    ((Creature*)unitTarget)->ForcedDespawn(3000);
                    return;
                }
                case 43096:                                 // Summon All Players
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 43097, true);
                    return;
                }
                case 43144:                                 // Hatch All Eggs
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 42493, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 43498:                                 // Siphon Soul
                {
                    // This spell should cast the next spell only for one (player)target, however it should hit multiple targets, hence this kind of implementation
                    if (!unitTarget || m_UniqueTargetInfo.rbegin()->targetGUID != unitTarget->GetObjectGuid())
                    {
                        return;
                    }

                    std::vector<Unit*> possibleTargets;
                    possibleTargets.reserve(m_UniqueTargetInfo.size());
                    for (TargetList::const_iterator itr = m_UniqueTargetInfo.begin(); itr != m_UniqueTargetInfo.end(); ++itr)
                    {
                        // Skip Non-Players
                        if (!itr->targetGUID.IsPlayer())
                        {
                            continue;
                        }

                        if (Unit* target = m_caster->GetMap()->GetPlayer(itr->targetGUID))
                        {
                            possibleTargets.push_back(target);
                        }
                    }

                    // Cast Siphon Soul channeling spell
                    if (!possibleTargets.empty())
                    {
                        m_caster->CastSpell(possibleTargets[urand(0, possibleTargets.size()-1)], 43501, false);
                    }

                    return;
                }
                // Demon Broiled Surprise
                case 43723:
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    ((Player*)m_caster)->CastSpell(unitTarget, 43753, true, m_CastItem, NULL, m_originalCasterGUID, m_spellInfo);
                    return;
                }
                case 44845:                                 // Spectral Realm
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // teleport all targets which have the spectral realm aura
                    if (unitTarget->HasAura(46021))
                    {
                        unitTarget->RemoveAurasDueToSpell(46021);
                        unitTarget->CastSpell(unitTarget, 46020, true);
                        unitTarget->CastSpell(unitTarget, 44867, true);
                    }

                    return;
                }
                case 44869:                                 // Spectral Blast
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // If target has spectral exhaustion or spectral realm aura return
                    if (unitTarget->HasAura(44867) || unitTarget->HasAura(46021))
                    {
                        return;
                    }

                    // Cast the spectral realm effect spell, visual spell and spectral blast rift summoning
                    unitTarget->CastSpell(unitTarget, 44866, true, NULL, NULL, m_caster->GetObjectGuid());
                    unitTarget->CastSpell(unitTarget, 46648, true, NULL, NULL, m_caster->GetObjectGuid());
                    unitTarget->CastSpell(unitTarget, 44811, true);
                    return;
                }
                case 44875:                                 // Complete Raptor Capture
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    Creature* creatureTarget = (Creature*)unitTarget;

                    creatureTarget->ForcedDespawn();

                    // cast spell Raptor Capture Credit
                    m_caster->CastSpell(m_caster, 42337, true, NULL);
                    return;
                }
                case 44997:                                 // Converting Sentry
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    Creature* creatureTarget = (Creature*)unitTarget;

                    creatureTarget->ForcedDespawn();

                    // Converted Sentry Credit
                    m_caster->CastSpell(m_caster, 45009, true);
                    return;
                }
                case 45030:                                 // Impale Emissary
                {
                    // Emissary of Hate Credit
                    m_caster->CastSpell(m_caster, 45088, true);
                    return;
                }
                case 45235:                                 // Blaze
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 45236, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 45260:                                 // Karazhan - Chess - Force Player to Kill Bunny
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 45259, true);
                    return;
                }
                case 45714:                                 // Fog of Corruption (caster inform)
                {
                    if (!unitTarget || m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    unitTarget->CastSpell(m_caster, m_spellInfo->CalculateSimpleValue(eff_idx), true);
                    return;
                }
                case 45717:                                 // Fog of Corruption (player buff)
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 45726, true);
                    return;
                }
                case 45785:                                 // Sinister Reflection Clone
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, m_spellInfo->CalculateSimpleValue(eff_idx), true);
                    return;
                }
                case 45833:                                 // Power of the Blue Flight
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 45836, true);
                    return;
                }
                case 45892:                                 // Sinister Reflection
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Summon 4 clones of the same player
                    for (uint8 i = 0; i < 4; ++i)
                    {
                        unitTarget->CastSpell(unitTarget, 45891, true, NULL, NULL, m_caster->GetObjectGuid());
                    }
                    return;
                }
                case 45976:                                 // Open Portal
                case 46177:                                 // Open All Portals
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // portal visual
                    unitTarget->CastSpell(unitTarget, 45977, true);

                    // break in case additional procressing in scripting library required
                    break;
                }
                case 45989:                                 // Summon Void Sentinel Summoner Visual
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // summon void sentinel
                    unitTarget->CastSpell(unitTarget, 45988, true);

                    return;
                }
                case 46372:                                 // Ice Spear Target Picker
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 46359, true);
                    return;
                }
                case 46289:                                 // Negative Energy
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 46285, true);
                    return;
                }
                case 46430:                                 // Synch Health
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->SetHealth(m_caster->GetHealth());
                    return;
                }
                case 49357:                                 // Brewfest Mount Transformation
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (!m_caster->HasAuraType(SPELL_AURA_MOUNTED))
                    {
                        return;
                    }

                    m_caster->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

                    // Ram for Alliance, Kodo for Horde
                    if (((Player*)m_caster)->GetTeam() == ALLIANCE)
                    {
                        if (m_caster->GetSpeedRate(MOVE_RUN) >= 2.0f)
                            // 100% Ram
                            m_caster->CastSpell(m_caster, 43900, true);
                        else
                            // 60% Ram
                            m_caster->CastSpell(m_caster, 43899, true);
                    }
                    else
                    {
                        if (((Player*)m_caster)->GetSpeedRate(MOVE_RUN) >= 2.0f)
                            // 100% Kodo
                            m_caster->CastSpell(m_caster, 49379, true);
                        else
                            // 60% Kodo
                            m_caster->CastSpell(m_caster, 49378, true);
                    }
                    return;
                }
                case 50243:                                 // Teach Language
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // spell has a 1/3 chance to trigger one of the below
                    if (roll_chance_i(66))
                    {
                        return;
                    }

                    if (((Player*)m_caster)->GetTeam() == ALLIANCE)
                    {
                        // 1000001 - gnomish binary
                        m_caster->CastSpell(m_caster, 50242, true);
                    }
                    else
                    {
                        // 01001000 - goblin binary
                        m_caster->CastSpell(m_caster, 50246, true);
                    }

                    return;
                }
                case 51582:                                 // Rocket Boots Engaged (Rocket Boots Xtreme and Rocket Boots Xtreme Lite)
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (BattleGround* bg = ((Player*)m_caster)->GetBattleGround())
                    {
                        bg->EventPlayerDroppedFlag((Player*)m_caster);
                    }

                    m_caster->CastSpell(m_caster, 30452, true, NULL);
                    return;
                }
                case 52845:                                 // Brewfest Mount Transformation (Faction Swap)
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (!m_caster->HasAuraType(SPELL_AURA_MOUNTED))
                    {
                        return;
                    }

                    m_caster->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

                    // Ram for Horde, Kodo for Alliance
                    if (((Player*)m_caster)->GetTeam() == HORDE)
                    {
                        if (m_caster->GetSpeedRate(MOVE_RUN) >= 2.0f)
                            // Swift Brewfest Ram, 100% Ram
                            m_caster->CastSpell(m_caster, 43900, true);
                        else
                            // Brewfest Ram, 60% Ram
                            m_caster->CastSpell(m_caster, 43899, true);
                    }
                    else
                    {
                        if (((Player*)m_caster)->GetSpeedRate(MOVE_RUN) >= 2.0f)
                            // Great Brewfest Kodo, 100% Kodo
                            m_caster->CastSpell(m_caster, 49379, true);
                        else
                            // Brewfest Riding Kodo, 60% Kodo
                            m_caster->CastSpell(m_caster, 49378, true);
                    }
                    return;
                }
            }

            // All IconID Check in there
            switch (m_spellInfo->SpellIconID)
            {
                    // Berserking (troll racial traits)
                case 1661:
                {
                    uint32 healthPerc = uint32((float(m_caster->GetHealth()) / m_caster->GetMaxHealth()) * 100);
                    int32 melee_mod = 10;
                    if (healthPerc <= 40)
                    {
                        melee_mod = 30;
                    }
                    if (healthPerc < 100 && healthPerc > 40)
                    {
                        melee_mod = 10 + (100 - healthPerc) / 3;
                    }

                    int32 hasteModBasePoints0 = melee_mod;  // (EffectBasePoints[0]+1)-1+(5-melee_mod) = (melee_mod-1+1)-1+5-melee_mod = 5-1
                    int32 hasteModBasePoints1 = (5 - melee_mod);
                    int32 hasteModBasePoints2 = 5;

                    // FIXME: custom spell required this aura state by some unknown reason, we not need remove it anyway
                    m_caster->ModifyAuraState(AURA_STATE_BERSERKING, true);
                    m_caster->CastCustomSpell(m_caster, 26635, &hasteModBasePoints0, &hasteModBasePoints1, &hasteModBasePoints2, true, NULL);
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            switch (m_spellInfo->ID)
            {
                case 11189:                                 // Frost Warding
                case 28332:
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // increase reflection chance (effect 1) of Frost Ward, removed in aura boosts
                    SpellModifier* mod = new SpellModifier(SPELLMOD_EFFECT2, SPELLMOD_FLAT, damage, m_spellInfo->ID, UI64LIT(0x0000000000000100));
                    ((Player*)unitTarget)->AddSpellMod(mod, true);
                    break;
                }
                case 11958:                                 // Cold Snap
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // immediately finishes the cooldown on Frost spells
                    const SpellCooldowns& cm = ((Player*)m_caster)->GetSpellCooldownMap();
                    for (SpellCooldowns::const_iterator itr = cm.begin(); itr != cm.end();)
                    {
                        SpellEntry const* spellInfo = sSpellStore.LookupEntry(itr->first);

                        if (spellInfo->SpellClassSet == SPELLFAMILY_MAGE &&
                            (GetSpellSchoolMask(spellInfo) & SPELL_SCHOOL_MASK_FROST) &&
                            spellInfo->ID != 11958 && GetSpellRecoveryTime(spellInfo) > 0)
                        {
                            ((Player*)m_caster)->RemoveSpellCooldown((itr++)->first, true);
                        }
                        else
                        {
                            ++itr;
                        }
                    }
                    return;
                }
                case 32826:                                 // Polymorph Cast Visual
                {
                    if (unitTarget && unitTarget->GetTypeId() == TYPEID_UNIT)
                    {
                        // Polymorph Cast Visual Rank 1
                        const uint32 spell_list[6] =
                        {
                            32813,                          // Squirrel Form
                            32816,                          // Giraffe Form
                            32817,                          // Serpent Form
                            32818,                          // Dragonhawk Form
                            32819,                          // Worgen Form
                            32820                           // Sheep Form
                        };
                        unitTarget->CastSpell(unitTarget, spell_list[urand(0, 5)], true);
                    }
                    return;
                }
                case 38194:                                 // Blink
                {
                    // Blink
                    if (unitTarget)
                    {
                        m_caster->CastSpell(unitTarget, 38203, true);
                    }

                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_WARRIOR:
        {
            // Charge
            if ((m_spellInfo->SpellClassMask & UI64LIT(0x1)) && m_spellInfo->SpellVisualID == 867)
            {
                int32 chargeBasePoints0 = damage;
                m_caster->CastCustomSpell(m_caster, 34846, &chargeBasePoints0, NULL, NULL, true);
                return;
            }
            // Execute
            if (m_spellInfo->SpellClassMask & UI64LIT(0x20000000))
            {
                if (!unitTarget)
                {
                    return;
                }

                int32 basePoints0 = damage + int32(m_caster->GetPower(POWER_RAGE) * m_spellInfo->EffectChainAmplitude[eff_idx]);
                m_caster->CastCustomSpell(unitTarget, 20647, &basePoints0, NULL, NULL, true, 0);
                m_caster->SetPower(POWER_RAGE, 0);
                return;
            }

            switch (m_spellInfo->ID)
            {
                    // Warrior's Wrath
                case 21977:
                {
                    if (!unitTarget)
                    {
                        return;
                    }
                    m_caster->CastSpell(unitTarget, 21887, true); // spell mod
                    return;
                }
                case 30012:                                 // Move
                {
                    if (!unitTarget || unitTarget->HasAura(39400))
                    {
                        return;
                    }

                    unitTarget->CastSpell(m_caster, 30253, true);
                }
                case 30284:                                 // Change Facing
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(m_caster, 30270, true);
                    return;
                }
                case 37144:                                 // Move (Chess event player knight move)
                case 37146:                                 // Move (Chess event player pawn move)
                case 37148:                                 // Move (Chess event player queen move)
                case 37151:                                 // Move (Chess event player rook move)
                case 37152:                                 // Move (Chess event player bishop move)
                case 37153:                                 // Move (Chess event player king move)
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    // cast generic move spell
                    m_caster->CastSpell(unitTarget, 30012, true);
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // Life Tap
            if (m_spellInfo->SpellClassMask & UI64LIT(0x0000000000040000))
            {
                float cost = m_currentBasePoints[EFFECT_INDEX_0];

                if (Player* modOwner = m_caster->GetSpellModOwner())
                {
                    modOwner->ApplySpellMod(m_spellInfo->ID, SPELLMOD_COST, cost, this);
                }

                int32 dmg = m_caster->SpellDamageBonusDone(m_caster, m_spellInfo, uint32(cost > 0 ? cost : 0), SPELL_DIRECT_DAMAGE);
                dmg = m_caster->SpellDamageBonusTaken(m_caster, m_spellInfo, dmg, SPELL_DIRECT_DAMAGE);

                if (int32(m_caster->GetHealth()) > dmg)
                {
                    // Shouldn't Appear in Combat Log
                    m_caster->ModifyHealth(-dmg);

                    int32 mana = dmg;

                    // Improved Life Tap mod
                    Unit::AuraList const& auraDummy = m_caster->GetAurasByType(SPELL_AURA_DUMMY);
                    for (Unit::AuraList::const_iterator itr = auraDummy.begin(); itr != auraDummy.end(); ++itr)
                    {
                        if ((*itr)->GetSpellProto()->SpellClassSet == SPELLFAMILY_WARLOCK && (*itr)->GetSpellProto()->SpellIconID == 208)
                        {
                            mana = ((*itr)->GetModifier()->m_amount + 100) * mana / 100;
                        }
                    }

                    m_caster->CastCustomSpell(m_caster, 31818, &mana, NULL, NULL, true);

                    // Mana Feed
                    int32 manaFeedVal = m_caster->CalculateSpellDamage(m_caster, m_spellInfo, EFFECT_INDEX_1);
                    manaFeedVal = manaFeedVal * mana / 100;
                    if (manaFeedVal > 0)
                    {
                        m_caster->CastCustomSpell(m_caster, 32553, &manaFeedVal, NULL, NULL, true, NULL);
                    }
                }
                else
                {
                    SendCastResult(SPELL_FAILED_FIZZLE);
                }

                return;
            }
            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            switch (m_spellInfo->ID)
            {
                case 28598:                                 // Touch of Weakness triggered spell
                {
                    if (!unitTarget || !m_triggeredByAuraSpell)
                    {
                        return;
                    }

                    uint32 spellid = 0;
                    switch (m_triggeredByAuraSpell->ID)
                    {
                        case 2652:  spellid =  2943; break; // Rank 1
                        case 19261: spellid = 19249; break; // Rank 2
                        case 19262: spellid = 19251; break; // Rank 3
                        case 19264: spellid = 19252; break; // Rank 4
                        case 19265: spellid = 19253; break; // Rank 5
                        case 19266: spellid = 19254; break; // Rank 6
                        case 25461: spellid = 25460; break; // Rank 7
                        default:
                            sLog.outError("Spell::EffectDummy: Spell 28598 triggered by unhandeled spell %u", m_triggeredByAuraSpell->ID);
                            return;
                    }
                    m_caster->CastSpell(unitTarget, spellid, true, NULL);
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            switch (m_spellInfo->ID)
            {
                case 5420:                                  // Tree of Life passive
                {
                    // Tree of Life area effect
                    int32 health_mod = int32(m_caster->GetStat(STAT_SPIRIT) / 4);
                    m_caster->CastCustomSpell(m_caster, 34123, &health_mod, NULL, NULL, true, NULL);
                    return;
                }
                case 29201:                                 // Loatheb Corrupted Mind triggered sub spells
                {
                    uint32 spellid = 0;
                    switch (unitTarget->getClass())
                    {
                        case CLASS_PALADIN: spellid = 29196; break;
                        case CLASS_PRIEST: spellid = 29185; break;
                        case CLASS_SHAMAN: spellid = 29198; break;
                        case CLASS_DRUID: spellid = 29194; break;
                        default: break;
                    }
                    if (spellid != 0)
                    {
                        m_caster->CastSpell(unitTarget, spellid, true, NULL);
                    }
                }
            }
            break;
        }
        case SPELLFAMILY_ROGUE:
        {
            switch (m_spellInfo->ID)
            {
                case 5938:                                  // Shiv
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    Player* pCaster = ((Player*)m_caster);

                    Item* item = pCaster->GetWeaponForAttack(OFF_ATTACK);
                    if (!item)
                    {
                        return;
                    }

                    // all poison enchantments is temporary
                    uint32 enchant_id = item->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT);
                    if (!enchant_id)
                    {
                        return;
                    }

                    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
                    if (!pEnchant)
                    {
                        return;
                    }

                    for (int s = 0; s < 3; ++s)
                    {
                        if (pEnchant->Effect[s] != ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL)
                        {
                            continue;
                        }

                        SpellEntry const* combatEntry = sSpellStore.LookupEntry(pEnchant->EffectArg[s]);
                        if (!combatEntry || combatEntry->DispelType != DISPEL_POISON)
                        {
                            continue;
                        }

                        m_caster->CastSpell(unitTarget, combatEntry, true, item);
                    }

                    m_caster->CastSpell(unitTarget, 5940, true);
                    return;
                }

                case 31231:                                 // Cheat Death
                {
                    // Cheating Death
                    m_caster->CastSpell(m_caster, 45182, true);
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            // Steady Shot
            if (m_spellInfo->SpellClassMask & UI64LIT(0x100000000))
            {
                if (!unitTarget || !unitTarget->IsAlive())
                {
                    return;
                }

                bool found = false;

                // check dazed affect
                Unit::AuraList const& decSpeedList = unitTarget->GetAurasByType(SPELL_AURA_MOD_DECREASE_SPEED);
                for (Unit::AuraList::const_iterator iter = decSpeedList.begin(); iter != decSpeedList.end(); ++iter)
                {
                    if ((*iter)->GetSpellProto()->SpellIconID == 15 && (*iter)->GetSpellProto()->DispelType == 0)
                    {
                        found = true;
                        break;
                    }
                }

                if (found)
                {
                    m_damage += damage;
                }
                return;
            }
            // Kill command
            if (m_spellInfo->SpellClassMask & UI64LIT(0x00080000000000))
            {
                if (m_caster->getClass() != CLASS_HUNTER)
                {
                    return;
                }

                // clear hunter crit aura state
                m_caster->ModifyAuraState(AURA_STATE_HUNTER_CRIT_STRIKE, false);

                // additional damage from pet to pet target
                Pet* pet = m_caster->GetPet();
                if (!pet || !pet->getVictim())
                {
                    return;
                }

                uint32 spell_id = 0;
                switch (m_spellInfo->ID)
                {
                    case 34026: spell_id = 34027; break;    // rank 1
                    default:
                        sLog.outError("Spell::EffectDummy: Spell %u not handled in KC", m_spellInfo->ID);
                        return;
                }

                pet->CastSpell(pet->getVictim(), spell_id, true);
                return;
            }

            switch (m_spellInfo->ID)
            {
                case 23989:                                 // Readiness talent
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // immediately finishes the cooldown for hunter abilities
                    const SpellCooldowns& cm = ((Player*)m_caster)->GetSpellCooldownMap();
                    for (SpellCooldowns::const_iterator itr = cm.begin(); itr != cm.end();)
                    {
                        SpellEntry const* spellInfo = sSpellStore.LookupEntry(itr->first);

                        if (spellInfo->SpellClassSet == SPELLFAMILY_HUNTER && spellInfo->ID != 23989 && GetSpellRecoveryTime(spellInfo) > 0)
                        {
                            ((Player*)m_caster)->RemoveSpellCooldown((itr++)->first, true);
                        }
                        else
                        {
                            ++itr;
                        }
                    }
                    return;
                }
                case 37506:                                 // Scatter Shot
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // break Auto Shot and autohit
                    m_caster->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
                    m_caster->AttackStop();
                    ((Player*)m_caster)->SendAttackSwingCancelAttack();
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            switch (m_spellInfo->SpellIconID)
            {
                case 156:                                   // Holy Shock
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    int hurt = 0;
                    int heal = 0;

                    switch (m_spellInfo->ID)
                    {
                        case 20473: hurt = 25912; heal = 25914; break;
                        case 20929: hurt = 25911; heal = 25913; break;
                        case 20930: hurt = 25902; heal = 25903; break;
                        case 27174: hurt = 27176; heal = 27175; break;
                        case 33072: hurt = 33073; heal = 33074; break;
                        default:
                            sLog.outError("Spell::EffectDummy: Spell %u not handled in HS", m_spellInfo->ID);
                            return;
                    }

                    if (m_caster->IsFriendlyTo(unitTarget))
                    {
                        m_caster->CastSpell(unitTarget, heal, true);
                    }
                    else
                    {
                        m_caster->CastSpell(unitTarget, hurt, true);
                    }

                    return;
                }
                case 561:                                   // Judgement of command
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 spell_id = m_currentBasePoints[eff_idx];
                    SpellEntry const* spell_proto = sSpellStore.LookupEntry(spell_id);
                    if (!spell_proto)
                    {
                        return;
                    }

                    if (!unitTarget->hasUnitState(UNIT_STAT_STUNNED) && m_caster->GetTypeId() == TYPEID_PLAYER)
                    {
                        // decreased damage (/2) for non-stunned target.
                        SpellModifier* mod = new SpellModifier(SPELLMOD_DAMAGE, SPELLMOD_PCT, -50, m_spellInfo->ID, UI64LIT(0x0000020000000000));

                        ((Player*)m_caster)->AddSpellMod(mod, true);
                        m_caster->CastSpell(unitTarget, spell_proto, true, NULL);
                        // mod deleted
                        ((Player*)m_caster)->AddSpellMod(mod, false);
                    }
                    else
                    {
                        m_caster->CastSpell(unitTarget, spell_proto, true, NULL);
                    }

                    return;
                }
            }

            switch (m_spellInfo->ID)
            {
                case 31789:                                 // Righteous Defense (step 1)
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        SendCastResult(SPELL_FAILED_TARGET_AFFECTING_COMBAT);
                        return;
                    }

                    // 31989 -> dummy effect (step 1) + dummy effect (step 2) -> 31709 (taunt like spell for each target)
                    Unit* friendTarget = !unitTarget || unitTarget->IsFriendlyTo(m_caster) ? unitTarget : unitTarget->getVictim();
                    if (friendTarget)
                    {
                        Player* player = friendTarget->GetCharmerOrOwnerPlayerOrPlayerItself();
                        if (!player || !player->IsInSameRaidWith((Player*)m_caster))
                        {
                            friendTarget = NULL;
                        }
                    }

                    // non-standard cast requirement check
                    if (!friendTarget || friendTarget->getAttackers().empty())
                    {
                        ((Player*)m_caster)->RemoveSpellCooldown(m_spellInfo->ID, true);
                        SendCastResult(SPELL_FAILED_TARGET_AFFECTING_COMBAT);
                        return;
                    }

                    // Righteous Defense (step 2) (in old version 31980 dummy effect)
                    // Clear targets for eff 1
                    for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
                    {
                        ihit->effectMask &= ~(1 << 1);
                    }

                    // not empty (checked), copy
                    Unit::AttackerSet attackers = friendTarget->getAttackers();

                    // selected from list 3
                    for (uint32 i = 0; i < std::min(size_t(3), attackers.size()); ++i)
                    {
                        Unit::AttackerSet::iterator aItr = attackers.begin();
                        std::advance(aItr, rand() % attackers.size());
                        AddUnitTarget((*aItr), EFFECT_INDEX_1);
                        attackers.erase(aItr);
                    }

                    // now let next effect cast spell at each target.
                    return;
                }
                case 37877:                                 // Blessing of Faith
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 spell_id = 0;
                    switch (unitTarget->getClass())
                    {
                        case CLASS_DRUID:   spell_id = 37878; break;
                        case CLASS_PALADIN: spell_id = 37879; break;
                        case CLASS_PRIEST:  spell_id = 37880; break;
                        case CLASS_SHAMAN:  spell_id = 37881; break;
                        default: return;                    // ignore for not healing classes
                    }

                    m_caster->CastSpell(m_caster, spell_id, true);
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            // Rockbiter Weapon
            if (m_spellInfo->SpellClassMask & UI64LIT(0x400000))
            {
                uint32 spell_id = 0;
                switch (m_spellInfo->ID)
                {
                    case  8017: spell_id = 36494; break;    // Rank 1
                    case  8018: spell_id = 36750; break;    // Rank 2
                    case  8019: spell_id = 36755; break;    // Rank 3
                    case 10399: spell_id = 36759; break;    // Rank 4
                    case 16314: spell_id = 36763; break;    // Rank 5
                    case 16315: spell_id = 36766; break;    // Rank 6
                    case 16316: spell_id = 36771; break;    // Rank 7
                    case 25479: spell_id = 36775; break;    // Rank 8
                    case 25485: spell_id = 36499; break;    // Rank 9
                    default:
                        sLog.outError("Spell::EffectDummy: Spell %u not handled in RW", m_spellInfo->ID);
                        return;
                }

                SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell_id);

                if (!spellInfo)
                {
                    sLog.outError("WORLD: unknown spell id %i", spell_id);
                    return;
                }

                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                {
                    return;
                }

                for (int j = BASE_ATTACK; j <= OFF_ATTACK; ++j)
                {
                    if (Item* item = ((Player*)m_caster)->GetWeaponForAttack(WeaponAttackType(j)))
                    {
                        if (item->IsFitToSpellRequirements(m_spellInfo))
                        {
                            Spell* spell = new Spell(m_caster, spellInfo, true);

                            // enchanting spell selected by calculated damage-per-sec in enchanting effect
                            // at calculation applied affect from Elemental Weapons talent
                            // real enchantment damage
                            spell->m_currentBasePoints[1] = damage;

                            SpellCastTargets targets;
                            targets.setItemTarget(item);
                            spell->prepare(&targets);
                        }
                    }
                }
                return;
            }
            // Flametongue Weapon Proc, Ranks
            if (m_spellInfo->SpellClassMask & UI64LIT(0x0000000000200000))
            {
                if (!m_CastItem)
                {
                    sLog.outError("Spell::EffectDummy: spell %i requires cast Item", m_spellInfo->ID);
                    return;
                }
                // found spelldamage coefficients of 0.381% per 0.1 speed and 15.244 per 4.0 speed
                // but own calculation say 0.385 gives at most one point difference to published values
                int32 spellDamage = m_caster->SpellBaseDamageBonusDone(GetSpellSchoolMask(m_spellInfo));
                float weaponSpeed = (1.0f / IN_MILLISECONDS) * m_CastItem->GetProto()->Delay;
                int32 totalDamage = int32((damage + 3.85f * spellDamage) * 0.01 * weaponSpeed);

                m_caster->CastCustomSpell(unitTarget, 10444, &totalDamage, NULL, NULL, true, m_CastItem);
                return;
            }
            if (m_spellInfo->ID == 39610)                   // Mana Tide Totem effect
            {
                if (!unitTarget || unitTarget->GetPowerType() != POWER_MANA)
                {
                    return;
                }

                // Regenerate 6% of Total Mana Every 3 secs
                int32 EffectBasePoints0 = unitTarget->GetMaxPower(POWER_MANA)  * damage / 100;
                m_caster->CastCustomSpell(unitTarget, 39609, &EffectBasePoints0, NULL, NULL, true, NULL, NULL, m_originalCasterGUID);
                return;
            }

            break;
        }
    }

    // pet auras
    if (PetAura const* petSpell = sSpellMgr.GetPetAura(m_spellInfo->ID))
    {
        m_caster->AddPetAura(petSpell);
        return;
    }

    // Script based implementation. Must be used only for not good for implementation in core spell effects
    // So called only for not processed cases
    bool libraryResult = false;
    if (gameObjTarget)
    {
        libraryResult = sScriptMgr.OnEffectDummy(m_caster, m_spellInfo->ID, eff_idx, gameObjTarget, m_originalCasterGUID);
    }
    else if (unitTarget && (unitTarget->GetTypeId() == TYPEID_UNIT || unitTarget->GetTypeId() == TYPEID_PLAYER))
    {
        libraryResult = sScriptMgr.OnEffectDummy(m_caster, m_spellInfo->ID, eff_idx, unitTarget, m_originalCasterGUID);
    }
    else if (itemTarget)
    {
        libraryResult = sScriptMgr.OnEffectDummy(m_caster, m_spellInfo->ID, eff_idx, itemTarget, m_originalCasterGUID);
    }

    if (libraryResult || !unitTarget)
    {
        return;
    }

    // Previous effect might have started script
    if (!ScriptMgr::CanSpellEffectStartDBScript(m_spellInfo, eff_idx))
    {
        return;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell ScriptStart spellid %u in EffectDummy", m_spellInfo->ID);
    m_caster->GetMap()->ScriptsStart(DBS_ON_SPELL, m_spellInfo->ID, m_caster, unitTarget);
}
