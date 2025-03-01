#include "Config/Config.h"
#include "../botpch.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "AccountMgr.h"
#include "ObjectMgr.h"
#include "DatabaseEnv.h"
#include "PlayerbotAI.h"
#include "Player.h"
#include "RandomPlayerbotFactory.h"
#include "SystemConfig.h"

/**
 * A static map that stores the available races for each class.
 *
 * The key is a uint8 representing the class ID (e.g., CLASS_WARRIOR, CLASS_PALADIN).
 * The value is a vector of uint8 representing the race IDs (e.g., RACE_HUMAN, RACE_ORC)
 * that are available for that class.
 *
 * This map is initialized in the constructor of RandomPlayerbotFactory and is used to
 * determine the possible races when creating a random bot for a specific class.
 */
map<uint8, vector<uint8> > RandomPlayerbotFactory::availableRaces;

/**
 * Constructor for RandomPlayerbotFactory.
 * Initializes the available races for each class.
 * @param accountId The account ID for the random bot.
 */
RandomPlayerbotFactory::RandomPlayerbotFactory(uint32 accountId) : accountId(accountId)
{
    availableRaces[CLASS_WARRIOR].push_back(RACE_HUMAN);
    availableRaces[CLASS_WARRIOR].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_WARRIOR].push_back(RACE_GNOME);
    availableRaces[CLASS_WARRIOR].push_back(RACE_DWARF);
    availableRaces[CLASS_WARRIOR].push_back(RACE_ORC);
    availableRaces[CLASS_WARRIOR].push_back(RACE_UNDEAD);
    availableRaces[CLASS_WARRIOR].push_back(RACE_TAUREN);
    availableRaces[CLASS_WARRIOR].push_back(RACE_TROLL);
#if !defined(CLASSIC)
    availableRaces[CLASS_WARRIOR].push_back(RACE_DRAENEI);
#endif

    availableRaces[CLASS_PALADIN].push_back(RACE_HUMAN);
    availableRaces[CLASS_PALADIN].push_back(RACE_DWARF);
#if !defined(CLASSIC)
    availableRaces[CLASS_PALADIN].push_back(RACE_DRAENEI);
    availableRaces[CLASS_PALADIN].push_back(RACE_BLOODELF);
#endif

    availableRaces[CLASS_ROGUE].push_back(RACE_HUMAN);
    availableRaces[CLASS_ROGUE].push_back(RACE_DWARF);
    availableRaces[CLASS_ROGUE].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_ROGUE].push_back(RACE_GNOME);
    availableRaces[CLASS_ROGUE].push_back(RACE_ORC);
    availableRaces[CLASS_ROGUE].push_back(RACE_TROLL);
#if !defined(CLASSIC)
    availableRaces[CLASS_ROGUE].push_back(RACE_BLOODELF);
#endif
    availableRaces[CLASS_PRIEST].push_back(RACE_HUMAN);
    availableRaces[CLASS_PRIEST].push_back(RACE_DWARF);
    availableRaces[CLASS_PRIEST].push_back(RACE_NIGHTELF);
#if !defined(CLASSIC)
    availableRaces[CLASS_PRIEST].push_back(RACE_DRAENEI);
#endif
    availableRaces[CLASS_PRIEST].push_back(RACE_TROLL);
    availableRaces[CLASS_PRIEST].push_back(RACE_UNDEAD);
#if !defined(CLASSIC)
    availableRaces[CLASS_PRIEST].push_back(RACE_DRAENEI);
    availableRaces[CLASS_PRIEST].push_back(RACE_BLOODELF);
#endif
    availableRaces[CLASS_MAGE].push_back(RACE_HUMAN);
    availableRaces[CLASS_MAGE].push_back(RACE_GNOME);
#if !defined(CLASSIC)
    availableRaces[CLASS_MAGE].push_back(RACE_DRAENEI);
#endif
    availableRaces[CLASS_MAGE].push_back(RACE_UNDEAD);
    availableRaces[CLASS_MAGE].push_back(RACE_TROLL);
#if !defined(CLASSIC)
    availableRaces[CLASS_MAGE].push_back(RACE_DRAENEI);
    availableRaces[CLASS_MAGE].push_back(RACE_BLOODELF);
