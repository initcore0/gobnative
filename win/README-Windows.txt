gobnative for Windows
=====================

gobnative plays the CD versions of Gobliiins, Gobliins 2: The Prince
Buffoon and Goblins Quest 3 (Coktel Vision), as sold by GOG. No game files
are included: you need your own copy of the games.

Requirements: 64-bit Windows 10 (version 1903 or later) or Windows 11, and
a graphics driver with OpenGL 3.2 (any GPU of the last decade).

Start
-----

Unpack the zip anywhere and double-click gobnative.exe. Keep SDL2.dll next
to it.

Without a game it opens the game library. Download the games' offline
installers from GOG (setup_gobliiins_...exe, setup_gobliins_2_...exe,
setup_goblins_quest_3_...exe; they are only read, never run), click a
game's tile and choose its installer, or drop the installer onto the
window. The game is installed into

    %APPDATA%\gobnative\Games\<game>

and its tile starts it. "Add existing game folder..." plays a game folder
you already have (for example GOG's own installation folder, the one with
INTRO.STK). Settings, saves, screenshots and HD packs are kept per game in
%APPDATA%\gobnative\<game>\.

From a command prompt:

    gobnative.exe "C:\GOG Games\Gobliins 2"     play the game in that folder
    gobnative.exe --help                        the options
    gobinstall.exe setup_gobliins_2_...exe D:\Games\Gobliins2
                                                install a game from its installer

In the game, the backquote key (`, left of 1) opens the overlay with the
settings, filters, game speed, level select, quick save and the tools.

The other programs (console tools, see README.md): gobrun (headless
runner), gobview (resource viewer), gobexport (HD pack export), gobvideo
(Goblins Quest 3 videos), gobdis (script disassembler), stktool and
resdump (archives and resources).

Licence
-------

gobnative is free software under the GNU General Public License, version 3
or later (LICENSE.txt); its source code is at the project's repository.
It includes SDL2 (zlib licence), Dear ImGui (MIT) and dr_mp3 (public domain
or MIT-0); their licences are in the licences folder.
