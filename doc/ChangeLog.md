MaNGOS One Changelog
====================
This change log references the relevant changes (bug and security fixes) done
in recent versions.

0.21 (2017-01-02) - "The Battle for Outlands"
--------------------------------------------
Many Thanks to all the groups and individuals who contributed to this release.
- 210+ Commits since the previous release.

* Removed the old SD2 scripts and Added the new unified SD3 Submodule
* Removed the individual extractor projects and added a unified Extractors Submodule

* TODO: Add full list of fixes from Rel20 to 21 for both Server, Scripts and Database


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
