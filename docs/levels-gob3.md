# Goblins Quest 3: rooms, saves and gobnative's level tools

How Goblins 3's scripts string its rooms together and save the game, and what the engine
builds on that. Variables are written as in `docs/levels.md` (`v[n]` the 32-bit slot n,
`s[n]` the string at slot n, `b@n`/`w@n` bytes and words at byte offset n).

## 1. MENU and the rooms

INTRO opens the archives and the speech, then loads DEMO (the intro video and the title)
and MENU. MENU is the hub: it sets the 472x200 screen with the text bar
(`docs/graphics.md` 9), loads the fonts, sets up the save files' global properties and
then runs its room loop: `totSub s[580]` with flags 2, again and again, until `b@3623` is
1. `s[580]` names the room to run next (EMAP1000 for a new game); a room sets it before
it returns (EMAP1000 sets "inter0" or "inter2", INTER3 sets "EMAP1004", ...). Between two
rooms MENU also runs DEPLACE, JOKER or EDIT when `v[893]`, `v[565]` or `v[892]` ask for it.

The rooms call back into MENU with switchTotSub: function 10 (MENU's per-room set-up and
music), function 4 (the icon bar), and MENU calls the room's function 9.

The icon bar drops down when the pointer touches the top edge of the screen (a hover
hotspot over rows 0-1): disk (SAVE, LOAD, QUIT, VERSION), bulb (the joker), book (the
notes), and others.

## 2. Saves

Goblins 3 saves through virtual files (`SaveLoad_v3` in ScummVM; the engine's
`src/engine/saves_v3.cpp`, `SaveFormat::Gob3`):

| File | Layout | Engine |
|---|---|---|
| `CAT.INF` | 500 bytes of global properties (player names and the like, written as 40-byte strings), the index of 30 40-byte slot names at 500, then a full copy of the variables per slot from 1700 (`1700 + slot * v[16]`, where getFreeMem left the variables' size in `v[16]`) | properties in memory (the first size query reports no file, so MENU sets them up at every start, as in ScummVM); names and variables in the slot files |
| `IMA.INF` | 80-byte index (bytes 0-39 the scripts' own, 40-69 a flag per slot that holds a save, 70-79 zero), then per slot a screenshot: sprite 4 (118x160 here; 120 wide in the original, which rounds sprite widths up to 8) and the palette, `80 + slot * 19968` | screenshot in the slot file |
| `INTRO.$$$` | a temporary copy of a sprite (the screen under a talking head, ...): the size argument names the sprite, -(index + 1), less 1000 with the palette | in memory |
| `BLOC.INF` | the notes, 2560 bytes | `gob3-notes.sav` |
| `prot`, `config` | ignored: reads leave the variables and `v[1]` alone, sizes are -1 | |

The save screen proposes the room's name (BOAT for the ship) and asks for a confirmation.
MENU writes the slot's variables first and then its screenshot (the shrunk room:
every fourth column and row of the 472x160 window); the load list shows the names and,
for the line under the pointer, the screenshot.

**Slot files** (`gob3-slotNN.sav`, NN = 00-29, in the save folder): "G3NS", u32 version
(1), u32 variable bytes (0 while only the screenshot is there), the 40-byte name, the
variables, u32 screenshot width and height (0: none), u8 palette flag, the 768-byte
palette (6-bit VGA), the screenshot's pixels. A save writes the part it gets and keeps the
other.

`tests/check_saves_gob3.sh` saves from the menu in EMAP1000, restarts, finds the save and
its screenshot in the load list, and loads it: the variables equal those saved, byte for
byte, and Blount stands where he was.

## 3. The rooms in play order

The rooms' exits (the `s[580]` they set) give the order; the world is MENU's `b@3737`
(set by EMAP1003, EMAP1005, INTER9, INTER12, INTER14, EMAPBB15 and EMAP1017 on the way);
the names are MENU's own room names (text items 27, 11, 14, 15, 10, 20, 21 and 22, one per
world, the line `room number - the world's first room number`; MENU 0x483D proposes them
as save names):

