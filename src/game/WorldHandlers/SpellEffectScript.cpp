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
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

/**
 * @brief Executes script-driven spell effect behavior for special cases.
 *
 * @param eff_idx The script effect index.
 */
void Spell::EffectScriptEffect(SpellEffectIndex eff_idx)
{
    // TODO: we must implement hunter pet summon at login there (spell 6962)

    switch (m_spellInfo->SpellClassSet)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (m_spellInfo->ID)
            {
                case 1509:                                  // GM Mode OFF
                {
                    if (unitTarget->GetTypeId() == TYPEID_PLAYER)
                    {
                        ((Player*)unitTarget)->SetGameMaster(false);
                    }
                    break;
                }
                case 18139:                                 // GM Mode ON
                {
                    if (unitTarget->GetTypeId() == TYPEID_PLAYER)
                    {
                        ((Player*)unitTarget)->SetGameMaster(true);
                    }
                    break;
                }

                case 5249:                                  // Ice Lock
                {
                    if (unitTarget)
                    {
                        m_caster->CastSpell(unitTarget, 22856, true);
                        sLog.outString("EffectScriptEffect : %s target of spell 5249", unitTarget->GetName());
                    }
                    break;
                }
                case 8856:                                  // Bending Shinbone
                {
                    if (!itemTarget && m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spell_id = 0;
                    switch (urand(1, 5))
                    {
                        case 1:  spell_id = 8854; break;
                        default: spell_id = 8855; break;
                    }

                    m_caster->CastSpell(m_caster, spell_id, true, NULL);
                    return;
                }
                case 17512:                                 // Piccolo of the Flaming Fire
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    unitTarget->HandleEmoteCommand(EMOTE_STATE_DANCE);

                    return;
                }
                case 22539:                                 // Shadow Flame (All script effects, not just end ones to
                case 22972:                                 // prevent player from dodging the last triggered spell)
                case 22975:
                case 22976:
                case 22977:
                case 22978:
                case 22979:
                case 22980:
                case 22981:
                case 22982:
                case 22983:
                case 22984:
                case 22985:
                {
                    if (!unitTarget || !unitTarget->IsAlive())
                    {
                        return;
                    }

                    // Onyxia Scale Cloak
                    if (unitTarget->GetDummyAura(22683))
                    {
                        return;
                    }

                    // Shadow Flame
                    m_caster->CastSpell(unitTarget, 22682, true);
                    return;
                }
                case 24194:                                 // Uther's Tribute
                case 24195:                                 // Grom's Tribute
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint8 race = m_caster->getRace();
                    uint32 spellId = 0;

                    switch (m_spellInfo->ID)
                    {
                        case 24194:
                            switch (race)
                            {
                                case RACE_HUMAN:            spellId = 24105; break;
                                case RACE_DWARF:            spellId = 24107; break;
                                case RACE_NIGHTELF:         spellId = 24108; break;
                                case RACE_GNOME:            spellId = 24106; break;
                                    // next case not exist in 2.x officially (quest has been broken for race until 3.x time)
                                case RACE_DRAENEI:          spellId = 24108; break;
                            }
                            break;
                        case 24195:
                            switch (race)
                            {
                                case RACE_ORC:              spellId = 24104; break;
                                case RACE_UNDEAD:           spellId = 24103; break;
                                case RACE_TAUREN:           spellId = 24102; break;
                                case RACE_TROLL:            spellId = 24101; break;
                                    // next case not exist in 2.x officially (quest has been broken for race until 3.x time)
                                case RACE_BLOODELF:         spellId = 24101; break;
                            }
                            break;
                    }

                    if (spellId)
                    {
                        m_caster->CastSpell(m_caster, spellId, true);
                    }

                    return;
                }
                case 24320:                                 // Poisonous Blood
                {
                    unitTarget->CastSpell(unitTarget, 24321, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 24324:                                 // Blood Siphon
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    unitTarget->CastSpell(m_caster, unitTarget->HasAura(24321) ? 24323 : 24322, true);
                    return;
                }
                case 24590:                                 // Brittle Armor - need remove one 24575 Brittle Armor aura
                    unitTarget->RemoveAuraHolderFromStack(24575);
                    return;
                case 24714:                                 // Trick
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (roll_chance_i(14))                  // Trick (can be different critter models). 14% since below can have 1 of 6
                    {
                        m_caster->CastSpell(m_caster, 24753, true);
                    }
                    else                                    // Random Costume, 6 different (plus add. for gender)
                    {
                        m_caster->CastSpell(m_caster, 24720, true);
                    }

                    return;
                }
                case 24717:                                 // Pirate Costume
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Pirate Costume (male or female)
                    m_caster->CastSpell(unitTarget, unitTarget->getGender() == GENDER_MALE ? 24708 : 24709, true);
                    return;
                }
                case 24718:                                 // Ninja Costume
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Ninja Costume (male or female)
                    m_caster->CastSpell(unitTarget, unitTarget->getGender() == GENDER_MALE ? 24711 : 24710, true);
                    return;
                }
                case 24719:                                 // Leper Gnome Costume
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Leper Gnome Costume (male or female)
                    m_caster->CastSpell(unitTarget, unitTarget->getGender() == GENDER_MALE ? 24712 : 24713, true);
                    return;
                }
                case 24720:                                 // Random Costume
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spellId = 0;

                    switch (urand(0, 6))
                    {
                        case 0:
                            spellId = unitTarget->getGender() == GENDER_MALE ? 24708 : 24709;
                            break;
                        case 1:
                            spellId = unitTarget->getGender() == GENDER_MALE ? 24711 : 24710;
                            break;
                        case 2:
                            spellId = unitTarget->getGender() == GENDER_MALE ? 24712 : 24713;
                            break;
                        case 3:
                            spellId = 24723;
                            break;
                        case 4:
                            spellId = 24732;
                            break;
                        case 5:
                            spellId = unitTarget->getGender() == GENDER_MALE ? 24735 : 24736;
                            break;
                        case 6:
                            spellId = 24740;
                            break;
                    }

                    m_caster->CastSpell(unitTarget, spellId, true);
                    return;
                }
                case 24737:                                 // Ghost Costume
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Ghost Costume (male or female)
                    m_caster->CastSpell(unitTarget, unitTarget->getGender() == GENDER_MALE ? 24735 : 24736, true);
                    return;
                }
                case 24751:                                 // Trick or Treat
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Tricked or Treated
                    unitTarget->CastSpell(unitTarget, 24755, true);

                    // Treat / Trick
                    unitTarget->CastSpell(unitTarget, roll_chance_i(50) ? 24714 : 24715, true);
                    return;
                }
                case 25140:                                 // Orb teleport spells
                case 25143:
                case 25650:
                case 25652:
                case 29128:
                case 29129:
                case 35376:
                case 35727:
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 spellid;
                    switch (m_spellInfo->ID)
                    {
                        case 25140: spellid =  32568; break;
                        case 25143: spellid =  32572; break;
                        case 25650: spellid =  30140; break;
                        case 25652: spellid =  30141; break;
                        case 29128: spellid =  32571; break;
                        case 29129: spellid =  32569; break;
                        case 35376: spellid =  25649; break;
                        case 35727: spellid =  35730; break;
                        default:
                            return;
                    }

                    unitTarget->CastSpell(unitTarget, spellid, false);
                    return;
                }
                case 26004:                                 // Mistletoe
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->HandleEmote(EMOTE_ONESHOT_CHEER);
                    return;
                }
                case 26137:                                 // Rotate Trigger
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, urand(0, 1) ? 26009 : 26136, true);
                    return;
                }
                case 26218:                                 // Mistletoe
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    uint32 spells[3] = {26206, 26207, 45036};

                    m_caster->CastSpell(unitTarget, spells[urand(0, 2)], true);
                    return;
                }
                case 26275:                                 // PX-238 Winter Wondervolt TRAP
                {
                    uint32 spells[4] = {26272, 26157, 26273, 26274};

                    // check presence
                    for (int j = 0; j < 4; ++j)
                    {
                        if (unitTarget->HasAura(spells[j], EFFECT_INDEX_0))
                        {
                            return;
                        }
                    }

                    unitTarget->CastSpell(unitTarget, spells[urand(0, 3)], true);
                    return;
                }
                case 26465:                                 // Mercurial Shield - need remove one 26464 Mercurial Shield aura
                    unitTarget->RemoveAuraHolderFromStack(26464);
                    return;
                case 26656:                                 // Summon Black Qiraji Battle Tank
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Prevent stacking of mounts
                    unitTarget->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

                    // Two separate mounts depending on area id (allows use both in and out of specific instance)
                    if (unitTarget->GetAreaId() == 3428)
                    {
                        unitTarget->CastSpell(unitTarget, 25863, false);
                    }
                    else
                    {
                        unitTarget->CastSpell(unitTarget, 26655, false);
                    }

                    return;
                }
                case 27687:                                 // Summon Bone Minions
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Spells 27690, 27691, 27692, 27693 are missing from DBC
                    // So we need to summon creature 16119 manually
                    float x, y, z;
                    float angle = unitTarget->GetOrientation();
                    for (uint8 i = 0; i < 4; ++i)
                    {
                        unitTarget->GetNearPoint(unitTarget, x, y, z, unitTarget->GetObjectBoundingRadius(), INTERACTION_DISTANCE, angle + i * M_PI_F / 2);
                        unitTarget->SummonCreature(16119, x, y, z, angle, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 10 * MINUTE * IN_MILLISECONDS);
                    }
                    return;
                }
                case 27695:                                 // Summon Bone Mages
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 27696, true);
                    unitTarget->CastSpell(unitTarget, 27697, true);
                    unitTarget->CastSpell(unitTarget, 27698, true);
                    unitTarget->CastSpell(unitTarget, 27699, true);
                    return;
                }
                case 28352:                                 // Breath of Sargeras
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 28342, true);
                    return;
                }
                case 28374:                                 // Decimate (Naxxramas: Gluth)
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    int32 damage = unitTarget->GetHealth() - unitTarget->GetMaxHealth() * 0.05f;
                    if (damage > 0)
                    {
                        m_caster->CastCustomSpell(unitTarget, 28375, &damage, NULL, NULL, true);
                    }
                    return;
                }
                case 28560:                                 // Summon Blizzard
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->SummonCreature(16474, unitTarget->GetPositionX(), unitTarget->GetPositionY(), unitTarget->GetPositionZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 30000);
                    return;
                }
                case 29395:                                 // Break Kaliri Egg
                {
                    uint32 creature_id = 0;
                    uint32 rand = urand(0, 99);

                    if (rand < 10)
                    {
                        creature_id = 17034;
                    }
                    else if (rand < 60)
                    {
                        creature_id = 17035;
                    }
                    else
                    {
                        creature_id = 17039;
                    }

                    if (WorldObject* pSource = GetAffectiveCasterObject())
                    {
                        pSource->SummonCreature(creature_id, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 120 * IN_MILLISECONDS);
                    }
                    return;
                }
                case 29830:                                 // Mirren's Drinking Hat
                {
                    uint32 item = 0;
                    switch (urand(1, 6))
                    {
                        case 1:
                        case 2:
                        case 3:
                            item = 23584; break;            // Loch Modan Lager
                        case 4:
                        case 5:
                            item = 23585; break;            // Stouthammer Lite
                        case 6:
                            item = 23586; break;            // Aerie Peak Pale Ale
                    }

                    if (item)
                    {
                        DoCreateItem(eff_idx, item);
                    }

                    break;
                }
                case 30541:                                 // Blaze
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 30542, true);
                    break;
                }
                case 30769:                                 // Pick Red Riding Hood
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // cast Little Red Riding Hood
                    m_caster->CastSpell(unitTarget, 30768, true);
                    break;
                }
                case 30835:                                 // Infernal Relay
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 30836, true, NULL, NULL, m_caster->GetObjectGuid());
                    break;
                }
                case 30918:                                 // Improved Sprint
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Removes snares and roots.
                    unitTarget->RemoveAurasAtMechanicImmunity(IMMUNE_TO_ROOT_AND_SNARE_MASK, 30918, true);
                    break;
                }
                case 37142:                                 // Karazhan - Chess NPC Action: Melee Attack: Conjured Water Elemental
                case 37143:                                 // Karazhan - Chess NPC Action: Melee Attack: Charger
                case 37147:                                 // Karazhan - Chess NPC Action: Melee Attack: Human Cleric
                case 37149:                                 // Karazhan - Chess NPC Action: Melee Attack: Human Conjurer
                case 37150:                                 // Karazhan - Chess NPC Action: Melee Attack: King Llane
                case 37220:                                 // Karazhan - Chess NPC Action: Melee Attack: Summoned Daemon
                case 32227:                                 // Karazhan - Chess NPC Action: Melee Attack: Footman
                case 32228:                                 // Karazhan - Chess NPC Action: Melee Attack: Grunt
                case 37337:                                 // Karazhan - Chess NPC Action: Melee Attack: Orc Necrolyte
                case 37339:                                 // Karazhan - Chess NPC Action: Melee Attack: Orc Wolf
                case 37345:                                 // Karazhan - Chess NPC Action: Melee Attack: Orc Warlock
                case 37348:                                 // Karazhan - Chess NPC Action: Melee Attack: Warchief Blackhand
                {
                        if (!unitTarget)
                        {
                            return;
                        }

                        m_caster->CastSpell(unitTarget, 32247, true);
                        return;
                }
                case 32301:                                 // Ping Shirrak
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Cast Focus fire on caster
                    unitTarget->CastSpell(m_caster, 32300, true);
                    return;
                }
                case 33676:                                 // Incite Chaos
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 33684, true);
                    return;
                }
                case 34653:                                 // Fireball
                case 36920:                                 // Fireball (h)
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, unitTarget->GetMap()->IsRegularDifficulty() ? 23971 : 30928, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 35865:                                 // Summon Nether Vapor
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    float x, y, z;
                    for (uint8 i = 0; i < 4; ++i)
                    {
                        m_caster->GetNearPoint(m_caster, x, y, z, 0.0f, INTERACTION_DISTANCE, M_PI_F * .5f * i + M_PI_F * .25f);
                        m_caster->SummonCreature(21002, x, y, z, 0, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 30000);
                    }
                    return;
                }
                case 37431:                                 // Spout
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, urand(0, 1) ? 37429 : 37430, true);
                    return;
                }
                case 37775:                                 // Karazhan - Chess NPC Action - Poison Cloud
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 37469, true);
                    return;
                }
                case 37824:                                 // Karazhan - Chess NPC Action - Shadow Mend
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 37456, true);
                    return;
                }
                case 38358:                                 // Tidal Surge
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 38353, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 39338:                                 // Karazhan - Chess, Medivh CHEAT: Hand of Medivh, Target Horde
                case 39342:                                 // Karazhan - Chess, Medivh CHEAT: Hand of Medivh, Target Alliance
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, 39339, true);
                    return;
                }
                case 39341:                                 // Karazhan - Chess, Medivh CHEAT: Fury of Medivh, Target Horde
                case 39344:                                 // Karazhan - Chess, Medivh CHEAT: Fury of Medivh, Target Alliance
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    m_caster->CastSpell(unitTarget, m_spellInfo->CalculateSimpleValue(eff_idx), true);
                    return;
                }
                case 41055:                                 // Copy Weapon
                {
                    if (m_caster->GetTypeId() != TYPEID_UNIT || !unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (Item* pItem = ((Player*)unitTarget)->GetWeaponForAttack(BASE_ATTACK))
                    {
                        ((Creature*)m_caster)->SetVirtualItem(VIRTUAL_ITEM_SLOT_0, pItem->GetEntry());

                        // Unclear what this spell should do
                        unitTarget->CastSpell(m_caster, m_spellInfo->CalculateSimpleValue(eff_idx), true);
                    }

                    return;
                }
                case 41126:                                 // Flame Crash
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 41131, true);
                    break;
                }
                case 42281:                                 // Sprouting
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->RemoveAurasDueToSpell(42280);
                    unitTarget->RemoveAurasDueToSpell(42294);
                    unitTarget->CastSpell(unitTarget, 42285, true);
                    unitTarget->CastSpell(unitTarget, 42291, true);
                    return;
                }
                case 42578:                                 // Cannon Blast
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    int32 basePoints = m_spellInfo->CalculateSimpleValue(eff_idx);
                    unitTarget->CastCustomSpell(unitTarget, 42576, &basePoints, NULL, NULL, true);
                    return;
                }
                case 44876:                                 // Force Cast - Portal Effect: Sunwell Isle
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 44870, true);
                    break;
                }
                case 44811:                                 // Spectral Realm
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // If the player can't be teleported, send him a notification
                    if (unitTarget->HasAura(44867))
                    {
                        ((Player*)unitTarget)->GetSession()->SendNotification(LANG_FAIL_ENTER_SPECTRAL_REALM);
                        return;
                    }

                    // Teleport target to the spectral realm, add debuff and force faction
                    unitTarget->CastSpell(unitTarget, 46019, true);
                    unitTarget->CastSpell(unitTarget, 46021, true);
                    unitTarget->CastSpell(unitTarget, 44845, true);
                    unitTarget->CastSpell(unitTarget, 44852, true);
                    return;
                }
                case 45141:                                 // Burn
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 46394, true, NULL, NULL, m_caster->GetObjectGuid());
                    return;
                }
                case 45151:                                 // Burn
                {
                    if (!unitTarget || unitTarget->HasAura(46394))
                    {
                        return;
                    }

                    // Make the burn effect jump to another friendly target
                    unitTarget->CastSpell(unitTarget, 46394, true);
                    return;
                }
                case 45185:                                 // Stomp
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // Remove the burn effect
                    unitTarget->RemoveAurasDueToSpell(46394);
                    return;
                }
                case 45204:                                 // Clone Me!
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(m_caster, m_spellInfo->CalculateSimpleValue(eff_idx), true);
                    return;
                }
                case 45206:                                 // Copy Off-hand Weapon
                {
                    if (m_caster->GetTypeId() != TYPEID_UNIT || !unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    if (Item* pItem = ((Player*)unitTarget)->GetWeaponForAttack(OFF_ATTACK))
                    {
                        ((Creature*)m_caster)->SetVirtualItem(VIRTUAL_ITEM_SLOT_1, pItem->GetEntry());

                        // Unclear what this spell should do
                        unitTarget->CastSpell(m_caster, m_spellInfo->CalculateSimpleValue(eff_idx), true);
                    }

                    return;
                }
                case 45313:                                 // Anchor Here
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_UNIT)
                    {
                        return;
                    }

                    ((Creature*)unitTarget)->SetRespawnCoord(unitTarget->GetPositionX(), unitTarget->GetPositionY(), unitTarget->GetPositionZ(), unitTarget->GetOrientation());
                    return;
                }
                case 45918:                                 // Soul Sever
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER || !unitTarget->HasAura(45717))
                    {
                        return;
                    }

                    // kill all charmed targets
                    unitTarget->CastSpell(unitTarget, 45917, true);
                    return;
                }
                case 46203:                                 // Goblin Weather Machine
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 spellId = 0;
                    switch (rand() % 4)
                    {
                        case 0: spellId = 46740; break;
                        case 1: spellId = 46739; break;
                        case 2: spellId = 46738; break;
                        case 3: spellId = 46736; break;
                    }
                    unitTarget->CastSpell(unitTarget, spellId, true);
                    break;
                }
                case 46642:                                 //5,000 Gold
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    ((Player*)unitTarget)->ModifyMoney(50000000);
                    break;
                }
                case 48917:                                 // Who Are They: Cast from Questgiver
                {
                    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
                    {
                        return;
                    }

                    // Male Shadowy Disguise / Female Shadowy Disguise
                    unitTarget->CastSpell(unitTarget, unitTarget->getGender() == GENDER_MALE ? 38080 : 38081, true);
                    // Shadowy Disguise
                    unitTarget->CastSpell(unitTarget, 32756, true);
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            switch (m_spellInfo->ID)
            {
                case  6201:                                 // Healthstone creating spells
                case  6202:
                case  5699:
                case 11729:
                case 11730:
                case 27230:
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    uint32 itemtype;
                    uint32 rank = 0;
                    Unit::AuraList const& mDummyAuras = unitTarget->GetAurasByType(SPELL_AURA_DUMMY);
                    for (Unit::AuraList::const_iterator i = mDummyAuras.begin(); i != mDummyAuras.end(); ++i)
                    {
                        if ((*i)->GetId() == 18692)
                        {
                            rank = 1;
                            break;
                        }
                        else if ((*i)->GetId() == 18693)
                        {
                            rank = 2;
                            break;
                        }
                    }

                    static uint32 const itypes[6][3] =
                    {
                        { 5512, 19004, 19005},              // Minor Healthstone
                        { 5511, 19006, 19007},              // Lesser Healthstone
                        { 5509, 19008, 19009},              // Healthstone
                        { 5510, 19010, 19011},              // Greater Healthstone
                        { 9421, 19012, 19013},              // Major Healthstone
                        {22103, 22104, 22105}               // Master Healthstone
                    };

                    switch (m_spellInfo->ID)
                    {
                        case  6201:
                            itemtype = itypes[0][rank]; break; // Minor Healthstone
                        case  6202:
                            itemtype = itypes[1][rank]; break; // Lesser Healthstone
                        case  5699:
                            itemtype = itypes[2][rank]; break; // Healthstone
                        case 11729:
                            itemtype = itypes[3][rank]; break; // Greater Healthstone
                        case 11730:
                            itemtype = itypes[4][rank]; break; // Major Healthstone
                        case 27230:
                            itemtype = itypes[5][rank]; break; // Master Healthstone
                        default:
                            return;
                    }
                    DoCreateItem(eff_idx, itemtype);
                    return;
                }
            }
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            if (m_spellInfo->SpellClassMask & UI64LIT(0x0000000000800000))
            {
                if (!unitTarget || !unitTarget->IsAlive())
                {
                    return;
                }

                uint32 spellId2 = 0;

                // all seals have aura dummy
                Unit::AuraList const& m_dummyAuras = m_caster->GetAurasByType(SPELL_AURA_DUMMY);
                for (Unit::AuraList::const_iterator itr = m_dummyAuras.begin(); itr != m_dummyAuras.end(); ++itr)
                {
                    SpellEntry const* spellInfo = (*itr)->GetSpellProto();

                    // search seal (all seals have judgement's aura dummy spell id in 2 effect
                    if (!spellInfo || !IsSealSpell((*itr)->GetSpellProto()) || (*itr)->GetEffIndex() != 2)
                    {
                        continue;
                    }

                    // must be calculated base at raw base points in spell proto, GetModifier()->m_value for S.Righteousness modified by SPELLMOD_DAMAGE
                    spellId2 = (*itr)->GetSpellProto()->CalculateSimpleValue(EFFECT_INDEX_2);

                    if (spellId2 <= 1)
                    {
                        continue;
                    }

                    // found, remove seal
                    m_caster->RemoveAurasDueToSpell((*itr)->GetId());

                    // Sanctified Judgement
                    Unit::AuraList const& m_auras = m_caster->GetAurasByType(SPELL_AURA_DUMMY);
                    for (Unit::AuraList::const_iterator i = m_auras.begin(); i != m_auras.end(); ++i)
                    {
                        if ((*i)->GetSpellProto()->SpellIconID == 205 && (*i)->GetSpellProto()->Attributes == UI64LIT(0x01D0))
                        {
                            int32 chance = (*i)->GetModifier()->m_amount;
                            if (roll_chance_i(chance))
                            {
                                int32 mana = spellInfo->ManaCost;
                                if (Player* modOwner = m_caster->GetSpellModOwner())
                                {
                                    modOwner->ApplySpellMod(spellInfo->ID, SPELLMOD_COST, mana);
                                }
                                mana = int32(mana * 0.8f);
                                m_caster->CastCustomSpell(m_caster, 31930, &mana, NULL, NULL, true, NULL, *i);
                            }
                            break;
                        }
                    }

                    break;
                }

                m_caster->CastSpell(unitTarget, spellId2, true);

                return;
            }
            break;
        }
        case SPELLFAMILY_POTION:
        {
            switch (m_spellInfo->ID)
            {
                case 28698:                                 // Dreaming Glory
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 28694, true);
                    break;
                }
                case 28702:                                 // Netherbloom
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // 25% chance of casting a random buff
                    if (roll_chance_i(75))
                    {
                        return;
                    }

                    // triggered spells are 28703 to 28707
                    // Note: some sources say, that there was the possibility of
                    //       receiving a debuff. However, this seems to be removed by a patch.
                    const uint32 spellid = 28703;

                    // don't overwrite an existing aura
                    for (uint8 i = 0; i < 5; ++i)
                    {
                        if (unitTarget->HasAura(spellid + i, EFFECT_INDEX_0))
                        {
                            return;
                        }
                    }

                    unitTarget->CastSpell(unitTarget, spellid + urand(0, 4), true);
                    break;
                }
                case 28720:                                 // Nightmare Vine
                {
                    if (!unitTarget)
                    {
                        return;
                    }

                    // 25% chance of casting Nightmare Pollen
                    if (roll_chance_i(75))
                    {
                        return;
                    }

                    unitTarget->CastSpell(unitTarget, 28721, true);
                    break;
                }
            }
            break;
        }
    }

    // normal DB scripted effect
    if (!unitTarget)
    {
        return;
    }

    // Script based implementation. Must be used only for not good for implementation in core spell effects
    // So called only for not processed cases
    if (unitTarget->GetTypeId() == TYPEID_UNIT || unitTarget->GetTypeId() == TYPEID_PLAYER)
    {
        if (sScriptMgr.OnEffectScriptEffect(m_caster, m_spellInfo->ID, eff_idx, unitTarget, m_originalCasterGUID))
        {
            return;
        }
    }

    // Previous effect might have started script
    if (!ScriptMgr::CanSpellEffectStartDBScript(m_spellInfo, eff_idx))
    {
        return;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell ScriptStart spellid %u in EffectScriptEffect", m_spellInfo->ID);
    m_caster->GetMap()->ScriptsStart(DBS_ON_SPELL, m_spellInfo->ID, m_caster, unitTarget);
}
