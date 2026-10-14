# Levels, worlds and saves

Sections 1-8 are Gobliins 2: how MENU.TOT strings the game's rooms
together, what carries over between them, how the original saves and
loads, and how gobnative's level select and quick save sit on top of
that. Section 9 is Gobliiins (Goblins 1), whose levels INTER.TOT starts
by number. Offsets are script offsets as printed by
`gobdis`; `v[n]` is the 32-bit variable at slot `n`, `s[n]` a string at
slot `n`, `b@n` a byte at byte offset `n` (see `docs/opcodes.md`).

## 1. Rooms and worlds

Every one of GOB00-GOB22 is a playable room (a screen with its own puzzle
state). There are no separate "menu" or "intermediate" levels among them:
INTRO/INTRO0 play the intro, MENU runs the rooms, INTER is the short
picture shown between two rooms, JOKER is the hint screen, FINAL and END
the ending.

The rooms form seven worlds. Within a world the goblins walk freely between
its rooms; each world ends with a one-way exit into the next one. The place
names come from the world's map list (MONDEn.CAT, section 4); a room's map
entry is the one its travel table (section 5) pairs with it.

| World | Rooms (first room first) | Map names |
|-------|--------------------------|-----------|
| 1 | GOB01, GOB02, GOB00, GOB03 | Village, Fountain, Wizard, Giant |
| 2 | GOB08, GOB07, GOB04, GOB05, GOB06 | Trench, Tom, Kael, Vivalzart, Jazz |
| 3 | GOB10, GOB09, GOB11 | Guards, Forge, Well |
| 4 | GOB12, GOB13 | Wreck, Mermaid |
| 5 | GOB14, GOB15, GOB16 | Storeroom, Throne, Armor |
| 6 | GOB17, GOB18, GOB19, GOB20 | (none), Plant, Toys, Mountain |
| 7 | GOB21, GOB22 | Lab, Death Kingdom |

GOB17 is a one-way night-time prologue to world 6 with no map entry: it
adds a third character (`v[4785] = 1` at 0x35DA) and leads only to GOB18.
World 7's two names are matched by position, since the travel tables in
GOB21/GOB22 are copies of world 6's (they list GOB18-GOB20). The first room
of worlds 2-7 shows an intermission picture with a CD track (`g2inter1` to
`g2inter6`) on its first visit; for world 6 that is GOB18, not GOB17.

## 2. MENU's room loop

MENU's entry function (0x3759) initializes the session and then loops:

- Init: `v[480]` = language (5 is folded to 2); 0x3655 sets up the cursor,
  the default options and menu layout, byte 37 of the 40 name entries,
  `v[790] = 0`, and loads the menu strings from MENU.CAT. Then
  `v[791] = 0`, `v[2433] = 1` (show item names), `v[1353] = 1`,
  `s[3734] = "GOB01"`, `v[1701] = v[1705] = -1` (nothing in either goblin's
  hand).
- Loop (0x37D5 to 0x3AE9, until `v[791] == 1`):
  1. `v[790] >= 1000`: a save was chosen in the in-game load dialog; 0x3446
     reads slot `v[790] - 1000` from CAT.INF (the whole variable space) and
     sets `v[790] = 1`.
  2. `v[1353] != 0`: a new world starts. MENU reads world `v[1353]`'s
     names from MONDEn.CAT into `s[793]` (40 entries of 40 bytes), clears
     every entry's byte 39 (0x3725), marks the items the goblins carry into
     that world (below), resets byte 37 of each entry to 250 and
     `v[4344] = 0`, and clears `v[1353]`.
  3. `totSub "inter"`, then 0x3247 loads the room's own name list
     (`s[3734]` + ".cat", 20 entries into `s[3335]`) unless the room is
     FINAL.
  4. `totSub s[3734] flags 2` (0x3ACA) runs the room until it returns.
  5. `v[4345] == 100`: the room asked for a hint; `totSub "joker"`.

So the next room is whatever the finished room left in `s[3734]`; MENU
itself never decides the order. `v[790] == 2000` is tested at 0x37DA but
never set by any script.