| World | Rooms (name) |
|---|---|
| 1 | EMAP1000 (Boat), EMAP1002 (Idol), EMAP1003 (Death) |
| 2 | EMAP1004 (Country), EMAP1005 (Inn), EMAP1006 (Fortress) |
| 3 | EMAP1007 (Town), EMAP1008 and EMAPBB08 (Grocery), EMAP1009 (Lab) |
| 4 | EMAP1010 (Clouds), EMAP1011 and EMAPBB11 (Colossus), EMAP1012 (Bizoo) |
| 5 | EMAP1013 (Queen), EMAP1014 (King) |
| 6 | EMAP1015 and EMAPBB15 (Chessboard), EMAP1016 (Book) |
| 7 | EMAP1017 (Mirrors), EMAP1018 (Brain) |
| 8 | EMAP1019 (Gods), then FINAL |

The intermissions between them: INTER0 (EMAP1000 to EMAP1002), INTER2 (to EMAP1003),
INTER3 (to EMAP1004), INTER4 (EMAP1004 to EMAP1005), INTER7 (EMAP1007 to EMAP1009),
INTER8 (EMAP1008 to EMAPBB08), INTER9 (to EMAP1010), INTER12 (to EMAP1013), INTER13
(EMAP1013 to EMAP1014) and INTER14 (to EMAP1015). There is no EMAP1001, INTER1, INTER5,
INTER6, INTER10 or INTER11.

MENU also reads a `config` file, if there is one, for the room to start in (11 bytes: the
room's name, its world as a digit, and another digit for `v[1052]`): a developer's
level select. The engine ignores the file, as ScummVM does.

## 4. gobnative: level select, quick save, skipping

`src/engine/levels_v3.cpp` (`LevelSystem::Gob3`). Requests are served from the room's
main hotspot loop (`serviceLevelRequestsV3`, from `checkHotspots`) when the room is idle
in the state in which the icon bar would open: no keyframed sequence (`v[18]`..`v[21]` all
-1), no pending goblin action (`v[60]`, `v[1018]` 0), not being left (`b@3670`) or
restored (`v[163]`).

- **Level select** (the overlay's Levels tab, `gobrun --level-at T EMAP1010` or the room's
  number in play order): the room is left as the menu's LOAD leaves it (`b@3670 = 1`,
  `v[16] = 200`); when totSub has returned to MENU, the variables are set to the room's
  *recorded state* if there is one (below), otherwise to those of the game's first room
  start (captured when MENU first runs a room); `s[580]` becomes the target, `b@3737` its
  world and `b@3736` -1, so MENU loads that world's pictures (0x4FA8) and starts the room
  as if the player had just arrived. A request made during the intro skips the intro
  video and waits for the first room.
- **Room states:** a room's variables only make sense with everything the goblins did
  before (items, solved puzzles, the visit counters `b@(3740 + room number)`). Whenever
  MENU starts a room whose visit counter is 0 and that is not being restored from a save
  (`v[163]` 0), all variables are recorded for it, in memory and in `gob3-rooms.sav` in
  the save folder: "G3RS", u32 version 1, u32 count, then per room an 8-byte name
  (zero-padded), u32 size and the variables (the latest first visit wins). So the
  player's own progress, or a walkthrough run, is what a jump starts from: nothing
  game-derived ships with gobnative. MENU's world change (0x4E98, run because `b@3736`
  differs) resets the inventory to the world's first items, so the recorded state is
  applied again when MENU starts the room (`noteRoomStartV3`) - exactly the variables
  the room saw on that first visit. The Levels tab marks the rooms without a state
  ("not reached yet"; `gobrun --list-levels` prints "(reached)" for the others). A jump to an unrecorded room starts with the
  first room's variables (an empty inventory - the room may not be winnable), and nothing
  is recorded after it until a save is loaded or a new game starts.
- **Quick save** writes slot 29 (the last; shown as slot 30) under the name QUICKSAVE: first
  MENU's own save preparation (function 0x5B5B, run with switchTotSub, copies the room's
  state into its storage), then the variables and a screenshot made as the menu makes it,
  so the save shows in the game's load list. Refused while that slot holds a save made
  in the game's menu under another name.
- **Quick load** leaves the room as the menu's LOAD does (`v[163] = 1029`); MENU reads
  slot 29 and runs the saved room again with `v[163] = 1`, which restores it.
- **Skipping:** DEMO and FINAL are cutscenes (Skip ends their videos, as Escape does); the
  INTER intermissions, a room's entry up to its main loop, a room being left and the
  sequences its logic runs instead of the main loop are fast-forwarded, videos included
  (silently), until the player has control. A video in such a sequence is fast-forwarded
  with it; one elsewhere is just cancelled.
