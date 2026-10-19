# gobnative user guide

The full manual: building, every feature, the tools and the remaster
scripts. The project overview is in [README.md](../README.md).

A native macOS, Windows, LG webOS TV and web (WebAssembly) reimplementation of the
Coktel Vision Gob engine that plays **Gobliiins** (Goblins 1), **Gobliins 2:
The Prince Buffoon** and **Goblins Quest 3** (CD versions) in one app.
No ScummVM or DOSBox at runtime. Built with C++20 + SDL2.

The app was called gob2native while it played only Gobliins 2; the local
folder keeps that name, and `build/gob2native` still starts the app (a link
to `build/gobnative`).

You need your own copy of the games (e.g. from GOG); no game assets are
included in or distributed with this repository. The app installs a game
straight from its GOG installer (see "Installing from GOG" below).

Format and engine knowledge is derived from ScummVM's `gob` engine,
so this project is licensed under the GPL v3 (see `LICENSE`).

Third-party code:

- `third_party/dr_mp3` (dr_mp3 v0.7.3 by David Reid, public domain / MIT-0)
  decodes the CD audio image `Track1.mp3` and replacement music in MP3.
- `third_party/imgui` (Dear ImGui v1.92.9b by Omar Cornut, MIT; core plus
  the SDL2 and OpenGL 3 backends) draws the in-game overlay.

## Build

    cmake -S . -B build
    cmake --build build -j

This builds the app, `build/gobnative` (`build/gob2native` is a link to it,
for the name it had before), the tools below and the unit tests (`test_*`).

## Windows

gobnative and the tools build for 64-bit Windows 10 (1903 or later) and 11,
with MSVC or MinGW-w64; they need OpenGL 3.2 and `SDL2.dll` next to the
programs.

**Download.** Every push to `main` runs the GitHub workflow "Build"
(`.github/workflows/windows.yml`): an MSVC build, the unit tests, and the
package as the run's artifact. On GitHub: Actions, the latest "Build" run,
"Artifacts", `gobnative-windows-x64` (a zip; kept 30 days). With the `gh`
command line:

    gh run download --name gobnative-windows-x64 --dir gobnative-windows-x64