## 3. Inside a room

Every room's entry function has the same shape (GOB01: 0x2E1F):

- Load graphics and objects; `if v[790] == 1` restore from the variables:
  the objects' animation records and positions are copied back from the
  backup area (section 6) and the goblins are placed where they were.
- `if v[790] == 0` start afresh: each room keeps a visit counter in
  `v[2323]`-`v[2327]` (e.g. GOB01 uses `v[2324]`): 0 means the first visit
  (initial placement, intermission in a world's first room); above 1 the
  room restores its private state from its own storage area (GOB00:
  0x01FB reads `b@9092`-`b@9116`). `v[1883]` selects how the goblins walk
  in (which door); the room the goblins come from sets it.
- `v[60] = 0`, `v[790] = 0`, then the main loop
  `repeat <hotspot function> ... until v[60] == 1` (GOB01: 0x3254). The
  hotspot function is a single hotspot block with an 80 ms timer whose tick
  handler moves the goblins and runs the room's logic. Every room has
  exactly one such block.
- After the loop the room frees its animations (GOB01: 0x2B43) and the TOT
  ends, returning to MENU.

`v[60]` drives leaving: 2 starts the exit sequence (the tick keeps running
until the goblins' animations are done and no action is pending, e.g.
GOB01 0x407C, which then stores the room's private state with the room's
0x0080 function and sets `v[60] = 1`); 1 leaves at once. When `v[60] != 0`
the tick handler sets `v[16] = 100`, which ends the hotspot block.

`v[474]` / `v[1407]` hold the action in progress for goblin 0 / 1 (0 when
idle), `v[1400]` the goblin under control (0 or 1; 3 in rooms with the
third character). The icon-bar hotspot at the top edge (y 0-1) calls
`switchTotSub 0, 4` (MENU's function 4: the icon bar with inventory, map,
hints, options and the disk menu) only when
`(v[1400] == 0 && v[474] == 0) || (v[1400] == 1 && v[1407] == 0)`, or
whenever `v[1400] == 3`.

## 4. What carries over

All TOTs share one variable space, so everything persists unless a script
resets it. In particular:

- `s[793]`: the current world's item and place names, 40 bytes each. Byte
  39 is "in the inventory" for items and "visited" for places (the map
  lists places with byte 39 = 1); taking an item into a goblin's hand
  clears it (`v[1701]` / `v[1705]` hold the item index, -1 for none).
- Items carried into a new world are set by MENU when it loads the names:
  world 2 entries 0-2 (stone, bottle, matches); world 3 `b@3531` (kind
  elixir); world 4 `b@3371`, `b@3251` (stool, kind elixir); world 5
  `b@3371`, `b@3291`, `b@3571` (kind elixir, stool, glove) and
  `v[3722] = v[4761] = 0`; world 6 `b@3731` (file); world 7 `b@3251`
  (fountain water).
- Each room's private state lives in its own storage area and is restored
  on re-entry; the per-room working variables (`v[1695]`-`v[1708]`,
  `v[1869]`-`v[1898]`, ...) are shared and reloaded from that storage.
- The exits into a new world reset the visit counters `v[2323]`-`v[2327]`
  (GOB03 0x04C4, GOB08 0x1AAE, GOB11 0x2708, GOB13 0x0438, GOB16 0x13F7,
  GOB17 0x0CBD and GOB18 0x2155), so the next world's rooms see first
  visits.
- Options set in the icon bar: `v[3748]` music on/off, `v[2433]` item
  names on/off; `v[480]` is the language.

## 5. Leaving a room

Walking out: the room's action code sets `v[60] = 2`, `v[16] = 100`,
`v[1883]` (the door in the next room) and `s[3734]` (the next room, e.g.
GOB00 0x0447: `s[3734] = "gob02"`). An exit into the next world also sets
`v[1353]` to the new world's number and clears the visit counters:

| From | To | Where |
|------|----|-------|
| GOB03 | GOB08, world 2 | 0x04C4 / 0x0556 |
| GOB08 | GOB10, world 3 | 0x1AAE / 0x1BB8 |
| GOB11 | GOB12, world 4 | 0x26DC |
| GOB13 | GOB14, world 5 | 0x0438 / 0x060D |
| GOB16 | GOB17, world 6 | 0x13F7 / 0x1544 |
| GOB18 | GOB21, world 7 | 0x2155 / 0x21C9 |
| GOB22 | FINAL | 0x0234 |

Travelling by map: MENU's map icon lists the visited places of the world
and calls the room's entry 9 (GOB01: 0x33F1) with the choice in `v[476]`;
that stores the room's state (0x0080), sets `s[3734]` from a per-world
table (and sometimes `v[1883]`), then `v[60] = 2`, `v[16] = 100`. The
tables in GOB06, GOB17, GOB21 and GOB22 are leftovers from other worlds.

Hints: the room's entry 7 (GOB00: 0x478C) stores its state, sets
`v[60] = 1`, `v[790] = 1` and `v[4345] = 100`; MENU runs JOKER.TOT and then
the room again, which restores itself (`v[790] == 1`) and, seeing
`v[4345] == 100`, puts the goblins back (GOB00: 0x42C7).

## 6. Saving and loading

The save file is CAT.INF: a 600-byte index (15 names of 40 bytes, of which
the menu shows names up to 10 characters) followed by one full
variable-space snapshot per slot (gobnative keeps each slot in its own
file, see `src/engine/saves.cpp`). The disk icon (MENU 0x1ED6) offers
Save, Load, Quit and Version.

- Save (0x1D6D, 0x1C0A): pick a slot, type a name, confirm; 0x1AC9 writes
  the index, then 0x339A copies the 15 objects' animation records
  (`s[2435]`, 600 bytes) and positions (`v[3185]`-, `v[3260]`-, 15 each)
  into the backup at `s[3750]`, `v[3903]`, `v[3918]` and writes the whole
  variable space to the slot.
- Load (0x1E93, 0x1CC2, 0x1BCF): pick a slot, confirm; 0x1A45 sets
  `v[60] = 1`, `v[16] = 200` and `v[790] = 1000 + slot`. Back in the room,
  the hotspot block ends at its next tick (`v[16] != 0`), the main loop
  ends (`v[60] == 1`) without storing the room's state, and the room
  returns to MENU, which loads the slot, sets `v[790] = 1` and runs
  `s[3734]` (the saved room) again; the room restores itself from the
  backup.

Both happen inside the icon bar, i.e. while the room's main loop waits for
input with the menu-open condition of section 3 true.

## 7. gobnative: level select and quick save

`src/engine/levels.cpp`. All three features act at one safe point: the
room's single hotspot block is the outermost one being evaluated, the
interpreter is between frames in its input wait, the room is run by MENU,
`v[60] == 0`, `v[790] == 0` and the menu-open condition holds. A request
made earlier waits for that point (and skips the intro if it is running).

- Level select (`Interpreter::requestLevel`, overlay Levels tab, `gobrun
  --level-at`): at the safe point, leave the room like a menu load does
  (`v[60] = 1`, `v[16] = 200`). When the room's `totSub` has returned to
  MENU, the variables are replaced with a copy taken when MENU started its
  first room (a fresh game; the language, music and item-name options are
  kept), then set as the exit into that room would: `s[3734]` = room,
  `v[1353]` = its world (MENU then loads the world's names and carried
  items), `v[1883]` = the door used when first arriving (1 for GOB01, as
  INTRO0 sets it; 0 for every other room), visit counters cleared, and
  `v[1898] = 1` for GOB00 (GOB02 sets it on the way to the wizard). MENU's
  loop then shows INTER and starts the room normally, including the
  world's intermission.
- Quick save (`requestQuickSave`, Cmd+S / Ctrl+S): at the safe point, the
  object backup copy of MENU 0x339A, then the variable space is written to
  slot 14 with the name QUICKSAVE. It appears in the game's own load list
  (last row) and loads from there like any save.
- Quick load (`requestQuickLoad`, Cmd+L / Ctrl+L): at the safe point, the
  same variables as MENU 0x1A45 with slot 14, so MENU's own load path
  restores it.

Results are reported through `Host::notify` ("Saved", "Loaded", "Nothing
to load", "Can't save here"), shown as a toast by gobnative.

Limitations: a jump restores a fresh game's variables, so it reproduces the
state MENU and the exit set up for a world (names, carried items, visit
counters) but not anything else an earlier world may have left behind.
Jumping to a room other than a world's first one starts that world's other
rooms unvisited and their puzzles unsolved.

## 8. Scenes between and inside rooms, and skipping them

Apart from the intro and the ending (playMult animations), every scene the
player sits through is ordinary room script, measured with `gobrun
--watch-skip` (times from MENU's `totSub` of the room to the first input
wait of its main loop):

- INTER.TOT (section 2, step 3): fade to black, one of three pictures,
  fade in; about 1 s.
- A world's intermission, in the entry function of worlds 2-7's first room
  on its first visit (`v[2323] == 0 && v[790] == 0`, e.g. GOB14 0x3A0A):
  fades, a picture with a text, `playCDTrack "g2interN"`, then
  `repeat getCDTrackPos until v[59] == -1`, i.e. until 2 s after the track
  ends (no key test), fades, and the room's normal set-up. GOB08
  (`g2inter1`) 26 s, GOB10 (`g2inter2`) 24 s, GOB12 (`g2inter3`) 23 s,
  GOB14 (`g2inter4`) 19 s, GOB18 (`g2inter5`) 17 s, GOB21 (`g2inter6`)
  40 s.
- The goblins' arrival, also in the entry function: depending on `v[1883]`
  synchronous keyframed sequences (GOB01 0x328D / 0x32CF: `multSub` into
  slot 0 or 1, then `repeat call 0x356C; animate; animatePalette until
  v[18] == -1`), then `placeGoblin` / `moveGoblin`; 1-6 s (GOB01 from the
  intro 6 s, most rooms 2-4 s).
- Leaving: the goblin's exit action runs a synchronous sequence from the
  timer tick (GOB01 0x0395, action 50: `multSub`, `repeat blitCursor ...
  animatePalette until v[18] == -1`), then `v[60] = 2` and the exit
  sequence of section 3. World exits (section 5) are the same, followed by
  MENU's world change and the next world's intermission.
- Inside a room, an action's keyframed sequence normally runs
  asynchronously from the timer tick (`multSub` once, then each tick tests
  `v[18]`/`v[19]`), so the player keeps control of the other goblin; some
  actions block in a synchronous loop instead. Jingles (`PLAxxx`,
  `g20xxx`) go through a helper (GOB01 0x34DA) that waits for a playing
  jingle to end, starts the next and waits until it plays; the rooms'
  loop tracks (CAMPAGNE, INTERIEURE, STATUE, NUIT, AQUATIQUE) are
  restarted by the tick when the CD is idle and no sequence runs (GOB01
  0x1991).

gobnative (`src/engine/skip.cpp`) treats as skippable: INTER or MENU
running between rooms; a room before its main loop first waits for input;
a room with `v[60] != 0`; and a room whose main loop hasn't waited for input
for 250 ms (a synchronous sequence). Never while the game waits for input
anywhere else (icon bar, dialogs, `keyFunc 0`) or while a mouse button is
held. A skip fast-forwards: each engine frame advances the game clock and
the CD timeline by 1/60 s, as a frame does at normal speed, up to 1200
frames per host frame within a 12 ms budget, with effects silent (the
effect voice is timed on the game clock, so `v[1]` and the scripts' tests
of it behave as if it played), CD tracks started meanwhile only on the
timeline, and at the end the mixer restarted at the timeline's position. It ends when the room's main
loop waits for input with the selected goblin idle (the menu condition of
section 3) and `v[18]`-`v[21]` all -1 (or after 3 s of that with
sequences still running), when the intro or ending, JOKER or a screen that
waits for the player comes up, when a level jump or quick load is acted on,
or after 3 minutes of game time. Compared with an unskipped run at the same
point after the scene, objects and variables match except for clocks,
relax timers and the phase of idle animations.

## 9. Gobliiins: levels, access codes and gobnative's level select

Gobliiins (Goblins 1) has no MENU, no worlds and no saves. Its 22 levels
are script files of their own (AVT00, AVT001-AVT012, AVT014-AVT022), and
INTER.TOT starts them by number. `src/engine/levels_v1.cpp`.

**INTER** (entry 0x0080): if `v[286] >= 100` it subtracts 100, shows one of
three pictures at random, then `switch v[286]` loads the level: 0 AVT002,
1 AVT007, 2 AVT004, 3 AVT00, 4 AVT005, 5 AVT001, 6 and 8 AVT009, 7 AVT006,
9 AVT008, 10 AVT010, 11 AVT011, 12 AVT012, 13-21 AVT014-AVT022, 22 FINAL,
100 AVT003; -1 shows the game over picture and then the access code screen
(LOAD/EXIT). It also sets `v[372]`, the four magic objects shown in the
item bar (1 for level 5, 3 for level 1, 8 for levels 12-17, else 0). INTRO
sets `v[296] = 10` (full energy) and `v[286] = 200`.

**Play order.** Each level sets `v[286]` to the next level's number when
it starts, so the order is AVT003 (100), AVT00 (3), AVT002 (0), AVT004 (2),
AVT005 (4), AVT001 (5), AVT007 (1), AVT006 (7), AVT009 (6), AVT008 (9),
AVT010-AVT012 (10-12), AVT014-AVT022 (13-21), then FINAL (22). Number 8 is
never used.

**Energy.** `v[296]` (0-10) carries over from level to level; INTER never
resets it. A level remembers it on entry (`v[448]`, for AGAIN), draws the
bar from it (rows 167-170; when it isn't 10 the bar is filled up to it) and
loses a point for each mistake. At 0 the goblins collapse and the level
offers AGAIN (restart with the entry energy), LOAD (an access code) and
EXIT (game over).

**Solved.** A level's result is `v[16]` (1 lost, 3 solved, 5 loaded by
code, 2 quit); 3 comes from the exit sign (object 10, hotspot `v[287]`-
`v[290]`), shown when the level's goal is met (`v[136] = 1`) and clicked
with the right goblin and pocket (`v[444] = 3` or 33). The level then sets
`v[286] = 100 + next`, shows the access code (0x17EB in AVT003) and waits
for a click before loading INTER.

**Access codes** (7 letters; INTER 0x0B12, a level's 0x2A63): letter 6 is
`'A' + level number`, letter 7 a checksum (`'A' + (sum of letters 1-6) mod
26`), and letters 1-5 are a per-level base word (GAGEA for level 0, BULBE,
CANON, TOTOD, DRUID, FOUDR, GATEA, HAHAH, HIHIH, JONAS, FLUTE, DROIT,
BANJO, CUBEN, RALER, RATOP, GOBLI, ABCNS, LEMEI, LLEUR, JEUDE, ROLED, ETOUT
for 22, LESTP) raised by the energy, two per letter: INTER takes the
energy back as the sum of the raised amounts. With full energy the codes
for levels 2-22 of the play order: VQVQFDE, ICIGCAA, ECPQPCC, FTWKFEN,
HQWFTFW, DWNDGBW, JCJCJHM, ICVGCGT, LQPCUJV, HNWVGKB, FTQKVLE, DCPLQMH,
EWDGPNL, TCNGTOV, TCVQRPM, IQDNKQO, CDEPURJ, NGOGKSP, NNGWTTO, LGWFGUS,
TQNGFVC (the overlay's Levels tab and `gobrun --list-levels` list them;
`tests/check_gob1_levels.sh` types each one on the access code screen).

**gobnative.**

- Level select (overlay Levels tab, `gobrun --level-at T N` with N the
  level's number in play order or its script name): the running script
  file is ended the way a valid access code ends it: `v[286] = 100 + n`
  (200 for the first level), `v[296] = 10`, the code variables cleared
  (`v[128]`, `v[435]`, `v[426]`, `v[391]`, `v[378]`), the level's objects
  freed (goblin 1001) and INTER loaded. It is done at the level's main loop
  (its hotspot block waiting for input, `v[16] == 0`) or at any screen of a
  level or INTER that waits for a click (skull menu, AGAIN/LOAD/EXIT, game
  over, access code); requested during the intro, it skips the whole intro
  (both animations) and acts in the first level.
- Quick save (Cmd+S): only at a level's main loop. The game has no saves,
  so it writes a snapshot of its own, `gob1-quick.sav` in the save folder:
  "G1QS", a format version, the game key, the level's script and number,
  the variable space, the back surface and the anim surface (the clean
  room the objects are drawn over), the resource loaded into each cursor
  slot and the cursor animation, and `GoblinsV1::saveState()` (the walk and
  item maps, item positions, every goblin's and object's fields, the
  drawing list, the pocket, the movement state; its own version and the
  goblin/object counts, checked on load). State tables and sounds come
  from the AVO file, which the level loads again.
- Quick load (Cmd+L): leaves for the saved level with the saved energy (as
  a jump does), fast-forwards INTER and the level's start, and at the
  level's main loop puts the snapshot in place: the goblin state (refused,
  with nothing changed, if it doesn't fit the loaded level), the variables
  apart from VAR 0-15 and the language (`v[758]`), the room pixels the level
  changed since it was drawn, every shown object drawn again over the clean
  room (`GoblinsV1::redrawAll`), the item bar's pixels that differ (the
  portrait of the selected goblin, the item name) and the cursors. Damaged
  files and files of another format version are not offered or loaded.
  `tests/check_quicksave_gob1.sh` saves in the first level after the horn
  is picked up, loads from the next level, compares the variables and every
  goblin and object at both moments (equal) and solves the level from
  there.
- Skipping: INTRO and FINAL are cutscenes (Escape ends the current
  animation); INTER and a level's start (story text with narration, room
  fading in) up to its main loop are fast-forwarded, as are the sequences
  a level runs instead of its main loop (collapse, access code until a
  click). The fast-forward ends when the level's main loop waits for input,
  another screen starts or the game waits for a click.

**Walkthrough** (`tests/gob1/NN-TOT-name.txt`, played as one `gobrun
--script` run by `tests/check_gob1_walkthrough.sh`). The intro leads into
the first level and every level is solved in turn with full energy, shows
the next level's code and goes on through INTER, as a player sees it; the
last one leads to FINAL, which starts the intro again. The solutions were
read from each level's `switch v[285]` puzzle functions, its per-tick
function (monsters, timers, the exit condition) and the AVO maps
(`resdump --avo`). What the scripts had to take into account, all of it
the original's behaviour (the same code in ScummVM):

- Not every level has an exit sign: AVT007 (a bird carries the picker
  off), AVT018 (the goblins walk into the statue) and AVT021 (all three
  walk out at the bottom left) set `v[16] = 3` themselves, and AVT022
  ends with `v[16] = 6` and FINAL.
- AVT001 picks the thread that works at random when it starts (`v[307] =
  rand(2)`), with nothing on the screen to tell; the wrong one costs
  energy, so the script reads the variable (`ifvar`).
- AVT006 keeps `v[59]` (the busy lock) at 1 once its exit sign shows.
- Clicking the exit sign while the picker carries an item with the "use"
  cursor never ends the level: the forced drop (`goblin 2005`) starts the
  picker's act state, which the level takes as a use of the item, again
  and again. The scripts put the item down first (AVT006, AVT014).
- The path finding only follows direct lines or the way points from the
  nearest one, so long walks go in steps; a click on another goblin selects
  it, and a click on the goblin's own cell only turns it.
- Timed puzzles (AVT008's bird, AVT011's fly, AVT012's sorcerer, AVT017's
  dragon and foot, AVT022's bag) wait for the level's own variables
  (`untilvar`), and AVT017 syncs with the goblin's boredom animation
  (every 121 ticks), which would otherwise delay a click at random. The
  whole run also passes in the other languages, whose story narration
  shifts every timing and random number.
- AVT022 writes `v[106]` (the current goblin) itself without switching
  goblins, so `goblin N` goes by the engine's current goblin.