- The CD tracks ECHEQUI1/2 the chessboard's scripts ask for are ECHIQUI1/2 in the track
  table (`GameRules::trackRenames`, as ScummVM ticket #11335).

Checks: `tests/check_quicksave.sh <gob3 dir> build gob3` (quick save, quick load, the
refusals); `gobrun --level-at 3 EMAP1010` (or EMAP1015, EMAP1019) starts the room;
a run of the walkthrough scripts with `--save-dir DIR` records the room states in DIR,
after which `gobrun --save-dir DIR --level-at 3 EMAP1009 --script
tests/gob3/14-EMAP1009-lab.txt` plays the lab from the jump;
`gobrun --set-str 78.2 580 INTER3 --skip-at 85` fast-forwards the intermission and
EMAP1004's entry dialogue to the player's control.

## 5. The walkthrough scripts

`tests/gob3/NN-ROOM-name.txt` play the game room by room (`gobrun --script`, the commands
at the top of `tools/runner.cpp`); `tests/check_gob3_walkthrough.sh` plays them all as one
script from the intro and watches FINAL to the end of the game. Each file's header says
how its room works. Things common to the rooms:

- **Zones and hotspots.** A room keeps its zones as 8 words at `v[3721 + 8 * (zone - 2)]`
  (x, y, width, height, Blount's cell, the other goblin's cell; x 1000 hides a zone); the
  hotspot id is the zone + 3 (zone 0, the goblin, is hotspot 3; the other goblin 4). The
  action handlers test `v[60]` (Blount's zone) or `v[1018]` (the other goblin's) and the
  item held, `v[61]`. Item n's owned flag is the byte `7418 * 4 + 40 * n + 39` (b@29711 +
  40n), its name the as[7418] entry n - 1.
- **Timing windows.** Many puzzles accept an action only while a keyframed sequence is
  in some frames (`v[18]`..`v[21]` are the four slots' frames): catching a balloon or a
  flying skull, poking a flea, riding the brain's fish, tying the gods' notes. The
  scripts wait for them with `untilvar`; an `act` would wait until all sequences end.
- **Two goblins.** The other goblin (Chump, Ooya, Fulbert, the reflection, Were-Blount)
  is selected by clicking it (hotspot 4); in the two-hand rooms (the lab, the chessboard)
  only the right hand picks up and a right click puts its object back where it was.
- **Trigger cells** (`av[1076]`: x, y, Blount flag, other flag; `v[1027]` of them) start an
  action when a goblin steps on them: the spider and the brain's stairs send goblins
  back, the colonnade's top and the gods' chain must be stepped on in time.
- **Path finding** is the original's (way points, direct moves); from some cells it stops
  short of a reachable target (EMAP1013 below the fountain, EMAP1018 out of the well):
  the scripts click a way point first, as a player would.
- **World 4** spreads its puzzles over EMAP1010-1012 and EMAPBB11; Ooya can only travel by
  balloon, geyser and nacelle (b@5055 her place). Note: piercing the cloud while she
  rides past the giant also sets b@5058 (her ride to the corner), which the room tests
  first in the same frame; from then on she follows Blount between rooms.
- **World 6**: EMAP1015 keeps no inventory (the story book's objects appear on the table:
  0x322B); EMAPBB15 is a puzzle of moves (0x03C3, 0x1020; the walkthrough's moves were
  found with a search over those rules).
- **World 7**: the brain's switch (only Were-Blount can move it) cycles the vehicle's
  destination; the fish ride needs one goblin at the switch and the other at the vehicle
  while the bubble's rider walks back - Were-Blount rides the bubble, and his way back
  passes the switch.
- **World 8**: the bells open windows for each other and need the notes tied
  (b@31617, slot 3 frames 5600-5877) for the last pair.
- **Missing videos:** SOR (EMAP1019's arrival), PILOU (a coin toss, sound only), FORT21A
  and FORT21B (EMAP1006) and ROI2B (EMAP1014) are not in the GOG data; the rooms go on
  without them (VAR(11) = -1 as in ScummVM; a missing talking head holds its subtitle for
  2.5 s or a click).
- **FINAL** waits twice for a key: its loop polls with keyFunc 2 twice per pass and only a
  key caught by the second poll counts (mouse buttons aren't stored there); then the
  credits play to the end of MUS_FIN and `loadTot END` (no END.TOT on the CD) ends the
  game.
