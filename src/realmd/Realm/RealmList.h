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

/// \addtogroup realmd
/// @{
/// \file

#ifndef MANGOS_H_REALMLIST
#define MANGOS_H_REALMLIST

#include <ace/Singleton.h>
#include <ace/Null_Mutex.h>
#include <ace/INET_Addr.h>
#include "Common.h"

/**
 * @brief
 *
 */
struct RealmBuildInfo
{
    int build; /**< TODO */
    int major_version; /**< TODO */
    int minor_version; /**< TODO */
    int bugfix_version; /**< TODO */
    int hotfix_version; /**< TODO */
};

enum RealmVersion
{
    REALM_VERSION_VANILLA     = 0,
    REALM_VERSION_TBC         = 1,
    REALM_VERSION_WOTLK       = 2,
    REALM_VERSION_CATA        = 3,
    REALM_VERSION_MOP         = 4,
    REALM_VERSION_WOD         = 5,
    REALM_VERSION_LEGION      = 6,
    REALM_VERSION_BFA         = 7,
    REALM_VERSION_SHADOWLANDS = 8,
    REALM_VERSION_COUNT       = 9
};

/**
 * This is used to make a link between build number and actual wow version that
 * it belongs to. To get the connection between them, ie turn a build into a version
 * one would use \ref RealmList::BelongsToVersion the other way around is not available
 * as it does not make sense and isn't needed.
 */
RealmBuildInfo const* FindBuildInfo(uint16 _build);

/**
 * @brief
 *
 */
typedef std::set<uint32> RealmBuilds;

/// Storage object for a realm
/**
 * @brief
 *
 */
struct Realm
{
    std::string name;
    ACE_INET_Addr ExternalAddress;
    ACE_INET_Addr LocalAddress;
    ACE_INET_Addr LocalSubnetMask;
    uint8 icon;
    RealmFlags realmflags;                                  // realmflags
    uint8 timezone;
    uint32 m_ID;
    AccountTypes allowedSecurityLevel;                      // current allowed join security level (show as locked for not fit accounts)
    float populationLevel;
    RealmBuilds realmbuilds;                                // list of supported builds (updated in DB by mangosd)
    RealmBuildInfo realmBuildInfo;                          // build info for show version in list
};

/**
 * @brief Storage object for the list of realms on the server
 *
 */
class RealmList
{
    public:
        /**
         * @brief
         *
         */
        typedef std::map<std::string, Realm> RealmMap;
        typedef std::list<const Realm*> RealmStlList;
        typedef std::pair<RealmStlList::const_iterator, RealmStlList::const_iterator> RealmListIterators;
        typedef std::map<uint32, RealmVersion> RealmBuildVersionMap;

        static RealmList& Instance();

        RealmList();
        ~RealmList() {};

        void Initialize(uint32 updateInterval);
        /**
         * Initializes a map holding a link from build number to a version.
         * \see RealmVersion
         */
        void InitVersionToBuild();

        void UpdateIfNeed();

        /**
         * Get's the iterators for all realms supporting the given version as a pair,
         * the first member is a iterator to the begin() and the second is an iterator
         * to the end().
         * @param build the build number to fetch the iterators for
         * @return iterators to the begin() and end() part of the realms supporting
         * the given build, if there is no matching build iterators are given to end()
         * and end() of a list.
         */
        RealmListIterators GetIteratorsForBuild(uint32 build) const;

        /**
         * Returns how many realms we have available for the current build
         * @param build the build we want to know number of available realms for
         * @return the number of available realms
         */
        uint32 NumRealmsForBuild(uint32 build) const;

        /**
         * @return the total number of realms available
         * \see RealmList::NumRealmsForBuild
         */
        uint32 size() const { return m_realms.size(); };
    private:
        /**
         * Checks what version (ie, vanilla, tbc) a certain build number belongs to
         * @param build the build you want to check the version for
         * @return the corresponding version to the given build number
         */
        RealmVersion BelongsToVersion(uint32 build) const;

        /**
         * Adds entries to a map containing a link from a build number to a certain
         * wow version, ie: \ref RealmVersion::REALM_VERSION_VANILLA.
         * \see RealmVersion
         */
        void InitBuildToVersion();
        /**
         * Adds the given \ref Realm to a list sorted by version, ie: vanilla, tbc etc. This
         * in turn is used to only present the compatible realms to the clients connecting,
         * ie: vanilla clients will only see vanilla realms.
         *
         * This is controlled by what you set in the allowedbuilds field in the realm.realmlist
         * database, if you set more than one build the first one found in there will be
         * used, so if you tag a realm as this: "8606 6141" only TBC clients will be able to
         * see the realm and connect to it.
         * @param realm the realm you want to add to the sorted list, should be done for all realms
         * \see RealmVersion
         */
        void AddRealmToBuildList(const Realm& realm);

        void UpdateRealms(bool init);
        /**
         * @brief
         *
         * @param ID
         * @param name
         * @param address
         * @param port
         * @param icon
         * @param realmflags
         * @param timezone
         * @param allowedSecurityLevel
         * @param popu
         * @param builds
         */
        void UpdateRealm(uint32 ID, const std::string& name, ACE_INET_Addr const& address, ACE_INET_Addr const& localAddress, ACE_INET_Addr const& localSubnetmask, uint32 port, uint8 icon, RealmFlags realmflags, uint8 timezone, AccountTypes allowedSecurityLevel, float popu, const std::string& builds);
    private:
        RealmMap m_realms;                                    ///< Internal map of realms
        RealmStlList m_realmsByVersion[REALM_VERSION_COUNT]; ///< This sorts the realms by their supported build
        RealmBuildVersionMap m_buildToVersion;
        uint32   m_UpdateInterval;
        time_t   m_NextUpdateTime;
};

#define sRealmList RealmList::Instance()

#endif
/// @}
