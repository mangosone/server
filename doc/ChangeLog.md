MaNGOS One Changelog
====================
This change log references the relevant changes (bug and security fixes) done
in recent versions.

0.22 (2021-01-01 to now) - "Rise up Brave Warrior"
--------------------------------------------------
* Initial Release 22 Commit


0.21 (2017-01-02 to 2021-01-01) - "The Battle for Outlands"
-----------------------------------------------------------
Many Thanks to all the groups and individuals who contributed to this release.
- 210+ Commits since the previous release.

* Removed the old SD2 scripts and Added the new unified SD3 Submodule
* Removed the individual extractor projects and added a unified Extractors Submodule

* (from cab's repo) UpdateSpeed added
* [AI] Accounting disabled LoS check while mob casting
* [Appveyor] Remove no-longer needed file
* [Build] Add OpemSSL1.0.2j installers
* [Build] Added MySQL 5.7 support
* [Build] Enhanced Build System
* [Build] Force building SOAP and PlayerBots in testing
* [Build] move core definition into cmake
* [Build] MySQL CMake Macro rewrite
* [Build] some minor cmake updates from Zero
* [Build] Update to newer build system, based on the work of H0zen
* [Build] Updated Build Number
* [Cleanup] Complete cleanup of cmake build system and Warden implementation
* [Cleanup] Fix Locale
* [Cleanup] Remove tabs which have crept into the source
* [Core] Added missing change from previous commit
* [Core] Allow dying creatures to deal damage when casting spells [cs2314]
* [Core] Allow GAMEOBJECT_TYPE_GOOBER to start DBScripts on GO Use [cz2673]
* [Core] Anger Management should only work in combat
* [Core] Chess Event: partially fix the aura of 30019. [cs2302]
* [Core] Correct sutble typo
* [Core] Fix a crash. Using format specifiers without matching values is a no-go.
* [Core] Fix compiler warning.
* [Core] Fix guid sent in SMSG_PLAYERBOUND, it should be caster's guid, not players (cs2322)
* [Core] Fix logic error introduced in [cz2670]  [cz2675]
* [Core] Fix player talents. Thanks H0zen
* [Core] Fix resource leaks in DBCFileLoader [cs2310]
* [Core] Fix spells with the target combination (TARGET_SCRIPT, TARGET_SELF). [cs2315]
* [Core] Fix stealthing animation for group members. [cs2305]
* [Core] Fixed broken mmaps
* [Core] GuidString already includes information like "Creature" [cs2313]
* [Core] Implement CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN [cs2307]
* [Core] Implement Inferno spell for Baron Geddon [cs2309]
* [Core] Implement possibility to force enable/disable mmap-usage for specific creatures. [cs2306]
* [Core] Implement spell effects 34653 and 36920. [cs2304]
* [Core] Implement SPELL_ATTR_EX5_CAN_CHANNEL_WHEN_MOVING [cs2299]
* [Core] Make use of attribute SPELL_ATTR_UNAFFECTED_BY_INVULNERABILITY [cs2300]
* [Core] Override spell range for script target spells when not provided. [cs2301]
* [Core] Skip item SPELLTRIGGER_ON_USE at form change. [cs2308]
* [Core] Trigger spell 51655 for any "Weak Alcohol" spell cast (zs2320)
* [Core] Use pet level modifiers for SPELL_EFFECT_SUMMON_PET (56) [cs2319]
* [Core][SD2]Major SD2 refactoring (see DB also)
* [DB] Set min db levels
* [DB] Update expected content revision
* [DbDocs] The Big DB documentation update
* [DBScripts] Implement SCRIPT_COMMAND_SEND_MAIL (c2639)
* [Dep] Corrected Dep submodule version
* [DEP] Fix simultaneous connection contention issue
* [Dep] Make ACE build on other OSes too
* [Dep] Update dep submodule
* [DEP] Update Stormlib v9.21
* [Dep] Updated Dep submodule
* [Deps] Dep library updated. Thanks H0zen/xfury
* [DEPS] Update zlib version to 1.2.8
* [Deps] Updated dependant library version
* [Doc] Corrected Wiki URL
* [Docs] Fix some broken links
* [Docs] Updated extraction readme
* [EasyBuild] Add support for newer MySQL/MariaDB versions
* [EasyBuild] Added EasyBuild subModule
* [EasyBuild] EasyBuild updated to V2
* [EasyBuild] Fix cmake crash on French OS
* [Easybuild] Fixed a crash. Thanks MadMaxMangos for finding.
* [Easybuild] Reactivate VS2019 support
* [EasyBuild] Updated MySQL and Cmake library locations
* [Easybuild] Updated to include VS2015 support
* [EasyBuild] Updated to Support modified build system and enhancements
* [Eluna] Adjust eluna calls
* [Eluna] Fix crash when accessing players not on any map
* [Eluna] Fix uint32_t errors (VS 2013)
* [Eluna] Remove Eluna Submodule URL
* [ELUNA] SpellAttr fixes and more #120
* [Eluna] Update Eluna
* [EXTRACTORS] Improvement made to getBuildNumber()
* [Extractors] Minor cleanup to fix some warning messages
* [Extractors] removed useless code
* [Extractors] Updated extractors to fix movement bug. Will need to reextract
* [FIX] Fixed a logic error in EasyBuild
* [Fix] Fixed up merge errors
* [Fix] non initialised variable. Thanks xfury
* [Fix] Players that weren't in group couldn't loot items
* [GM] .ticket command rework, ported from Zero
* [Install] Extended lazymangos.sh to support all cores
* [Install] Port changes from Zero
* [Linux] Fix playerbots in getmangos.sh. Thanks Tom Peters
* [LINUX] Updated script
* [Log] Partially debugged logging code, still not connected to the core Current state: player leveling works and reputation change must to, but this all needs to be tested extensively because of possible crashes. Part connecting logging to the core I do not push yet. The logging type PROGRESS is added (and mostly debugged).
* [Loot] 50% player damage to mob reuired for reward Unchanged TC backport.
* [Map] Disable access to PvP barracks for too low ranks and other faction H: Hall of Legends, A: Champion's Hall This is a crude hack. TODO: change areatrigger_teleport table to include condition ID; add condition for honor rank. More TODO: refactor whole condition system to TC-likeness.
* [Pets] The next Bestial Swiftness improvement Looks like the only issue left: the pet returns to the master at normal "follow" speed after combat.
* [PlayerBot] Initial implementation of ike3's playerbots
* [Realmd] Fixed Broken Patching system
* [Realmd] Resolve SRP6a authentication bypass issue. Thanks DevDaemon
* [realmd] Resolved authentication bypass. Thanks namreeb
* [Scripts] Allow SD3 scripted dummy and script spelleffects upon players
* [Scripts] DBScripts-Engine: Implement new commands
* [Scripts] Fix typo in boss_thermaplugg script.
* [SD2] Few missing script bindings (see DB also).Tnx to Foereaper
* [SD2] Removed deprecated SD2
* [SD2] Restoring few overlooked creature AIs
* [SD2]Sync core version to DB
* [SD2]Typing error fix (boss_jeklik) synced from Zero
* [SD3] Added initial ScriptDev3 changes
* [SD3] Correct typo
* [SD3] Fix ashara compile error on Linux. Thanks H0zen
* [SD3] Fix BRD issues and server crash. Thanks H0zen
* [SD3] Fix deeprun rat roundout crash
* [SD3] Fix error in submodule
* [SD3] fix Quest 7603 - Kroshius
* [SD3] Fix quest Kodo Roundup (#69)
* [SD3] Fix SD3::CreateInstanceData to work with non-instantiable maps
* [SD3] Fix server crash on quest 4021
* [SD3] Fix sleeping peon in durotar
* [SD3] Fix Stratholme Unforgiven spawn location
* [SD3] Fix up SD3 version
* [SD3] MC: correct spawning of Majordomo
* [SD3] Missing pointers added back
* [SD3] Naxx: Gothik - redesign
* [SD3] ScriptDev3 Updates
* [SD3] SpellAttr fixes and more #120
* [SD3] SpellAttr fixes and more for mangosOne #120
* [SD3] Step back SD3 until eluna is ready
* [SD3] Update Onyxia script
* [SD3] Update SD3 submodule
* [SD3] Update submodule
* [SD3] Update UBRS door logic
* [SD3] Updated for BRD arena fix
* [SD3] Updated TAQ
* [Spell] Stop casting non-instant spell when target is lost Rogue vanish, hunter feign death, probably invis potion are handled. Spell is interrupted w/o cooldown set (even if channeled) and mana loss. https://www.getmangos.eu/issue.php?issueid=736
* [Sync] Add Linux restart scripts from Zero
* [SYNC] full fixes from Zero
* [SYNC] Minor project sync from Zero
* [Sync] Sync Platform Defines
* [Tool] Merged vmaps extract/assembler. Updated scripts
* [TOOLS] Added unified extractor submodule
* [Tools] Corrected URL's in Extraction scripts
* [Tools] Extractor updates
* [TOOLS] Fixed mmap extractor binary name used in MoveMapGen.sh
* [TOOLS] Fixed mmap extractor binary name used in various scripts
* [Tools] Updated Unified Extractor subModule
* [Warden] Refactor to match other cores
* A few GO commands (sync to the Zero)
* a minor cleanup
* A simple combat movement scheme of silenced mob-caster
* Add ability to force a level in Creature::SelectLevel()
* Add ability to force a level in Creature::SelectLevel()
* Add change missing from previous commit
* Add check and error message to schedule_wakeup call. Thanks H0zen
* Add Codacy badge and status
* Add Core support for Franklin the Amiable / Klinfran the crazed (#118)
* Add game event hooks and update eluna version
* Add mangosd full versioning information on windows
* Add missing Eluna call
* Add missing include
* add new mangos 'family' icons. Thanks UnkleNuke for the original design
* Add possibility to write cmangos command via a whisp.
* Add realmd full versioning information on windows
* Add some additional detail to some pool error messages. Thanks H0zen
* Add state for GM command completed quests. Thanks H0zen for assistance
* Add support for new comment column
* Add support for spell 28352
* Add Ubuntu 19.04 case for Prerequisites install (#77)
* Added `disables` table
* Added AppVeyor Build Status
* Added comment on ".ticket accept", ".ticket accept on" & ".ticket accept off"
* Added line removed in error
* Added missing file from last commit
* Added some SD3 docs
* Added support for openssl on OSX systems running OpenSSL 1.0.2g (installed using homebrew). (#115)
* Adding new distribution support (Fedora) (#16)
* Adding support for Player Bots submodule in installer. (#20)
* Adding support for Ubuntu: Curl dependencies added - Adding support when several WoW clients path are detected. Only the first one is selected - Adding support for database updates. Only last folder (alphabetically sorted) will be takenxw
* Adding support of several known Linux distribution for dependancies setup (#175)
* Adds custom emote to wyrmthalak script
* Adjust the source code and build enviornment so that Mangos Zero will build on ARM32. (#79)
* Allow player demorph to DB script. Fire arrival event for the last node of player taxi path on CMSG_MOVE_SPLINE_DONE.
* Amend Core name
* Another attempt to fix crashes in BIH engine.
* Another rogue stealth tweak.
* Another world server crash fix (ported from Zero)
* Apply ACE changes from Zero
* Apply style fix
* Appveyor supplied fix for openSSL 1.0
* Arcane Missile fix removed
* Auction House Seller Option, ported from Zero
* AuctionHouse Bot fixes (#170)
* Autobroadcast should be disabled by default
* AutoBroadcast system.
* Avoid CPU- and compiler-dependent part
* backport nosttbc 404f4b1 fixes for spirit of redemption
* backport nost-tbc a8a5d84 allow mounting while stealthed
* BRD Grimm Guzzler related updates
* Bump M1 Magic Map Number
* Caretaker Dilandrus - HellFire
* Changed email return for item that can't be equiped anymore. Before the email was sent with an empty body and the subject was to long to be displayed in the player email. Now the Email is sent with the subject 'Item could not be loaded to inventory.' and the body as the subject message before. (#71)
* Channel world: disabling join/leave announces
* Chest with quest loot deactivation (2622b33)
* Clarify some issues regarding negative angles. The client seems to handle them strange.
* Clean up readme a little
* Correct spell damage taken on melee attacks.
* Correct tauren male/female scale.
* Correct Typo for default status
* Corrections to the build system.
* Correctly determine premature winner for AB, AV, EY and WS
* Create a docker container image and runing it with docker-compose (#164)
* Database revision refactor
* debug recv Command added
* Description of the meaning of the format strings added
* Disable BG: another way to do
* Disable OSX build checking until we have an OSX dev to get them fixed
* Disable spawns: base checking entry, additionally - guid Set `flags` to 1 and `data` to guid. If flags not set, all spawns of the entry are disabled.
* Disables: DISABLE_TYPE_ITEM_DROP=10. Also fixed memory leak
* Disables: item drop disable Quest loot cannot be disabled here, disable the quest instead. Not the most efficient implementation (chance is checked before disable), but working.
* Elite HP fix
* Eluna update version - Fix duplicate timer update (#57)
* Enable areatrigger teleports in battleground
* Enhance lazylinux.sh to take care of resources generation after mangos setup. (#171)
* ensure bins are marked as executable (#108)
* Finkle Einhorn is now spawned after skinning The Beast in UBRS.
* First major styling change
* Fix "crash" by ".ticket accept" (unable to handled no args) Fix .ticket info (ticket req Gamemaster+, info req Moderator+)
* Fix "Level 0" bug.
* Fix ACE_TSS usage.
* Fix AH notification before Auction sold
* Fix AHBot SetPricesOfItem (#87)
* Fix Arcane Missile self cast bug.
* Fix bsd build after 756c8ff7
* fix by H0zen - Arcane Missile self cast bug
* Fix cherry-pick error
* Fix Compile with Latest SD3 : GameObjectAI methods - and some Dire Maul things #95
* Fix crash at startUp due to command localization loading
* Fix crash in BIH module due to uninitialized member variable. (#172)
* Fix crash on taming rare creatures.
* Fix crash when using command helps (#93)
* Fix Deeprun spell used on player
* Fix displaying the right ranks of spells in spellbook
* Fix Eluna build
* Fix Feral Swiftness talent
* Fix floating point model for VS 2015 (#52)
* Fix FreeBSD build and clean spaces.
* Fix Gameobject startup errors
* Fix hunter traps.
* Fix instance cleanup at startup (#99)
* Fix last startup errors. Thank Chuck5ta for assistance
* fix linux shell script error. (#82)
* Fix logo Url
* Fix mac build
* Fix non PCH build and update Eluna (#85)
* Fix OpenSSL travis for mac
* Fix part of NPC localized text cannot be displayed.
* Fix pdump write command and add check to pdump load (#106)
* Fix player kicking
* Fix possible problem with 'allow two side interaction' and loot.
* Fix potential NullPointerException on C'Thun (#107)
* Fix previous commit.
* Fix PvPstats table to fit with its web app
* Fix quest 9433   - References https://www.getmangos.eu/forums/topic/10391-broken-quest-bug-report/   - Thanks 
* Fix quest rewards appearing twice in chat
* Fix quests 4512 & 4513
* fix reference to dockerFiles to match with real files name (#92)
* Fix removal of cast bar when triggering spells.
* Fix SD3 Build (#114)
* Fix send mail and send item commands
* Fix sending the right GAMEOBJECT_FACING field to the client.     - fixes https://www.getmangos.eu/bug-tracker/mangos-one/undercity-elevators-bug-r1341/
* Fix server crash on CONDITION_GAMEOBJECT_IN_RANGE check.
* Fix server crash on using transports.     - Fixes issue https://www.getmangos.eu/issue.php?issueid=1019
* Fix server crash when NPC pets die due to periodic damage auras.
* Fix server crash. Thank H0zen/mpfans
* Fix Simone the seductress (#121)
* Fix some codacy detected issues
* Fix some compiler warnings and project sync
* Fix some include paths in tools.
* Fix some startup (fake) errors
* Fix the crash introduced in #a5a6bf2
* Fix the network module.     - This fix must be applied on all cores.     - Solved a nasty race condition between network threads.
* fix typo in previous commit
* Fix typo in VMap BIH generation
* Fix Unit::SetConfused to work on players.
* Fix up after changes
* Fix various compilation errors that occur on linux
* Fix Warlock Drain Soul mechanic while in a party
* Fix whisper blocking
* Fix whisper not showing in sender's chat.
* Fix wrong use of uninitialized locks. Whenever ACE_XXX_Thread_Mutexes are used, there are 3 fundamental rules to obey: 1. Always make sure the lock is initialized before use; 2. Never put 2 locks each other in memory (false sharing effect); 3. Always verify that the lock is really acquired - use ACE_XXX_GUARD macros;
* FIX-CENTOS-BUILD Added epel repo
* FIX-CENTOS-BUILD Fixed centos 7 build
* Fixed memory issue with msbuild build
* Fixed OpenSSL location (#118)
* Fixes Error "There is no game at this location" (#172)
* fixes error of uninitialized member of World
* Fixing a glitch.
* Force version change
* Forgot a couple required OSX and Unix library names
* Forgot to merge some changes when porting vmap-extractor from ZERO.
* Format specifiers was not correct in lootmgr
* full block {} expected in the control structure. Part 1
* g++ was not installed without build-essential (#80)
* GM chat badge (PlayerChatTag is a bitfield)
* Gm ticket handling fixes
* Gossip Item Script support (#124)
* GroupHandler: prevent cheater self-invite
* Hai'shulud script updated.
* Hopefully make Travis happy
* Hunter pet speed (#58)
* Implement command localization
* Implement OpenSSL 1.1.x support
* Implement quest_relations table. Based on work by Hozen
* Implemented school immunity for creature from database
* Implementing CAST_FAIL_NO_LOS EventAI
* Improving Build system and removing Common.h clutter
* Initial commit
* InstantFlightPaths, ported from Zero
* Interface to a built-in bit manipulating function
* Interrupt autoshot by deselecting target
* linux/getmangos.sh: default to build client tools (#19)
* Make GM max speed customisable through mangosd.conf (#89)
* Make Mangos compatible with newer MySQL pt2. Based by work by leprasmurf
* Make some errors more verbose.
* Make Travis build functional.
* Many fixes ported from Zero
* Minor corrections to the build system.
* Minor declaration adjustment
* Minor styling tidy up
* Minor typo corrected (#184)
* Minor typo tidy up
* Misplaced SendGameObjectCustomAnim(): may be used for GOs only
* Missed one spot in previous commit.
* Missed spots.
* Missing delimiter (#88)
* More lock fixes. Also fix the .character level command
* More lock fixes. Also fix the .character level command
* More minor fixes for worldserver startup/shutdown on linux
* More robust checks on mutex acquire.     - When using ACE_xxx_Guard, the caller must ensure the internal lock is really acquired before entering the critical section (see http://www.dre.vanderbilt.edu/Doxygen/6.0.1/html/libace-doc/a00186.html#_details - Warnings paragraph)
* More SQL delimiting for modern servers (#166)
* Move DB revision struct to cpp
* Necessary include path for osx
* New thread pool reactor implementation and refactoring world daemon. (#8)
* New vmap-extractor ported from ZERO.
* Not so bright warning if DB content newer than core awaits
* Now we can inspect player when GM mode is ON (#98)
* Ogre brew only in Blade's Edge
* Ogre Brew script
* Ogre Brew script replaced
* PLAYER_EVENT_ON_LOOT_ITEM fix for eluna. Thanks mostlikey
* Port of spell_linked system from Zero
* Ported ACE/Threading changes from MangosZero
* Ported multithreaded map updater from ZERO.
* Prevent duplicate Auction Expired mails
* Q7636_P1_FIX_Solenor_the_slayer
* Re-align with added [GM] .ticket command rework
* Refactored db_scripts The unity! - Based on the original work of H0zen for Zero.
* Refactoring lazy Linux script to: (#167) [ci skip]
* Regex requires gcc 4.9 or higher
* Reload command do not announce to player accs
* Remove last reminents of obsolete npc_gossip table
* Remove obsolete code
* remove obsolete code. Thanks H0zen
* Remove Prepared solutions and add EasyBuild for Windows
* Remove Remnants of Two obsolete tables: npc_trainer_template & npc_vendor_template
* Remove unnecessary argument from a function
* Remove unused include directives
* Removed OpenSSL1.1.x blocker
* Removed SD2 database binding
* Removed SFN wildcards
* Removed unnecessary cmake macro
* Replacing aptitude by apt-get on Ubuntu by default. Added support for Red Har 'Experimental'
* Rogue Stealth Update (#62)
* SD3 fix linux compile and reference latest SD3 commit
* SendObjectDeSpawnAnim: one more misplaced method
* Server Banner and Status redone
* Server-owned world channel
* Server-side "prohibit spell school" implementation
* Several major improvements to Linux installer. (#15)
* Some fixes to make clang happier
* Spell class - Fix for inaccessible class data
* Spell target update.
* SpellAttr fixes and more for MangosOne #120
* Sql delimit (#89)
* Standard event sending, allows handling by SD3 also
* Style cleanup from the Mangos Futures Team
* Styling Cleanup
* Suppress some clang warnings and cleanup redundant directives.
* Supress Travis build on OS X until a fix is found.
* Swapped 'dbscripts_on_creature_movement' warning with 'dbscripts' â€¦ (#97)
* Switch off non-existing playerbots
* SylvainNeau (author) - Ogre Brew script
* Synchronized Conf files for easier comparison
* Tab cleanup
* The Big Command Files Reorganization. Based on the work of Elmsroth for MangosZero (#90)
* The Endless Hunger script update
* Triggered spells do not consume power - missed part
* Triggered spells do not consume power (mana, rage so on)
* Trimming Ubuntu dependencies (#17)
* Update conf file to work with modern SQL servers
* update deeprun rat roundup script
* Update dependencies and necessary changes to tools
* Update deprecated row_format_fixed
* Update Expected Base DB to Rel21_13_002
* Update getmangos.sh
* Update lazymangos.sh script to handle custom database configuration. (#169)
* Update mangosd.conf.dist.in
* Update SD3 to fix gossip scripts
* Updated ACE to latest version and fixed appveyor
* Updated copyright info.
* Updated databases version.
* Updated EasyBuild to v1.5e
* Updated Readme.md and icons
* Updated the build system
* Updated to latest SD3 master
* Updated Vmaps enhancements from Zero.
* Updates to previous commit. Includes:     - supress few clang warnings     - fix SD2 compiling     - more fixes for FreeBSD builds     - fix an include path in tools
* Updating Debian Sources (#169)
* Upgrading checks for Database::CheckDatabaseVersion (#86)
* URL update
* use canonical target names for zlib and bzip2
* Validate the spawn distance passed to RandomMovementGenerator constructor.
* Various Unit.cpp Fixes


0.20 (2015-02-31) - Points of departure
---------------------------------------
Many Thanks to all the groups and individuals who contributed to this release.

* Some of the dependant file groups have been made into submodules
* i.e. all the dependant libraries (dep folder) and realmd
* Add a configurable delay between when a creature respawns and when is can aggro due to movement [c2469]
* Add ACTION_T_SET_STAND_STATE for EventAI [s2102]
* Add check for session being NULL so that we don't crash from console commands
* Add CMake source groups to target 'shared' [c2056]
* Add core support for spell 29201. (cs2294)
* Add missing trap id for SendGameObjectCustomAnim call [c2135]
* Add new Regen Health / Power flags and rename database fields accordingly [c2128]
* Added Character dbdocs support
* Added clang support
* Added dbdocs for character DB
* Added Email address to pinfo command output
* Added Linux helper script. Thanks The-Great-Sephiroth
* Added missing bounds check in loop.
* Added missing EVENT_T_REACHED_WAYPOINT
* Added missing lines in Trainer Menu's to get deleted.
* Added missing loot for benedicts chest
* Added Realmd dbdocs support
* Added script for quest 4021 Counterattack
* Adjusted MapId, MapX and MapY variables to increase movemap extraction performance
* Allow casting a random selected spell (cs2236)
* Allow creating Non-Instance Maps without player (c2574) @kid10
* Allow GAMEOBJECT_TYPE_TRAP respawn using SCRIPT_COMMAND_RESPAWN_GAMEOBJECT (cs2253)
* Allow target 60 to use script target whenever required (c2551)
* Applied missing OutdoorPVP Commit
* Better value to check distance between owner and pet. [c2123] This problem was introduced in [c2094]
* Big rename of creature_template fields. [c2091]
* Blink improved. (cs2260) - Implement generating path
* Clarify weather packet (c2649)
* Cleanup world state sending [c2090]
* Closed memory leaks and compiler warnings.
* Code corrections to scripts (karazhan & stratholme)
* Complete redesign of waypoint system.
* Correct some BG chat message missing target name. Thank to @CamilleMoon for pointing. [s2095]
* Cuergo's Gold quest
* Demon messages cleaned up.
* Enabled SOAP in Windows BuildEverything solutions
* Extractor helper script paths fixed.
* Fix "Unknown player" bug. (cs2293)
* Fix .goname command (cs2247)
* fix .npc factionid command after creature_template change
* Fix BIH::intersectRay crash. Thanks TC
* Fix bug that cause AreaAura reaply because the code doesn't search the correct rank of it. (c2570)
* Fix cmake macros for FreeBSD systems.
* Fix conflicting typedef error in ACE Prevent assert on platforms where we cannot change thread priority [c2107]
* Fix creature flee dont loose target. Also Feared creature will loose correctly target then target will be restored. [c2108]
* Fix Double to Float conversion warnings
* Fix for missing Text in Pet Trainer Menu.
* Fix freeze if opcode.txt file does not exist on .debug send opcode [s2082]
* Fix issue with afk playerin duel logout request from client after 30 mins.
* Fix LANG_ADDON use on Guild Channels and avoid a possible crash. [c2085]
* Fix logic bug in SelectAuraRankForLevel (c2569)
* Fix one warning and suppress a few when using cmake 3.0
* Fix PostgreSQL bindings and add support to build directly with PostgreSQL instead of MySQL. [s2112]
* Fix problem with scaling vmap model. VMap and MMap DO NOT need to be rebuilt. (c2518)
* Fix raid instance reset crash and add a server command to force reset. Also added a new server command to force reset by an admin. (z2538)
* Fix reloading horde controlled capture point [c2468]
* Fix showing skirmish or rated arena queue icon [c2084]
* Fix skinning loot bug. Thanks to @TheTrueAnimal for pointing (c2219)
* Fix some more reserved identifiers
* Fix spell 9712 (c2142)
* Fix TARGET_SELF-TARGET_SCRIPT target combination Also introduce more symmetric behaviour for TARGET_FOCUS_OR_SCRIPTED_GO (cs2248)
* Fix Tauren druid size when shapeshifted. [c2480]
* Fix use of SpellBonusWithCoeffs In case of taken damage/heal calculation the information of the caster must be used (cs2243)
* Fixed architecture name.
* Fixed Copy step location
* Fixed Incorrect data structure used
* Fixed Incorrect ID for Cleansed Whipper Root in script
* Fixed up world/local channel chat to comply with tbc
* Fixing Chest Loot Issue
* Fixing Group Loot Issue
* Fixing massive spawns in Fargodeep-Mine + Set latest SQL as required
* Get rid of bounding radius in GetNearPoint[2D] and ObjectPosSelector [c2066]
* Grouped Hunter pets gain full experience.
* Home Bind Update Removed a useless function for Home Bind, where it was causing a player to return to a inn or start location, and not to their last known position before the logout.
* Implement 2 chat channel responses [s2073]
* Implement Battleground scores storage system (c2204)
* Implement CREATURE_FLAG_EXTRA_ACTIVE (c2578)
* Implement new stats system for Health and Mana. Core now suport health and mana from new stats system. [s2093]
* Implement spell effect 42281 (c2141)
* Implement support for Hunter Talent "Ferocious Inspiration"; adds infrastructure to support other, similar talent based effects in the future. (cs2242)
* Implement TARGET_RANDOM_UNIT_CHAIN_IN_AREA (c2548) Also unify the TARGET_RANDOM_CHAIN_IN_AREA code
* Improve "NPCs gets stuck in melee animation while casting". [c2110]
* Improve handling of TargetMMGen [c2068]
* Improve readability of the code and avoid visual studio warning c4996, I don't think this can cause problems with gcc [c2475]
* Improve Startup efficency & Allow recursive CanSpawn checks
* Initialize power type and power type values for creatures [s2130]
* Loot Handler fix, last revision
* Map extractor messages have been cleaned up.
* Moved enum to correct position in list
* New condition added (re condition table)
* Ogre brew only in Blade's Edge
* Partially implement movement related fixes found in noted commit
* Polymorph fix
* Pool-System: Allow pooling non-lootable gameobjects (cs2280)
* Prevent creatures from pulling too many nearby monsters, for example during certain scripted events. [s2101]
* Properly use utf8cpp and Mersennetwister.
* Readd GO scale to GO BoundingRadius calculation that was removed accidently recently (cs2257)
* Refactored aura code to make it more readable
* Remove invisibility aura (aura 18) based on attribute (c2553) Passive and negative invisibility auras are not removed on entering combat
* Remove unused argument in CalculateMeleeDamage [c2100]
* Removed an unused/doubled Option in Shaman Trainer Menu.
* Removed Creature_item functions added in error from Zero
* Removed local realmd repo and added universal realmd submodule
* Removed the tr1 namespace references in the CLANG section for the UNORDERED_MAP and UNORDERED_SET defines.
* Rename fields after recent DB changes (c2597/cs2239)
* Rename m_respawnAggroDelay to make it more generic [c2470]
* Renamed table scripted_event_id to scripted_event. [m2434]
* Reorganize code to allow rage rewarded for critter type. [s2104]
* Restore chat whisper and GM chat. [s2088]
* Restructure game folder
* Script files are now merged with the main project
* Simple fix for Stealth is removed on fall damage problem Surely could be done better way... [c2121]
* Small Update to remove unresolved tickets when deleting a character to prevent them from hanging in there when they can't ever be resolved
* SOAP bindings are now optional.
* Some changes to Random chance calculation (c2567)
* Some updates to the GM ticket system, secure one of the inputs coming from the user and mark client closed tickets as solved
* Start dbscripts_on_spell for SPELL_EFFECT_TRIGGER_MISSILE with missing spell id. (cs2284)
* Sync QuestLevel variable Type
* The Unforgiven SD2 script added
* Totally Cueergo Gold's Script updated
* Totem are immune to their own spell workaround fix
* Triage quest now doable but still nerve-racking
* Tuken'kash gong (RFD) scripted
* Update to ticket handling and hopefully a fix to logging
* Updated dbdocs definitions
* Updated HandleMoveTimeSkippedOpcode to use proper definition
* Updated Scripts to revision 2795
* Updated shutdown function to work the same as Zero
* Use shiny new function to play music with the spell (c2208)
		

0.19 (2014-07-xx) - The Genius of Tom Rus (Not released publically)
--------------------------------
Major changes for this build which require your attention when upgrading include:

Many Thanks to all the groups and individuals who contributed to this release.

* In the win folder there is a new solution "BuildEverything" which does just that.
  It builds the Core, Extraction Tools and Scripts library.
* From this release Eluna scripting has been added. Many thanks to the Eluna Team

* Added VS2013 support
Major changes for this build which require your attention when upgrading include
awesome things such as these:

* The *mangos* build system has been overhauled, and we are now using CMake
  only. For Linux and FreeBSD users this means you can *always* use packages as
  provided by your distribution, and for Windows users this means you'll now
  have to download and install dependencies just once.
  We recommend that our Windows users pick up pre-built dependencies from
  [GNUWin32](http://gnuwin32.sourceforge.net/).
* The tools for map extraction and generation from the game client are finally
  first class citizens when you build *mangos*, and will be built, too.
* The `genrevision` application has been removed from the build. Revision data
  and build information is now extracted via [Git](http://git-scm.com/) only.
* SOAP bindings for the world server are now optional, and will be disabled by
  default when building *mangos*. If you need them, there is a CMake switch
  available to enable the bindings.
* The output given by all map tools has been cleaned up, and will now give you
  useful information such as the map version, or complete usage instructions.
  Pass the `--help` parameter, and any map tool will provide usage instructions!
* Documentation has been rewritten and converted to **Markdown** format, which
  is readable and converts nicely to HTML when viewing in the repository browser.
* Documentation has been added for all map tools including usage instructions
  and examples.
* Player movement has been rewritten, and now factors in possible issues such as
  lag when sending out character movement. This also means, looting when moving
  is no longer possible, and will be canceled.
* Looting in groups has been corrected, and you should now be able to use round
  robin, master looter, free for all and need before greed looting.
* EventAI is now more verbose, and will validate targets for commands upon server
  start-up. It's very likely that you will see many more errors now. Additionally
  the `npc aiinfo` command will display more useful info.
* **ScriptDev2** has been merged into the server repository! You do not need to
  make a clone, and *may need to delete* previously checkouts of the scripts
  repository. This also means, *ScriptDev2* will now always be built when you
  build the *mangos* server.

Also numerous minor fixes and improvements have been added, such as:

* Using potions for power types not used by a class will now raise the correct
  error messages, e.g. Warriors can no longer consume Mana potions.
* Hunter pets will receive full experience when their masters are grouped.
* Mobs fleeing will do so now in normal speed, instead of crazy speed.
* The world server will now provide improved, readable output on start-up, and
  less confusing messages for identical issues.
* In-game commands `goname` and `namego` have been replaced with `appear` and
  `summon`. If you happen to find other commands with weird naming, let us know!
* We have done extensive house-keeping and removed many TBC specific code parts,
  and replaced TBC specific values with the proper vanilla WoW counterparts.
  This includes the TBC spell modifiers, which now have been dropped and are no
  longer available.
* Unprivileged player accounts will no longer be able to execute mangos dot
  commands in the in-game chat. If you need this, enable `PlayerCommands` in
  the mangosd configuration. The default setting is off.

 * The tools for map extraction and generation from the game client are finally
   first class citizens when you build *mangos-one*, and will be built, too.
 * SOAP bindings for the world server are now optional, and will be disabled by
   default when building *mangos-one*. If you need them, there is a CMake switch
   available to enable the bindings.
 * Documentation has been rewritten and converted to **Markdown** format, which
   is readable and converts nicely to HTML when viewing in the repository browser.
 * EventAI is now more verbose, and will validate targets for commands upon server
   start-up. It's very likely that you will see many more errors now. Additionally
   the `npc aiinfo` command will display more useful info.
 * Merged scripts directly to core repository.

Also numerous minor fixes and improvements have been added, such as:

 * Using potions for power types not used by a class will now raise the correct
   error messages, e.g. Warriors can no longer consume Mana potions.
 * Hunter pets will receive full experience when their masters are grouped.
 * The world server will now provide improved, readable output on start-up, and
   less confusing messages for indentical issues.
 * In-game commands `goname` and `namego` have been replaced with `appear` and
   `summon`. If you happen to find other commands with weird naming, let us know!
