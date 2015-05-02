MaNGOS One Changelog
====================
This change log references the relevant changes (bug and security fixes) done
in recent versions.

0.20 (2015-02-31) - Points of departure
---------------------------------------
Many Thanks to all the groups and individuals who contributed to this release.

* Some of the dependant file groups have been made into submodules
* i.e. all the dependant libraries (dep folder) and realmd

		

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
