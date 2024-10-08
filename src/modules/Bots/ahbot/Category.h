#pragma once
#include "Config.h"
#include "PricingStrategy.h"
#include "ItemPrototype.h"
#include "SharedDefines.h"
#include "Util.h"

using namespace std;

namespace ahbot
{
    class Category
    {
    public:
        Category() : pricingStrategy(NULL) {}
        virtual ~Category() { if (pricingStrategy) delete pricingStrategy; }

    public:
        virtual bool Contains(ItemPrototype const* proto) { return false; }
        virtual string GetName() { return "default"; }
        virtual string GetDisplayName() { return GetName(); }
        virtual string GetLabel() { return GetName(); }

        virtual uint32 GetMaxAllowedAuctionCount();
        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto);
        virtual uint32 GetStackCount(ItemPrototype const* proto);
        virtual uint32 GetSkillId() { return 0; }

        virtual PricingStrategy* GetPricingStrategy();

    private:
        PricingStrategy *pricingStrategy;
    };

    class Consumable : public Category
    {
    public:
        Consumable() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_CONSUMABLE;
        }

        virtual string GetName() { return "consumable"; }
        virtual string GetLabel() { return "Consumables"; }

        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 10;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            if (proto->Quality > ITEM_QUALITY_UNCOMMON)
            {
                return 1;
            }

            uint32 maxStackSize = proto->GetMaxStackSize();
            if (maxStackSize == 1)
            {
                return 1;
            }

            if (maxStackSize <= 10)
            {
                return urand(1, 10);
            }

            return urand(1, 4) * maxStackSize / 5;
        }
    };

    class Quest : public Category
    {
    public:
        Quest() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_QUEST;
        }
        virtual string GetName() { return "quest"; }

        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 5;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            if (proto->Quality > ITEM_QUALITY_UNCOMMON)
            {
                return 1;
            }

            uint32 maxStackSize = proto->GetMaxStackSize();
            if (proto->Quality == ITEM_QUALITY_UNCOMMON && maxStackSize > 10)
            {
                maxStackSize = urand(1, 10);
            }

            if (maxStackSize > 20)
            {
                maxStackSize = urand(1, 20);
            }

            return maxStackSize;
        }
    };

    class Trade : public Category
    {
    public:
        Trade() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
#if defined(CLASSIC)
            return proto->Class == ITEM_CLASS_TRADE_GOODS;
#else
            return proto->Class == ITEM_CLASS_TRADE_GOODS ||
                    proto->Class == ITEM_CLASS_MISC ||
                    proto->Class == ITEM_CLASS_REAGENT;
#endif
        }
        virtual string GetName() { return "trade"; }

        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 5;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            uint32 maxStack = proto->GetMaxStackSize();
            if (maxStack < 2)
            {
                return maxStack;
            }

            switch (proto->Quality)
            {
            case ITEM_QUALITY_NORMAL:
                return maxStack;
            case ITEM_QUALITY_UNCOMMON:
                return urand(1, maxStack);
            }

            return 1;
        }
    };

    class TradeSkill : public Trade
    {
    public:
        TradeSkill(uint32 skill) : Trade(), skill(skill) {}

    public:
        virtual bool Contains(ItemPrototype const* proto);
        virtual string GetName();
        virtual string GetLabel();
        virtual uint32 GetSkillId() { return skill; }

    private:
        bool IsCraftedBySpell(ItemPrototype const* proto, uint32 spellId);
        bool IsCraftedBy(ItemPrototype const* proto, uint32 craftId);
        uint32 skill;
    };

    class Reagent : public Category
    {
    public:
        Reagent() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_REAGENT && proto->ItemLevel > 1;
        }
        virtual string GetName() { return "reagent"; }
        virtual string GetLabel() { return "reagents"; }
    };

    class Recipe : public Category
    {
    public:
        Recipe() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_RECIPE && proto->ItemLevel > 1;
        }
        virtual string GetName() { return "recipe"; }
        virtual string GetLabel() { return "recipes and patterns"; }
        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 1;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            return 1;
        }
    };

    class Equip : public Category
    {
    public:
        Equip() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return (proto->Class == ITEM_CLASS_WEAPON ||
                proto->Class == ITEM_CLASS_ARMOR) && proto->ItemLevel > 1;
        }
        virtual string GetName() { return "equip"; }
        virtual string GetLabel() { return "armor and weapons"; }
        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 1;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            return 1;
        }
    };

    class Other : public Category
    {
    public:
        Other() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Quality > ITEM_QUALITY_POOR && (
                proto->Class == ITEM_CLASS_MISC) && proto->ItemLevel > 1;
        }
        virtual string GetName() { return "other"; }

        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 1;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            return 1;
        }
    };

    class Quiver : public Category
    {
    public:
        Quiver() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_QUIVER && proto->ItemLevel > 1;
        }

        virtual string GetName() { return "quiver"; }
        virtual string GetLabel() { return "quivers and ammo poaches"; }

        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 1;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            return 1;
        }
    };

    class Projectile : public Category
    {
    public:
        Projectile() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_PROJECTILE;
        }

        virtual string GetName() { return "projectile"; }
        virtual string GetLabel() { return "projectiles"; }

        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 5;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            return proto->GetMaxStackSize();
        }
    };

    class Container : public Category
    {
    public:
        Container() : Category() {}

    public:
        virtual bool Contains(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_CONTAINER && proto->ItemLevel > 1;
        }

        virtual string GetName() { return "container"; }
        virtual string GetLabel() { return "containers"; }

        virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto)
        {
            return 1;
        }

        virtual uint32 GetStackCount(ItemPrototype const* proto)
        {
            return 1;
        }
    };

    class QualityCategoryWrapper : public Category
    {
        public:
            QualityCategoryWrapper(Category* category, uint32 quality);

        public:
            virtual bool Contains(ItemPrototype const* proto);
            virtual uint32 GetMaxAllowedAuctionCount();
            virtual string GetName() { return category->GetName(); }
            virtual string GetDisplayName() { return combinedName; }
            virtual string GetLabel() { return category->GetLabel(); }
            virtual uint32 GetMaxAllowedItemAuctionCount(ItemPrototype const* proto);
            virtual uint32 GetStackCount(ItemPrototype const* proto) { return category->GetStackCount(proto); }
            virtual PricingStrategy* GetPricingStrategy() { return category->GetPricingStrategy(); }
            virtual uint32 GetSkillId() { return category->GetSkillId(); }

        private:
            uint32 quality;
            Category* category;
            string combinedName;
    };
};