#endif

    availableRaces[CLASS_WARLOCK].push_back(RACE_HUMAN);
    availableRaces[CLASS_WARLOCK].push_back(RACE_GNOME);
    availableRaces[CLASS_WARLOCK].push_back(RACE_UNDEAD);
    availableRaces[CLASS_WARLOCK].push_back(RACE_ORC);
#if !defined(CLASSIC)
    availableRaces[CLASS_WARLOCK].push_back(RACE_BLOODELF);
#endif

#if !defined(CLASSIC)
    availableRaces[CLASS_SHAMAN].push_back(RACE_DRAENEI);
#endif
    availableRaces[CLASS_SHAMAN].push_back(RACE_ORC);
    availableRaces[CLASS_SHAMAN].push_back(RACE_TAUREN);
    availableRaces[CLASS_SHAMAN].push_back(RACE_TROLL);
#if !defined(CLASSIC)
    availableRaces[CLASS_SHAMAN].push_back(RACE_DRAENEI);
#endif
    availableRaces[CLASS_HUNTER].push_back(RACE_DWARF);
    availableRaces[CLASS_HUNTER].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_HUNTER].push_back(RACE_ORC);
    availableRaces[CLASS_HUNTER].push_back(RACE_TAUREN);
    availableRaces[CLASS_HUNTER].push_back(RACE_TROLL);
#if !defined(CLASSIC)
    availableRaces[CLASS_HUNTER].push_back(RACE_DRAENEI);
    availableRaces[CLASS_HUNTER].push_back(RACE_BLOODELF);
#endif

    availableRaces[CLASS_DRUID].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_DRUID].push_back(RACE_TAUREN);
}

/**
 * Creates a random bot for the specified class.
 * @param cls The class of the bot to create.
 * @return True if the bot was created successfully, false otherwise.
 */
bool RandomPlayerbotFactory::CreateRandomBot(uint8 cls)
{
    sLog.outDetail("Creating new random bot for class %d", cls);

    uint8 gender = rand() % 2 ? GENDER_MALE : GENDER_FEMALE;

    uint8 race = availableRaces[cls][urand(0, availableRaces[cls].size() - 1)];
    string name = CreateRandomBotName(gender);
    if (name.empty())
    {
        return false;
    }

    uint8 skin = urand(0, 7);
    uint8 face = urand(0, 7);
    uint8 hairStyle = urand(0, 7);
    uint8 hairColor = urand(0, 7);
    uint8 facialHair = urand(0, 7);
    uint8 outfitId = 0;

#if !defined(CLASSIC)
    WorldSession* session = new WorldSession(accountId, NULL, SEC_PLAYER, MAX_EXPANSION, 0, LOCALE_enUS);
#else
    WorldSession* session = new WorldSession(accountId, NULL, SEC_PLAYER, 0, LOCALE_enUS);
#endif
    if (!session)
    {
        sLog.outError("Couldn't create session for random bot account %d", accountId);
        delete session;
        return false;
    }

    Player *player = new Player(session);
    if (!player->Create(sObjectMgr.GeneratePlayerLowGuid(), name, race, cls, gender, skin, face, hairStyle, hairColor, facialHair, outfitId))
    {
        player->DeleteFromDB(player->GetObjectGuid(), accountId, true, true);
        delete session;
        delete player;
        sLog.outError("Unable to create random bot for account %d - name: \"%s\"; race: %u; class: %u; gender: %u; skin: %u; face: %u; hairStyle: %u; hairColor: %u; facialHair: %u; outfitId: %u",
            accountId, name.c_str(), race, cls, gender, skin, face, hairStyle, hairColor, facialHair, outfitId);
        return false;
    }

    player->setCinematic(2);
    player->SetAtLoginFlag(AT_LOGIN_NONE);
    player->SaveToDB();

    sLog.outDetail("Random bot created for account %d - name: \"%s\"; race: %u; class: %u; gender: %u; skin: %u; face: %u; hairStyle: %u; hairColor: %u; facialHair: %u; outfitId: %u",
        accountId, name.c_str(), race, cls, gender, skin, face, hairStyle, hairColor, facialHair, outfitId);

    return true;
}

