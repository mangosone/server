#ifndef _RandomPlayerbotMgr_H
#define _RandomPlayerbotMgr_H

#include "Common.h"
#include "PlayerbotAIBase.h"
#include "PlayerbotMgr.h"

class WorldPacket;
class Player;
class Unit;
class Object;
class Item;

using namespace std;

/**
 * @brief Manager class for handling random player bots.
 * This class provides methods to manage random player bots, including their creation, randomization, and various actions.
 */
class RandomPlayerbotMgr : public PlayerbotHolder
{
public:
    /**
     * @brief Constructor for RandomPlayerbotMgr.
     * Initializes the random player bot manager.
     */
    RandomPlayerbotMgr();

    /**
     * @brief Destructor for RandomPlayerbotMgr.
     */
    virtual ~RandomPlayerbotMgr();

    /**
     * @brief Gets the singleton instance of RandomPlayerbotMgr.
     * @return Reference to the singleton instance of RandomPlayerbotMgr.
     */
    static RandomPlayerbotMgr& instance()
    {
        static RandomPlayerbotMgr instance;
        return instance;
    }

    /**
     * @brief Updates the AI internal state.
     * @param elapsed Time elapsed since the last update.
     */
    virtual void UpdateAIInternal(uint32 elapsed);

public:
    /**
     * @brief Handles player bot console commands.
     * @param handler Pointer to the chat handler.
     * @param args The command arguments.
     * @return True if the command is handled successfully, false otherwise.
     */
    static bool HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args);

    /**
     * @brief Checks if a given player is a random bot.
     * @param bot Pointer to the player.
     * @return True if the player is a random bot, false otherwise.
     */
    bool IsRandomBot(Player* bot);

    /**
     * @brief Checks if a given player ID is a random bot.
     * @param bot The player ID.
     * @return True if the player ID is a random bot, false otherwise.
     */
    bool IsRandomBot(uint32 bot);

    /**
     * @brief Randomizes the given player bot.
     * @param bot Pointer to the player bot.
     */
    void Randomize(Player* bot);

    /**
     * @brief Randomizes the given player bot for the first time.
     * @param bot Pointer to the player bot.
     */
    void RandomizeFirst(Player* bot);

    /**
     * @brief Increases the level of the given player bot.
     * @param bot Pointer to the player bot.
     */
    void IncreaseLevel(Player* bot);

    /**
     * @brief Schedules a teleport for the given player bot.
     * @param bot The player ID.
     * @param time The time to schedule the teleport.
     */
    void ScheduleTeleport(uint32 bot, uint32 time = 0);

    /**
     * @brief Handles a command from a player.
     * @param type The type of the command.
     * @param text The text of the command.
     * @param fromPlayer The player who sent the command.
     */
    void HandleCommand(uint32 type, const string& text, Player& fromPlayer);

    /**
     * @brief Handles a remote command.
     * @param request The command request.
     * @return The result of the command.
     */
    string HandleRemoteCommand(string request);

    /**
     * @brief Handles player logout.
     * @param player Pointer to the player.
     */
    void OnPlayerLogout(Player* player);

    /**
     * @brief Handles player login.
     * @param player Pointer to the player.
     */
    void OnPlayerLogin(Player* player);

    /**
     * @brief Gets a random player.
     * @return Pointer to the random player.
     */
    Player* GetRandomPlayer();

    /**
     * @brief Prints statistics about the random player bots.
     */
    void PrintStats();

    /**
     * @brief Gets the buy multiplier for the given player bot.
     * @param bot Pointer to the player bot.
     * @return The buy multiplier.
     */
    double GetBuyMultiplier(Player* bot);

    /**
     * @brief Gets the sell multiplier for the given player bot.
     * @param bot Pointer to the player bot.
     * @return The sell multiplier.
     */
    double GetSellMultiplier(Player* bot);

    /**
     * @brief Gets the loot amount for the given player bot.
     * @param bot Pointer to the player bot.
     * @return The loot amount.
     */
    uint32 GetLootAmount(Player* bot);

    /**
     * @brief Sets the loot amount for the given player bot.
     * @param bot Pointer to the player bot.
     * @param value The loot amount.
     */
    void SetLootAmount(Player* bot, uint32 value);

    /**
     * @brief Gets the trade discount for the given player bot.
     * @param bot Pointer to the player bot.
     * @return The trade discount.
     */
    uint32 GetTradeDiscount(Player* bot);

    /**
     * @brief Refreshes the given player bot.
     * @param bot Pointer to the player bot.
     */
    void Refresh(Player* bot);

    /**
     * @brief Teleports the given player bot to a random location based on their level.
     * @param bot Pointer to the player bot.
     */
    void RandomTeleportForLevel(Player* bot);

    /**
     * @brief Teleports the given player bot to a random location.
     * @param bot Pointer to the player bot.
     * @param mapId The map ID.
     * @param teleX The X coordinate.
     * @param teleY The Y coordinate.
     * @param teleZ The Z coordinate.
     */
    void RandomTeleport(Player * bot, uint32 mapId, float teleX, float teleY, float teleZ);

    /**
     * @brief Gets the maximum allowed bot count.
     * @return The maximum allowed bot count.
     */
    int GetMaxAllowedBotCount();

    /**
     * @brief Processes the given player bot.
     * @param player Pointer to the player bot.
     * @return True if the bot is processed successfully, false otherwise.
     */
    bool ProcessBot(Player* player);

    /**
     * @brief Revives the given player bot.
     * @param player Pointer to the player bot.
     */
    void Revive(Player* player);

protected:
    /**
     * @brief Internal handler for bot login.
     * @param bot Pointer to the player bot.
     */
    virtual void OnBotLoginInternal(Player * const bot) {}

private:
    /**
     * @brief Gets the event value for a given player bot and event.
     * @param bot The player ID.
     * @param event The event name.
     * @return The event value.
     */
    uint32 GetEventValue(uint32 bot, string event);

    /**
     * @brief Sets the event value for a given player bot and event.
     * @param bot The player ID.
     * @param event The event name.
     * @param value The event value.
     * @param validIn The validity duration of the event.
     * @return The event value.
     */
    uint32 SetEventValue(uint32 bot, string event, uint32 value, uint32 validIn);

    /**
     * @brief Gets the list of random player bots.
     * @return The list of random player bots.
     */
    list<uint32> GetBots();

    /**
     * @brief Adds random player bots.
     * @return The number of random player bots added.
     */
    uint32 AddRandomBots();

    /**
     * @brief Processes the given player bot.
     * @param bot The player ID.
     * @return True if the bot is processed successfully, false otherwise.
     */
    bool ProcessBot(uint32 bot);

    /**
     * @brief Schedules randomization for the given player bot.
     * @param bot The player ID.
     * @param time The time to schedule the randomization.
     */
    void ScheduleRandomize(uint32 bot, uint32 time);

    /**
     * @brief Teleports the given player bot to a random location.
     * @param bot Pointer to the player bot.
     * @param locs The list of possible locations.
     */
    void RandomTeleport(Player* bot, vector<WorldLocation> &locs);

    /**
     * @brief Gets the level of a zone based on its coordinates.
     * @param mapId The map ID.
     * @param teleX The X coordinate.
     * @param teleY The Y coordinate.
     * @param teleZ The Z coordinate.
     * @return The level of the zone.
     */
    uint32 GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ);

private:
    vector<Player*> players; ///< List of players.
    int processTicks; ///< Number of process ticks.
    map<uint8, vector<WorldLocation> > locsPerLevelCache; ///< Cache of locations per level.
};

#define sRandomPlayerbotMgr RandomPlayerbotMgr::instance()

#endif
