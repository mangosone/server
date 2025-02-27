#pragma once
#include "../../LootObjectStack.h"
#include "../Value.h"

namespace ai
{
    /**
     * @brief Class representing a value for loot strategy.
     * Inherits from ManualSetValue with a pointer to LootStrategy.
     */
    class LootStrategyValue : public ManualSetValue<LootStrategy*>
    {
    public:
        /**
         * @brief Construct a new Loot Strategy Value object
         *
         * @param ai Pointer to the PlayerbotAI object.
         */
        LootStrategyValue(PlayerbotAI* ai) : ManualSetValue<LootStrategy*>(ai, normal) {}

        /**
         * @brief Destroy the Loot Strategy Value object
         * Deletes the default value.
         */
        virtual ~LootStrategyValue() { delete defaultValue; }

        // Static members representing different loot strategies
        static LootStrategy *normal, *gray, *all, *disenchant;

        /**
         * @brief Get an instance of LootStrategy by name.
         *
         * @param name Name of the loot strategy.
         * @return LootStrategy* Pointer to the loot strategy instance.
         */
        static LootStrategy* instance(string name);
    };
}