/**
 * Generates a random name for a bot.
 *
 * This function queries the database to find a random name from the `ai_playerbot_names` table
 * that is not already used by a character in the `characters` table. It ensures that the generated
 * name is unique and available for use.
 *
 * @return A randomly generated bot name, or an empty string if no names are available.
 */
string RandomPlayerbotFactory::CreateRandomBotName(uint8 gender)
{
    // Query the database to get the maximum name_id from the ai_playerbot_names table
    QueryResult *result = CharacterDatabase.Query("SELECT MAX(`name_id`) FROM `ai_playerbot_names`");
    if (!result)
    {
        sLog.outError("No more names left for random bots");
        return "";
    }

    // Fetch the result and get the maximum name_id
    Field *fields = result->Fetch();
    uint32 maxId = fields[0].GetUInt32();
    delete result;

    // Query the database to get a random name that is not already used by a character
    QueryResult* result2 = CharacterDatabase.PQuery("SELECT `n`.`name` FROM `ai_playerbot_names` n "
            "LEFT OUTER JOIN `characters` e ON `e`.`name` = `n`.`name` "
            "WHERE `e`.`guid` IS NULL and `n`.`gender` = '%u' order by rand() limit 1", gender);
    if (!result2)
    {
        sLog.outError("No more names left for random bots for gender: '%u'", gender);
        return "";
    }

    fields = result2->Fetch();
    string name = fields[0].GetString();
    delete result2;

    // Return the generated name
    return name;
}

/**
 * Creates random bots for the configured number of accounts.
 */
void RandomPlayerbotFactory::CreateRandomBots()
{
    if (sPlayerbotAIConfig.deleteRandomBotAccounts)
    {
        sLog.outString("Deleting random bot accounts...");
        QueryResult* results = LoginDatabase.PQuery("SELECT `id` FROM `account` where `username` like '%s%'", sPlayerbotAIConfig.randomBotAccountPrefix.c_str());
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                sAccountMgr.DeleteAccount(fields[0].GetUInt32());
            } while (results->NextRow());
            delete results;
        }

        CharacterDatabase.Execute("DELETE FROM ai_playerbot_random_bots");
        sLog.outString("Random bot accounts deleted");
    }

    for (int accountNumber = 0; accountNumber < sPlayerbotAIConfig.randomBotAccountCount; ++accountNumber)
    {
        ostringstream out; out << sPlayerbotAIConfig.randomBotAccountPrefix << accountNumber;
        string accountName = out.str();
        QueryResult* results = LoginDatabase.PQuery("SELECT `id` FROM `account` where `username` = '%s'", accountName.c_str());
        if (results)
        {
            delete results;
            continue;
        }

        string password = "";
        for (int i = 0; i < 10; i++)
        {
            password += (char)urand('!', 'z');
        }
        sAccountMgr.CreateAccount(accountName, password);

        sLog.outDebug( "Account %s created for random bots", accountName.c_str());
    }

    LoginDatabase.PExecute("UPDATE account SET expansion = '%u' where username like '%s%%'", 1, sPlayerbotAIConfig.randomBotAccountPrefix.c_str());

    int totalRandomBotChars = 0;
    for (int accountNumber = 0; accountNumber < sPlayerbotAIConfig.randomBotAccountCount; ++accountNumber)
    {
        ostringstream out; out << sPlayerbotAIConfig.randomBotAccountPrefix << accountNumber;
        string accountName = out.str();

        QueryResult* results = LoginDatabase.PQuery("SELECT `id` FROM `account` where `username` = '%s'", accountName.c_str());
        if (!results)
        {
            continue;
        }

        Field* fields = results->Fetch();
        uint32 accountId = fields[0].GetUInt32();
        delete results;

        sPlayerbotAIConfig.randomBotAccounts.push_back(accountId);

        int count = sAccountMgr.GetCharactersCount(accountId);
        if (count >= 10)
        {
            totalRandomBotChars += count;
            continue;
        }

        RandomPlayerbotFactory factory(accountId);
        for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
        {
            if (cls != 10 && cls != 6)
            {
                factory.CreateRandomBot(cls);
            }
        }

        totalRandomBotChars += sAccountMgr.GetCharactersCount(accountId);
    }

    sLog.outString("%d random bot accounts with %d characters available", sPlayerbotAIConfig.randomBotAccounts.size(), totalRandomBotChars);
}

