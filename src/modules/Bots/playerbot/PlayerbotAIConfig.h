#pragma once

#include "Config.h"

class Player;
class PlayerbotMgr;
class ChatHandler;

/**
 * @brief Configuration class for Playerbot AI.
 * This class holds all the configuration parameters for the Playerbot AI and provides methods to initialize and access these parameters.
 */
class PlayerbotAIConfig
{
public:
    /**
     * @brief Constructor for PlayerbotAIConfig.
     * Initializes all configuration parameters with default values.
     */
    PlayerbotAIConfig();

    /**
     * @brief Gets the singleton instance of PlayerbotAIConfig.
     * @return Reference to the singleton instance of PlayerbotAIConfig.
     */
    static PlayerbotAIConfig& instance()
    {
        static PlayerbotAIConfig instance;
        return instance;
    }

public:
    /**
     * @brief Initializes the Playerbot AI configuration by reading from the configuration file.
     * @return True if initialization is successful, false otherwise.
     */
    bool Initialize();

    /**
     * @brief Checks if a given account ID is in the random bot account list.
     * @param id The account ID to check.
     * @return True if the account ID is in the list, false otherwise.
     */
    bool IsInRandomAccountList(uint32 id);

    /**
     * @brief Checks if a given item ID is in the random bot quest item list.
     * @param id The item ID to check.
     * @return True if the item ID is in the list, false otherwise.
     */
    bool IsInRandomQuestItemList(uint32 id);

    bool enabled; ///< Indicates if the Playerbot AI is enabled.
    bool allowGuildBots; ///< Indicates if guild bots are allowed.
    uint32 globalCoolDown, reactDelay, maxWaitForMove, expireActionTime, dispelAuraDuration, passiveDelay, repeatDelay;
    float sightDistance, spellDistance, reactDistance, grindDistance, lootDistance, shootDistance,
        fleeDistance, tooCloseDistance, meleeDistance, followDistance, whisperDistance, contactDistance,
        aoeRadius;
    uint32 criticalHealth, lowHealth, mediumHealth, almostFullHealth;
    uint32 lowMana, mediumMana;

    uint32 openGoSpell; ///< The spell ID for opening game objects.
    bool randomBotAutologin; ///< Indicates if random bots should auto-login.
    bool botAutologin; ///< Indicates if bots should auto-login.
    std::string randomBotMapsAsString; ///< Comma-separated string of random bot maps.
    std::vector<uint32> randomBotMaps; ///< List of random bot maps.
    std::list<uint32> randomBotQuestItems; ///< List of random bot quest items.
    std::list<uint32> randomBotAccounts; ///< List of random bot accounts.
    std::list<uint32> randomBotSpellIds; ///< List of random bot spell IDs.
    uint32 randomBotTeleportDistance; ///< The teleport distance for random bots.
    float randomGearLoweringChance; ///< The chance of lowering gear for random bots.
    float randomBotMaxLevelChance; ///< The chance of random bots reaching max level.
    uint32 minRandomBots, maxRandomBots;
    uint32 randomBotUpdateInterval, randomBotCountChangeMinInterval, randomBotCountChangeMaxInterval;
    uint32 minRandomBotInWorldTime, maxRandomBotInWorldTime;
    uint32 minRandomBotRandomizeTime, maxRandomBotRandomizeTime;
    uint32 minRandomBotReviveTime, maxRandomBotReviveTime;
    uint32 minRandomBotPvpTime, maxRandomBotPvpTime;
    uint32 minRandomBotsPerInterval, maxRandomBotsPerInterval;
    uint32 minRandomBotsPriceChangeInterval, maxRandomBotsPriceChangeInterval;
    bool randomBotJoinLfg; ///< Indicates if random bots should join Looking For Group.
    bool randomBotLoginAtStartup; ///< Indicates if random bots should login at startup.
    uint32 randomBotTeleLevel; ///< The teleport level for random bots.
    bool logInGroupOnly, logValuesPerTick;
    bool fleeingEnabled; ///< Indicates if fleeing is enabled for bots.
    std::string combatStrategies, nonCombatStrategies;
    std::string randomBotCombatStrategies, randomBotNonCombatStrategies;
    uint32 randomBotMinLevel, randomBotMaxLevel;
    float randomChangeMultiplier;
    uint32 specProbability[MAX_CLASSES][3]; ///< Probability of class specs for random bots.
    std::string commandPrefix; ///< Prefix for bot commands.
    std::string randomBotAccountPrefix; ///< Prefix for random bot accounts.
    uint32 randomBotAccountCount; ///< Number of random bot accounts.
    bool deleteRandomBotAccounts; ///< Indicates if random bot accounts should be deleted.
    uint32 randomBotGuildCount; ///< Number of random bot guilds.
    bool deleteRandomBotGuilds; ///< Indicates if random bot guilds should be deleted.
    std::list<uint32> randomBotGuilds; ///< List of random bot guilds.
    bool randomBotShowHelmet; ///< Indicates if random bots should show helmets.
    bool randomBotShowCloak; ///< Indicates if random bots should show cloaks.
    bool enableGreet; ///< Indicates if greeting is enabled for bots.

    bool guildTaskEnabled; ///< Indicates if guild tasks are enabled.
    uint32 minGuildTaskChangeTime, maxGuildTaskChangeTime;
    uint32 minGuildTaskAdvertisementTime, maxGuildTaskAdvertisementTime;
    uint32 minGuildTaskRewardTime, maxGuildTaskRewardTime;
    uint32 guildTaskAdvertCleanupTime;

    uint32 iterationsPerTick; ///< Number of iterations per tick.

    int commandServerPort; ///< Port for the command server.

    /**
     * @brief Gets the value of a configuration parameter by name.
     * @param name The name of the configuration parameter.
     * @return The value of the configuration parameter as a string.
     */
    std::string GetValue(std::string name) const;

    /**
     * @brief Sets the value of a configuration parameter by name.
     * @param name The name of the configuration parameter.
     * @param value The value to set the configuration parameter to.
     */
    void SetValue(std::string &name, std::string value);

private:
    Config config; ///< Configuration object for reading from the configuration file.
};

#define sPlayerbotAIConfig MaNGOS::Singleton<PlayerbotAIConfig>::Instance()
