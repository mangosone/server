#include "../botpch.h"
#include "Category.h"
#include "ItemBag.h"
#include "AhBotConfig.h"
#include "PricingStrategy.h"

using namespace ahbot;

/**
 * @brief Get the stack count for an item based on its quality.
 *
 * @param proto The item prototype.
 * @return uint32 The stack count.
 */
uint32 Category::GetStackCount(ItemPrototype const* proto)
{
    if (proto->Quality > ITEM_QUALITY_UNCOMMON)
    {
        return 1;
    }

    return urand(1, proto->GetMaxStackSize());
}

/**
 * @brief Get the maximum allowed auction count for a specific item.
 *
 * @param proto The item prototype.
 * @return uint32 The maximum allowed auction count.
 */
uint32 Category::GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
{
    return 0;
}

/**
 * @brief Get the maximum allowed auction count for the category.
 *
 * @return uint32 The maximum allowed auction count.
 */
uint32 Category::GetMaxAllowedAuctionCount()
{
    return sAhBotConfig.GetMaxAllowedAuctionCount(GetName());
}

/**
 * @brief Get the pricing strategy for the category.
 *
 * @return PricingStrategy* The pricing strategy.
 */
PricingStrategy* Category::GetPricingStrategy()
{
    if (pricingStrategy)
    {
        return pricingStrategy;
    }

    ostringstream out; out << "AhBot.PricingStrategy." << GetName();
    string name = sAhBotConfig.GetStringDefault(out.str().c_str(), "default");
    return pricingStrategy = PricingStrategyFactory::Create(name, this);
}

/**
 * @brief Construct a new QualityCategoryWrapper object.
 *
 * @param category The base category.
 * @param quality The item quality.
 */
QualityCategoryWrapper::QualityCategoryWrapper(Category* category, uint32 quality) : Category(), quality(quality), category(category)
{
    ostringstream out; out << category->GetName() << ".";
    switch (quality)
    {
    case ITEM_QUALITY_POOR:
        out << "gray";
        break;
    case ITEM_QUALITY_NORMAL:
        out << "white";
        break;
    case ITEM_QUALITY_UNCOMMON:
        out << "green";
        break;
    case ITEM_QUALITY_RARE:
        out << "blue";
        break;
    default:
        out << "epic";
        break;
    }

    combinedName = out.str();
}

/**
 * @brief Check if the category contains the specified item.
 *
 * @param proto The item prototype.
 * @return true If the category contains the item.
 * @return false Otherwise.
 */
bool QualityCategoryWrapper::Contains(ItemPrototype const* proto)
{
    return proto->Quality == quality && category->Contains(proto);
}

/**
 * @brief Get the maximum allowed auction count for the quality category.
 *
 * @return uint32 The maximum allowed auction count.
 */
uint32 QualityCategoryWrapper::GetMaxAllowedAuctionCount()
{
    uint32 count = sAhBotConfig.GetMaxAllowedAuctionCount(combinedName);
    return count > 0 ? count : category->GetMaxAllowedAuctionCount();
}

/**
 * @brief Get the maximum allowed auction count for a specific item in the quality category.
 *
 * @param proto The item prototype.
 * @return uint32 The maximum allowed auction count.
 */
uint32 QualityCategoryWrapper::GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
{
    return category->GetMaxAllowedItemAuctionCount(proto);
}

/**
 * @brief Check if the trade skill category contains the specified item.
 *
 * @param proto The item prototype.
 * @return true If the trade skill category contains the item.
 * @return false Otherwise.
 */