**Run.** Unpack it anywhere and double-click `gobnative.exe`; the game
library opens. Download the GOG offline installers (they are only read,
never run), click a game's "Install..." and choose its installer in the file
dialog (or drop it onto the window); it is installed into
`%APPDATA%\gobnative\Games\<game>` and its tile's "Play" starts it. "More
options", "Add a game folder..." plays a folder you already have, such as the one GOG's
installer made (`C:\GOG Games\Gobliins 2`, the folder with `INTRO.STK`).
Settings, saves, screenshots and HD packs are per game in
`%APPDATA%\gobnative\<game>\` (the pref folder; `--pref-root` as on macOS).
From a command prompt, `gobnative.exe "C:\GOG Games\Gobliins 2"` plays a
folder directly, its messages go to that console, and the tools
(`gobinstall.exe`, `gobrun.exe`, ...) work as described below.
`README-Windows.txt` in the package says the same in short.

**Build with Visual Studio** (2022 or later, "Desktop development with
C++", CMake), with SDL2's VC development package
(`SDL2-devel-2.32.10-VC.zip` from SDL's GitHub releases, unpacked):

    cmake -S . -B build -A x64 -DSDL2_DIR=C:/path/to/SDL2-2.32.10/cmake
    cmake --build build --config Release
    ctest --test-dir build -C Release
    cmake --install build --config Release --prefix dist/gobnative-windows-x64

**Cross-build on macOS or Linux** with MinGW-w64 (`brew install
mingw-w64`) and SDL2's MinGW development package
(`SDL2-devel-2.32.10-mingw.tar.gz`, unpacked):

    scripts/build-windows.sh /path/to/SDL2-2.32.10

It builds in `build-win/` (toolchain file `cmake/mingw-w64.cmake`) and
writes `dist/gobnative-windows-x64/` and `dist/gobnative-windows-x64.zip`:
the programs (the C++ runtime linked in), `SDL2.dll`, the README and the
licences; it checks that no program needs another DLL than Windows' own
and `SDL2.dll`. The Windows build has no `gob2native` link and doesn't
migrate an old gob2native settings folder (there never was one on
Windows).

## Web

gobnative also runs in a browser (WebAssembly, built with Emscripten):
the same engine, overlay and game library. Bring your own GOG copy: the
page reads the GOG installers in the browser and keeps the games,
settings, saves, screenshots and HD packs in the browser's own storage
(IndexedDB). No game data is part of the build, and nothing is uploaded
anywhere. It is built and tested locally only (no public hosting).

**Build** with the Emscripten SDK (its SDL2 port is fetched by emcc the
first time):

    source /path/to/emsdk/emsdk_env.sh    # or set EMSDK; ../tools/emsdk is found by itself
    scripts/build-web.sh                  # writes dist/web/
    scripts/build-web.sh --serve 8080     # ... and serves it on http://localhost:8080/
    scripts/build-web.sh --serve 8080 --lan   # ... also to a phone on the same network

`dist/web/` holds `index.html`, `gobnative.js`, `gobnative.wasm` (about
2 MB), the home-screen app's `manifest.webmanifest`, `sw.js` and icons,
and the licences; any static web server can serve it (no special
headers: the build uses no threads, so no cross-origin isolation). The
GitHub workflow builds it too (artifact `gobnative-web`).

**Play.** Open the page: the game library appears. Click a game's
"Install..." and choose its GOG installer (`setup_...exe`) in the
browser's file dialog, or drop it onto the page; it is read into memory and
extracted with a progress bar (a few seconds), then the tile shows the
game's title picture and "Play". Under "More options", "Add a game
folder..." copies a game folder you already have (the folder with `INTRO.STK`; only the files directly
in it) into the browser's storage. Imported games stay there across
reloads; a tile's "Manual" opens the game's manual. In the game, the
backquote key opens the overlay as on the desktop; F12 saves a
screenshot as a download; "Back to library" (or holding Escape) reloads
the page into the library. The overlay's Video tab imports HD packs
(a pack folder, or a zip of one) and the Audio tab replacement music
files into the game's folders in the browser's storage.

**Touchscreens** (phones, tablets; also a touch laptop on the desktop).
A tap clicks where it lands; a long press (half a second) or a tap with
two fingers right-clicks (Gobliiins' walk/act/pick cursor, putting
things back in Goblins Quest 3, cancelling); dragging a finger moves the
pointer without clicking, to read the names of things. A finger held
beside the picture puts the pointer at that edge, which scrolls Goblins
Quest 3's wide rooms; taps there never click. After the first touch a bar
of buttons shows beside the picture (under it when the screen is
upright): **Menu** opens the overlay (tap beside it to close it; drag to
scroll it), **Skip** skips a scene (dimmed when there is none), **Right**
makes the next tap a right click, **Save** and **Load** quick save and
quick load (Load asks for a second tap), and **Keys** shows the phone's
keyboard for typing to the game (Gobliiins' level passwords; tap it
again to hide it). A mouse click hides the bar again. On a phone the
page drops its header and footer and doesn't zoom or scroll under the
fingers. The gestures are in `frontend/touch.h` (tested by
`test_touch`); synthetic touch events were checked in Chromium's
mobile emulation, not yet on real phones.

**On a phone.** Getting a game onto the phone, any of:
- Download the game's offline installer from GOG in the phone's browser,
  then tap Install... on the game and choose the file (in Downloads or
  the Files app; the page doesn't filter the dialog on a phone, which
  would grey the installers out).
- On a computer, "Export zip" on an installed game in gobnative's
  library writes the game's files into one zip (`Gobliins 2.zip`, the
  files at the top of the game's folder, without programs and archives;
  stored, not compressed). Send it to the phone (AirDrop, a cloud drive, a
  cable), then in the page choose More options, "Add a zip...".
- More options, "Add a game's files...": phones can't choose folders, so
  choose all the files in the game's folder instead.

Tapping a game's Play puts the page into fullscreen, held sideways,
where the browser allows it (Android); in portrait a hint suggests turning the
phone. An iPhone has no fullscreen for web pages: "Add to Home Screen"
(Safari's share menu) makes the page an app with the whole screen, which
also starts without a network once opened (`sw.js`, a network-first
cache of the page's own files; only over HTTPS). After an import the page
asks the browser to keep its storage, and the library says how much it
uses and whether it is kept. Safari clears a site's storage after seven
days without a visit unless it is a home-screen app, so on an iPhone add
it to the home screen. To try the page on a phone without hosting it,
`scripts/build-web.sh --serve 8080 --lan` serves it to the local network
(plain HTTP: no service worker, no persistent storage, no developer
options).

**How it works.** `tools/gobnative.cpp` and `frontend/library.cpp` run
one frame at a time (`GameRun`, `LibraryScreen`), driven by
`emscripten_set_main_loop` in the browser (about 60 frames a second);
the GL presenter compiles its shaders as GLSL ES 3.00 for WebGL 2; the
installer is extracted in steps of about 20 ms per frame (no threads);
audio goes through SDL to Web Audio. `/gobnative` (the pref root) is an
IndexedDB file system, loaded before the app starts and written back
after imports and settings changes, every few seconds and when the page
is hidden (`web/library_gobnative.js`, `frontend/web.h`).

**Limits.** Needs WebGL 2 (current Chrome, Edge, Firefox, Safari).
The page keeps in memory only what it needs, so that a phone's tab
doesn't run out: the library reads each game's small archives (about
15 MB for the three games: detection and the cover pictures), and Play
reads that game's folder and its settings, saves and HD packs (Goblins
Quest 3, the largest, about 145 MB) before it starts, which takes a
moment. Everything else (music, speech, videos, manuals, the other
games' HD packs) stays in the browser's storage. Measured in Chromium
with the three games installed: the library uses 20 MB of JavaScript
memory and 32 MB of WebAssembly memory (before: 257 MB and 128 MB),
Goblins Quest 3 running 162 MB and 134 MB (before: 258 MB and 154 MB).
An installer is read into memory in pieces that are let go as the
import goes on, and what was imported leaves memory once it's saved:
importing Goblins Quest 3 holds at most 165 MB of files (before: 372 MB,
and the 118 MB installer stayed until the page was closed).
Browsers may clear the storage of sites you haven't visited for a long
time unless they grant persistent storage (the page asks after an
import). A tile's
"Remove" deletes an imported game's files (its saves and settings stay).
The pointer can't scroll Goblins Quest 3's wide rooms
by leaving the page; HD pictures are decoded when first drawn (no
background threads), which can stutter the first time a room shows.
Cmd/Ctrl+S and Cmd/Ctrl+L are taken from the browser for quick
save/load; some browsers keep F12 for their developer tools.

**Testing on this computer.** Developer options are honoured only when
the page comes from `localhost`: `?installer=/path/on/the/server/setup.exe`
imports that installer (fetched from the same server, so serve a folder
that links to your installers; never copy them into the repository),
and `?arg=...` (repeated) passes command line arguments as on the
desktop, for example
`?arg=--play-at&arg=1&arg=gob2&arg=--quicksave-at&arg=30`.
`--frame-timer` keeps the frames going in a page the browser doesn't
show.

## LG webOS TV

gobnative runs on LG TVs with webOS as a native app (an `.ipk` package).
It was made on and for an LG OLED C1 (2021, webOS 6.5.3), the only TV it
has been tried on. Other webOS TVs should work if their GPU does OpenGL ES
3.0: webosbrew's compatibility check (in the GitHub build) finds what it
needs on every firmware it knows from webOS 4.10 on. The Homebrew Channel
listing asks for webOS 6 or later, the oldest release it was tried on; on
an older TV, install the `.ipk` from the release by hand. On a TV without
OpenGL ES 3.0 the app can't open its window and goes back to the home
screen.

**What you need:** a TV that installs packages from outside LG's store.
That means a rooted TV with the [Homebrew Channel](https://www.webosbrew.org/),
or Developer Mode. Only a rooted TV has been tried.

**Installing the package.** Every release has
`io.github.initcore0.gobnative_<version>_arm.ipk` and its Homebrew Channel
manifest. Every CI run keeps them as the `gobnative-webos` artifact. Any
of these installs or updates it; an update keeps your games and saves:

- the webOS CLI (`npm install -g @webos-tools/cli`), with the TV set up by
  `ares-setup-device`:

      ares-install io.github.initcore0.gobnative_0.2.0_arm.ipk

- the webosbrew Device Manager app, which installs a package file;
- from a checkout, on a rooted TV reachable over SSH as root:

      scripts/build-webos.sh --install root@192.168.1.50

  This builds, copies the package to the TV's `/tmp`, installs it as the
  Homebrew Channel does, and starts it.

**Putting the games on the TV.** On a computer, download each game's
offline backup installer from your GOG library (`setup_….exe`). Copy them
onto a USB stick (any folder, up to three folders deep) and plug it into
the TV. The library finds the stick by itself, checks each installer, and
shows **Install from USB** on its game's tile, with the size it takes.
Installing Gobliiins takes about 6 seconds on a C1. **Browse the USB
stick** opens a list of the stick's folders and installers, for files with
other names. A tile's **Remove** deletes an installed game; your saves and
settings stay.

**No USB stick? Send the games over the network** (a rooted TV with SSH):

    scripts/send-to-tv.sh root@192.168.1.50 "setup_gobliins_2_-_the_prince_buffoon_1.02_(20270).exe"

Start gobnative on the TV once first (it makes its data folder). The
script copies each installer, or a game's folder already installed on a
computer, into `data/Installers/` on the TV. The library finds them there
as it finds a stick's, within a few seconds, even while it's open: the
tile says "Copied to this TV" and **Install** installs it.

- An installer is removed after installing, to free the TV's storage.
- A game folder is moved into place at once.
- Copies arrive under a hidden name and are renamed when complete, so a
  half-sent file is never offered.
- They're given to the app's own user, since the app runs as one.


**The remote:**

| | In the library | In a game |
|---|---|---|
| Pointer and OK | everything | the mouse (left click) |
| Back | close a dialog, leave the app | right click (puts a held item away); closes the menu |
| Hold Back | asks "Quit gobnative?" | asks "Leave the game?" (keep playing, back to library, quit) |
| Red | Gobliiins: Play or Install from USB | Escape: skip a cutscene, leave a game menu (held: leave the game) |
| Green | Gobliins 2: Play or Install | the menu (overlay) |
| Yellow | Goblins Quest 3: Play or Install | show or hide the key tips under the picture |
| Blue | browse the USB stick | fast forward on and off |
| Arrows and OK | move a white ring over the buttons, and press | the game's arrow keys and Enter |

When the game asks for a name to type (Gobliins 2's save names), the TV's
on-screen keyboard comes up. Quick save and quick load are in the menu's
Levels tab. The game pauses while another app or the home screen is in
front.

**On the TV, the picture and the menu** fill the screen. The menu is 1.7
times larger, to be read from a sofa. Every display filter but CRT keeps 60
frames a second on a C1. CRT is heavier (about 40), and the Video tab says
so. HD packs work as on the desktop: put them in the game's `HDPacks`
folder inside the data folder below.

**Where things are.** The app runs in webOS's jail as a restricted user.
Everything it writes is in `data/` inside its own folder,
`/media/developer/apps/usr/palm/applications/io.github.initcore0.gobnative/data/`:

- `Games/` (the installed games);
- each game's settings, saves and HD packs;
- `gobnative.log`, the output of the last run.

**Building it.** You need three things, next to the repository or where
the variables say:

1. webosbrew's native SDK (`arm-webos-linux-gnueabi_sdk-buildroot`),
   unpacked, with its `relocate-sdk.sh` run once (`WEBOS_SDK`, else
   `../tools/arm-webos-linux-gnueabi_sdk-buildroot`):
   - Linux hosts: [buildroot-nc4](https://github.com/openlgtv/buildroot-nc4/releases);
   - macOS: webosbrew's [native-toolchain](https://github.com/webosbrew/native-toolchain/releases).
2. SDL2 for webOS, from [SDL-webOS](https://github.com/webosbrew/SDL-webOS/releases).
   Take a release's `SDL2-x.y.z-webos.tar.gz`, not the `-abi` one, and
   unpack it (`SDL2_WEBOS`, else the newest `../tools/sdl2-webos/SDL2-*`).
   The package bundles it, as the TVs' own SDL2 is too old.
3. `ares-package` from the webOS CLI (`npm install @webos-tools/cli`), on
   the PATH or in `../tools/webos-cli`.

Then:

    scripts/build-webos.sh                          # dist/*.ipk and its manifest
    scripts/build-webos.sh --test root@192.168.1.50 # also runs the unit tests on the TV
    scripts/build-webos.sh --install root@192.168.1.50

The package's version is the latest `vX.Y.Z` tag's (or `GOB_VERSION`).
CI builds the package on every push to main; a version tag puts it and its
manifest in the release. Notes from the port, including a GCC 12
coroutine bug that the engine works around, are in `port-plan.md`
(section 6.8).

**Homebrew Channel listing.** `webos/io.github.initcore0.gobnative.yml` is
ready for webosbrew's [apps repository](https://github.com/webosbrew/apps-repo).
Its `manifestUrl` points at the latest release's manifest, so new releases
need no new pull request.

## Tools

    build/stktool list    /path/to/INTRO.STK
    build/stktool extract /path/to/INTRO.STK outdir/
    build/gobvideo /path/to/goblins3 --list            # Goblins 3's IMD videos (imd.md)
    build/gobvideo /path/to/goblins3 --check-all       # decode every video
    build/gobvideo /path/to/goblins3 INTRO outdir/     # frames as BMP, sound as WAV
    build/gobinstall setup_gobliins_2_...exe [destdir] # install a game from its GOG installer (install.md)

## Roadmap

Done:

1. [x] STK archive reader + LZSS unpacker
2. [x] TOT script container parsing, EXT/resource loading, palettes/sprites viewer
3. [x] Script interpreter (inter_v2 opcodes, expression evaluator)
4. [x] Video/draw layer on SDL2, hotspots, mouse input
5. [x] Goblin/animation system, sound (PC speaker, sound effects, CD audio
   from Track1.mp3)
6. [x] Save/load, full playthrough of Gobliins 2
7. [x] Mods: overlay (backquote), display filters, sharp text, game speed and
   turbo, scene skipping, level select, quick save/load, hotspot highlighter,
   screenshots, enhanced sound effects, replacement music, HD packs (export,
   format, in-game compositor with live switching, rooms-only scope, sprite
   filters, alignment guide, per-object frame export/import) and remaster
   scripts (remote GPU upscaling, ComfyUI image models, ChatGPT round trips)
8. [x] Gobliiins (Goblins 1) and Goblins Quest 3 in the same engine and app:
   game detection, per-game settings, saves and packs, a game picker, the
   rename to `gobnative`. Gobliiins: v1 opcodes, Mult_v1, level files,
   Goblin_v1, level music and narration, passwords. Goblins Quest 3: v3
   opcodes, IMD videos and speech, wide scrolling rooms, stairs and ladders,
   its saves. Both played from the intro to the ending by headless click
   scripts. Milestones and findings in `port-plan.md`.

Also done: installing the games straight from their GOG installers (our
own Inno Setup reader with LZMA/LZMA2 decoders, `gobinstall`) and the game
library that replaced the picker (tiles with the games' title pictures,
import with a progress bar, back to the library from the overlay); see
"Installing from GOG" and `install.md`. The Windows build (see "Windows"; it
still needs a test on a real Windows PC), the web build
(WebAssembly: the library, imports from GOG installers into the
browser's storage, all three games; see "Web") and the LG webOS TV build
(a library for the remote, games installed from a USB stick, the colour
keys, Back as the right click; see "LG webOS TV").

Open:

9. [ ] Russian translation support for Gobliins 2: Cyrillic font patch,
   string extractor, language-pack loader with reflow, pseudo-translation
   test mode (the translation text itself to be supplied by the
   user/community). Not started; starts only after the user confirms.
10. [ ] Widescreen mod.
11. [ ] HD packs for Goblins Quest 3's IMD videos (they show their original
    pixels enlarged).

## Installing from GOG

Bought the games on GOG? Download the offline installers
(`setup_gobliiins_1.02_(20270).exe`, `setup_gobliins_2_-_the_prince_buffoon_1.02_(20270).exe`,
`setup_goblins_quest_3_1.02_(20270).exe`; on macOS too, they are only read,
never run) and start the app without a game folder:

    build/gobnative

It opens the **game library**: a tile for each game, with its year and
whether it is installed. For a game that isn't installed yet, a card shows
the steps (open your GOG library, download the installer, drop it here):
click the game's "Install..." and choose its installer in the file dialog,
or drop the installer onto the window. The app checks that
it is that game's installer, extracts the game's files with a progress bar
(Gobliins 2 takes about a second, Goblins Quest 3 three) into its own
folder (`~/Library/Application Support/gobnative/Games/<game>/`; on Windows
`%APPDATA%\gobnative\Games\<game>`), and the
tile shows the game's title picture: click it (or "Play") to play. Only what the game
needs is installed: its data files, the CD music (`Track1.mp3`) and the
manual (`Manual.pdf`); not ScummVM, GOG's own files or the floppy version.

On the command line, `build/gobinstall <setup.exe> [destdir]` does the same
(`--list` shows what is in an installer, `--floppy` adds the floppy version
in `FDD/`, `--check` only verifies it). The installer format and the tests
(every file compared with innoextract's extraction) are in
`install.md`.

## Play

Start the app without a folder for the game library (above), or with the
folder that holds your copy of the game (the GOG CD versions; the folder
with `INTRO.STK` and `Track1.mp3`):

    build/gobnative                                # the game library
    build/gobnative /path/to/gobliiins1           # Gobliiins (Goblins 1): INTRO.STK, GOB.LIC, Track1.mp3
    build/gobnative /path/to/gobliins2            # Gobliins 2: INTRO.STK, GOBNEW.LIC, Track1.mp3
    build/gobnative /path/to/goblins3             # Goblins Quest 3: TOT.STK, EXT.STK, IMD.ITK, USGOB3.ITK, Track1.mp3
    build/gobnative /path/to/gobliins2 --scale 4 --trace

The library shows a game as installed when it is in the app's `Games`
folder or in one of the recent game folders (the folders you played
from). "More options" at the bottom (closed at first) has "Add a game
folder..." (or drop a folder onto the window) for a folder of your own,
"Install from an installer..." for any of the three games, a field for a
folder's or an installer's path, the recent folders (double-click one to
play it) and the details of the latest message (paths, reasons); a tile's
small "From your folder" / "Installed by this app" shows its folder when
hovered. The overlay's Gameplay tab has "Back to
library".

The app and the tools detect the game from the folder (Gobliiins,
Gobliins 2 or Goblins Quest 3, CD versions; floppy versions, such as the
`FDD` folders GOG adds, are refused) and print what they found; `--game
gob1|gob2|gob3` picks the game instead. The window title names the game.
Gobliins 2 and Gobliiins are fully playable (Gobliiins: see "Gobliiins"
below); Goblins Quest 3 plays from its intro to its ending (see
"Goblins Quest 3" below).

Hold Escape for a second to quit. A short press of Escape skips the scene
that is playing (see "Skipping scenes" below): the intro, the ending, the
pictures between rooms and worlds, goblins entering or leaving a room. Hold
Tab for turbo speed. Hold left Option (left Alt on other systems) to see the
room's hotspots.

Press the backquote key (`` ` ``, below Escape) to open the overlay; the game
pauses while it is open, and `` ` `` or Escape resumes it. Tabs:

- **Video**: fullscreen, window scale (2x-6x), integer scaling, 4:3 aspect
  correction (on by default: the 320x200 image is shown as 320x240, like on a
  CRT), the display filter and its settings (see below), sharp text, the HD
  pack (see "HD packs and sharp text") and a screenshot button.
- **Audio**: master, CD music, effects and PC-speaker volumes, mute (and for
  Goblins Quest 3 the speech volume and "Speech on"); sound effects quality
  (enhanced or original); replacement music (see below).
- **Gameplay**: game language (English US/UK, French, German, Spanish,
  Italian; applied by restarting from the intro), restart, back to the
  game library, quit.
- **Speed**: game speed 1x, 1.2x, 1.5x or 2x; turbo speed (2x-8x, default
  4x) used while Tab is held; "Cutscenes at normal speed" (on by default);
  "Skip cutscene" (enabled while there is something to skip).
- **Levels**: every room of the game (GOB00-GOB22) grouped by world, with
  its place name from the game's own data; "Jump" starts the chosen room as
  if you had just arrived there (see below). Also quick save / quick load
  buttons. For Gobliiins: its 22 levels in play order with their access
  codes.
- **Tools**: the hotspot highlighter's options and a debug panel (see below).

### Filters

The Video tab picks how the 320x200 image is scaled to the window; changes
show at once (the game stays visible behind the overlay) and are saved.

- **Nearest**: blocky square pixels. Crisp at whole multiples, but with 4:3
  correction (a 3.6x or 4.8x vertical scale) some rows come out taller than
  others.
- **Smooth**: plain bilinear filtering.
- **Sharp bilinear** (default): square pixels that stay even at any size;
  only a one-pixel band at each pixel edge is blended. "Softness" widens the
  band up to plain bilinear. Identical to nearest at whole multiples.
- **Scale2x** / **Scale3x**: the EPX / AdvMAME pixel-art upscalers (2x and
  3x), then sharp bilinear to the window size.
- **Smooth edges (xBR-like)**: an edge-directed upscaler written after the
  idea of xBR (not a port of it): it compares colour changes along the two
  diagonals around each pixel corner and fills corners that stick out of a
  diagonal edge, along 45 degree, shallow or steep lines, anti-aliased.
  Rounds staircase edges off much more than Scale2x. "Edge rounding" sets
  how far; then sharp bilinear to the window size.
- **CRT**: a beam per source line (200 scanlines, wider for bright colours),
  horizontal beam sharpness, a phosphor mask (aperture grille, slot mask or
  shadow mask dots, with strength; about one triad per game pixel), glow,
  optional barrel curvature and vignette, brightness and display gamma; all
  in linear light. Looks best at 3x and above.

The upscalers render into an off-screen 2x-4x image that is reused every
frame; all filters take well under a millisecond per frame on an M-series
Mac (`--bench-at` measures them).

### HD packs and sharp text

**Sharp text** (Video tab, on by default) draws the game's text at the
window's resolution instead of enlarged 8x8 pixels: the game's own letters
with their staircase edges smoothed, rendered from distance fields, exactly
where the original text is. A letter shows sharply only while the screen
still holds exactly the pixels the game drew for it, so anything drawn over
text, or erasing it, brings back the original pixels at once. Not with the
CRT filter.

**HD packs** are upscaled versions of the game's pictures that you make
from your own copy with AI upscalers (for example ESRGAN models in
chaiNNer):

    build/gobexport /path/to/gobliins2 ~/gob2-export

writes every picture (271, of which 94 distinct) as a PNG with its palette
applied, a transparency mask and a JSON description, one folder per script
file, with a README.txt. Upscale the PNGs (not the masks) at 2x, 3x, 4x or
8x (or recolour them at 1x), keeping folders and names, and put the result
into `HDPacks/<pack name>/` next to `settings.ini`; choose the pack in the
Video tab, which lists problems (wrong sizes, unknown files) and shows how
much of the screen comes from the pack. Transparency is rebuilt from the
original pictures.

The game then runs through the **HD compositor**: everything the game draws
at 320x200 is drawn again at the pack's scale, with the pack's pictures in
place of the originals, so rooms, goblins, objects, the icon bar and the
cursor are shown in full colour at up to 2560x1600; fades work as in the
original. Choosing a pack (another one, one at a different scale, or None)
takes effect at once, also in the middle of a room, a menu or the intro:
the game remembers which picture every pixel came from and redraws its
layers for the new pack (30-60 ms at 4x). "Apply pack to: Room backgrounds
only" takes only backgrounds and overlays from the pack and keeps the
goblins, objects, cursors and the icon bar in their original art. Pictures
the pack lacks (or doesn't apply to) are enlarged by "Pictures without HD
art": big pixels, Scale2x/3x, smooth edges (xBR-like, computed once per
picture) or bilinear; palettes and fades apply to them as to the original,
and their transparent outlines stay the original ones unless "Smooth
transparent edges" is on. The game's own lines, fills and text keep their
pixels (text is drawn sharply). With an HD pack the Scale2x/3x and xBR
filters are not needed and the final scaling uses sharp bilinear; the CRT
filter shows the original pictures. Settings in `[video]`:
`hd_pack`, `hd_pack_scope` (`all` or `rooms`), `hd_sprite_filter`
(`original`, `scale2x`, `smooth-edges`, `smooth`), `hd_smooth_edges` (soft
edges on HD sprites instead of the exact original silhouettes). Details,
recommended models and the format: `hdpacks.md`.

    build/gobexport /path/to/gobliins2 --check-pack /path/to/pack
    build/gobexport /path/to/gobliins2 --scale-export ~/gob2-export /path/to/pack 4   # plain test pack

**Repainting a room** with an image model (a picture model redraws, it
doesn't just upscale) needs care: objects are drawn over the background
at fixed pixel positions, and animation sheets can't be repainted whole
because the game cuts each frame out of them by exact rectangles. Two tools
help (details in `hdpacks.md`, section 7):

    # 1. alignment guide: the room with every object outlined and named,
    #    over the original and over your pack, plus an objects-only layer
    build/gobrun /path/to/gobliins2 --start MENU --level-at 1 GOB01 --guide-at 2 ~/gob01/guide \
        --guide-pack ~/Library/Application\ Support/gobnative/Gobliins2/HDPacks/mypack --guide-hotspots
    # 2. every animation frame as its own PNG, with layers.txt naming the objects
    build/gobexport /path/to/gobliins2 --frames ~/gob01/frames --only GOB01 --guide ~/gob01/guide
    # 3. repaint frames (same framing, any size), then put them into the pack's sheets
    build/gobexport /path/to/gobliins2 --import-frames ~/gob01/repainted ~/gob2-export \
        ~/Library/Application\ Support/gobnative/Gobliins2/HDPacks/mypack

`scripts/fit-to-pack.py --export EXPORT --output OUTPUT --pack PACK` fits
whole pictures of any size (backgrounds) to an exact pack scale.

### Sound effects

The game's sound effects are 8-bit samples (played at 0.5-19 kHz, mostly
4-11 kHz) on a single voice; footsteps are 5 ms PC-speaker clicks.

- **Enhanced** (default): samples are resampled with a windowed-sinc
  (Kaiser) filter that cuts off just below the sample's own Nyquist
  frequency, which removes the harsh, metallic images of the sound above
  it; voices that are cut off fade out over 2 ms instead of clicking; the
  footstep click and other speaker tones get soft edges and a 4 kHz
  low-pass.
- **Original**: each sample value is held until the next one, as the Sound
  Blaster's DAC does (crunchy, with all the aliasing), and speaker tones are
  plain square waves.

Measured on the effects of the first room (`gobrun --audio-dump`, below),
enhanced lowers the energy above each sample's Nyquist frequency by about
37 dB and keeps the energy below it within 1 dB.

### Replacement music

Put MP3 or WAV files named after the game's CD tracks into the `Music`
folder next to `settings.ini` (created at the first start; "Open folder" in
the Audio tab) and they play instead of those tracks' CD music: for example
`campagne.mp3` (world 1's rooms), `interieure.wav`, `aquatique.mp3`,
`nuit.mp3`, `MINE.mp3`, `g2intro.mp3` (the intro). Names are not case
sensitive; when both exist, the WAV is used. The Audio tab lists the files
found (with their format, or why one can't be played) and every track
name of the game with its length; "Rescan" picks up new files, which apply
the next time the game starts the track. `--music-dir DIR` uses another
folder for one run.

WAV can be 8/16/24/32-bit PCM or 32/64-bit float, MP3 any rate dr_mp3
decodes; mono or stereo, any sample rate (converted to 44.1 kHz). OGG and
FLAC are not supported: they would need another vendored decoder. A file is
read into memory when the game starts its track (a WAV as 16-bit samples at
its own rate, about 10 MB a minute for 44.1 kHz stereo; an MP3 as it is),
so the audio thread never waits for the disk.

The game's timeline stays that of the CD: scripts start, poll and stop
tracks by their original positions and lengths (the intro and the ending
are synchronised to them). So a replacement starts with its track and, by
default, is cut where the CD track ends, with silence after a shorter one
until the game starts the next track (a room restarts its track 2 seconds
after the CD track's end, which restarts the file too). With "Play
replacements to their end" (`music_full_length` in `settings.ini`) it plays
on past the track's length: a room restarting the same track while the file
still plays doesn't interrupt it (after a file that ended, the next restart
plays it again), but starting another track or a stop from the game does.

### Hotspot highlighter and Tools

While left Option is held (left Alt on other systems), every hotspot the
room currently offers is outlined over the game image: blue for areas that
only react to the mouse passing over them, yellow for clickable ones, green
for text input fields. Left out are the entries the scripts keep out of the
player's way: hotspots parked off screen (at negative or 1000+
coordinates), full-screen catch-alls (right-click to cancel) and the 1x1
entries that drive the room's timer. The game has no names for its
hotspots (the item names shown at the bottom are printed by the hotspots'
own scripts), so outlines carry no label. The key never reaches the game:
the scripts only read keys with a character or a BIOS scan code (F1-F7,
Enter, Escape, text), and Option alone has neither; the left one sits next
to Cmd, and right Alt is left to typing (AltGr).

The Tools tab has "Always show hotspots", "Label hotspots with their slot"
(in the hotspot table) and a debug panel, all for the current run only:

- **Overview**: the running TOT and the environment stack (with script
  offsets), the room and world, the game clock and speed, counts of
  objects, hotspots and variables; "Keep the game running while the overlay
  is open" (for watching the other pages live; the game gets no input
  meanwhile), "Opcode trace" (every executed opcode goes to the trace) and
  "Copy diagnostics", which puts a short report on the clipboard for bug
  reports (TOT, room, clock, settings, objects, the last 40 trace lines).
- **Variables**: a watch list; add a variable by its 32-bit slot index
  (`v[60]`) or byte offset and see and edit it as int32, int16, int8 and
  string (Enter writes a string). A few the scripts use everywhere are
  listed to start with.
- **Objects**: the mult objects (position, animation, layer, frame,
  animType, order, static/paused, where last drawn) and the goblins' cell,
  leg target, destination, pathExistence, state and next state; hovering a
  row outlines the object in the game image.
- **Hotspots**: the hotspot table (slot, id, type, button, rectangle, key,
  flags, enter/leave blocks); hovering a row shows the hotspot, grey rows
  are the ones the highlighter leaves out.
- **Trace**: the engine's trace (the last 20000 lines, recorded with or
  without `--trace`) with a filter (`a,b` either, `-c` without), pause and
  copy.

### Screenshots

F12 (or "Take screenshot" in the Video tab) saves a PNG of the game image as
shown (after the filter, without the overlay) to the `Screenshots` folder
next to `settings.ini`, named after the date and time; a message shows the
file name. F12 never reaches the game.

### Level select, quick save and quick load

Cmd+S quick saves and Cmd+L quick loads (Ctrl+S / Ctrl+L on other systems;
F5 and the other F-keys belong to the game). A quick save is an ordinary
save in the game's last slot, named QUICKSAVE, so it also shows up in the
game's own load list; a short message in the corner confirms it ("Saved",
"Loaded", "No quick save to load"). A quick save never overwrites a save
made in the game's menu: if the last slot (slot 15 in the menu) holds a save
with another name, quick save refuses until that slot is freed, and quick
load leaves such a save alone too (it only loads a save named QUICKSAVE).
`tests/check_quicksave.sh /path/to/gobliins2` checks both headlessly.

Jumping to a room, quick saving and quick loading all happen the moment the
room is idle and the game would let you open its menu (not while a goblin
is busy or a cutscene plays), and go through the game's own flow: the room
is left the way the menu's Load leaves it and MENU starts the next room. A
jump resets the game to its state at the start of a new game and then sets
up the chosen room's world the way the exit into it does (world items and
place names, the items carried over, first visits), so the first room of a
world plays as if you had just reached it; other rooms start with the rest
of their world unvisited. How the game strings its rooms together is
described in `levels.md`.

Each game has its own folder for settings (`settings.ini`), save games, HD
packs (`HDPacks`), replacement music (`Music`) and screenshots
(`Screenshots`) under `~/Library/Application Support/gobnative/` on macOS:
`Gobliiins/`, `Gobliins2/` and `Goblins3/`. The `settings.ini` in
`gobnative/` itself keeps the recent game folders for the picker.
`--settings FILE` uses another settings file and `--save-dir DIR` another
save folder. `--scale N` sets the window scale for that run only.
`--pref-root DIR` uses DIR instead of `gobnative/` (`DIR/Gobliins2/`, ...),
which is what tests should use; `--print-pref-dir` prints the game's folder
and quits.

**Gobliins 2's folder from before the rename.** gob2native kept Gobliins 2's
files in `~/Library/Application Support/gob2native/Gobliins2/`. The first
time gobnative starts Gobliins 2 (and only if `gobnative/Gobliins2/` doesn't
exist yet) it moves that folder to `gobnative/Gobliins2/` with a single
rename, which copies nothing, and leaves a link
`gob2native/Gobliins2 -> ../gobnative/Gobliins2` behind, so an older
gob2native build (even one still running) finds and writes the same files.
Nothing is copied or deleted. If the move fails (another disk, no
permission), the app keeps using the old folder where it is and tries again
on the next start. What happened is printed, shown for a moment in the
window, and written to `gobnative/migration.log`. To undo it (with the app
closed): `cd ~/Library/Application\ Support && unlink gob2native/Gobliins2 &&
mv gobnative/Gobliins2 gob2native/` (`unlink` removes only the link). If
`gobnative/Gobliins2/` already exists, the old folder is left alone and not
used.

### Gobliiins

    build/gobnative /path/to/gobliiins1

The intro (the king's feast, then the title and credits) leads into the
first level. Controls, as in the original (see the game's `Manual.pdf`):
left click walks the selected goblin there or acts with the cursor shown; a
right click in the room cycles the cursor between walk, act (the fist) and,
for the picker (Dwayne), pick up (the hand); click the crystal ball in the
item bar, or another goblin, to select the next goblin. The skull in the
bar (or Escape during play) opens the quit menu (AGAIN, LOAD with an access
code, EXIT). When a level is solved its access code for the next level is
shown; the game itself has no saves.

- **Level select**: the Levels tab lists the 22 levels in play order with
  the access code each one has with full energy (Level 2 VQVQFDE, Level 3
  ICIGCAA, ...); "Jump" starts a level the way its access code does, with
  full energy. You can also type the codes on the game's own access code
  screen.
- **Quick save and load** (Cmd+S / Cmd+L): the quick save is a snapshot of
  the level as it is (goblins, objects, items, energy), written to
  `gob1-quick.sav` next to `settings.ini`; quick load goes back to that
  level and restores it, from wherever you are. Saving works while you
  play a level (not on the access code or game over screens).
- **Skipping**: Escape ends the intro's or the ending's animation (each of
  the intro's two animations); the picture before a level, a level's story
  text with its narration and the room fading in, and the goblins
  collapsing are fast-forwarded. Turbo (Tab) and the speeds of the Speed tab
  work as in Gobliins 2; the intro and the ending stay at 1x.
- **Languages**: English (US and UK give the same texts), French, German,
  Spanish and Italian (changed in the Gameplay tab, applied by restarting
  from the intro); the six levels with narration play it in the game
  language.
- **Replacement music**: the CD tracks are `extsor1`, `mine`, `statue`,
  `zombie`, `campagne`, `interieure`, `aquatique`, `nuit` (level music),
  `intro`, `g1final` (the intro and the title), and the narration tracks
  `fra1`-`fra6`, `all1`-`all6`, `ang1`-`ang6`, `esp1`-`esp6`,
  `ita1`-`ita6`; the Audio tab lists them.
- **HD packs**: `build/gobexport /path/to/gobliiins1 ~/gob1-export` exports
  its pictures (one folder per script file: AVT00-AVT022 are the levels);
  packs go into `~/Library/Application Support/gobnative/Gobliiins/HDPacks/`.
  The repainting guide and the frame export work for its levels too
  (`hdpacks.md`).

Settings, the quick save, packs, music and screenshots live in
`~/Library/Application Support/gobnative/Gobliiins/` (`--pref-root DIR` for
another root). How the game strings its levels together and what the quick
save holds: `levels.md` section 9.

### Goblins Quest 3

    build/gobnative /path/to/goblins3

The intro video plays with its sound, then the title, and the game starts
on Blount's ship. Controls, as in the original (see the game's
`Manual.pdf`): left click walks Blount there or acts on what is under the
cursor; the rooms are wider than the screen and scroll when the pointer
touches the left or right edge (the pointer stays above the text bar at the
bottom, where the dialogue and the names show); touch the top edge for the
icon bar (disk: SAVE, LOAD, QUIT, VERSION; bulb: jokers; book: notes). The
characters speak their lines in talking-head videos, with the text in the
bar.

- **Saves**: the game's own SAVE and LOAD (30 slots, with a screenshot of
  each save in the load list) write `gob3-slotNN.sav` next to
  `settings.ini`.
- **Level select**: the Levels tab lists the 22 rooms by world with their
  names from the game (Boat, Idol, Death, Country, ..., Gods); "Jump"
  starts a room as you first reached it in your latest game: the items and
  progress you had then (recorded in `gob3-rooms.sav` next to the saves
  whenever you enter a room for the first time). A room you haven't reached
  yet is marked so and starts without the items and progress of the rooms
  before it, which may leave it unwinnable.
- **Missing videos**: a few videos the scripts ask for are not in the GOG
  data (SOR, Were-Blount's arrival among the gods; ROI2B, a line of King
  Bodd's; FORT21A/B; PILOU); the game goes on without them, as ScummVM
  does, and a missing talking head keeps its subtitle up for a moment.
- **Quick save and load** (Cmd+S / Cmd+L): slot 30, named QUICKSAVE, as
  the game's own SAVE would write it (it shows in the game's load list);
  refused while that slot holds a save you made in the game's menu.
- **Skipping**: Escape (or Skip) ends a video; the intermissions between
  rooms, a room's opening scene and the sequences it plays without your
  control are fast-forwarded, their videos included. Turbo and the speeds
  work as in Gobliins 2; videos always play at 1x, in step with their sound.
- **Speech**: the GOG release has only the English speech (USGOB3.ITK); in
  the other languages the characters speak English under the translated
  text. "Speech on" (Audio tab) turns the speech off: the talking heads
  then move silently and the text carries the dialogue (it always shows).
  The cutscenes' sound plays at the effects volume.
- **Replacement music**: the CD tracks of `MUS_GOB3.LIC` (NEF1, IDOLE1,
  MUS_1, ...; the Audio tab lists them).
- **HD packs**: `build/gobexport /path/to/goblins3 ~/gob3-export` exports
  its pictures; packs go into
  `~/Library/Application Support/gobnative/Goblins3/HDPacks/` and work in
  the scrolling rooms. The videos are not part of packs (they show their
  original pixels, enlarged). Sharp text works with its 8x10 fonts.

Settings, saves, packs, music and screenshots live in
`~/Library/Application Support/gobnative/Goblins3/`. How the game strings
its rooms together and saves: `levels-gob3.md`; its videos:
`imd.md`; its opcodes: `opcodes.md` section 11; the wide screens:
`graphics.md` section 9.

### Game speed

Everything the game logic times (sleeps, animation frame pacing, the level's
80 ms game tick, cursor animation, fades) runs on a game clock that goes
faster than real time at speeds above 1x, so goblins walk and act faster.
The CD music and its timeline stay on real time, so the music never drifts
from the position the scripts read. The window title shows the speed when it
isn't 1x.

The intro and the ending are timed to the CD music: the scripts wait for CD
positions and the animation player runs at a fixed frame rate in between.
So with "Cutscenes at normal speed" on, the INTRO, INTRO0 and FINAL scripts
(and any running full-screen animation) always run at 1x, turbo included;
use Escape or "Skip cutscene" instead. Turned off, the animation runs ahead
of the music.

Tab is free because the scripts never test it (their hotspot keys are F1-F7,
Enter and Escape); it never reaches the game.

### Skipping scenes

Escape (or "Skip cutscene" in the Speed tab) skips:

- **The intro and the ending**, as in the original (also before the intro's
  animation has started): the animation stops and the game goes on from
  where the scene ends.
- **Everything between two rooms** during which you have no control: the
  short picture shown between rooms (INTER), a world's intermission (the
  picture with its text and CD music shown when the goblins first reach
  worlds 2-7, 17-40 seconds), the goblins dropping or walking into a room,
  the goblins leaving a room (from the moment the exit animation starts,
  through the next room's arrival), and other scenes a room plays while it
  takes control away (the scripted animations some actions start).

Scenes in rooms are not cut short but fast-forwarded: the game's own
scripts keep running, as fast as the computer allows, with the game clock
and the CD timeline advancing as they would at normal speed, music and
sound effects silent, until you have control again (the room waiting for
input with no scripted animation running), another screen starts or the
game waits for a key or a click. A 20-second intermission is over within a
frame or two, and the room ends up exactly as if the scene had played: the
same goblin and object positions, items and puzzle state; only timers and
idle animations are at another point. The music then carries on from where
the CD timeline is (a replacement music file restarts the track, since
those can't seek).

At any other time the skip does nothing and Escape only reaches the game,
as before: while you play (including while a goblin carries out an action
and you control the other one), in the icon bar's menus, in dialogs and on
screens that wait for a key or a click. A level jump requested from the
Levels tab during a scene skips the rest of that scene but not the target
room's own intermission.

The window is drawn with OpenGL 3.2+ core. Debug options for checking the
output without looking at the window (times in wall-clock seconds; these
runs use your real settings folder unless you add `--pref-root DIR`):

    build/gobnative /path/to/gobliins2 --screenshot-after 25 shot.png   # save the window (PNG, or BMP for .bmp), then quit
    build/gobnative /path/to/gobliins2 --overlay-at 20 Audio --screenshot-after 22 shot.png
    build/gobnative /path/to/gobliins2 --filter crt --screenshot-after 15 crt.png  # filter for this run only
    build/gobnative /path/to/gobliins2 --key-at 10 F12 --screenshot-dir /tmp/shots  # press a key (SDL key name)
    build/gobnative /path/to/gobliins2 --bench-at 12 300                 # time each filter, print ms/frame, quit
    build/gobnative /path/to/gobliins2 --restart-at 10                  # exercise "Restart game"
    build/gobnative /path/to/gobliins2 --start MENU --level-at 5 GOB13  # jump to a room
    build/gobnative /path/to/gobliins2 --quicksave-at 15 --quickload-at 30 --save-dir /tmp/saves
    build/gobnative /path/to/gobliins2 --start MENU --level-at 3 GOB07 --show-hotspots --screenshot-after 15 hs.png
    build/gobnative /path/to/gobliins2 --overlay-at 12 Tools/Objects --screenshot-after 13 obj.png  # tab/sub-tab
    build/gobnative /path/to/gobliins2 --start MENU --level-at 3 GOB00 --sharp-text \
        --mouse-at 15 77 42 --screenshot-after 18 hover.png           # move the mouse (game pixels)
    build/gobnative /path/to/gobliins2 --click-at 25 29 10 ...       # click there
    build/gobnative /path/to/gobliins2 --hdpacks-dir /tmp/packs --hd-pack Test --overlay-at 4 Video ...
    build/gobnative /path/to/gobliins2 --hd-pack Test --hd-tint ...  # tint what the pack doesn't cover
    build/gobnative /path/to/gobliins2 --hd-pack Test --hd-stats --hd-dump-at 12 /tmp/f  # times; HD + 320x200 frames
    build/gobnative /path/to/gobliins2 --hd-pack none --hd-switch-at 8 Test --hd-option-at 10 hd_sprite_filter smooth ...
    build/gobnative --pref-root /tmp/pref --pick-at 2 /path/to/goblins3   # choose a folder in the picker
    build/gobnative --pref-root /tmp/pref --screenshot-after 2 picker.png # the picker's window

## Viewer

    build/gobview /path/to/gobliins2      # build/gob2view is a link to it

Up/Down switch script file, Left/Right switch sprite, T toggles transparency, 1-4 zoom.

    build/resdump /path/to/gobliins2 GOB00 [outdir]   # list resources, dump sprites as BMP
    build/resdump /path/to/gobliiins1 --avo [AVT003]  # Gobliiins level files (goblins-v1.md)

The viewer, `resdump`, `gobdis` and `gobexport` open every `.STK` and
`.ITK` archive in the game folder, so they also read the Goblins 1 and
Goblins 3 data.

## Disassembler

    build/gobdis /path/to/gobliins2 GOB00    # readable listing of one script file
    build/gobdis /path/to/gobliins2 --all    # verify every script decodes cleanly

Tests: `build/test_detect` (game detection, rules and archive slots with
generated game folders),
`build/test_expression`, `build/test_png`, `build/test_mixer` (effects
quality, speaker clicks, music files), `build/test_hdpack` (PNG reader, edge
smoothing, HD pack loading with a generated pack; no game data needed),
`build/test_hdrender` (HD compositor: layers kept in step with the drawing,
repairs, pack pictures, fades), `build/test_avo` (Gobliiins level files,
synthetic), `build/test_imd` (IMD videos, synthetic), `build/test_install`
(the GOG installer reader: LZMA/LZMA2, SHA-1/MD5, paths; with the folder of
the installers as argument also the three installers),
`tests/check_install.sh /path/to/installers` (every file of the three GOG
installers extracted by gobinstall equals innoextract's),
`tests/check_quicksave.sh /path/to/gobliins2`, and for
Gobliiins (`/path/to/gobliiins1`): `tests/check_gob1_level1.sh` and
`tests/check_gob1_walkthrough.sh` (click scripts, `tests/gob1/*.txt`, play
the whole game from the intro through the ending, every level with full
energy),
`tests/check_gob1_levels.sh` (level select and access codes of all 22
levels), `tests/check_quicksave_gob1.sh` (quick save and load) and
`tests/check_hd_gob1.sh` (export, 4x test pack, HD frames equal the
320x200 ones), and for Goblins Quest 3: `tests/check_saves_gob3.sh` (save
from the game's menu, restart, load), `tests/check_quicksave.sh
/path/to/goblins3 build gob3` and `tests/check_hd_gob3.sh` (export, 4x
test pack, HD frames equal the 320x200 ones, in scrolled rooms too).

Opcode operand layouts are documented in `opcodes.md`.

## Headless runner

    build/gobrun /path/to/gobliins2 --press-every 5          # run from the intro, trace to stdout
    build/gobrun /path/to/gobliiins1 --game gob1 --frames 600 # the game is detected; --game picks it
    build/gobrun /path/to/gobliins2 --start GOB05 --quiet    # start a specific script file
    build/gobrun /path/to/gobliins2 --language german        # french, german, british, spanish, italian, american

    build/gobrun /path/to/gobliins2 --speed 2 --click 110 230 135 --watch 9758   # game speed, trace a variable byte
    build/gobrun /path/to/gobliins2 --skip-at 5 --key 30 0x011B                  # skip request, Escape key
    build/gobrun /path/to/gobliins2 --press-every 3 --press-stop 100 --level-at 110 GOB08 \
        --skip-at 115 --watch-skip --print-objects --frames 9000   # skip an intermission
    build/gobrun /path/to/gobliins2 --press-every 3 --press-stop 100 --level-at 110 GOB14 --frames 9000
    build/gobrun /path/to/gobliins2 --press-every 3 --press-stop 100 --save-dir /tmp/saves \
        --quicksave-at 115 --click 118 250 150 --quickload-at 134 --dump-vars vars.bin --frames 9000
    build/gobrun /path/to/gobliiins1 --start INTER --set-var 0 286 -1 --snap-every 2 /tmp/shots
        # set a variable at a time (here: Gobliiins' password screen)
    build/gobrun /path/to/gobliiins1 --start INTER --set-var 0 286 100 --set-var 0 296 10 \
        --click 2 165 182 --rclick 3 150 100 --click 4 180 56 --print-objects --frames 1200
        # Gobliiins' first level: select the fighter, "act" cursor, kick the gate
    build/gobrun /path/to/gobliiins1 --list-levels          # levels, scripts and access codes
    build/gobrun /path/to/gobliiins1 --level-at 3 5 --frames 3000   # Gobliiins: level 5 of the play order
    build/gobrun /path/to/gobliiins1 --level-at 3 2 --save-dir /tmp/s --quicksave-at 20 \
        --state-on-notify /tmp/s/state      # variables and objects at the moment of the save
    build/gobrun /path/to/gobliiins1 --hd-pack /path/to/pack --hd-dump-at 20 /tmp/hd/intro
        # the HD compositor headless: both frames as PNG, and how far apart they are
    build/gobrun /path/to/goblins3 --press-every 5 --press-stop 60 --move 90 319 100 --frames 6000
        # Goblins 3: the pointer at the right edge scrolls the first room
    build/gobrun /path/to/goblins3 --press-every 5 --press-stop 70 --set-str 78.2 580 INTER3
        # string variable s[580] = "INTER3" before MENU starts its first room
    build/gobrun /path/to/goblins3 --level-at 3 EMAP1010 --no-speech --speech-volume 1
    build/gobrun /path/to/goblins3 --press-every 5 --press-stop 60 --script tests/gob3/01-EMAP1000-boat.txt \
        --log-text --frames 20000
        # a player script: each step when the one before is done (tools/runner.cpp lists the commands)

Audio can be checked without listening: `--audio-dump FILE.wav` writes the
mixer's output in step with the simulated clock (sample n is simulated time
n / 44100 s), `--volumes MASTER MUSIC EFFECTS SPEAKER` (0..1) isolates a
source, `--effects original|enhanced` picks the effects rendering and
`--music-dir DIR` / `--music-full-length` play replacement music:

    build/gobrun /path/to/gobliins2 --press-every 3 --press-stop 100 --frames 9000 \
        --volumes 1 0 1 0 --effects original --audio-dump fx.wav
    build/gobrun /path/to/gobliins2 --press-every 3 --press-stop 100 --frames 9000 \
        --music-dir mymusic --audio-dump music.wav

`--script FILE` plays a list of player actions, each started when the previous one is
done: clicks at screen or room positions (scrolling the room first), clicks on a hotspot
by id, waiting until the room gives the player control back, for a variable or a hotspot,
taking an item from Goblins 3's inventory by name, quick save and load, snapshots and
traces (the commands are listed at the top of `tools/runner.cpp`); `--log-text` traces the
text on the screen (subtitles, hover names) whenever it changes.
`tests/check_gob3_walkthrough.sh` plays Goblins Quest 3 room by room with such scripts
(`tests/gob3/`), from the intro through Were-Blount's worlds to the end of the ending.
For Gobliiins the scripts say what the current goblin does (`goblin N`, `walk`/`act`/
`pick`/`use X Y`, `exit`); `tests/check_gob1_walkthrough.sh` plays its 22 levels
(`tests/gob1/`) from the intro through FINAL in one run, checking each level's code:

    build/gobrun /path/to/gobliiins1 --level-at 3 5 --script tests/gob1/05-AVT005-giant.txt --frames 30000

Runs the scripts on a simulated clock without a window. Opcodes that are not
implemented yet are logged as `stub` and skipped. `--trace-ops` also logs
each level game tick (`timed tick`). `--watch-skip` logs when a skip becomes
possible or impossible (what enables the Skip button); `--print-objects` and
`--print-hotspots` print the mult objects (positions, animation fields,
goblin cells and states) and the hotspot table at the end of the run, and
`--dump-vars FILE` the variable space, for comparing a skipped run with an
unskipped one; `--state-on-notify PREFIX` writes both (PREFIX-N.vars,
PREFIX-N.objects) at the moment a quick save or quick load completes.
`--hd-pack DIR` runs the HD compositor with that pack and `--hd-dump-at T
PREFIX` writes the HD and the 320x200 frame and prints their difference
(`hdpacks.md` 4.8). `--guide-at T DIR` writes a repainting guide for the room
running at time T, as soon as it is idle (`--guide-scale N`,
`--guide-pack PACKDIR`, `--guide-hotspots`; `hdpacks.md` section 7)
and ends the run. Fast-forwards in gobrun always run 1200 engine frames (20 s of
game time) per simulated frame, so runs stay reproducible.

## Remastering with the OpenAI Image API

`scripts/openai-remaster.py` repaints pictures with OpenAI's GPT Image models
(default `gpt-image-2.5-sunburst`, the one for precise edits) and installs
them pixel-exact into an HD pack. Put `OPENAI_API_KEY=...` into `.env` at the
repository root (git-ignored) or the environment. Each picture is sent at 4x
(or `--scale 8`) of its size with nearest-neighbour enlargement, and the
answer is asked for at exactly that size, so it lines up with the game.

    scripts/openai-remaster.py --dry-run                   # what would be sent
    scripts/openai-remaster.py --pack openai-gob01         # GOB01's background
    scripts/openai-remaster.py --tots "GOB02 GOB03" --quality xhigh --pack openai
    scripts/openai-remaster.py --ids 30007 --tots GOB01 --force --prompt-file my-prompt.txt

To touch up part of a picture already in the pack, give the rectangle in
the original picture's pixels and say what's wrong; the pack's picture goes
out with the original as a reference and a mask, and only the rectangle (with
soft edges) is pasted back:

    scripts/openai-remaster.py --tots GOB00 --pack openai-gob01 --fix 110 60 154 112 \
        --prompt "the old wizard in the armchair: two long pointed ears, small nose, black hair"

Sprites: `scripts/openai-frames.sh` puts all frames of the given animation
layers (from a `gobexport --frames` folder) into one picture, a row per
animation, so one request paints the same character in all of them; a
remastered background goes along as the style reference. The answer is cut
back into frames, each realigned to its original outline, and imported with
`--import-frames --alpha` into a pack that starts as a copy of the rooms
pack (pick it with pack scope "all"):

    scripts/openai-frames.sh --layers "GOB01/anim0/layer0 GOB01/anim0/layer1 GOB01/anim0/layer4" \
        --name green-goblin --describe "bald, big round eyes, green shirt, purple trousers"

A whole character: `scripts/openai-character.py` takes all of a
character's animation layers, paints each distinct frame once (identical
frames in other animations share the result), in batches of about 20
frames, each sent with the room's remastered background (style) and, from the
second batch on, the first batch's answer as the character reference. Every
frame is realigned to its original outline and the lot is imported with
`--import-frames --alpha` (outlines from the paint, not the old pixel mask):

    scripts/openai-character.py --name green-goblin --layers "GOB01/anim0/layer0 GOB01/anim0/layer1 ..." \
        --describe "bald, big round eyes, green shirt, purple trousers" --pack openai-goblins

Characters painted into a background whose moving parts are sprites (the
wizard in GOB00): `scripts/openai-overlay.py` puts every distinct frame onto
a crop of the pack's remastered background exactly where the game draws it,
has the model repaint only the pixel-art parts to match the painting around
them, and cuts each frame back out with the game's outline (smoothed); frames
with goblin pieces are left alone:

    scripts/openai-overlay.py --name wizard --tot GOB00 \
        --layers "anim30004/layer0 anim30004/layer1 anim30004/layer7 anim30004/layer51" --pack openai-gob01

`--box X0 Y0 X1 Y1` (screen pixels) switches to piece mode: frames may contain
goblins, and only the pieces inside the box are written, straight into the
pack's sheets (for example the wizard hit by a stone a goblin throws);
`--frame-list "layer:frame,frame ..."` picks the frames.

To undo a repaint you don't like, `scripts/hdpack-revert.py` puts the sheet
pieces of chosen animations in a pack back to the original art (pieces also
used by `--keep-layers` stay as they are):

    scripts/hdpack-revert.py --pack openai-gob01 --tot GOB03 --layers "anim30004/layer46 anim30004/layer48"

Whole sprite sheets: `--kinds "animation sprite"` sends each unique
animation sheet in one request (pieces on flat magenta, padded to a size the
API takes); the answer's magenta is filled with the neighbouring paint and
its magenta edge cast removed, and the game cuts pieces out with its own
outline. `--ref-pack PACK` adds the picture's own room background from PACK
as the style reference. Identical sheets in other rooms use it through the
pack's fallback, so 42 requests cover the whole game:

    scripts/openai-remaster.py --tots "GOB00 GOB01 ..." --kinds "animation sprite" \
        --pack openai-sheets --ref-pack openai-gob01

Every answer is also kept with its prompt, size and token usage (and an
estimated cost) in `../upscale-openai/<pack>/`. OpenAI bills each picture
(a 1280x672 background at high quality was about $0.03), and the game's
pictures are sent to OpenAI: use your own copy and keep the packs to
yourself. Pick the pack in the overlay's Video tab; pack scope "rooms" keeps
the original sprites.

## Building an HD pack on a remote GPU

`scripts/hdpack-remote.sh` does the whole round trip in one command: it
exports the pictures with `gobexport`, sends the colour images to a CUDA
machine over SSH, upscales them there with an OpenModelDB model
(`scripts/upscaler/upscale.py`, PyTorch + spandrel, VRAM-aware tiling), fetches
the results into `HDPacks/<model>-<scale>x/` and validates the pack.

    scripts/hdpack-remote.sh --setup                     # once: venv, PyTorch CUDA, models on the box
    scripts/hdpack-remote.sh --model nxbrz --scale 4     # export, upscale, install, validate
    scripts/hdpack-remote.sh --model deviance --pack rooms-4x

Models: `nxbrz` (pixel-art sprites), `deviance` (painted art, backgrounds),
`rebout` (sprite style), `unpainter` (8x, low-res/dithered art; use
`--scale 4` to shrink its output). The box is `--host USER@HOST`, or `GOB_GPU_HOST`
in the environment or in a git-ignored `scripts/local.env`; everything remote lives in `/root/gob2-upscaler`.
Unchanged pictures are skipped on later runs unless `--force` is given.

The scripts (`hdpack-remote.sh`, `remaster-remote.sh`, `remaster-frames.sh`,
`chatgpt-frames.sh`, `chatgpt-rooms.py`) install into Gobliins 2's
`HDPacks`, found by `scripts/gobpaths.sh`: `gobnative/Gobliins2/HDPacks`, or
the old `gob2native/Gobliins2/HDPacks` while that is still a real folder
(before gobnative's first start has moved it). Their export cache is
`~/Library/Caches/gobnative` (an existing `Caches/gob2native` is kept).
`PACKS_DIR` (or `GOB_PREF_ROOT`, another root instead of
`gobnative/`) in the environment overrides the pack folder.