/**
 * Creates random guilds for the random bots.
 */
void RandomPlayerbotFactory::CreateRandomGuilds()
{
    vector<uint32> randomBots;
    QueryResult* results = LoginDatabase.PQuery("SELECT `id` FROM `account` where `username` like '%s%%'", sPlayerbotAIConfig.randomBotAccountPrefix.c_str());
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 accountId = fields[0].GetUInt32();

            QueryResult* results2 = CharacterDatabase.PQuery("SELECT `guid` FROM `characters` where `account` = '%u'", accountId);
            if (results2)
            {
                do
                {
                    Field* fields = results2->Fetch();
                    uint32 guid = fields[0].GetUInt32();
                    randomBots.push_back(guid);
                } while (results2->NextRow());
                delete results2;
            }

        } while (results->NextRow());
        delete results;
    }

    if (sPlayerbotAIConfig.deleteRandomBotGuilds)
    {
        sLog.outString("Deleting random bot guilds...");
        for (vector<uint32>::iterator i = randomBots.begin(); i != randomBots.end(); ++i)
        {
            ObjectGuid leader(HIGHGUID_PLAYER, *i);
            Guild* guild = sGuildMgr.GetGuildByLeader(leader);
            if (guild) guild->Disband();
        }
        sLog.outString("Random bot guilds deleted");
    }

    int guildNumber = 0;
    vector<ObjectGuid> availableLeaders;
    for (vector<uint32>::iterator i = randomBots.begin(); i != randomBots.end(); ++i)
    {
        ObjectGuid leader(HIGHGUID_PLAYER, *i);
        Guild* guild = sGuildMgr.GetGuildByLeader(leader);
        if (guild)
        {
            ++guildNumber;
            sPlayerbotAIConfig.randomBotGuilds.push_back(guild->GetId());
        }
        else
        {
            Player* player = sObjectMgr.GetPlayer(leader);
            if (player)
            {
                availableLeaders.push_back(leader);
            }
        }
    }

    for (; guildNumber < sPlayerbotAIConfig.randomBotGuildCount; ++guildNumber)
    {
        string guildName = CreateRandomGuildName();
        if (guildName.empty())
        {
            break;
        }

        if (availableLeaders.empty())
        {
            sLog.outError("No leaders for random guilds available");
            break;
        }

        int index = urand(0, availableLeaders.size() - 1);
        ObjectGuid leader = availableLeaders[index];
        Player* player = sObjectMgr.GetPlayer(leader);
        if (!player)
        {
            sLog.outError("Cannot find player for leader %u", leader);
            break;
        }

        Guild* guild = new Guild();
        if (!guild->Create(player, guildName))
        {
            sLog.outError("Error creating guild %s", guildName.c_str());
            break;
        }

        sGuildMgr.AddGuild(guild);
        sPlayerbotAIConfig.randomBotGuilds.push_back(guild->GetId());
    }

    sLog.outString("%d random bot guilds available", guildNumber);
}

/**
 * Creates a random name for a guild.
 * @return The generated name for the guild.
 */
string RandomPlayerbotFactory::CreateRandomGuildName()
{
    QueryResult* result = CharacterDatabase.Query("SELECT MAX(`name_id`) FROM `ai_playerbot_guild_names`");
    if (!result)
    {
        // Log an error and return an empty string if no names are left
        sLog.outError("No more names left for random guilds");
        return "";
    }

    Field *fields = result->Fetch();
    uint32 maxId = fields[0].GetUInt32();
    delete result;

    uint32 id = urand(0, maxId);
    result = CharacterDatabase.PQuery("SELECT `n`.`name` FROM `ai_playerbot_guild_names` n "
            "LEFT OUTER JOIN guild e ON e.name = n.name "
            "WHERE e.guildid IS NULL AND n.name_id >= '%u' LIMIT 1", id);
    if (!result)
    {
        sLog.outError("No more names left for random guilds");
        return "";
    }

    fields = result->Fetch();
    string name = fields[0].GetString();
    delete result;

    // Return the generated name
    return name;
}