bool TradeSkill::Contains(ItemPrototype const* proto)
{
    if (!Trade::Contains(proto))
    {
        return false;
    }

    for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
    {
        SkillLineAbilityEntry const* skillLine = sSkillLineAbilityStore.LookupEntry(j);
        if (!skillLine || skillLine->skillId != skill)
        {
            continue;
        }

        if (IsCraftedBy(proto, skillLine->spellId))
        {
            return true;
        }
    }

    for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
    {
        CreatureInfo const* co = sCreatureStorage.LookupEntry<CreatureInfo>(id);
        if (!co || co->TrainerType != TRAINER_TYPE_TRADESKILLS)
        {
            continue;
        }

        uint32 trainerId = co->TrainerTemplateId;
        if (!trainerId)
        {
            trainerId = co->Entry;
        }

        TrainerSpellData const* trainer_spells = sObjectMgr.GetNpcTrainerTemplateSpells(trainerId);
        if (!trainer_spells)
        {
            trainer_spells = sObjectMgr.GetNpcTrainerSpells(trainerId);
        }

        if (!trainer_spells)
        {
            continue;
        }

        for (TrainerSpellMap::const_iterator itr = trainer_spells->spellList.begin(); itr != trainer_spells->spellList.end(); ++itr)
        {
            TrainerSpell const* tSpell = &itr->second;

            if (!tSpell || tSpell->reqSkill != skill)
            {
                continue;
            }

            if (IsCraftedBy(proto, tSpell->spell))
            {
                return true;
            }
        }
    }

    for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
    {
        ItemPrototype const* recipe = sItemStorage.LookupEntry<ItemPrototype>(itemId);
        if (!recipe)
        {
            continue;
        }

        if (recipe->Class == ITEM_CLASS_RECIPE && (
            (recipe->SubClass == ITEM_SUBCLASS_LEATHERWORKING_PATTERN && skill == SKILL_LEATHERWORKING) ||
            (recipe->SubClass == ITEM_SUBCLASS_TAILORING_PATTERN && skill == SKILL_TAILORING) ||
            (recipe->SubClass == ITEM_SUBCLASS_ENGINEERING_SCHEMATIC && skill == SKILL_ENGINEERING) ||
            (recipe->SubClass == ITEM_SUBCLASS_BLACKSMITHING && skill == SKILL_BLACKSMITHING) ||
            (recipe->SubClass == ITEM_SUBCLASS_COOKING_RECIPE && skill == SKILL_COOKING) ||
            (recipe->SubClass == ITEM_SUBCLASS_ALCHEMY_RECIPE && skill == SKILL_ALCHEMY) ||
            (recipe->SubClass == ITEM_SUBCLASS_FIRST_AID_MANUAL && skill == SKILL_FIRST_AID) ||
            (recipe->SubClass == ITEM_SUBCLASS_ENCHANTING_FORMULA && skill == SKILL_ENCHANTING) ||
            (recipe->SubClass == ITEM_SUBCLASS_FISHING_MANUAL && skill == SKILL_FISHING)
            ))
        {
            for (uint32 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                if (IsCraftedBy(proto, recipe->Spells[i].SpellId))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

/**
 * @brief Check if the item is crafted by the specified spell.
 *
 * @param proto The item prototype.
 * @param spellId The spell ID.
 * @return true If the item is crafted by the spell.
 * @return false Otherwise.
 */
bool TradeSkill::IsCraftedBySpell(ItemPrototype const* proto, uint32 spellId)
{
    SpellEntry const *entry = sSpellStore.LookupEntry(spellId);
    if (!entry)
    {
        return false;
    }

    for (uint32 x = 0; x < MAX_SPELL_REAGENTS; ++x)
    {
        if (entry->Reagent[x] <= 0)
        {
            continue;
        }

        if (proto->ItemId == entry->Reagent[x])
        {
            sLog.outDetail("%s is crafted by %s", proto->Name1, entry->SpellName[0]);
            return true;
        }
    }

    return false;
}

/**
 * @brief Check if the item is crafted by the specified spell or any of its triggered spells.
 *
 * @param proto The item prototype.
 * @param spellId The spell ID.
 * @return true If the item is crafted by the spell or its triggered spells.
 * @return false Otherwise.
 */
bool TradeSkill::IsCraftedBy(ItemPrototype const* proto, uint32 spellId)
{
    if (IsCraftedBySpell(proto, spellId))
    {
        return true;
    }

    SpellEntry const *entry = sSpellStore.LookupEntry(spellId);
    if (!entry)
    {
        return false;
    }

    for (uint32 effect = EFFECT_INDEX_0; effect < MAX_EFFECT_INDEX; ++effect)
    {
        uint32 craftId = entry->EffectTriggerSpell[effect];
        SpellEntry const *craft = sSpellStore.LookupEntry(craftId);
        if (!craft)
        {
            continue;
        }

        for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            uint32 itemId = craft->Reagent[i];
            if (itemId == proto->ItemId)
            {
                sLog.outDetail("%s is crafted by %s", proto->Name1, craft->SpellName[0]);
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Get the name of the trade skill.
 *
 * @return string The name of the trade skill.
 */
string TradeSkill::GetName()
{
    switch (skill)
    {
    case SKILL_TAILORING:
        return "tailoring";
    case SKILL_LEATHERWORKING:
        return "leatherworking";
    case SKILL_ENGINEERING:
        return "engineering";
    case SKILL_BLACKSMITHING:
        return "blacksmithing";
    case SKILL_ALCHEMY:
        return "alchemy";
    case SKILL_COOKING:
        return "cooking";
    case SKILL_FISHING:
        return "fishing";
    case SKILL_ENCHANTING:
        return "enchanting";
    case SKILL_MINING:
        return "mining";
    case SKILL_SKINNING:
        return "skinning";
    case SKILL_HERBALISM:
        return "herbalism";
    case SKILL_FIRST_AID:
        return "firstaid";
    default:
        return "unknown"; // Add a default return value
    }
}

/**
 * @brief Get the label for the trade skill.
 *
 * @return string The label for the trade skill.
 */
string TradeSkill::GetLabel()
{
    switch (skill)
    {
    case SKILL_TAILORING:
        return "tailoring materials";
    case SKILL_LEATHERWORKING:
    case SKILL_SKINNING:
        return "leather and hides";
    case SKILL_ENGINEERING:
        return "engineering materials";
    case SKILL_BLACKSMITHING:
        return "blacksmithing materials";
    case SKILL_ALCHEMY:
    case SKILL_HERBALISM:
        return "herbs";
    case SKILL_COOKING:
        return "fish and meat";
    case SKILL_FISHING:
        return "fish";
    case SKILL_ENCHANTING:
        return "enchants";
    case SKILL_MINING:
        return "ore and stone";
    case SKILL_FIRST_AID:
        return "first aid reagents";
    default:
        return "unknown";
    }
}
