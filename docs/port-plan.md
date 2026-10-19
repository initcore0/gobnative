# Port plan: Gobliiins (Goblins 1) and Goblins Quest 3 in the same engine

This is the plan and the progress log for extending the engine, which so far
plays Gobliins 2, so that one app plays all three games. Each game is
detected from its data folder. Goblins 1 comes first, then Goblins 3.
**Goblins 2 must keep working at every commit.**

Sections:
1. Status
2. Game data findings
3. Tool status
4. Engine gap analysis: what we have, and what each game needs
5. Architecture decisions
6. Milestones
7. Open issues

References:
- ScummVM paths are relative to
  ScummVM's `engines/gob/` (in its source tree).
- The IMD decoder is not in the sparse checkout. Get it with
  `git -C .../reference/scummvm show HEAD:video/coktel_decoder.cpp`.
- `v[n]` is the 32-bit variable in slot n, as in `docs/levels.md`.

## 1. Status

- [x] P0. Recon and planning (this document). Tools open every STK/ITK
  archive (commit "Tools: open every STK/ITK archive of the game folder").
- [x] G0. Multi-game plumbing and detection (commits "Engine: game
  identity, detection, per-game rules and version hooks", "Tools: --game
  and game detection ...", "gob2native: per-game pref folders, --pref-root,
  --game, title from the game"; notes in 6.1)
- [x] G1.1. v1 dispatch and data opcodes, gobdis v1 layouts (commits
  "gobdis: version 1 operand layouts ...", "Engine: version 1 func and
  draw opcodes (Gobliiins)"; notes in 6.2)
- [x] G1.2. Draw_v1, statics and Mult_v1 (commits "Engine: scenery
  statics ...", "Engine: Mult_v1 player, v1 scenery ops and v1
  printTotText", "gobrun: --set-var T SLOT VALUE"; notes in 6.2)
- [x] G1.3. Map_v1 and the AVO loader (commit "Formats: Gobliiins .AVO
  level files; resdump --avo"; notes in 6.2)
- [x] G1.4. Goblin_v1 part 1: objects, states, drawing, level music
  (commit "Engine: Gobliiins goblins, objects and level map (Goblin_v1,
  Map_v1)"; notes in 6.2)
- [x] G1.5. Goblin_v1 part 2: movement, items, selection (same engine
  commit, plus "gobrun --rclick; a click script that solves Gobliiins'
  first level"; notes in 6.2)
- [x] G1.6. Narration, every level started headlessly (commit "Engine:
  Gobliiins narration tracks with the level story text"; notes in 6.2;
  no full playthrough yet)
- [x] G1.7. Level select, skips, quick save (commits "Engine: Gobliiins
  level select, skips and quick save (G1.7)", "Overlay: Gobliiins levels
  with their access codes in the Levels tab"; notes in 6.2)
- [x] G1.8. Mods integration (commits "gobexport and gobrun: HD packs for
  Gobliiins", "Gobliiins' goblins and objects in the object inspection and
  the room guide", the click script commits and the docs commit; notes in
  6.2). Gobliiins is playable: every level starts and runs.
- [x] G1.9. Gobliiins played through (commits "gobrun --script for
  Gobliiins; the walkthrough as one run from the intro", the "Gobliiins
  walkthrough: ..." commits for levels 5-22 and the ending, "Gobliiins
  cursor as Draw_v1 draws it; ..."; notes in 6.2). Click scripts play the
  whole game from the intro through FINAL in one run, every level with
  full energy (`tests/check_gob1_walkthrough.sh`).
- [x] G3.1. v3 opcodes and ITK archives (commits "Data files: archive
  slots, closeArchive and the voice archive fallback", "Engine: version 3
  opcodes (Goblins 3) and gobdis v3 goblin names"; notes in 6.3)
- [x] G3.2. IMD decoder (commit "Formats: IMD video decoder, gobvideo
  tool and test_imd (G3.2)"; notes in 6.3)
- [x] G3.3. Video player and playImd (commits "Engine: read version 1.3
  mult IMD keys (Goblins 3 rooms run)", "Engine: IMD playback (playImd,
  getImdInfo) with a speech voice (G3.3)"; notes in 6.3)
- [x] G3.4. Wide screens, split bar and scrolling (commit "Engine: wide
  screens, the split bar and scrolling (G3.4)"; notes in 6.3)
- [x] G3.5. Mult IMD keys and the v3 mult redraw (commits "Engine: read
  version 1.3 mult IMD keys (Goblins 3 rooms run)" (G3.3), "Engine: mult
  IMD keys, the v3 redraw skip and palette cycling (G3.5)"; notes in 6.3)
- [x] G3.6. Goblin_v3 and map version 3 (commit "Engine: Goblin_v3 and
  map version 3 (G3.6)"; notes in 6.3)
- [x] G3.7. Goblins 3 saves (commit "Engine: Goblins 3 saves (CAT.INF,
  IMA.INF, INTRO.$$$, BLOC.INF) (G3.7)"; notes in 6.3)
- [x] G3.8. Goblins 3 rules: level select, quick save, skipping (commit
  "Engine: Goblins 3 level select, quick save, skipping and rules (G3.8)";
  notes in 6.3)
- [x] G3.9. Goblins 3 mods and polish (commits "Speech on/off for Goblins
  3; cutscene sound at the effects volume", "Sharp text for glyphs up to
  8x16 (Goblins 3's 8x10 fonts)", "gobexport: Goblins 3's loadSpriteToPos
  uses; tests/check_hd_gob3.sh", "README: Goblins Quest 3 ..."; notes in
  6.3). Goblins 3 runs from its intro through every room with videos,
  speech, scrolling, goblins, saves and the mods; it has not been played
  through to the end.
- [x] G3.10. Goblins 3 played through (commits "gobrun --script: player
  scripts; ...", the "Goblins 3 walkthrough: ..." and "gob3 walkthrough:
  ..." commits for worlds 1-8, "Goblins 3: hold a missing talking head's
  subtitle for a while", "Goblins 3 level select: start a room as the
  player first reached it", "Goblins 3: quick save only once the room has
  updated its state", "Goblins 3: the ending (FINAL) to the end"; notes in
  6.3). Click scripts play the whole game from the intro
  through FINAL (`tests/check_gob3_walkthrough.sh`).
- [x] R. Rename to `gobnative` and migrate the Gob2 settings folder
  (commits "Rename the app to gobnative; move Gobliins 2's folder; game
  picker", "Scripts: find Gobliins 2's packs before and after the
  gobnative move", "docs: gobnative ..."; notes in 6.4)
- [x] I. Install from the GOG installers and the game library (commits
  "Install games from their GOG installers: Inno Setup reader,
  gobinstall", "docs: install.md, ...", "Game library: tiles per game,
  import from the GOG installer, back to library"; notes in 6.5)
- [x] W. Windows build (MSVC and MinGW-w64, GitHub Actions; notes in 6.6)
- [x] Web. WebAssembly build with Emscripten: the library, imports into
  the browser's storage, all three games (commits "Web build: ..." and
  "CI: build the web version ..."; notes in 6.7)

Baselines for the current build (Gob2 engine), measured headlessly with
`gobrun --press-every 5`:

**Goblins 1 (after G1.8):**
- Level select (all 22 levels, from the intro or any level or INTER
  screen), access codes (all 21 accepted by the password screen), quick
  save/load (exact restore of variables and goblin state), skipping and
  fast-forward, speed, the overlay's Levels tab, HD export/packs (exact 4x
  frames in 40 scenes), the room guide, frames export, replacement music
  and sharp text work; the GUI plays it (`--pref-root` runs: level jump,
  quick save and load, HD pack, overlay screenshots).
- Played through (G1.9): `tests/check_gob1_walkthrough.sh` plays all 22
  levels from the intro through FINAL in one run, each solved with full
  energy and showing the next level's code; it also passes in the other
  languages. No goblin-logic bug turned up (the level quirks the scripts
  work around are the original's, `docs/levels.md` 9).

**Goblins 1 (after G1.5):**
- The levels show their goblins and objects, animate them (idle and
  boredom animations, sounds, footsteps) and play their CD music. Walking
  (with way points and ladders), acting, picking up, dropping, swapping
  items and switching goblins work: `tests/check_gob1_level1.sh` solves
  the first level (AVT003) with clicks and INTER starts the next one
  (AVT00), with no stubs.
- Level order (INTER): AVT003 first (v[286] = 100), which sets the next
  level to 3 = AVT00, which sets 0 = AVT002, and so on (6.2, G1.5 notes).
- Starting a level directly needs INTER and full energy:
  `--start INTER --set-var 0 286 N --set-var 0 296 10` (N = 100 for
  AVT003, 3 for AVT00, ...). With `--start AVT00` the energy `v[296]` is 0
  and the level is lost at once (the AGAIN/LOAD/EXIT screen).

**Goblins 1 (after G1.2):**
- INTRO.TOT runs to its end with no stubs and draws both mults: the intro
  (the king's feast) at the mult's 10 fps in sync with the `intro` CD
  track (the last frame at CD 127.8 s of the 130.2 s track), then the
  title/credits mult with `g1final`, then `loadTot INTER` with
  `v[286] = 200`, and INTER loads AVT003 (the first level) at 150.5 s.
- AVT003 shows its room (statics), the item bar and item names; the only
  stubs are the goblin ops (G1.4/G1.5), skipped by paramCount, so no
  goblins appear yet.
- `--start FINAL` plays the ending mult (to 105 s) and returns to INTRO,
  which starts at once (the CD is stopped at the TOT load).
- `--start INTER --set-var 0 286 -1` shows the game-over screen, then the
  password screen (LOAD/EXIT); a typed code is accepted and INTER loads
  the level.
- `gobdis --all` decodes all 25 TOTs with 0 errors (was 179).

**Goblins 3 (after G3.9):**
- INTRO -> DEMO (INTRO.IMD with sound, the title) -> MENU -> EMAP1000
  with no stubs; every room (EMAP1000-1019, EMAPBB08/11/15, INTER0-14,
  FINAL) runs 60 s from MENU with no stubs or errors (`gobrun --set-str
  78.2 580 ROOM`); only EMAP1019's SOR.IMD is missing from the data.
- Talking heads with speech and subtitles, mult IMD animations (INTER0,
  INTER3, EMAP1002), 472-wide rooms with the text bar and edge scrolling,
  Blount walking (stairs, ladders), the game's SAVE/LOAD with
  screenshots, level select (22 rooms by world), quick save/load,
  fast-forward, speech option, sharp text, HD packs in scrolling rooms
  (`tests/check_hd_gob3.sh`), the room guide and frame export.
- Played through (G3.10): `tests/check_gob3_walkthrough.sh` plays every
  room from the intro, world by world, Were-Blount's rooms included, and
  watches FINAL to the end of the game. No goblin-logic bug turned up; the
  fixes were around it (room states for level select, quick-save timing,
  FINAL's key polling, missing videos).

**Goblins 3 (before G3.1):**
- The game gets surprisingly far: INTRO opens TOT.STK and EXT.STK, then
  plays DEMO and MENU, then starts EMAP1000.
- Stubs reached: openItk, playImd, setScrollOffset, loadStatic,
  renderStatic, loadSound, and getTotTextItemPart (40 calls).
- After 27 s it stops with "unknown block type 22 at 0x2B13 in EMAP1000",
  right after a `loadMult` with IMD keys.
- Since G0, MENU's CAT.INF writes are ignored ("ignoring write"), because
  Gob3 has no save format yet; the Gob2 handler used to take them.
- `gobdis --all` decodes all 39 TOTs without errors, because the v3 operand
  layouts equal v2's.

## 2. Game data findings

### 2.1 Goblins 1 (GOG CD, the game folder, e.g. `gobliiins1/`)

- **Files.** Everything is in `INTRO.STK` (125 entries). It matches
  ScummVM's "CD v1.02" multilingual entry (`detection/tables_gob1.h:171-235`;
  md5 of the first 5000 bytes is `8bd873137b6831c896ee8ad217a6a398`).
  The folder also holds:
  - `GOB.LIC`, the CD track table. The engine loads it at startup; no
    script does.
  - `Track1.mp3`, the CD audio (47 MB, one track holding every piece of
    music and narration).
  - `Manual.pdf`.
  - `FDD/`: the floppy version (DISK1-4.STK and INTRO.STK). Not targeted.
- **Archive contents:**
  - 25 TOTs, all version **1.1**, 759 variables (3036 bytes):
    - INTRO (copy protection and main menu; the protection texts are in
      every language);
    - INTER (level dispatcher and password screen);
    - FINAL;
    - AVT00, AVT001-AVT012 and AVT014-AVT022, the 22 levels (there is no
      AVT013).
  - 25 EXT files.
  - 22 `.AVO` files, one per level, read by `Map_v1::loadMapObjects`. Each
    holds:
    - the pass map and items map, 26x28 cells;
    - 40 waypoints and 20 item positions;
    - the sound list;
    - the state tables of the goblins and objects, with item-to-object
      links.
  - 46 `.SND` samples (some LZSS-packed).
  - 5 fonts: J1, J3, JEUCAR1, JEULET0 and PROTECT. The fonts are 6x8.
  - L360.GDR and LVGA.GDR, DOS video drivers we don't need.
- **Texts.** They are embedded in each TOT (textsOffset != 0) and contain
  all five languages. Scripts select the text by `v[16]`; for example,
  AVT005 items 2-6 are FR, DE, EN, ES and IT. There are no .ANG/.DAT
  files.
  - **The language must stay in 0..4.** American (5) would break the
    selection. ScummVM clamps it to 2 (English) for narration.
- **Level flow.**
  - INTRO's menu either starts AVT00 or sets `v[286]` and loads INTER.
  - INTER maps the level number `v[286]` (0-21) to an AVT TOT, in a
    non-numeric order:
    - 0 → AVT002, 1 → AVT007, 2 → AVT004, 3 → AVT00, ...;
    - 22 → FINAL;
    - 100 → AVT003.
  - INTER also implements the "ACCESS CODE" password entry.
- **No save system.** The scripts never call readData or writeData;
  progress is kept by level passwords.
- **CD music.** GOB.LIC lists 40 tracks:
  - music: extsor1, mine, statue, zombie, campagne, interieure,
    aquatique, nuit;
  - intro and g1final;
  - fra/all/ang/esp/ita 1-6, per-language narration for AVT005, 006, 012,
    016, 019 and 022.

  The scripts only play `intro` and `g1final` themselves. Level music and
  narration are hard-coded per TOT in the engine:
  - `Sound::cdPlayBgMusic` (`sound/sound.cpp:565`) is restarted from goblin
    op 1003 (drawObjects) whenever nothing is playing;
  - `Sound::cdPlayMultMusic` (`:602`) is started by `Draw_v1::printTotText`.
- **Copy protection.** INTRO.TOT asks for a colour chart. ScummVM skips it
  by ignoring a `callSub(3905)` in the start TOT (`inter_v1.cpp:720`).
  Whether the GOG build reaches that path has to be checked with a trace.

### 2.2 Goblins 3 (GOG CD, the game folder, e.g. `goblins3/`)

ScummVM: "CD v1.02" multilingual, `detection/tables_gob3.h:342-402`.

- **INTRO.STK** (187 entries):
  - INTRO.TOT;
  - fonts GOB31, GOB32 and GOB33.LET (8x10, larger than Gob2's 8x8);
  - three .GDR drivers;
  - per-TOT text files in five languages: `.DAT` (FR), `.ALL` (DE), `.ANG`
    (EN), `.ESP` and `.ITA`. There is no `.USA`, so American falls back to
    `.ANG`; our TotFile already does that.
- **TOT.STK** holds 36 TOTs, all version **1.3**, 7916 variables:
  - MENU (the hub), EDIT, DEPLACE, JOKER;
  - INTER0-14;
  - EMAP1000-1019 and EMAPBB08/11/15 (the rooms);
  - FINAL.

  Every TOT has textsOffset 0, meaning external texts.
- **EXT.STK** holds the .EXT files and DEMO.TOT (the intro).
- **IMD.ITK** holds 45 IMD videos. ITK archives are STK archives under
  another name. Of the 45:
  - 5 are full-screen 320x200: INTRO (2.1 MB), INT9, INT120, INT121 and
    INTERV;
  - the rest are in-room animations.
- **USGOB3.ITK** (96 MB) holds 462 IMDs: the **speech**. These are
  talking-head videos with 22050 Hz 8-bit audio:
  - 455 are about 61x68 or 92x75 at 7 fps;
  - 7 are larger clips at 10 fps.

  INTRO.TOT opens the voice ITK by language: FRGOB3, DEGOB3, USGOB3 or
  ITGOB3. Spanish uses a byte read from VOICE.INF instead. GOG ships only
  USGOB3.
- **MDOS.IMD** is a loose 320x200 IMD with 400 frames and 8.6 kHz audio.
  No script references it; it is probably a launcher logo, so we ignore it.
- **MUS_GOB3.LIC** holds 52 CD tracks in `Track1.mp3` (43 MB). INTRO.TOT
  loads it with readLIC. Two quirks:
  - the scripts say ECHEQUI1/2 where the LIC says ECHIQUI1/2;
  - ScummVM corrects that (`sound/sound.cpp:636`).
- **Screens:**
  - Menus and cutscenes use 320x200.
  - Rooms call `initScreen 0x14, 472, 200`: a wider back surface with a
    40-line fixed bar and 152 px of horizontal scroll.
  - EMAP1012 uses 320x240 (vertical scroll).
  - `setScrollOffset` appears about 560 times and `scroll` about 190.
  - Mouse-edge scrolling is on (`Init_v3::initVideo` sets
    `_noScroll = false`).
- **Maps** are version 3: pass width 65, logical screen width 640. They add
  stairs (pass value 10, goblin states 40-43), ladders (20; states 38/39)
  and pass value 19 (states 26/27).
- **Mult data in 1.3 TOTs** can carry IMD keys (`Mult_v2::loadImds`).
  EMAP1002/1006/1010, INTER0/3 and FINAL use them.
- **Saves.** The scripts use these virtual files:
  - `CAT.INF`: 500 bytes of global props, a 30-slot index of 1200 bytes,
    and the slots from offset 1700;
  - `IMA.INF`: 30 screenshots of 120x160 plus a palette;
  - `INTRO.$$$`: a temporary sprite;
  - `BLOC.INF`;
  - `config` and `VERSION`, ignored.

  See `save/saveload_v3.cpp`.
- **Copy protection.** None is skipped in ScummVM. DEMO.TOT loads
  `prot.let`, which is absent; the branch probably depends on `v[16]`.
  Check with a trace.

### 2.3 TOT versions

Byte 39-41 of the header holds the version string:

| Game | Version |
|---|---|
| Gob1 | "1.1" |
| Gob2 | "1.2" |
| Gob3 | "1.3" |

All three games use 32-bit variables, the same expression evaluator, the
same block and hotspot encodings, and the same text item format (with v1
differences in printTotText, section 4.1).

## 3. Tool status (after P0)

| Tool | Gob1 | Gob3 |
|---|---|---|
| `stktool list/extract` | works (INTRO.STK, FDD/*.STK) | works (TOT/EXT/INTRO.STK, IMD.ITK, USGOB3.ITK) |
| `resdump` | works | works (now opens TOT.STK/EXT.STK) |
| `gobexport` | 415 pictures with the palettes the game shows them in (G1.8); `--frames` works | every picture, opaque/transparent from the scripts' uses; a 4x pack matches the game exactly (G3.9); `--frames` works; IMD frames are not exported |
| `gobdis --all` | 0 errors (v1 layouts since G1.1) | 0 errors; v3 goblin names, mults list their IMD files |
| `gob2view` | lists TOTs and sprites | lists TOTs and sprites |
| `gobrun` | intro, all levels, ending and the password screen play; all 22 levels and the ending played by click scripts (G1.9); `--list-levels`, `--level-at N`, quick save/load, `--hd-pack`/`--hd-dump-at` (G1.8) | intro, every room, videos with `--audio-dump` speech, `--list-levels`, `--level-at`, quick save/load, `--set-str`, `--state-on-trace`, `--no-speech`, HD dumps (G3.9) |
| `gobvideo` | - | lists, checks (`--check-all`: 508 videos) and decodes IMDs to BMP/WAV (G3.2) |
| `resdump --avo` | dumps the 22 level files (G1.3) | - |

## 4. Engine gap analysis

What we have, implemented as v2 for Gob2 in one `Interpreter` class with
switch-based dispatch:
- a coroutine interpreter: funcBlock, callSub, switch tables, hotspot
  blocks;
- the expression evaluator, TotFile (with external texts) and the STK
  reader;
- Draw for 320x200 surfaces: sprites, fonts and text, cursor, fades,
  captures, invalidation;
- mult_v2 with scenery anims and statics loading;
- map_v2 and goblin_v2 (`engine/goblins.cpp`);
- hotspots and input fields;
- SND samples on one voice, and the PC speaker;
- LIC-based CD audio from Track1.mp3, plus replacement music;
- saves in the Gob2 format (`gob2-slotNN.sav`, cat.inf/bloc.inf/save.inf);
- levels, quick save and skip logic, all specific to Gob2 (room tables,
  `v[60]`, the MENU flow);
- the HD compositor, the overlay, the presenter, and sharp text (fonts up to
  8x8 only).

### 4.1 Goblins 1 needs

ScummVM builds Init_v1, Video_v1, Inter_v1, Mult_v1, Draw_v1, Map_v1,
Goblin_v1 and Scenery_v1, and no SaveLoad.

**Opcodes.** Same table indexing as v2. Differences:

- **D21 is getCDTrackPos.** It takes no operands and writes `v[5]`, or
  32767 when nothing plays. In v2, D21 is waitCDTrackEnd.
- **No v2 draw ops 0x13, 0x23/24, 0x40-0x43, 0x50-0x56 or 0x80-0x88.**
- **D01 playMult** takes the whole int16 as checkEscape. **D02** ignores its
  operand. **D0A setRenderFlags** is a plain assignment.
- **D14 initMult** works with `Mult_v1` render data. **D17 loadMultObject**
  has no goblin animTypes.
- **F25 goblinFunc** is completely different: `cmd, paramCount`, then
  operands that depend on cmd.
  - Groups: object setters 1-16, getters 21-37, map ops 40-44, goblin ops
    50-604, and engine calls 1000-1015 and 2005.
  - Results go to `v[59]`.
  - About 680 lines in `inter_v1.cpp:1516-2861` plus `manipulateMap`.
- **F3A loadSound** reads `slot=valExpr, id=u16`. An id of 0xFFFF skips 9
  bytes. **F39 stopSound** stops AdLib and the blaster.
- **F3E getFreeMem and F3F checkData must not write `v[16]`.** v2 does,
  and in Gob1 `v[16]` is the language.
- **F4D readData** takes int16 size and offset. **F4E writeData** is a
  no-op.
- **checkSwitchTable** always evaluates the expression, and **the last
  matching case wins** (v2: the first).
- F11 printText is transparent for backColor >= 16; F17 animPalInit is
  single-channel; animatePalette has one channel.

**Draw_v1 / Video_v1 / Init_v1:**
- printTotText (`draw_v1.cpp:154`) has an 8-byte rect, a back colour, a
  line/bar/rect command list, then a text stream with the 0xBA variable
  insert. It starts narration (cdPlayMultMusic).
- Simpler animateCursor, blitCursor and spriteOperation.
- The sprite decoder output equals v2's.
- Default fonts: `jeulet1, jeulet2, jeucar1, jeumath`. Only JEUCAR1 exists;
  the scripts load the rest.

**Mult_v1** (`mult_v1.cpp`, 670 lines):
- The resource layout is close to v2. There are no IMD keys; sound keys use
  slots `19-n`; cmd 3 skips 6 bytes.
- It has its own dirty-rect renderer, `Mult_v1::animate` (about 215
  lines), with `_renderData`, and no goblin hook.
- `Scenery_v1::loadAnim` waits for the CD track to end. `_maxDrawOrder` is
  100.

**Map_v1 and Goblin_v1** are the biggest piece (about 3400 lines in
ScummVM):
- `.AVO` loading (`map_v1.cpp:64`), including hard-coded state patches.
- `Gob_Object` / `Gob_State` state machines.
- 3 goblins plus a pseudo-goblin for extra animations, 20 objects, and the
  pocket item.
- nextLayer, animateObjects, drawObjects (with state sounds), boredom,
  switchGoblin.
- The movement chain doMove, moveInitStep, adjustDest/adjustTarget,
  moveTreatRopeStairs and moveAdvance, plus path finding.
- treatItem: pick, place, swap.
- Variable binding at byte offsets 236-456 (saveGobDataToVars /
  loadGobDataFromVars).
- None of this exists for Gob2: its goblins are mult objects.

**Audio:**
- SND samples, as in Gob2.
- PC-speaker footsteps.
- The CD track table from GOB.LIC, loaded at startup.
- Per-TOT background music and narration tables (section 2.1).

**Fixes ScummVM applies:**
- the copy-protection skip;
- when INTER.TOT reaches pos 2933 with `v[285] != 0`, set `v[285] = 0` (the
  goblin stuck after reload, `inter_v1.cpp:891`);
- clamp the language to 0..4.

**Saves:** none in the original, so quick save needs a format of our own
(G1.7).

### 4.2 Goblins 3 needs

ScummVM builds Init_v3, Video_v2, Inter_v3, Mult_v2, Draw_v2, Map_v2,
Goblin_v3, Scenery_v2 and SaveLoad_v3. The engine is almost entirely Gob2's;
the work is in features Gob2 never used.

**Opcodes:**
- Func 0x22/0x23 speakerOn/Off: the footstep hack, which we already have.
- Func 0x32 copySprite: a sparse retrace when the destination is the front
  surface (dissolve).
- **Goblin func 100 is `o3_wobble`**, not handleGoblins. Gob3 has no
  handleGoblins and no protracker. Our goblinFunc must branch on version.
- Gob3 relies on these existing v2 draw ops that Gob2 never used:
  - initScreen modes 0x14/0x18;
  - 0x81 scroll and 0x82 setScrollOffset;
  - 0x83 playImd and 0x84 getImdInfo;
  - 0x85 openItk and 0x86 closeItk;
  - 0x87/0x88 (no-ops);
  - loadStatic, renderStatic and freeStatic;
  - func 0x18-0x1A (getTotTextItemPart; check addHotspot and removeHotspot
    usage);
  - loadSound (func 0x3A).

**Wide screens and scrolling:**
- Variable-size back surfaces (472x200, 320x240).
- A split bar: `_splitHeight2`, `_splitStart`.
- A scroll offset, with retrace = the scrolled area plus the fixed bar rows.
- `Game::evaluateScroll` at the screen edges, and the scroll offset added to
  the mouse.
- This touches Draw, hotspots, the cursor, the HD compositor and the
  presenter.

**Goblin_v3 and Map v3:**
- Map version 3: pass width 65, screen width 640.
- movePathFind branches for stairs, ladders and pass value 19.
- `advMovement` for states 26/27 and 38-43 (two tiles in Y for 40-43).
- placeObject sets the look direction.
- The EMAP1018 Blount animType fix.
- 141 lines in ScummVM, plus `goblin_v2.cpp:248-334`.

**Mult in 1.3 TOTs:**
- IMD keys, loaded when the static count has bit 0x80 (`mult_v2.cpp:96,
  186, 254-307`).
- Playback through advanceObjects and `Mult_v2::playImd`.
- The animate redraw skip for version > 2.

**IMD video** (new subsystem; format summary in G3.4):
- Gob3 needs only frame types 0x01, 0x02, 0x81 and 0x82, standard and
  extended LZ77, 8-bit unsigned PCM audio, 0xFFF4 palettes and 0xFFF0.
- Playback is always blocking, into the back surface or the front surface.
- Palette commands follow palCmd; VAR(7) = frame count, VAR(11) = current
  frame.
- Escape aborts.

**Speech** is simply the audio of the USGOB3.ITK talking-head IMDs. Scripts
pick the clip (string `v[0x59]`) and close it afterwards. Subtitles are text
items flagged 0x8000 in their first word. ScummVM forces `_doSubtitles` on
for Gob3 CD.

**CD music:** readLIC MUS_GOB3, which already works, plus the ECHEQUI
rename.

**Saves:** SaveLoad_v3 (section 2.2). It needs a new slot file layout that
holds the props and the screenshot.

**Fonts:** 8x10. Draw handles them; sharp text must be generalised beyond
8x8.

**Hacks:**
- EMAP1008: evaluateScroll on every mouse move;
- CAIL1/2: keep the cursor during the video;
- EMAP1018: Blount's animType.

## 5. Architecture decisions

### 5.1 Game identity

Add a new `src/engine/game.h` to gobcore:

```cpp
enum class GameId { Gob1, Gob2, Gob3 };
struct GameInfo {
	GameId id;
	const char *key;        // "gob1", "gob2", "gob3" (--game, settings, file names)
	const char *title;      // "Gobliiins", "Gobliins 2", "Goblins Quest 3"
	const char *prefFolder; // "Gobliiins", "Gobliins2", "Goblins3"
	int engineVersion;      // 1, 2, 3: selects opcode/goblin/mult/draw variants
	const char *startTot;   // "INTRO" for all three
	// languages offered, default fonts, CD LIC loaded at startup ("GOB" for Gob1), ...
};
const GameInfo &gameInfo(GameId);
std::optional<GameId> detectGame(const std::filesystem::path &dir); // + reason on failure
```

**Detection** reads only file names and STK tables (no hashing, so other
releases of the same game also work):

| Game | Signature |
|---|---|
| Gob3 | `TOT.STK` and `EXT.STK`, with `EMAP*.TOT` in TOT.STK (or `MUS_GOB3.LIC`) |
| Gob2 | `INTRO.STK` containing `GOB00.TOT` and `MENU.TOT` (GOBNEW.LIC beside it) |
| Gob1 | `INTRO.STK` containing `AVT00.TOT` / `INTER.TOT` (GOB.LIC beside it) |

- If no signature matches, use the version byte of INTRO.TOT: 1.1, 1.2 or
  1.3.
- A folder with only `FDD/` or DISK*.STK is reported as "floppy version,
  not supported".
- `--game gob1|gob2|gob3` overrides detection.
- With no game folder given, the app shows a picker (a folder dialog plus
  recent folders kept in the shared settings).
- Unit test `test_detect` builds fake game folders with generated tiny STK
  files, so it needs no game data.

### 5.2 Version variants in the interpreter

- **One Interpreter, with a version layer per opcode family.** The
  coroutine core, expression evaluator, hotspots, input, fades, CD and
  mixer are shared. ScummVM's inheritance (v3 → v2 → v1) becomes explicit
  dispatch:
  - `execFunc`, `execDraw` and `goblinFunc` first try a version hook
    (`execFuncV1`, `execDrawV1`, `goblinFuncV1` in
    `engine/interpreter_v1.cpp`; `goblinFuncV3` etc. in
    `engine/interpreter_v3.cpp`);
  - when the hook returns "not handled", they fall back to the existing v2
    switch;
  - a v2 case that must not run for another version (D21, the v2-only draw
    ops in v1, gob 100 in v3) is masked by its hook.
  - This keeps the Gob2 code path byte-for-byte unchanged.
- **Subsystems that differ wholesale become their own classes** rather than
  more Interpreter members. `Interpreter` is already about 600 header
  lines, so new code goes into components that hold a reference to the
  interpreter's services (Draw, Scenery, Variables, Mixer, DataFiles):
  - `GoblinsV1` (`engine/goblins_v1.{h,cpp}`): AVO loader, Gob_Object state
    machines, movement, items, var binding;
  - `MultV1` (`engine/mult_v1.cpp`): loader and renderer;
  - `ImdDecoder` (`formats/imd.{h,cpp}`, pure, unit-testable) and
    `VideoPlayer` (`engine/video.{h,cpp}`);
  - `SaveFormat` per game (section 5.5).
  - The existing v2 goblins and mult stay as they are; v3 changes go into
    them as small `if (_ver >= 3)` branches, as ScummVM does.
- **The disassembler** gets the same version selection: operand specs per
  version, chosen from the TOT header's version byte, so `gobdis` and
  `gobexport` stay correct for every game.
- **Game rules** (`engine/rules.h`) collect what the engine hard-codes per
  game:
  - the cutscene TOTs (`inCutscene`);
  - the skippable transitions (`skip.cpp`);
  - the room/level list and the jump mechanism (`levels.cpp`);
  - the quick save slot and format;
  - the copy-protection skip;
  - CD background/narration tables (Gob1);
  - data workarounds (ECHEQUI, the Gob1 INTER reload fix, the EMAP1018
    Blount fix).

  Gob2's current behaviour moves behind `rules` unchanged. Games without an
  implementation of a feature report it as unavailable, and the overlay
  hides the button.

### 5.3 Per-game folders and settings

- **Done in G0:** `--pref-root DIR` (the game's folder is
  `DIR/<prefFolder>/`); without it, Gob2 keeps
  `SDL_GetPrefPath("gob2native", "Gobliins2")` and Gob1/Gob3 already use
  `gobnative/Gobliiins/` and `gobnative/Goblins3/`, so only Gob2 needs the
  migration in R. Save files are named `<key>-slotNN.sav`. **Done in R:**
  the shared `gobnative/settings.ini` (recent folders, last game) and the
  picker of 5.1 (`frontend/picker.cpp`, `frontend/prefs.cpp`).
- The SDL pref root becomes `gobnative`, with one subfolder per game:
  `~/Library/Application Support/gobnative/{Gobliiins,Gobliins2,Goblins3}/`.
  Each subfolder holds `settings.ini`, save games, `HDPacks/`, `Music/` and
  `Screenshots/`, as the Gob2 folder does today.
- File names of saves get the game key: Gob2 keeps `gob2-slotNN.sav` and
  `gob2-notes.sav`; Gob3 uses `gob3-...`; Gob1 uses `gob1-quick.sav`.
- Video and audio preferences stay per game, which is simplest and lets a
  game keep its own pack and filter. A small shared
  `gobnative/settings.ini` holds only the recent game folders and the last
  game played.
- `--settings`, `--save-dir`, `--hdpacks-dir` and `--music-dir` keep
  overriding everything. A new `--pref-root DIR` replaces the SDL pref
  root, so tests and the migration check can run in scratch folders.
  **Every test must pass `--pref-root` or the individual overrides.**

### 5.4 App rename and Gob2 settings migration (milestone R)

- The CMake target and binary become `gobnative`, window titles
  "Gobliins 2 (gobnative)" and so on.
- A `gob2native` executable keeps working: a post-build symlink in `build/`,
  or a second tiny target sharing the sources until the README stops
  mentioning it.
- The tools keep their names (`gobrun`, `gobdis`, ...); `gob2view` becomes
  `gobview`, with an alias.

**Migration** of `~/Library/Application Support/gob2native/Gobliins2/` must
never lose data. It runs only for Gob2, and only when
`gobnative/Gobliins2/` does not exist yet:

1. If `gob2native/Gobliins2/` exists, `rename()` it to
   `gobnative/Gobliins2/`. That is the same volume, so the move is atomic
   and copies nothing, which matters because HD packs can be gigabytes.
2. Create a symlink `gob2native/Gobliins2 -> ../gobnative/Gobliins2`, so an
   older gob2native build still finds its settings, saves and packs.
3. If the rename fails (another volume, permissions), keep using the old
   folder in place: the app uses `gob2native/Gobliins2/` as Gob2's pref
   folder and logs why. It never copies-and-deletes.
4. Never delete anything. If both folders exist, the new one wins and the
   old one is left untouched.

Testing: `--pref-root` points at a scratch root that contains a fake
`gob2native/Gobliins2/` with a settings.ini, a save and a pack folder. Check
the result and check that a second start does nothing. Never run this
against the real folder from a test. The user's real folder migrates the
first time they start the renamed app.

### 5.5 Saves per game

- **Gob2:** unchanged (G2NS slot files).
- **Gob3:** a new handler for `CAT.INF`:
  - the props block, kept as `gob3-props.sav`;
  - 30 slots, each a slot file with magic "G3NS" holding the name, the
    variables and a 120x160 screenshot with its palette, for `IMA.INF`;
  - `INTRO.$$$` as the temporary sprite;
  - `BLOC.INF` notes of 2560 bytes.
- **Gob1:** has no saves. Quick save writes an engine snapshot
  (`gob1-quick.sav`) in our own format: current TOT, the variables, the
  GoblinsV1 state, pocket, objects and map items. It is loaded at the
  level's idle point. Before that exists (G1.7), the level select (password
  route) is the fallback.

### 5.6 Mods features per game

| Feature | Gob1 | Gob3 |
|---|---|---|
| overlay, filters, speed, screenshots | as is | as is; the presenter shows the 320x200 window of the scrolled screen |
| turbo, skip | skip intro/final/mult cutscenes; fast-forward via rules | skip IMD cutscenes (Escape already aborts IMDs); fast-forward rules |
| level select | `v[286] = level` and load INTER, the way a password does | MENU-driven like Gob2, to be studied (G3.8) |
| quick save | engine snapshot (G1.7) | through CAT.INF in the last slot, like Gob2 |
| HD packs, export, frames, guide | per TOT as today | per TOT; IMD frames are not covered (later: an IMD frame export/import tool) |
| replacement music | track names from GOB.LIC | track names from MUS_GOB3.LIC |
| sharp text | 6x8 fonts | needs >8x8 support |

## 6. Milestones

Each milestone fits one agent session. Rules for every one of them:

- Keep Gob2 green: run all `test_*`, `tests/check_quicksave.sh
  ../gobliins2`, `gobdis ../gobliins2 --all` (0 errors), and a
  `gobrun ../gobliins2 --press-every 3 --press-stop 100 --frames 9000`
  that reaches GOB01.
- Update this file's checkboxes.
- Document new formats and opcodes in `docs/`.
- Headless checks use `gobrun` with scratch `--save-dir` (and
  `--pref-root` once it exists). `--snap-every N DIR` writes screens as BMP
  for a visual check. "No stubs" means `grep -c stub` on the trace is 0
  over the stated run.

### 6.1 G0: multi-game plumbing and detection

- **Work:**
  - `engine/game.h` (GameInfo, detectGame);
  - `--game` in gob2native and every tool;
  - the Interpreter takes a GameInfo, with `_ver` and the hook points of
    5.2 as empty stubs;
  - `engine/rules` with Gob2's current rules moved behind it;
  - default fonts and the startup LIC from GameInfo (Gob1: `GOB.LIC`);
  - VAR16, VAR13 and VAR14 kept per game;
  - per-game pref folders under a configurable pref root (`--pref-root`),
    without the rename and migration yet: the app still uses
    `gob2native/Gobliins2` for Gob2;
  - window title from GameInfo.
- **Accept:**
  - `test_detect` passes (fake folders);
  - `gobrun` prints the detected game for all three folders;
  - the Gob2 regression set is unchanged;
  - the trace of a Gob2 run is byte-identical before and after, apart from
    timestamps (diff the first 9000 frames).
- **Done.** Results:
  - `test_detect` passes; all other tests pass; `check_quicksave.sh` OK;
    `gobdis ../gobliins2 --all` output identical (0 errors).
  - Gob2 stdout byte-identical to the pre-G0 build (timestamps included,
    the clock is simulated) for: the 9000-frame intro run (ends in GOB01),
    the same run with `--trace-ops --language german` (25980 lines),
    a `--level-at 110 GOB08 --skip-at 115 --watch-skip --print-objects`
    run, and a quick save/quick load run (identical `--dump-vars` and slot
    file; only the save path in the trace differs). The detected game goes
    to stderr ("game: ..."), so stdout is unchanged.
  - gobrun, gobdis, resdump, gobexport, gob2view and gob2native report
    Gob1, Gob2 and Gob3 for the three GOG folders; FDD folders with
    DISKn.STK are refused as floppy versions (the tools fall back to Gob2
    with a warning).
  - The GUI starts all three games with a scratch `--pref-root` (window
    titles "Gobliiins (gob2native)" etc.); the Gob1 overlay has no Levels
    tab and lists only its five languages.
- **Notes for the next agent:**
  - `engine/game.{h,cpp}`: `GameInfo` (key, title, prefFolder,
    engineVersion, start archive/TOT, default fonts, startup LIC, VAR
    13/14/15 values, languages, `scriptLanguage()` for VAR 16: Gob1 clamps
    to 0..4), `detectGame()`, `selectGame()` (with `--game`),
    `selectToolGame()` (tools), `takeGameOption()` (strips `--game KEY`
    from argv anywhere, so positional parsing is unchanged).
  - `engine/rules.{h,cpp}`: `GameRules` per game. Gob2: cutscene TOTs,
    transition TOTs, `LevelSystem::Gob2`, `SaveFormat::Gob2`, the INTRO0
    copy-protection calls, the GOB06 switchTotSub fix. Gob1 and Gob3 rules
    are empty: no level select, quick save, fast-forward or saves (the
    scripts see the save files as missing); only playMult counts as a
    cutscene. `levels.cpp` and `skip.cpp` stay Gob2's implementation,
    gated through `roomUnderMenu()` / `hasFastForward()`; G1.7 and G3.8
    add their own systems as new `LevelSystem` values.
  - The Interpreter takes the `GameInfo` (`game()`, `rules()`, `_ver`).
    Hooks: `execFuncV1/V3` (before the switch in `execFunc`; the
    instruction's operands are unread), `execDrawV1/V3` (after the draw op
    byte), `goblinFuncV1/V3` (after cmd and paramCount), all `Task<bool>`
    returning false for now, in `engine/interpreter_v1.cpp` and
    `engine/interpreter_v3.cpp`. They are not called for Gob2, so the Gob2
    path is unchanged. `getFreeMem`/`checkData` still write `v[16]`
    (mask them in the v1 hook, G1.1).
  - The front end: `OverlayHost::currentGame()`; the Levels tab is only
    added when `hasLevelSelect()`; a saved language the game doesn't offer
    becomes the game's first language (Gob1: English UK).
  - Test runs: `gobrun ... --save-dir <scratch>`; the GUI with
    `--pref-root <scratch>` (it creates `<scratch>/<prefFolder>/` with
    Music/ and HDPacks/). Never run the GUI without it from a test.

### 6.2 Goblins 1

**G1.1: v1 interpreter basics, part 1 (dispatch and data ops).**
- **Work:**
  - v1 func and draw hooks: D21 getCDTrackPos, F3A loadSound, F3E and F3F
    without `v[16]`, F4D and F4E, checkSwitchTable "last match wins",
    playMult and freeMultKeys operands, setRenderFlags;
  - the disassembler's v1 operand specs, including the goblinFunc layout;
  - GOB.LIC at startup;
  - the language clamped to 0..4 for Gob1;
  - the copy-protection skip (find the call by trace).
- **Accept:**
  - `gobdis ../gobliiins1 --all` reports 0 errors;
  - `gobrun ../gobliiins1` plays INTRO to the main menu with
    `playCDTrack "intro"` resolved and no stubs in INTRO;
  - Gob2 is green.

- **Done.** Results:
  - `gobdis ../gobliiins1 --all`: 0 errors (769 functions, 20946
    instructions). `gobdis` output for every Gob2 and Gob3 TOT is
    byte-identical to the pre-G1.1 build.
  - `gobrun ../gobliiins1 --press-every 5 --frames 9000`: INTRO plays
    both mults with `playCDTrack "intro"` and `"g1final"` resolved, no
    stubs in INTRO, then INTER loads AVT003 (see the baselines in 1).
  - Gob2: all tests pass, `check_quicksave.sh` OK, the 9000-frame intro
    run and the `--trace-ops --language german` run are byte-identical to
    the pre-G1.1 build.
- **Notes for the next agent:**
  - Opcode differences are documented in `docs/opcodes.md` section 10
    (func, draw, goblinFunc layout with operand counts, mult sound keys).
  - `src/engine/interpreter_v1.cpp`: `execFuncV1` (assign, stopSound,
    loadSound, getFreeMem, checkData, readData, writeData, masks 0x18-0x1A),
    `execDrawV1` (playMult, freeMultKeys, setRenderFlags, getCDTrackPos,
    masks the v2-only draw ops), `goblinFuncV1` (skips by paramCount and
    traces `stub goblin N`), `checkSwitchTableV1` (called from
    `checkSwitchTable` when `_ver == 1`). printText has two `_ver == 1`
    branches in `interpreter.cpp`. `loadSoundResource()` was split out of
    `loadSoundSearch()` (mult.cpp) for loadSound.
  - The disassembler takes `_ver` from the TOT header (`versionMinor`);
    `drawSpecV1()` and `goblinOpV1()` hold the v1 tables.
  - **Copy protection:** the GOG CD INTRO.TOT has no `callSub(3905)` (no
    such bytes at all), so no rule was added (`docs/opcodes.md` 10.4).
  - Gobliiins uses no loadSound, playSound, readData, writeData, checkData
    or animPalInit (sounds come from the AVO files), so those hooks are
    untested on real data. getFreeMem is in every level's quit hotspot.
  - The mults still go through the v2 loader and renderer: playMult runs
    the frames and the CD sync works, but nothing is drawn (G1.2). The v2
    loader would misread v1 sound keys with cmd 3/5; the intro mults have
    no sound keys.
  - Not done (left for later milestones): ScummVM stops the CD at every
    TOT load in Gob1 (`game.cpp:588`, "Gobliiins music stopping"); keyFunc
    and animatePalette do not call v1 animPalette (unused by the data).

**G1.2: Draw_v1 and Mult_v1 (the intro and the level pictures).**
- **Work:**
  - v1 printTotText (with the 0xBA variable insert);
  - animateCursor/blitCursor differences;
  - `MultV1`: loader, sound keys, the `_renderData` renderer,
    playMultInit;
  - D14/D17 v1; Scenery_v1 loadAnim (waits for the CD); maxDrawOrder 100.
- **Accept:**
  - the intro animation plays in sync with the CD (snapshots at 10/20/30 s
    show the intro, not black);
  - FINAL plays (`--start FINAL`);
  - `--start INTER` shows the password screen and accepts a typed code
    (`--type`).

- **Done.** Results:
  - Snapshots every 10 s of `gobrun ../gobliiins1 --frames 9500` show the
    feast mult at 20/60/100 s, the title mult at 140 s and AVT003 at
    152 s. FINAL and the INTER password screen as in the baselines (1).
    No stubs other than goblin ops in INTRO, FINAL, INTER.
  - Gob2: tests, `check_quicksave.sh`, the 9000-frame intro run, the
    `--trace-ops --language german` run and the `--level-at 110 GOB08
    --skip-at 115 --watch-skip --print-objects` run are byte-identical to
    the pre-G1.1 build; a 2500-frame Gob3 run is identical too.
- **Notes for the next agent:**
  - Formats and behaviour: `docs/animation.md` section 5 (statics,
    Mult_v1 loader, playMult, renderer), `docs/graphics.md` 1.3b
    (Draw_v1 printTotText).
  - `Scenery` now has statics (`loadStatic`, `freeStatic`,
    `renderStatic`, `updateStatic`, `curStatic`/`curStaticLayer`,
    `maxDrawOrder` = 100 for v1); `reset()` frees them. Gob3 can reuse
    them for its loadStatic/renderStatic.
  - `engine/mult_v1.cpp`: `loadMultV1`, `playMultV1` (+ `playMultInitV1`,
    static/anim/palette/fade keys), `animateV1`, `newCycleAnimV1`,
    `freeMultKeysV1`, `sceneryLoadStatic`, `sceneryLoadAnimV1` (waits for
    the CD track first), `loadMultObjectV1`. Draw op dispatch in
    `execDrawV1`; `printTotTextV1` in `interpreter_v1.cpp`.
  - `MultObject::store`: objects can keep their position and anim data in
    a private `Variables` (playMult's own four objects); null means the
    script variables. All access goes through `objVars()`.
  - initMult (draw 0x14) is the version 2 one: Gobliiins' animDataSize is
    10 as in Gobliins 2. The AVT levels call initMult once; Goblin_v1's
    drawObjects (G1.4) will use that anim surface.
  - The intro mults use static keys, anim keys, one palette key and text
    keys; no mult uses fade or sound keys, so those paths are untested.
  - Not done: the v1 animateCursor/blitCursor differences (the v2 ones
    run; the headless snapshots do not show the cursor, so this is
    unchecked); printTotText's collision check
    (render flag 4, unused: Gobliiins never calls setRenderFlags) and the
    narration start (G1.6).

**G1.3: Map_v1 and the AVO loader.**
- **Work:**
  - `GoblinsV1` skeleton: AVO parser (pass map, items map, waypoints, item
    positions, sounds, goblin/object state tables, item-to-object) with the
    hard-coded patches;
  - a `resdump`-style dump (`gobdis --avo` or `resdump --avo`) for
    checking;
  - `docs/goblins-v1.md` format section.
- **Accept:**
  - all 22 AVO files parse with consumed size equal to file size (or a
    documented remainder);
  - a unit test with a synthetic AVO passes.

- **Done.** Results:
  - `resdump ../gobliiins1 --avo`: all 22 files parse ("22 files, 0
    failed"); each leaves 2 or 90 bytes unread, which the engine never
    reads either (a zero word, in 11 files preceded by 88 bytes of what
    look like editor rectangle records; `docs/goblins-v1.md` 1).
  - `test_avo` passes (a synthetic level with every section, goblins
    reading the object position records, truncated files).
- **Notes for the next agent:**
  - `formats/avo.{h,cpp}`: `parseAvo()` is the pure format; the engine
    applies the hard-coded patches in `GoblinsV1::loadMapObjects`
    (`docs/goblins-v1.md` 2). `resdump <game> --avo [NAME]` prints a
    summary of all files, or one file in full (maps, way points, states).

**G1.4: Goblin_v1, part 1 (objects, states, drawing).**
- **Work:**
  - goblin ops 1-37 (setters and getters), 1000-1004, 2005, 150-252;
  - nextLayer, animateObjects, drawObjects (with state sounds, which
    restart the CD background music);
  - variable binding at bytes 236-456;
  - per-TOT CD background music table.
- **Accept:**
  - `gobrun --start AVT00` shows the first level with its three goblins
    standing and idle animations running (snapshot);
  - level music plays (`--audio-dump`, non-silent CD channel);
  - no stubs in AVT00 before the first click.

- **Done.** Results:
  - `gobrun ../gobliiins1 --start INTER --set-var 0 286 3 --set-var 0 296
    10` shows AVT00 with its three goblins standing, the apples and the
    item bar (snapshots); from the intro (`--press-every 5`) AVT003 shows
    its goblins with idle and boredom animations, a rabbit and a bird.
    `--start AVT00` alone starts with no energy (see the baselines in 1).
  - Level music: `playCDTrack "mine"` (AVT00) / `"extsor1"` (AVT003)
    when the level starts; the `--audio-dump` is non-silent from the
    first seconds.
  - No stubs in AVT00 or AVT003 before the first click (nor after).
  - Gob2: tests, `check_quicksave.sh`, `gobdis --all` and the 9000-frame
    intro run identical to the pre-G1.3 build.
- **Notes for the next agent:**
  - `engine/goblins_v1.{h,cpp}`: `GoblinsV1`, a friend component of the
    Interpreter (created for version 1), ports Map/Map_v1, Goblin/
    Goblin_v1 and the v1 goblin ops; `goblinFuncV1` calls
    `GoblinsV1::goblinFunc`. Behaviour in `docs/goblins-v1.md` 3-8.
  - Goblin sounds live in interpreter sound slots 40-55
    (`kSoundSlotBase`); `loadSoundData()` was split out of
    `loadSoundResource()` (mult.cpp) and `startCdTrack()` out of the
    playCDTrack op, both unchanged for Gob2.
  - Missing states and out-of-range item ids (which would crash or read
    garbage in the original) use harmless defaults; the first missing
    state is traced once ("goblins: missing state").
  - `gobrun --print-objects` prints the goblins (with their cells), the
    objects and the pocket after the mult objects.

**G1.5: Goblin_v1, part 2 (movement, items, selection).**
- **Work:**
  - doMove, moveInitStep, adjust*, target*, moveTreatRopeStairs,
    moveAdvance and path finding;
  - switchGoblin, peekGoblin, boredom;
  - treatItem: pick, place, swap;
  - ops 40-44, 50-53, 200-203, 500-605 and 1005-1015;
  - footsteps on the speaker;
  - the INTER `v[285]` reload fix.
- **Accept:**
  - scripted `--click` runs walk a goblin to a target cell and back (print
    objects), pick up and drop an item in AVT00, and switch goblins;
  - a click script that solves AVT00 (written in the session, kept in
    `tests/`) reaches INTER and AVT001.

- **Done.** Results:
  - Walking: `--click` runs walk goblin 0 across AVT00 (direct paths and
    through way points, up the ladder to the branch at cell 15,7);
    `--print-objects` shows the target cells. AVT00's lower path is cut
    by the broken bridge (the pass map has no walkable cell there until
    the script mends it), so "no path" there is correct.
  - Items: in AVT00 the picker drops the pickaxe on an empty cell (pick
    cursor, click) and picks it up again (pocket object 5, item 2), with
    goblin sound 14 at 9000/5000 Hz.
  - Switching: the portrait click switches goblins 0 -> 1 -> 2 -> 0.
  - `tests/check_gob1_level1.sh ../gobliiins1` solves AVT003: the fighter
    kicks the gate (the horn falls), the picker picks it up and blows it
    (a branch falls), the wizard turns the branch into a pickaxe, the
    picker swaps the horn for it and clicks the exit; the level code
    VQVQFDE shows, then INTER starts AVT00. No stubs.
  - Footsteps: speaker clicks on frames 3 and 6 of each walking cycle
    (non-silent speaker channel in `--audio-dump` with `--volumes 1 0 0
    1`).
  - Gob2: green (as for G1.4).
- **Notes for the next agent:**
  - The plan asked for a script solving AVT00 that reaches AVT001. The
    real order is different: INTER maps level 100 to AVT003 (the first
    level the intro starts), AVT003 sets the next level to 3 (AVT00), and
    AVT00 sets 0 (AVT002). So the test solves the first level instead.
    AVT00 itself (the tree with the apples and the broken bridge) is not
    solved yet; a G1.6 click script could add it.
  - Script conventions found while solving (useful for more click
    scripts): v[111] is the cursor action (0 walk, 3 act/use, 4 pick;
    right clicks in the room cycle walk -> act -> pick (goblin 0 only) ->
    walk), v[756] "use the pocket item", v[285] the item being acted on
    (the level's switch dispatches its puzzle functions on it), v[59] the
    "busy" lock, v[296] the energy (0..10), v[16] the level result (1
    lost, 3 solved), the portrait at (165, 182) switches goblins, and the
    exit "GO" sign is object 10 at hotspot (v[287], v[288], v[289],
    v[290]). Click points are offset by the cursor's hot spot: +6,+7 when
    acting, +7,+12 when picking.
  - The INTER `v[285]` reload workaround of ScummVM does not apply to the
    CD INTER.TOT (`docs/goblins-v1.md` 9).
  - `gobrun --rclick T X Y` was added (a right click).

**G1.6: Goblins 1 hotspots, narration, full playthrough pass.**
- **Work:**
  - narration tracks via printTotText (6 levels, per language);
  - the energy bar and the remaining v1 opcodes found by running every
    level through `INTER` with `--level` (password route);
  - fix desyncs.
- **Accept:**
  - every AVT level starts and reaches its idle loop without stubs or
    errors (loop over levels 0-21 headlessly);
  - INTRO→AVT00 and FINAL play;
  - narration audible in English and French dumps.

- **Done** (except a full playthrough). Results:
  - Every level, started through INTER (`--set-var 0 286 N` for N = 100
    and 0-21, full energy), loads its objects and runs 1500 frames with
    no stubs or errors; snapshots show all 22 rooms with their goblins.
    With 45 pseudo-random clicks, right clicks and goblin switches per
    level over 100 s, no level reaches a stub, a missing state or an
    error, also under AddressSanitizer/UBSan.
  - Narration: AVT005 plays `fra1` with `--language french` and `ang1`
    with british/american, audible in `--audio-dump` (CD volume only),
    then the level music (`zombie`) when it ends.
  - INTRO -> INTER -> AVT003 and FINAL -> INTRO play with no stubs.
  - No v1 opcode was missing: the scripts reach no stub.
- **Notes for the next agent:**
  - The narration table is in `printTotTextV1` (interpreter_v1.cpp).
  - The energy bar is drawn by the level scripts (fillRect on rows
    167-170 from `v[296]`); nothing engine-side.
  - Not done: a full playthrough (only level 1 has a click script);
    checking each level's puzzles needs their solutions. The access code
    shown after a level is computed from a per-level table in the script
    (function 0x15BA in AVT003) and the remaining energy; after level 1
    with full energy it is VQVQFDE, which the password screen accepts.

**G1.7: Goblins 1 rules: level select, skip, quick save.**
- **Work:**
  - rules for Gob1: level list (INTER's table, with names from the manual or
    the TOT texts), jump = `v[286]` plus load INTER;
  - cutscenes = INTRO and FINAL, playMult skip;
  - fast-forward for scripted sequences;
  - quick save/load as an engine snapshot (5.5) with a version field.
- **Accept:**
  - `gobrun --level-at T 5` reaches level 5;
  - `tests/check_quicksave_gob1.sh` (save, load, compare `--dump-vars` and
    `--print-objects`) passes;
  - the overlay Levels tab lists the 22 levels (screenshot test with
    `--overlay-at`).

- **Done.** Results:
  - `gobrun ../gobliiins1 --level-at 5 5` reaches AVT005 (level 5 of the
    play order) with full energy; `tests/check_gob1_levels.sh` jumps to all
    22 levels from the intro and types the 21 access codes on INTER's
    password screen: every level starts with energy 10, no stubs.
  - `tests/check_quicksave_gob1.sh` passes: refusals (no quick save, not in
    a level), save in AVT003 after the horn is picked up, load from AVT00
    with energy 4: back in AVT003, variables (apart from VAR 0-15) and the
    `--print-objects` lines identical at the save and the load, then the
    rest of the walkthrough solves the level; a truncated file and a file
    of another version are refused.
  - The GUI's Levels tab lists the 22 levels with their codes
    (`gob2native ../gobliiins1 --pref-root <scratch> --level-at 3 5
    --overlay-at 12 Levels --screenshot-after 14 shot.png`); GUI quick save
    and quick load (`--quicksave-at`, `--quickload-at`) work.
  - Gob2: all tests, `check_quicksave.sh`, `gobdis --all` (0 errors), the
    9000-frame intro run, the `--level-at 110 GOB08 --skip-at 115
    --watch-skip --print-objects` run and the quick save/load run are
    byte-identical to the pre-G1.7 build (save paths and VAR 5-11, the
    wall clock, aside).
- **Notes for the next agent:**
  - `engine/levels_v1.cpp` holds the whole Gobliiins level system
    (`LevelSystem::Gob1` in `engine/rules.{h,cpp}`; behaviour in
    `docs/levels.md` 9). The public Interpreter API (`levels()`,
    `requestLevel`, `requestQuickSave/Load`, `hasQuickSave`,
    `currentLevel`) branches to the `...V1` functions; Gob2's code paths
    are untouched. Requests are serviced from every `checkHotspots` wait
    (`serviceLevelRequestsV1(levelLoop)`): saves only at the level's main
    loop, jumps and loads also at INTER's and a level's other waits. A
    jump ends the TOT with `_totToLoad = "INTER"`, `_terminate = 1`.
  - The quick save is our own snapshot (`gob1-quick.sav`): "G1QS", version
    1, key, level, variables, back and anim surfaces, cursor resources
    (`_cursorResources`, recorded by `loadCursorResource`) and
    `GoblinsV1::saveState()` (one template `serialize()` for both
    directions over `engine/snapshot.h`'s Writer/Reader; bump
    `kStateVersion` / `kSnapshotVersion` when fields change). Loading
    re-enters the level through INTER and applies the snapshot at its main
    loop: `GoblinsV1::redrawAll` redraws the objects over the clean room
    and only differing pixels are copied (so HD layers stay mostly intact:
    a quick load adds about 64 live pixels).
  - Skipping: Gob1 branches in `skip.cpp` (`inSkippableSequence`,
    `fastForwardEnd`); `noteTotStartV1` resets `_roomLoopReached` for each
    level. `_skipAllMults` makes a skip end both intro animations (used
    by level requests during the intro).
  - gobrun: `--list-levels`, `--state-on-notify PREFIX`, `--level-at T N`
    (Gob1: Nth level of the play order), and `printObjectState()` shared by
    `--print-objects`.

**G1.8: Goblins 1 mods integration.**
- **Work:**
  - `gobexport` / `--frames` / guide for Gob1 (correct palettes per AVT
    level, README in the export);
  - HD compositor check with a scaled test pack;
  - replacement music names;
  - README section for Gob1;
  - sharp text for 6x8 fonts.
- **Accept:**
  - `gobexport ../gobliiins1 X` then `--scale-export X P 4` then `gobrun
    --hd-pack` dumps match the 1x frames (the existing hdrender checks
    generalised);
  - the GUI starts Gob1 with a scratch `--pref-root` and takes a screenshot
    (with a timeout, killing only our PID).

- **Done.** Results:
  - `tests/check_hd_gob1.sh`: export, 4x nearest pack, 40 headless HD dumps
    (intro, doll, title, level 1 being solved, its code, INTER, AVT00, a
    quick load, game over, password, FINAL, a story text, all 22 rooms) all
    at mean difference 0.000 with 0 values off. The GUI with the same pack
    (`--hdpacks-dir --hd-pack Test --hd-dump-at`) matches too.
  - The GUI runs Gobliiins with a scratch `--pref-root` (screenshots of the
    Levels tab, a story text with sharp text, AVT00 after a quick load;
    killed only by our own PID, none hung).
  - `gobrun --guide-at` outlines Gobliiins' goblins and objects;
    `gobexport --frames --only AVT003` writes 168 layers; replacement
    music (`--music-dir` with `EXTSOR1.wav`) replaces level music; sharp
    text works with the 6x8 fonts (nothing to change).
  - Gob2: export (every file), `gobdis --all` and the regression runs are
    byte-identical.
- **Notes for the next agent:**
  - gobexport for Gob1 (`docs/hdpacks.md` 1): the disassembler records v1
    `loadSpriteToPos` and static descriptor pictures (only for `_ver ==
    1`, so Gob2's export is unchanged) and `ResourceUse::LevelObjects`
    (goblin 1000); `roomPalette()` picks the palette after it. Gob3 will
    need its own look at palettes (IMD-heavy, external texts) and at
    statics (`_ver == 3` can reuse the v1 static recording if it fits).
  - gobrun `--hd-pack DIR` / `--hd-dump-at T PREFIX` make HD checks
    headless and deterministic; `tests/check_hd_gob1.sh` is the template
    for a Gob3 version (with scrolling rooms).
  - `Interpreter::objects()` returns GoblinsV1's goblins (0-3) and objects
    (100 + n) for Gob1, so the Tools tab, `--print-objects` and the guide
    show them.
  - Click scripts (`tests/check_gob1_walkthrough.sh`): solutions were read
    from each level's `switch v[285]` puzzle functions (`v[106]` goblin,
    `v[114]` pocket object, `v[60]`/`v[61]` the goblin's state/frame, where
    states above 39 read as state - 40, `v[82]`-`v[105]` the acted-on
    object, `v[756]` set by the tick when the mouse is over a "use" area),
    the AVO item map (`resdump --avo`) and `--print-objects`. The original
    path finding only follows direct lines or way points from the nearest
    one, so from some cells nothing is reachable (AVT004: from the left
    column the way points lead up a ladder); the scripts walk in steps
    there, as a player would. In zsh, pass click lists with `${=VAR}`
    (zsh doesn't split variables), or write them out.
  - Done in G1.9: click scripts for levels 5-22. Was: levels 5-22 had no click script (they start, run and were
    fuzzed with random clicks in G1.6); the v1 animateCursor/blitCursor
    differences (G1.2 note) are still unchecked; the rename (R) is not
    done, so the app is still `gob2native`.

**G1.9: Gobliiins played through.**
- **Work:** click scripts for levels 5-22 and the ending, chained in one
  run from the intro as the game's own flow goes (no level select, no
  codes typed), with gobrun script commands for Gobliiins; engine bugs
  found on the way; the v1 cursor differences.
- **Accept:** `tests/check_gob1_walkthrough.sh` plays the whole game with
  full energy; all three games' checks stay green.

- **Done.** Results:
  - `tests/check_gob1_walkthrough.sh ../gobliiins1`: the intro, levels
    1-22 (each code equal to the Levels tab's code for the next level,
    energy 10), FINAL and the restart of the intro, in 1.5 s of real time.
    The same script also solves every level with `--language` french,
    german, italian, spanish and american (other narration lengths, so
    other timings and random numbers).
  - Choice: one run from the intro rather than chained level codes or
    quick saves, because the game's own flow allows it (each solved level
    leads on through INTER) and it checks what a player sees, codes
    included. Each level file also runs alone from the level select
    (`gobrun --level-at 3 N --script tests/gob1/NN-...txt`).
  - Cursor: version 1 shows the hotspot's cursor cell, never an animation
    frame (Gobliiins' cursor 2, shown while a button is held, is an empty
    cell, so the cursor vanishes during a click, as in ScummVM's
    Draw_v1), and its showCursor/blitCursor rules (`docs/graphics.md`
    5.5b); checked in the GUI with a scratch `--pref-root`.
  - No engine bug in the goblin logic was found: the six solver passes
    compared every suspicious case with ScummVM (path finding,
    initGoblin, clearPathExistence, moveFindItem) and found the same code.
  - Gob2: all tests, `check_quicksave.sh`, `gobdis --all` (identical for
    all three games, 0 errors) and the 9000-frame intro run to GOB01
    (identical trace; the variables differ only in VAR 10-11, the wall
    clock) as before; Gob3's checks, its walkthrough included, pass.
- **Notes for the next agent:**
  - gobrun script commands for Gobliiins (`tools/runner.cpp`): `goblin
    N` (portrait clicks until the engine's current goblin is N), `walk`/
    `act`/`pick`/`use X Y` (right clicks set the cursor, the click is
    placed so the cursor's hot spot lands on X,Y, then idle), `exit` (the
    exit sign, or a level that ends by itself, or FINAL; traces "(TOT
    solved, code C, energy E)"), `ifvar`, `tot NAME`. `roomIdle()` for
    Gobliiins is `levelIdleV1()`: no puzzle step (`v[285]`), no busy lock
    (`v[59]`, ignored once the exit sign shows), the level not ending, the
    current goblin neither walking nor acting.
  - Level quirks the scripts handle (all original behaviour): levels that
    end without an exit sign (AVT007, AVT018, AVT021, AVT022 into FINAL),
    AVT001's random thread (`v[307]`), the exit clicked while the picker
    carries an item with the use cursor loops forever, path finding in
    steps, clicks on a goblin select it (`docs/levels.md` 9).
  - Timed puzzles wait for the level's own variables; AVT017 syncs with
    the boredom animation. Keep that style when changing scripts: the
    chained run's random numbers differ from a single level's run.

### 6.3 Goblins 3

**G3.1: v3 opcodes and ITK.**
- **Work:**
  - `goblinFuncV3`: 100 = wobble (until implemented, a palette fade-in, no
    handleGoblins);
  - openItk/closeItk (STK archives under `.ITK`; a missing voice ITK
    (FRGOB3, DEGOB3, ITGOB3) falls back to the English USGOB3.ITK, as
    decided in the open issues);
  - copySprite retrace;
  - getTotTextItemPart (func 0x1A);
  - loadSound 0x3A, loadStatic, renderStatic and freeStatic (statics are
    shared with v1, so check what G1.2 built);
  - subtitles flag (0x8000) with `_doSubtitles` on;
  - the disassembler's v3 names.
- **Accept:**
  - `gobrun ../goblins3` runs INTRO→DEMO→MENU→EMAP1000 with only playImd,
    scroll and mult-IMD stubs left;
  - Gob2 is green.

- **Done.** Results:
  - `gobrun ../goblins3 --press-every 5` runs INTRO -> DEMO -> MENU ->
    EMAP1000 with only playImd (DEMO's INTRO.IMD) and setScrollOffset
    stubs, then stops at the known desync after EMAP1000's loadMult with
    IMD keys (0x2B13, G3.5). openItk opens USGOB3.ITK and IMD.ITK;
    getTotTextItemPart fills the item names (`as[7418]`: COIN, UMBRELLA,
    PLUNGER, ...); EMAP1000's statics load and render.
  - `gobdis ../goblins3 --all` output unchanged (no goblin ops in the
    data); Gob1 and Gob2 `gobdis --all` identical.
  - Gob2: all tests, `check_quicksave.sh`, the 9000-frame intro run and
    the `--trace-ops --language german` run byte-identical to the pre-G3.1
    build. Gob1: all `check_gob1_*.sh`, `check_quicksave_gob1.sh` and
    `check_hd_gob1.sh` pass.
- **Notes for the next agent:**
  - Behaviour: `docs/opcodes.md` section 11. Code: `engine/interpreter_v3.cpp`
    (hooks plus `printTotTextV3`, `getTotTextItemPart`, `addHotspotV3`,
    `removeHotspotV3`, `loadSoundV3`, `sparseRetrace`, `wobble`). The
    version 2 opcodes Gobliins 2 never uses live there too, so the shared
    switch stays Gob2's.
  - `DataFiles` has archive slots with a base flag (`openArchive(name,
    base)`, `closeArchive(base)`); manageDataFile("") is still ignored
    (Gob2 behaviour; Goblins 3 calls it only when quitting).
  - The speech fallback is `GameRules::voiceArchives` / `voiceFallback`
    (`archiveFallback()`), traced as `openItk "FRGOB3.ITK" (missing:
    opened USGOB3.ITK instead)`. Spanish reads VOICE.INF (absent: 0 ->
    FRGOB3 -> USGOB3), so no VOICE.INF rule was needed.
  - `Interpreter::subtitles` (default on, as ScummVM forces for the CD
    version) hides text items flagged 0x8000 when off; G3.9's speech
    option sets it. INTRO sets render flag 0x400 (no subtitles) for the
    voiced languages; ignored, as ScummVM does.
  - No Goblins 3 text uses printTotText commands other than 2, 3 and 5,
    and no script uses addHotspot, removeHotspot or a goblin op, so those
    paths are untested on real data.

**G3.2: IMD decoder.**
- **Work:**
  - `formats/imd.{h,cpp}`: header v2-v4, coordinates, frame tables, audio
    properties, the frame command loop, chunk types 0x01, 0x02, 0x81 and
    0x82;
  - LZ77 standard and extended (0x1234/0x5678);
  - 8-bit PCM slices; palettes;
  - `docs/imd.md` with the format;
  - `gobvideo` tool: decode an IMD to BMP frames and WAV (scratch output
    only).
- **Accept:**
  - every IMD in IMD.ITK, USGOB3.ITK and MDOS.IMD decodes with the frame
    count matching and no chunk overrun (a `--check-all` mode);
  - `test_imd` passes with synthetic IMDs built in the test (each chunk
    type and LZ77 variant).

- **Done.** Results:
  - `gobvideo ../goblins3 --check-all`: 508 videos (IMD.ITK, USGOB3.ITK,
    MDOS.IMD), 18660 frames, 0 failed (frame counts match, no overrun).
    Frames checked by eye: INTRO's cabin, a talking head, MDOS's logo;
    INTRO's WAV is 52.9 s of 22050 Hz sound for 356 frames at 7 fps.
  - `test_imd` passes (every block type, both LZ77 variants, palette
    chunk and command, sound slices, breaks, rectangles, offset table,
    seek with and without it, a jump, broken data).
- **Notes for the next agent:**
  - Format: `docs/imd.md`. `ImdDecoder::load()` takes the whole file;
    `decodeFrame(dst, w, h, pitch, frame)` draws into any 8-bit buffer at
    the video's position (`setXY`), clipped, and returns the dirty
    rectangles, palette change and the frame's sound (signed 8-bit, the
    first frame's `soundStart` carries the pre-buffered slices).
  - Goblins 3 uses only block types 0x01, 0x02, 0x81, 0x82, versions 2
    and 4, no jumps. The jump keeps ScummVM's frame numbering (target - 1).
  - Seeking without an offset table re-decodes from the start (ScummVM
    does that only for silent videos and with an off-by-one; ours lands on
    the asked frame).

**G3.3: video player and playImd.**
- **Work:**
  - `VideoPlayer` slots;
  - o2_playImd (startFrame -2/-1 semantics, palCmd, back/front surface
    targets, invalidation, VAR(7)/VAR(11), Escape abort);
  - getImdInfo;
  - audio via a new mixer voice (speech) with its own volume ("Speech" in
    the Audio tab);
  - blocking playback on the game clock (video frame rate from the audio
    rate);
  - the CAIL1/2 cursor fix.
- **Accept:**
  - DEMO plays INTRO.IMD with sound (snapshots and `--audio-dump`);
  - a room's talking head plays with speech (dump shows speech-channel
    energy while the clip runs).

- **Done.** Results:
  - DEMO plays INTRO.IMD (356 frames, 1.0 s to 52.4 s at 7 fps) with its
    sound, then the Goblins 3 title; snapshots at 20/40 s show the cabin
    scenes, the `--audio-dump` with `--volumes 1 0 0 0` (speech only) has
    energy from 0 to 52 s.
  - A talking head: in EMAP1000, `--set-var 90 60 1002` (the event that
    starts Blount's first line) plays NEF1.IMD at the speaker with the
    subtitle "I must leave this ship with the barrel that is at the
    stern."; speech-channel energy from 90 to 94 s only (26 frames at
    7 fps), nothing before or after.
  - EMAP1000 now runs (the 0x2B13 desync was the version 1.3 mult loader
    not reading the IMD file names; the keys are read, played in G3.5).
  - The GUI (scratch `--pref-root`) plays the intro video; the Audio tab
    shows a Speech slider for Goblins 3 only.
  - `test_mixer` covers the speech voice; Gob2/Gob1 regression set green
    (identical traces, gobdis output, all check scripts).
- **Notes for the next agent:**
  - Behaviour: `docs/imd.md` 4. Code: `engine/video.cpp` (`playImd`,
    `getImdInfo`, `openVideo`, `playVideo`, `playVideoFrame`), the primary
    video in `Interpreter::_video`. Only blocking playback of the primary
    slot exists; Goblins 3's scripts only ever play whole videos
    (startFrame 0, lastFrame -1). Live (non-blocking) videos and other
    slots are what G3.5's mult IMD keys need (`Mult_v2::playImd` /
    `advanceObjects`, ScummVM's `VideoPlayer::play` with noBlock).
  - `_inPlayImd` makes `inCutscene()` true: videos run at 1x and the Skip
    button cancels them (as Escape does).
  - Mixer: `playSpeech/stopSpeech/speechPlaying`, `Volumes::speech`; the
    front end's `Settings::speechVolume` (`audio/speech`), the runner's
    `--speech-volume`. The speech on/off option (G3.9) can mute the voice
    for videos from the speech archive (USGOB3.ITK) and keep subtitles on.
  - Rooms call `writeData "INTRO.$$$"` before a talking head (the temp
    sprite of the screen under it; G3.7) and scroll the room to the
    speaker (G3.4), so the heads are placed as in an unscrolled room for
    now.

**G3.4: wide screens, split bar and scrolling.**
- **Work:**
  - variable-size back surface;
  - initScreen 0x14/0x18 with split;
  - scroll and setScrollOffset;
  - retrace of the scrolled window plus the bar;
  - evaluateScroll at the mouse edges; the scroll offset in the mouse
    coordinates and hotspots;
  - the EMAP1008 fix;
  - HD compositor and presenter handling of the scroll window
    (`hdrender` tests for a 472-wide surface).
- **Accept:**
  - EMAP1000 shows the room and the bar;
  - `--move` to the right edge scrolls (snapshots differ by the expected
    offset);
  - `test_hdrender` covers scrolling;
  - Gob2 is green.

- **Done.** Results:
  - EMAP1000 shows the ship and the empty text bar (MENU's initScreen
    0x2814, 472, 200); `--move 90 319 100` scrolls the room by the full
    152 pixels within a second (snapshots at 89 and 91 s differ by
    exactly 152 columns above the bar; the bar rows are identical).
  - `test_hdrender` covers a 472x200 screen with a 40-row split: the HD
    frame equals the composed view, changes in the shown window upload
    only their tiles, scrolling shows the rest, and a 320x200 screen is
    shown directly again.
  - The GUI (scratch `--pref-root`, Escape over the intro, pointer moved
    to the right edge) shows EMAP1000 scrolled to its end with the bar.
  - No stubs from INTRO to EMAP1000; Gob2/Gob1 regression set green
    (identical traces and gobdis output, all check scripts incl.
    `check_hd_gob1.sh`).
- **Notes for the next agent:**
  - Behaviour: `docs/graphics.md` 9. `Draw::initScreen(w, h, split)`
    (the old `initScreen()` is the 320x200 case), `scrollX/Y` (scripts),
    `viewX/Y` (shown), `splitHeight/splitStart`, `screenSurface()` (the
    320x200 view, composed only when `composed()`), `displayToSurface()`.
    `Interpreter::screen()` returns `screenSurface()`, so the front end,
    snapshots and the HD compositor see the display.
  - Mouse: `pollMouseV3()` (from `checkKeys` for version 3) clamps to the
    area above the bar (warping the pointer back), adds the view offset
    and requests edge scrolling; `evaluateScroll()` runs in the hotspot
    loop. The runner's host ignores warps, which doesn't matter: a touch
    of the right edge scrolls to the end anyway.
  - Not checked: EMAP1012's vertical scrolling (320x240) and the bottom
    edge; the room guide (`tools/roomguide.cpp`) and gobexport still look
    at the front surface (G3.9).
  - Blount does not appear in EMAP1000 yet (the goblin is placed through
    map version 3: G3.6), so talking heads are placed at the wrong spot.

**G3.5: mult IMD keys and the v3 mult redraw.**
- **Work:** `Mult_v2::loadImds`-equivalent and playback through
  advanceObjects; the v3 redraw skip.
- **Accept:**
  - EMAP1000 runs past 0x2B13 (no desync);
  - EMAP1002 and INTER3 show their IMD animations (snapshots);
  - 60 s of each EMAP room headless with no errors.

- **Done.** Results:
  - EMAP1000 runs past 0x2B13 (since G3.3).
  - IMD animations: INTER0 shows INTERV.IMD (Blount's barrel flight
    under the umbrella); INTER3 plays the comic panels (BDA..BDFIN) after
    the letter, when no key is pressed (a key skips them, as in the
    original); in EMAP1002, `--set-var 95 61 -1 --set-var 95 60 33`
    starts Blount's fall into the hole: TROUA-C play in the mult, then
    the IDO15 talking head.
  - Rooms: a sweep (`gobrun --set-str 78.2 580 ROOM`, 60 s
    each from MENU's first room start) over EMAP1000-1019, EMAPBB08/11/15,
    INTER0-14 and FINAL: no stubs, no errors. EMAP1001, INTER1, INTER5,
    INTER6, INTER10 and INTER11 don't exist (MENU ends the game); EMAP1019
    asks for SOR.IMD, which the GOG data lacks (VAR 11 = -1, as in
    ScummVM).
  - Gob2/Gob1 regression set green.
- **Notes for the next agent:**
  - Behaviour: `docs/animation.md` 2.13 (IMD keys, the redraw skip),
    `docs/opcodes.md` 11.2 (animPalInit). Code: `advanceAnimKeys` /
    `advanceImdKeys` in `engine/mult.cpp` (the IMD part only runs for
    mults with IMD files, so Gob2 is unchanged), `playMultImd` and the
    shared `videoFrame()` in `engine/video.cpp`.
  - The mult video is shown without the original's per-frame wait (the
    room's animatePalette paces it); reverse sequences (multSub flag
    0x400) are still not implemented (no Goblins 3 script uses them).
  - `gobrun --set-str T 580 ROOM` (set at 78.2 s in a `--press-every 5
    --press-stop 70` run) makes MENU start ROOM instead of EMAP1000: the
    quickest way into any room until G3.8's level select.
  - Palette cycling (`animPalInit`/`animPalette`) also runs for Gob1/Gob2
    paths but never starts there (their scripts don't use func 0x17).

**G3.6: Goblin_v3 and Map v3.**
- **Work:**
  - map version 3 (pass width 65, screen width 640);
  - movePathFind stairs, ladders and 19-branches;
  - advMovement 26/27 and 38-43;
  - placeObject look direction;
  - the EMAP1018 Blount fix.
- **Accept:** scripted clicks walk Blount up and down stairs and ladders in
  a room that has them (print objects shows the cell path and states
  40-43/38/39).

- **Done.** Results:
  - Blount now stands in EMAP1000 (cell 6,21) and walks. Click scripts
    with `--print-objects` (EMAP1000): up the zigzag stairs 6,21 -> 4,10
    (states 40, 42, 40 on the stair cells 8,20 7,18 8,16 9,14 8,12 7,10),
    down again (43, 41, 43), up the rope in column 11 (state 38 from 11,24
    to 11,6, then 12,4) and down it (39), up the pass-19 ladder in column
    13 (26).
  - The 60 s room sweep still finds no stubs or errors; Gob2 regression
    set (plus `--level-at 110 GOB05/GOB14/GOB20 --print-objects` runs and
    the GOB08 skip run) byte-identical, Gob1 checks pass.
- **Notes for the next agent:**
  - Behaviour: `docs/goblins.md` 6. Code in `engine/goblins.cpp`:
    `_passStride` / `_mapScreenW` (set by loadMapObjects from the map
    version), `isMovement` / `advMovement` (now members, v3 states for
    `_ver >= 3`), the stairs/ladder branches of `movePathFind` (only when
    the map is 640 wide), the pass-11 fix in `goblinMove`, the queued
    newState in `goblinMoveAdvance` and the v3 part of `placeGoblin`.
  - Our `getDirection` still lacks ScummVM's pass 3/6 ladder shortcuts
    (`map.cpp:202`); no Goblins 3 map has those values, and adding them
    would change Gobliins 2 if its maps did.
  - Not checked: Were-Blount (the second goblin in the later rooms), the
    EMAP1018 workaround on real data, EMAP1012's vertical scroll with
    walking.

**G3.7: Goblins 3 saves.**
- **Work:**
  - the CAT.INF handler (props, 30-slot index, slots);
  - IMA.INF screenshots (120x160 sprite plus palette);
  - INTRO.$$$; BLOC.INF;
  - the `gob3-` slot files.
- **Accept:** save from the game menu, restart, load (click script)
  restores `--dump-vars` exactly; the screenshot shows in the load list
  (snapshot).

- **Done.** Results:
  - `tests/check_saves_gob3.sh ../goblins3` passes: in EMAP1000 the menu
    (pointer to the top edge, disk icon, SAVE, first line, Return for
    "BOAT", I CONFIRM) writes `gob3-slot00.sav` with the variables and
    the 118x160 screenshot; a new run shows BOAT with its screenshot in
    the LOAD list (snapshot differs from an empty list; checked by eye:
    the shrunk ship) and loading it restores all 31664 bytes of variables
    exactly (`--state-on-trace "loaded save slot"`), Blount back at 6,21.
  - Gob2/Gob1 regression set green.
- **Notes for the next agent:**
  - Format and flow: `docs/levels-gob3.md` 2; code `engine/saves_v3.cpp`
    (`SaveFormat::Gob3`; the shared `saveKind`/`saveFileSize`/`saveLoad`/
    `saveStore` dispatch to the `...V3` functions, `SaveKind::Ignore` for
    prot/config, `SaveKind::Screenshot` for IMA.INF).
  - The global properties (CAT.INF 0-499) live in memory only, as in
    ScummVM: the first CAT.INF size query answers "no file" so MENU writes
    fresh ones. If the original kept them across starts (player names?),
    a `gob3-props.sav` would be the place.
  - `gobrun --state-on-trace TEXT PREFIX` writes PREFIX-N.vars/.objects
    when a trace line contains TEXT; useful for any save/load check.
  - Our sprites keep their width (ScummVM rounds sprite widths up to 8:
    120x160 screenshots); the slot file stores the real size, so the
    screenshots round-trip, but a slot file from ScummVM is not readable
    (ours is our own format anyway).

**G3.8: Goblins 3 rules (levels, skip, quick save).**
- **Work:**
  - study how MENU runs rooms and write `docs/levels-gob3.md`;
  - level select;
  - cutscene and IMD skip;
  - fast-forward rules;
  - quick save in the last slot (30) as for Gob2;
  - the ECHEQUI rename.
- **Accept:** `check_quicksave.sh` generalised (`--game gob3`) passes, and
  `--level-at` reaches three different rooms.

- **Done.** Results:
  - `tests/check_quicksave.sh ../goblins3 build gob3` passes (slot 29
    QUICKSAVE written twice, quick loaded, a renamed slot refused for
    both). A quick save after walking Blount to 4,10, loaded in a new run:
    variables identical at the "Loaded" moment, Blount at 4,10 again.
  - `--level-at 3 EMAP1010`, `EMAP1015`, `EMAP1019` (requested during the
    intro: the intro video is cancelled, EMAP1000 left at its first idle
    moment) and `--level-at 90 EMAP1005` from EMAP1000 start those rooms
    (snapshot: the Inn with Blount); `--list-levels` lists the 22 rooms
    with their names (Boat ... Gods); the GUI's Levels tab shows them by
    world with "Now in world 4: Clouds (EMAP1010)" and quick save.
  - Skip in INTER3 fast-forwards the intermission (its narration video
    included) and EMAP1004's entry dialogue until the player has control.
  - Gob2/Gob1 regression set green; `test_detect` updated for the rules.
- **Notes for the next agent:**
  - Behaviour: `docs/levels-gob3.md` 1, 3, 4. Code: `engine/levels_v3.cpp`;
    the shared entry points (`levels`, `currentLevel`, `requestLevel`,
    `requestQuickSave/Load`, `hasQuickSave`, `quickSaveSlotTaken`,
    `noteRoomStart`, `finishLevelJump`) branch to the `...V3` functions;
    `serviceLevelRequestsV3` is a coroutine (it runs MENU's save
    preparation with `switchTotSub(0, 0x5B5B)`), awaited from
    `checkHotspots`. Gob3 branches in `skip.cpp` (`inSkippableSequence`,
    `fastForwardEnd`), and `skipCutscene` fast-forwards a video that
    belongs to a skippable sequence.
  - A level jump resets the variables to the first room's start, so the
    rooms start "fresh" (no items or progress from earlier rooms); rooms
    deep in the game may expect earlier progress (not checked room by
    room). Jumping to a world's first room from its INTER isn't done
    (the intermission is not shown). (Superseded in G3.10: a room the
    player has reached starts from the variables of that first visit.)
  - `GameRules::trackRenames` / `cdTrackName()` applies to every
    playCDTrack.

**G3.9: Goblins 3 mods and polish.**
- **Work:**
  - wobble;
  - sharp text for 8x10 fonts;
  - gobexport/frames for Gob3 (scrolled backgrounds wider than 320);
  - HD packs with scrolling;
  - the Speech on/off option in the overlay (subtitles on whenever speech
    is off; decided in the open issues);
  - README.
- **Accept:** an HD test pack works in a scrolling room (dumps), and the GUI
  starts Gob3 with a scratch pref root.

- **Done.** Results:
  - `tests/check_hd_gob3.sh ../goblins3` passes: export, 4x nearest pack,
    14 HD dumps (intro video, title, EMAP1000 before and after scrolling
    to its right end, the icon bar, a talking head, INTER3 and its comic
    panels, EMAP1005/1010/1012 (320x240)/1015/1017/1019 via the level
    select) all at mean difference 0.000 with 0 values off. The GUI with
    the same pack (`--hdpacks-dir --hd-pack Test --mouse-at 50 319 100
    --hd-dump-at 56`) shows the scrolled room in HD; its dump differs only
    by the cursor (6 pixels).
  - Speech: `--no-speech` silences NEF1 (speech-channel energy 0 instead
    of 6630) while the intro video's sound (now at the effects volume)
    still plays; the Audio tab has "Speech on" and the Speech slider.
  - Sharp text draws the 8x10 subtitles at window resolution (GUI
    screenshot at 4x).
  - `gobexport --frames --only EMAP1000` writes 2392 frames in 210
    layers; `gobrun --guide-at 88` writes the 1888x800 guide of the whole
    472-wide room. Gob1/Gob2 exports byte-identical to the pre-G3 build.
  - The GUI starts Goblins 3 with a scratch `--pref-root` (screenshots of
    the intro, the Audio and Levels tabs, a scrolled room, sharp text).
- **Notes for the next agent:**
  - Wobble was done in G3.1 (no Goblins 3 script calls it).
  - Text cells (`Draw::TextCell`) hold glyphs up to 8x16 by rows
    (`Font::glyphRows`); the sharp-text atlas has square 8x16 cells
    (`SharpText::kCellStride`), 4160x4160 texels.
  - The video voice: `Mixer::playSpeech(pcm, rate, voice)`; `voice`
    (a video from the speech archive, `DataFiles::archiveOf` vs
    `_voiceArchive` remembered by openItk) picks the speech volume and is
    muted by `Interpreter::speech`; other videos use the effects volume.
  - The disassembler records `loadSpriteToPos` uses for every version but
    2 (Gob2's export unchanged), so Goblins 3's opaque room parts export
    opaque.
  - Open: a full playthrough (click scripts per room, as for Gobliiins'
    first levels); Were-Blount; the room guide's hover-text line still
    assumes the bar at the bottom 12 rows; IMD frames have no HD pack
    format (they show enlarged); the rename (R). (The playthrough and
    Were-Blount: done in G3.10.)

**G3.10: Goblins 3 played through.**
- **Work:** click scripts for every room (`tests/gob3/NN-ROOM-name.txt`,
  45 files, played as one script by `tests/check_gob3_walkthrough.sh`);
  engine fixes for what they hit; a level select that starts a room with
  the player's own progress; a graceful missing video.
- **Done.** Results:
  - The check plays from the intro through all eight worlds and FINAL
    (the game ends at END.TOT, which the CD doesn't have) with no stub or
    error. Per world the scripts also run from a level jump with recorded
    room states (scratch runs).
  - **Level select with room states:** whenever MENU starts a room for the
    first time in a game (visit counter `b@(3740 + room number)` 0, not a
    restore), all variables are recorded (`gob3-rooms.sav` in the save
    folder, docs/levels-gob3.md 4); a jump starts from them, applied again
    after MENU's world change (0x4E98) has reset the inventory. Nothing
    game-derived ships: the states come from the player's own play (or a
    walkthrough run). Unreached rooms still start from the first room's
    variables, and the Levels tab says so.
  - **Quick save timing:** a quick save made as a room starts kept the
    previous room's goblin positions (the room copies them into its
    variables in the main loop's timed update): saves and jumps now wait
    for that update (`_roomLoopPasses`).
  - **Missing videos:** SOR.IMD (EMAP1019's arrival), PILOU (a sound-only
    coin toss), FORT21A/B and ROI2B (EMAP1014) are absent from the GOG data
    (and from FDD/). ScummVM just sets VAR(11) to -1 and goes on; so do we,
    and a missing talking head (a positioned video that would play to its
    end) is held for 2.5 s (or a click) so its subtitle can be read
    (`holdMissingClip`).
  - **FINAL:** its loops poll the keys with keyFunc 2 twice per pass
    (only a key caught by the second poll counts; mouse buttons are not
    stored); a Goblins 3 key poll within 20 ms of the last one now waits a
    frame, as ScummVM's handleBusyWait slows such loops, instead of
    running the op budget every frame. The headless runner plays the
    mixer through for Goblins 3 (as a sound device would), so samples end
    and playMult stops (the credits looped forever before).
  - No goblin-logic bug was found. Path finding that stops short (e.g.
    EMAP1013 from the fountain, EMAP1018 from the well) matches the
    original algorithm (checked with the ScummVM-equivalent simulation in
    the scratch dir); the scripts click a waypoint first.
- **Notes for the next agent (milestone R and later):**
  - The runner commands added for this (tools/runner.cpp header): `room`,
    `act`, `actb`, `zone`, `at`, `item NAME[#N]`, `drop`, `untilvar SLOT V
    [HI]`, `untilzone`, `print`, `printb`, `inventory`, `hotspots`,
    `objects`, `vars`, `--log-text`. Hotspot ids in Goblins 3 rooms are the
    room's zone number + 3 (zone 0 Blount = hotspot 3, zone 1 the other
    goblin = 4).
  - Rename work touches `gob3-rooms.sav` only through
    `Interpreter::roomStatesPathV3` (save folder + game key); keep the key
    `gob3` when renaming so players keep their recorded rooms.
  - Many rooms have timing windows (a hotspot or flag valid for a few
    frames of a keyframed sequence); the scripts wait for them with
    `untilvar` on v[18]-v[21]. If animation timing changes, these are the
    first scripts to break; docs/levels-gob3.md 5 lists the mechanics.

### 6.4 R: rename and migration

This can happen at any point after G0. Doing it before G1.8, so the Gob1
README already says `gobnative`, is the recommended order.

- **Work:** the app target `gobnative` with the `gob2native` alias, the
  pref root `gobnative`, and the migration of 5.4; README and docs renamed.
- **Accept:**
  - the scratch-root migration test: rename, symlink, second start is a
    no-op, rename failure falls back to the old folder;
  - Gob2 regression set;
  - the real user folder is untouched by any test.
- **Done.** Results:
  - **Names:** CMake project and app target `gobnative`
    (`tools/gobnative.cpp`, was `tools/gob2.cpp`), the viewer `gobview`;
    post-build links `build/gob2native -> gobnative` and
    `build/gob2view -> gobview`. Window titles "Gobliins 2 (gobnative)",
    the overlay window and the diagnostics say gobnative, as do the
    gobexport/frames READMEs. Kept on purpose: the repository folder, the
    `// gob2native - ` file headers, the save keys (`gob1`/`gob2`/`gob3`,
    so `gob2-slotNN.sav`, `gob3-rooms.sav` etc. are found as before) and
    the JSON format ids of exports (`gob2native-export`,
    `gob2native-frames`, `gob2native-guide`), which existing exports carry.
  - **Pref root:** `SDL_GetPrefPath("", "gobnative")` (macOS:
    `~/Library/Application Support/gobnative/`), or `--pref-root DIR`; the
    old root is always `<root>/../gob2native`, so a scratch root
    `X/gobnative` with `X/gob2native/Gobliins2` reproduces the real layout.
    `gamePrefFolder()` (`frontend/prefs.cpp`) implements 5.4 exactly: only
    for Gob2, only when `gobnative/Gobliins2` doesn't exist (checked
    without following links); one `rename()`; then the relative link
    `gob2native/Gobliins2 -> ../gobnative/Gobliins2`. If the rename fails,
    the old folder is used in place (and the move is retried next start);
    if the link fails, the folder is renamed back and used in place (if
    even that fails, the files stay in the new folder and the log says an
    old build won't find them). A dangling old link (new folder removed)
    gives a fresh new folder; the player's own link to a folder elsewhere
    is used in place; a file named like the old folder is left alone. Each
    step is logged to stderr, the Tools tab's trace, an 8 s toast and
    `<root>/migration.log` (appended, with a timestamp; nothing is written
    when nothing happened).
  - **Concurrency:** an old gob2native that is running keeps its open files
    (the inode moved) and its later writes by path go through the link
    (checked: a running pre-rename build's F12 screenshot and settings
    landed in the moved folder); one started later finds everything
    through the link (SDL's `mkdir` of the existing link is harmless). If
    two gobnative instances race, the loser sees the new folder and uses it.
  - **Shared settings and picker:** `<root>/settings.ini` keeps
    `[recent] folder1..8` and `[app] last_game` (`AppSettings`); each
    started game folder is put first. Without a folder argument the app
    shows the picker (own SDL window, GL context and ImGui context, gone
    before the game window opens): recent folders with the detected game,
    a path field (`~` expanded, quotes stripped), Browse (macOS `osascript
    choose folder`), drag and drop, Enter/double-click to play, Escape to
    quit; only folders `selectGame()` accepts (respecting `--game`). Debug:
    `--pick-at T DIR`, and `--screenshot-after` shoots the picker when no
    folder is given. `--print-pref-dir` resolves (and migrates) the game's
    folder, prints it and exits (works with `--game KEY` and no folder).
  - **Scripts:** `scripts/gobpaths.sh` (and `gob_paths()` in
    `chatgpt-rooms.py`) choose `gobnative/Gobliins2/HDPacks`, or the old
    folder while it is still a real one, and never create the new folder
    before the move (which would make the app ignore the old one).
  - **Tests:** `test_prefs` (scratch temp dir): fresh start, the move
    (inode unchanged, every file and nested pack folder, link target,
    contents through the link, an open file of a "running old build",
    writes by path through the link, `mkdir` on the link), second start a
    no-op (log unchanged), both folders exist, rename failure (read-only
    root) and a later successful retry, link failure with move-back and
    with the move-back impossible (via `prefsAfterMoveHook`), foreign and
    dangling links, a file in the way, the recent list. End to end in the
    scratch dir with the real binaries: `--print-pref-dir`, a GUI run (the
    screen is byte-identical to the pre-rename build's at 20 s), the
    picker (screenshots), a pick that migrates (toast), the read-only
    fallback, the pre-rename build run on the migrated layout and while
    the migration happens. The regression set passes: all `test_*`,
    `check_quicksave.sh` (gob2, gob3), `check_quicksave_gob1.sh`, the
    `check_gob1_*` scripts, `check_saves_gob3.sh`, `check_hd_gob1/gob3.sh`,
    `check_gob3_walkthrough.sh`; `gobdis --all` output is identical for
    the three games (0 errors); the Gob2 intro run to GOB01 (`--press-every
    3 --press-stop 100 --frames 9000`) is byte-identical, also with
    `--trace-ops` (gobrun has no path or name strings in its output).
- **What happens on the player's machine:** at the first start of the
  renamed app for Gobliins 2 (also via `build/gob2native`), with
  `~/Library/Application Support/gob2native/Gobliins2/` a real folder and
  no `gobnative/Gobliins2/` (the state when R was done), the folder is
  renamed to `gobnative/Gobliins2/` and the link is left. Undo, with the
  app closed: `cd ~/Library/Application\ Support && unlink
  gob2native/Gobliins2 && mv gobnative/Gobliins2 gob2native/`.
- **Notes for the next agent:**
  - Every GUI test run must pass `--pref-root` (a run without it migrates
    and uses the real folder). gobrun and the tools never touch pref
    folders.
  - `test_prefs` builds from `src/frontend/prefs.cpp` and `ini.cpp`
    directly (the front end has no library of its own).
  - Switching games inside a running app is not supported; the picker
    only runs before the game window opens. (Since I: the picker is the
    game library, and "Back to library" restarts the process into it.)

### 6.5 I: install from GOG, game library

Goal (user): make the app easy to try for people who just bought the
games on GOG.

- **Done.** Results:
  - **Installer reader** (`src/install/`, format notes in
    `docs/install.md`): our own implementation, with innoextract
    (zlib licence, a read-only checkout in `reference/innoextract`) as the
    format reference and ground truth. Offset table from the PE resources,
    CRC-checked LZMA header blocks, setup header/entries for data versions
    5.3.9-6.3 (tested: 5.5.7 unicode, all three installers), data
    entries, LZMA1/LZMA2 chunks (own decoders after the LZMA SDK
    specification; XZ Embedded was approved but not needed), the x86 call
    filter, SHA-1 per stored file, GOG Galaxy files (parts deflated with
    zlib, real names and MD5 in `before_install`/`after_install` script
    calls; inflated with the PNG reader's inflater). Step-based, no
    threads or platform code in the core (for Windows and web builds).
  - **Findings:** GOG's 2020s installers keep almost every file under a
    hashed `{tmp}` name in deflated parts of up to 10 MB; the real names
    are only in the Galaxy script calls (innoextract's default naming
    maps `{app}` to `app/`, the Galaxy names are relative to the game
    folder). The Windows version fields of items are ranges (20 bytes).
    The setup headers are at the end of the file, so a truncated download
    fails at once ("the installer ends too early").
  - **What is installed:** identified from the file list (TOT/EXT.STK +
    the Gob3 speech/music files, INTRO.STK + GOBNEW.LIC, INTRO.STK +
    GOB.LIC); the game's data, Track1.mp3 and Manual.pdf (FDD/ with
    `--floppy`); into `<dest>.partial`, then the game detection must agree
    before the rename. 49, 54 and 151 MB, 1.1/1.2/2.8 s.
  - **gobinstall** (`--list`, `--floppy`, `--check`, `--all`, `--merge`).
  - **Library** (`frontend/library.cpp`, replaces `picker.cpp`): tiles
    with the title pictures read from the installed data at runtime
    (Gob1 INTRO res 30012 with palette 2, Gob2 INTRO0 res 2 with palette
    3, Gob3 DEMO res 30000 with palette 0, as gobexport pairs them),
    neutral placeholders otherwise; import (macOS `choose file` dialog,
    drag and drop of the .exe, typed path) on a worker thread with a
    progress bar and Cancel, into `<pref root>/Games/<prefFolder>/`, which
    is then put first in the recent list; a folder that isn't the game in
    the way is renamed "<name> (old)", never deleted. A game counts as
    installed when a recent folder or `Games/<prefFolder>` holds it.
    "Add existing game folder..." and the recent list as before. "Back to
    library" (Gameplay tab) execs the app again with only `--pref-root`
    and `--game`.
  - **Tests:** `test_install` (decoders, hashes, paths, junk files; with
    the installers' folder: identification, plans, stepped extraction);
    `tests/check_install.sh`: `gobinstall --all` equals innoextract for
    all 149/147/160 files of the three installers, the installed games
    equal innoextract's files. GUI runs with scratch `--pref-root`:
    imports of all three installers (`--import-at`, `--tile-import-at`),
    the wrong installer on a tile, an already installed game, cancel and
    closing during an import (the partial folder is removed), a folder in
    the way, playing each imported game (`--play-at`; Gob2 screen
    byte-identical to starting with the folder), "Back to library"
    (`--back-to-library-at`), screenshots.
  - **Not done / limits:** multi-part installers (`setup-N.bin`),
    encrypted installers, zlib/bzip2 chunks; no file dialog on Linux yet
    (drag and drop and the path field work there; Windows got its dialogs
    in 6.6); file times are not restored.
- **Redesign for players (October 2026, after the user's "it looks like a
  hacker interface"):** commits "Front end theme: Nunito font, warm dark
  colours (overlay)" and "Library: a game launcher for players, not a
  debug panel".
  - **Font:** Nunito (SIL OFL 1.1, the approved download from
    github.com/googlefonts/nunito), vendored in `third_party/fonts/nunito`
    as static Regular/Bold instances of the variable font
    (`scripts/nunito-instances.py`; stb_truetype draws only the default
    instance, which is ExtraLight), 55 KB each, compiled in by
    `cmake/embed.cmake` (the web build has it too). `frontend/theme.h`:
    the fonts (plus ImGui's fixed-width one, used by the Tools tab's
    tables), the colours and a shared ImGui style; the overlay uses it.
  - **Library:** header, tiles with year, status and one primary button
    (Play / Install...), tinted placeholder cards (name, year, a large
    faint number) for missing games, a list layout below 600 points, one
    row sized to fit the window (the last frame's free height), a "get the
    missing games" card with three steps, a progress card, plain-language
    messages in a banner (details in tooltips and "More options"), and
    "More options" (closed) for folders, paths and the recent list.
  - **Tests:** unit tests, `check_install.sh`, `check_quicksave.sh` gob2;
    screenshots with 0, 1 and 3 games (scratch `--pref-root`), during and
    after an import, a wrong installer, cancel, `--play-at`; the web build
    in the browser pane (library at 1000x720 and 555 wide, Remove dialog,
    a dev-hook import); overlay tabs.
  - **Limits:** at heights under about 700 points with a game missing the
    tiles stop at their smallest size and the page scrolls a little; the
    title is shown twice on placeholder tiles (cover and body).

### 6.6 W: Windows build

Goal (user): gobnative and the tools on Windows, built locally (MinGW
cross build) and by GitHub Actions (MSVC).

- **Done.** Results:
  - **Portability:** the code builds with MSVC (C++20, `/permissive-`,
    `/utf-8`, static runtime) and MinGW-w64 GCC (static libstdc++/libgcc/
    winpthread); missing standard includes, wide `path::c_str()` in
    printf, `getpid` in tests fixed. No engine change: the engine had no
    POSIX or endianness assumptions.
  - **Paths:** every program has a manifest (`win/gobnative.manifest`)
    with the UTF-8 code page (Windows 10 1903+), so `std::string` paths,
    `fopen` and `std::filesystem` stay UTF-8 as on macOS (CI checks a
    non-ASCII pref root), long paths, and `asInvoker` (otherwise
    `gobinstall.exe` asks for elevation because of its name). SDL's pref
    path is `%APPDATA%\gobnative\`; no gob2native migration and no
    `gob2native`/`gob2view` links on Windows.
  - **gobnative.exe** is a GUI program (the tools are console programs);
    started from a command prompt it writes to that console. Library
    dialogs: `IFileOpenDialog` for the installer (Downloads folder first)
    and for "Add existing game folder..." (`src/frontend/win32.cpp`).
    "Back to library" starts a new process (`CreateProcessW`). OpenGL 3.2
    core through SDL as before (1.1 entry points from `opengl32.dll` when
    `wglGetProcAddress` gives a sentinel). DPI scaling like macOS (window
    in points, drawable in pixels).
  - **Builds:** `cmake/mingw-w64.cmake`, `scripts/build-windows.sh`
    (package in `dist/gobnative-windows-x64[.zip]`, import and subsystem
    checks); `.github/workflows/windows.yml`: MSVC Release build with
    the official SDL2 VC package, all `test_*`, a start of every packaged
    program, the package as the run's artifact; a macOS build + tests job.
  - **Not verified:** nothing has run on a real Windows desktop yet (no
    Wine here; CI has no GPU or display): the window, OpenGL, audio, the
    dialogs and an import need a test by a person on Windows.

### 6.7 Web: WebAssembly build

Goal (user): play in the browser with one's own GOG installers; game
data is never bundled or hosted; local build and test only.

- **Done.** Results:
  - **Toolchain:** Emscripten 6.0.12 (approved; `../tools/emsdk`), its
    SDL2 2.32.10 port, `-fwasm-exceptions` (the engine throws; C++20
    coroutines need no flags), no pthreads (no COOP/COEP), 4 MB stack,
    memory growth up to 4 GB. `CMakeLists.txt` builds only `gobnative`
    under Emscripten (gobcore, ImGui with `IMGUI_IMPL_OPENGL_ES3`);
    `scripts/build-web.sh` writes `dist/web/` (index.html, gobnative.js,
    gobnative.wasm ~2.2 MB, licences); `--serve` for a local server. CI
    job "Web (Emscripten)" uploads `gobnative-web`.
  - **Frame loop:** the library is a `LibraryScreen` (open/frame/close)
    and the game a `GameRun` (frame/finish) in `tools/gobnative.cpp`;
    the desktop loops call the same frames (behaviour unchanged; the
    library still waits on events there). In the browser one
    `emscripten_set_main_loop` callback runs the library, then
    `openGame()` for the chosen folder, then the game, held to about 60
    frames a second (the engine steps once per frame). Leaving the game
    (quit, "Back to library", holding Escape) writes the storage and
    reloads the page, as the desktop app execs itself.
  - **WebGL 2:** the presenter's GLSL 1.50 shaders compile as GLSL ES
    3.00 by swapping the first line for `#version 300 es` plus highp
    float/int (they were written for that); `readPixels` reads RGBA; the
    library's title textures use RGB8; ES 3.0 contexts from SDL. Desktop
    GL untouched.
  - **Storage:** `/gobnative` (the pref root) is IDBFS, loaded in a
    preRun and written back after imports, settings, screenshots, every
    5 s and when the page is hidden (`web/library_gobnative.js`). The
    browser is asked for persistent storage.
  - **Memory (phones):** the page loads only what it needs, with its own
    load and sync on IDBFS's database (no migration; IDBFS's own sync
    would delete what wasn't loaded). Big files (in `Games/<game>` all but
    the top-level `.STK`s, and HD packs, music, screenshots) get an empty
    file marked as stored; Play fills in the game's folder and its
    settings folder (`web::load`, ~0.15 s for Goblins Quest 3) before
    `openGame()`. The sync leaves stored files alone, copies them within
    IndexedDB when moved (moved aside, renamed from `.incoming`) and
    empties written files the page doesn't need (after an import). Files
    in `/tmp` (installers, zips) are held in 4 MB pieces; after
    `GameInstaller::begin()` the pieces behind the read position are
    dropped (`web::freeAsRead`). Dropped installers are now removed after
    the import too. `INITIAL_MEMORY` 32 MB (was 128). Chromium, three
    games installed: library 20 MB JS heap + 32 MB wasm (was 257 + 128),
    Goblins Quest 3 running 162 + 134 MB (was 258 + 154), its import at
    most 165 MB of files held (was 372, and the installer stayed). Tested
    again in the pane: library and covers, `?installer=` import, zip
    import, Remove, moved stored files, persistence across reloads, Play
    of each game, quick save, reload and quick load (Gob2), Back to
    library, the Manual read from the storage.
  - **Files:** `<input type=file>` (installer `.exe`, game folder with
    `webkitdirectory`, pack folder, pack `.zip`, music files) read with
    `File.arrayBuffer()` and written into MEMFS without a copy
    (`canOwn`); the app polls the picker each frame (`frontend/web.h`).
    Installers go to `/tmp` (memory) and are deleted after the import; a
    game folder's top-level files (no .exe/.dll/...) go to
    `Games/.incoming`, are detected and renamed into `Games/<game>`. HD
    pack zips are unpacked by the new `ZipExtractor` (`install/zip.h`,
    stored/deflate, CRC, top folder dropped, unit-tested) in 20 ms steps.
    A tile's "Remove" deletes an imported game; "Manual" opens its PDF.
  - **Import:** `GameInstaller::step()` for ~20 ms per frame; the whole
    Goblins Quest 3 installer decodes in 4.0 s as WebAssembly in Node
    (2.8 s native), so a visible page imports it in a few seconds.
  - **No threads:** the HD compositor rebuilds layers on one thread and
    `HdPack::prefetch` is off (pictures decode when first drawn).
  - **Tests** (a local `python3 -m http.server` on 127.0.0.1 serving
    `dist/web` plus links to the installers, outside the repository;
    Claude's browser pane): library shown; imports of all three
    installers (`?installer=`, and the tile's file dialog with injected
    `File`s) and a game folder; persistence across reloads; each game
    starts and renders (Gob1 intro, Gob2 intro and rooms, Gob3 IMD
    intro); audio running (Web Audio peak level measured: CD music, Gob3
    speech); overlay opens (Video/Audio tabs); quick save in a room,
    reload, quick load restores it; Back to library; HD pack zip and
    folder imports (pack opened: 10 pictures at 2x), music import,
    Remove. 60 frames/s at 0.5-0.8 ms per frame for the engine.
  - **Not verified / limits:** fullscreen and F12's screenshot download
    (both need a real user gesture or start downloads); Safari and
    Firefox (only Chromium was used); real mouse play over a long
    session; IndexedDB eviction. The test pane was hidden, so
    the browser throttled its frames: `--frame-timer` (posted messages
    instead of animation frames) kept tests at full speed.

## 7. Open issues

- **Gob1 copy protection:** resolved in G1.1: the GOG INTRO.TOT has no
  `callSub(3905)`, so nothing needs skipping.
- **Gob1 language:** we offer FR/DE/EN/ES/IT and map American and British
  to 2.
- **Gob3 speech in other languages:** GOG has only USGOB3.ITK, and scripts
  open FRGOB3/DEGOB3/ITGOB3 for other languages. Options:
  - open USGOB3 instead, which gives English speech under French text;
  - play without speech, showing subtitles.

  **Decided (user):** for languages without Goblins 3 speech, fall back to
  the English speech file (USGOB3.ITK), and offer a "Speech" on/off
  option; with speech off, subtitles are on. **Done:** the ITK fallback
  (G3.1), the speech voice and its volume (G3.3), "Speech on" in the Audio
  tab and settings (G3.9); subtitles always show (as ScummVM forces them
  for the CD version).
- **Gob3 Spanish** reads `VOICE.INF` (absent): resolved without a rule,
  the missing file reads as 0 -> FRGOB3 -> the USGOB3 fallback.
- **Gob3 split bar:** resolved: the pointer is kept above the bar (rows
  0-159), as in the original; the bar only shows text, the inventory and
  menus open elsewhere (the icon bar at the top edge).
- **HD packs and IMD frames:** IMD frames are not pictures in TOT/EXT. A
  separate pack format for video frames (per IMD and frame) is possible
  later; until then they use "Pictures without HD art".
- **Gob1 save snapshots:** resolved in G1.7 (`docs/levels.md` 9: what is
  serialized, versions, refusal of other files).
- **Floppy versions** (FDD folders) are not targeted. Detection says so.
- **Random numbers across platforms:** resolved. `std::rand()` differs
  between libcs (glibc on Linux/webOS, macOS libc, MSVC), so headless
  traces and click-script runs drifted apart across platforms (seen on
  the webOS TV: Gob2's trace matched the Mac's until GOB01's random
  ambient sounds). `script/random.h` (`gob::Random`) is the Park-Miller
  "minimal standard" generator (`x = 16807 * x mod 2^31-1`), which is
  exactly macOS libc's `rand()` (checked over 10^6 draws); it starts at
  seed 1 as an unseeded `rand()` does, so the Mac traces and every click
  script recorded with them are unchanged. The Interpreter owns one
  (`Interpreter::random(n)`): script `rand()`, Gob2/Gob3 goblin boredom
  (`handleGoblins`) and Gobliiins' boredom, relax timers and speaker
  pitches (`GoblinsV1`) all draw from it; `std::rand()` is gone from the
  engine. `gobrun --seed N` picks another sequence (default 1); the GUI
  uses seed 1 too. Saves and Gobliiins' quick save snapshot don't carry
  its state: the original's saves didn't, the games don't depend on it,
  and a loaded game simply continues the session's sequence. Checked:
  all `test_*` (`test_expression` checks the first values), Gob2
  `check_quicksave.sh`, `gobdis --all` 0 errors for all three games, the
  9000-frame run reaches GOB01 with `--seed 1` equal to the default
  (`--seed 2` differs first at GOB01's ambient sounds), and every
  Gobliiins and Goblins 3 check passes unchanged, both walkthroughs
  included; no click script needed changes.
