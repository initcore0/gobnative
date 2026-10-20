# Gobliiins: level files, map and goblins (version 1)

Implementation notes for Gobliiins' level objects: the `.AVO` files, the
walk and item maps (`Map_v1` + `Map`), and the goblin and object state
machines (`Goblin_v1` + `Goblin`). The reference is ScummVM's
`engines/gob`; citations are `file:line` relative to
ScummVM's `engines/gob` (in its source tree).
Gobliins 2's map and goblins, which work differently (goblins are mult
objects there), are in `docs/goblins.md`.

Notation as in `docs/goblins.md`: `v[n]` is the 32-bit variable n (byte
offset 4n). A **cell** is 12 x 6 pixels; the version 1 map is 26 x 28
cells.

Statements marked **[data]** were checked against the 22 `.AVO` files of
the GOG CD version with `resdump <game> --avo`.

---

## 1. The .AVO file

`goblin 1000 loadObjects` passes a string variable holding the level's
`.AVJ` name (`AVT00.AVJ`). The engine strips the extension and loads
`<name>.avo` from the archives; only when that is missing does the original
read the older `.AVJ` + `.PAS`/`.POS` files, which no CD level needs
(ScummVM refuses them in `loadMapsInitGobs`). Every level has an `.AVO`
**[data]**.

All values are little-endian. Layout (`map_v1.cpp:64-140`):

| bytes | content |
|---|---|
| 728 | pass map, `int8 [28][26]`, row-major |
| 728 | items map, `int8 [28][26]`, sign-extended into the engine's `int16` items map |
| 160 | 40 way points: `u16 x, u16 y` (the list ends at the first one off the map, x or y = -1) |
| 60 | 20 item positions: `u8 x, u8 y, i8 orient` (where a goblin stands to act on item i; orient 0 or -4: it faces left, 4 or -1: it faces right; the negative values come from the engine's dummy target, section 4) |
| 132 | unused (32 + 76 + 4 + 20) |
| 3 x (2 + 14n) | three name lists: `u16 n`, n x 14-byte names (scenery/animation names of the editor) |
| 2 + 14n | **sounds**: `u16 n` (at most 20), n x 14-byte NUL-padded sample names; the engine appends `.SND` |
| 28 | unused (4 + 24) |
| 4 | `u16 gobPosCount, u16 objPosCount` |
| 8 per record | goblin position records, then object position records: `u16 xPos, yPos, order, state` |
| ... | **goblins**: `u16 count`, then per goblin an object block (below) |
| ... | **objects**: `u16 count`, then per object an object block |
| ... | `u16 n` records of 30 bytes, `u32 flag`, 56 bytes, and 30 more bytes when flag != 0 (skipped) |
| 50 | unused |
| 2 | `u16 hasItemTable`; when 0 the file ends here |
| 1456 | unused |
| 2 + 27n | **item to object**: `u16 n`, n x (20 bytes, `i16 objectIndex`, 5 bytes); entry i maps item id i to an object index (-1: none) |

**Object block** (`Map_v1::loadGoblins`/`loadObjects`):
- `u32 [40][6]` state pointers: a non-zero value marks state s, column c as
  present (the values are DOS pointers of the editor and mean nothing else);
- 160 unused bytes, `u8 multObjIndex`, a pad byte;
- for every present state, in state-major order, a 30-byte record:
  `i16 animation, i16 layer`, 8 unused, `i16 unk0, i16 unk1`, 2 unused,
  `u32 hasSound`, `i16 sndItem` (when hasSound is 0 the word is skipped and
  sndItem = -1), `i16 freq, i16 repCount, i16 sndFrame`.

Each goblin/object takes its position record from a running pointer into
the position table: goblins start at the first goblin record, objects at
the first object record. **When gobPosCount is 0 the goblins read the
object records** (AVT005, AVT019 and AVT021); the positions are replaced
when the level places its goblins, so this is harmless **[data]**.

**[data]**:
- Every level has 4 goblins: the three player goblins (31 to 38 states)
  and a pseudo-goblin (index 3) that holds shared animations (5 to 38
  states). Goblins 0-2 have multObjIndex 3: their states above 39 are
  states of goblin 3 (section 3.3).
- 6 to 10 objects; 3 to 10 sounds (AVT005 lists an empty ninth name, which
  is not a file and is skipped).
- Only column 0 of the state tables is used by the engine.
- All 22 files parse to their end except for **2 or 90 trailing bytes**
  that the engine never reads: a zero word, preceded in 11 files by 88
  bytes that look like three editor rectangle records.

## 2. Load-time patches

The engine patches the loaded tables (`map_v1.cpp:236-260, 335-357`):

- goblins 0, 1, 2: state 39 column 0 becomes a new state with layer 98, 99
  and 100 respectively (animation 0, no sound);
- goblin 2: states 10, 11, 28 and 29, and goblin 1: states 10 and 11, get
  `sndFrame = 13`;
- goblin 3 gets 30 more states, 40 to 69, each `animation 9, layer
  state - 40`, no sound (its table has 70 rows);
- object 10 is replaced by a dummy object with one state (animation 9,
  layer 27), placed animated, then `type = 1` (hidden) and `unk14 = 1`.
  The scripts move it with `goblin 1015` and show or hide it with
  `setType`.

Sounds: `diamant1.snd` is always loaded into goblin sound 14 (the pick and
drop sound); the listed samples go into goblin sounds 0..n-1, skipping
names that are not files.

The engine implementation is `engine/goblins_v1.cpp` (`GoblinsV1`), the
pure parser `formats/avo.{h,cpp}` (unit test `tests/test_avo.cpp`).

## 3. Goblins and objects

### 3.1 The object record (`Gob_Object`, `goblin.h:69`)

Goblins (`_goblins[4]`) and objects (`_objects[20]`) share one record. The
fields the scripts see through the getters and setters (section 7):

| field | meaning |
|---|---|
| `state`, `stateColumn` | the current state (column 0 in practice) |
| `animation`, `curFrame` | the state's animation slot (set from the state) and frame |
| `xPos`, `yPos` | where the layer's frame 0 is drawn |
| `left/top/right/bottom` | where the object was last drawn (0 when not drawn) |
| `dirty*` | the union of the old and new rectangle while redrawing |
| `nextState` | state to switch to at the end of the cycle (-1: stay) |
| `multState` | like nextState, but taking precedence; above 39 it is state `multState - 40` of goblin `multObjIndex` (the pseudo-goblin) |
| `actionStartState` | the state at frame 1 of the current action |
| `curLookDir` | 0 left, 2 up, 4 right, 6 down |
| `type` | 0 shown, 1 hidden, 3 hidden after the next draw (picked up) |
| `doAnim`, `noTick`, `tick`, `maxTick` | animateObjects advances doAnim objects every maxTick ticks |
| `pickable`, `relaxTime`, `visible`, `order`, `unk14` | pickup flag, idle timer for op 500, drawn or only tracked, draw order (signed byte), keep looping |
| `stateMach`, `realStateMach` | the states in use (its own, or the pseudo-goblin's) and its own |

The char fields are signed bytes in the original and wrap the same way
here.

### 3.2 Placing (`Goblin_v1::placeObject`)

At level load every goblin (not animated) and object (animated) whose
state exists is reset: frame 0, tick 1/1, `type 0`, `nextState` and
`multState -1`, look left, visible, not pickable, a random `relaxTime`
(0-29), `order = bottom / 24 + 3` from the frame's bounds, and its
rectangle collapsed to `(xPos, yPos)`. It is then appended to the drawing
list (object 10 first, then goblins 0-3, then objects 0..n-1).

`goblin 1004 loadMap` (`Map::loadMapsInitGobs`) then switches goblins 0-2
to their `nextState` (the scripts set state 21 with op 250 first), stands
each one on its cell: `y = (cellY + 1) * 6 - (bottom - pieceTop)`,
`x = cellX * 12 - (left - pieceLeft)` (the frame's first piece is its
anchor), makes goblin 0 current and lets the other two animate.

### 3.3 Animation (`goblin 1002 animateObjects`)

For each shown object with `doAnim == 1` and `noTick == 0`: the tick
counter runs to `maxTick`, then the frame advances. At the end of the
layer the frame wraps to 0 and the object moves by the layer's
`animDelta`; with no next state, no multState and `unk14 == 0` it stops on
its last frame (`toRedraw = 0`). Otherwise `multState` (from the
pseudo-goblin when above 39) or else `nextState` becomes the state, and
the object is redrawn.

The current goblin is not animated here (`doAnim 0`): `doMove` (section 5)
advances it.

### 3.4 Drawing (`goblin 1003 drawObjects`)

1. Objects of type 3 are marked for redraw, type 1 are not.
2. Each object to redraw is erased by copying its last rectangle from the
   anim surface (the clean background `initMult` captured) to the back
   surface; for shown objects the dirty rectangle is the union with the
   new frame's bounds.
3. The list is sorted by `order`, then `bottom`, the current goblin last
   among equals (an exchange sort, `Goblin::sortByOrder`).
4. In that order: objects to redraw are drawn (clipped to the mult area;
   invisible ones are only measured), and the static planes in front of
   them are redrawn (`Scenery::updateStatic(order)`). A type 3 object
   becomes type 1. Objects that stay are redrawn inside the dirty
   rectangles of the moved ones they overlap, again with the static planes
   in front.
5. State sounds: a state whose `sndFrame` high byte is set holds two
   effects, one per byte of `sndFrame`, `sndItem`, `freq` (in hundreds of
   Hz) and `repCount`; otherwise one effect with the values as they are.
   The effect plays when the object is on that frame (item 0xFF / -1:
   none). Goblin sounds are the level's sample list (section 2); the
   engine keeps them in sound slots 40-55.
6. The PC CD version restarts the level's music whenever the CD is silent
   (`Sound::cdPlayBgMusic`): AVT00 mine, AVT001 nuit, AVT002 campagne,
   AVT003 extsor1, AVT004 interieure, AVT005/006 zombie, AVT007/008
   campagne, AVT009/010 extsor1, AVT011 interieure, AVT012 zombie, AVT014
   nuit, AVT015 interieure, AVT016 statue, AVT017 zombie, AVT018 statue,
   AVT019 mine, AVT020 statue, AVT021 mine, AVT022 zombie.

### 3.5 Boredom

Every 120 steps of the current goblin (`moveAdvance`), `showBoredom` runs
for the three goblins: an idle goblin standing in state 21 gets a random
fidget (multState 80-106 + goblin), others return to 21; the current one,
standing in states 18-21 at the end of its cycle with `v[59] == 0`, gets
multState 104 + goblin.

## 4. Clicks and targets

`goblin 1005 moveGoblin` runs one step of the current goblin with
`cont = 1` and the action in a variable (0 walk, 3 act / use the pocket
item, 4 pick up); when the "use" variable is set the action is 3.
`goblin 1010 moveGoblin0` runs the step without a new target. A new target
is only taken while `v[59] == 0` (the scripts set it while an action
plays) and the goblin is not acting (states 10/11) or hidden:

1. The click point is the mouse, plus (6, 7) for act and (7, 12) for pick
   (the cursor's hot spot).
2. `moveFindItem`: when acting, a click inside a shown object smaller than
   40 pixels aims at its middle; the cell is `(x / 12, y / 6)`; if that
   cell holds no item, the first neighbour that does (below, below right,
   right, above right, above, above left, left, below left) is taken.
3. `adjustDest`: a click on a blocked cell (not an item when acting) moves
   to the nearest walkable cell left, right, below or above, by pixel
   distance.
4. `adjustTarget`: picking on an empty cell tries the cells above, right
   and above right.
5. `targetDummyItem`: an empty walkable cell becomes item 0's position,
   with orient -4 or -1 by the goblin's facing.
6. `targetItem`: the item id comes from the high byte of the cell's item
   word when picking a pickable item, when the low byte is empty, or for
   the wizard (goblin 2) acting; otherwise from the low byte. The goal is
   the item's position (section 1), shifted along the item's cells for
   the low/high byte cases, and for a pick the walkable cell at the foot
   of the item's sprite.
7. `initiateMove`: way points nearest to the goblin and the goal; a direct
   path (`checkDirectPath`: following `getDirection` reaches the goal),
   else a path through the way points (`checkLongPath`), else no move.
8. `moveCheckSelect`: a click on the goblin's own feet makes it turn
   towards the player (states 16/23); a click on another goblin selects
   it (`switchGoblin`); a click on its own cell when acting acts at once.

### 4.1 Directions (`Map::getDirection`)

On a ladder (pass 3) or rope (6), a wanted vertical move continues up or
down if the next cell is open. Otherwise the first open cell of: W / E
(straight), N, NW, NE (up), S, SW, SE (down), NE, N, E (up right), SE, S,
E (down right), NW, N, W (up left), SW, S, W (down left).

## 5. Steps (`doMove`, `moveAdvance`, `movePathFind`)

Per step of the current goblin: its order becomes `bottom / 24 + 3`
(unless `v[59]` is set or it is in states 30/31), its cell is reloaded
when another goblin moved last, then `moveInitStep` (section 4),
`moveTreatRopeStairs` and `moveAdvance`.

`movePathFind` picks the next state from the direction to the current leg
goal (way point by way point for long paths): walking W/E/N/S turn
through the rotation table first (`rotStates`: rows from W, N, E, S to
columns W, N, E, S: 0 22 23 24 / 13 2 12 14 / 16 15 4 17 / 27 25 26 6),
diagonals are states 1, 3, 5, 7. Ladders: N/S on pass 3 are states 8/9;
ropes (pass 6) are for goblin 1 only (states 28/29) and stop the others.
On arrival to act: goblin 0 with an empty pocket picking nothing shrugs
(multState 104), else the goblin faces the item's orient: state 10 (left)
or 11 (right). Standing: state 20 on a ladder, else 18 (facing up or
right) or 19.

`moveAdvance` advances the frame, updates the facing from the state,
clicks the speaker (50-70 Hz, 5 ms) on frames 3 and 6 of walking states,
plays goblin sound 14 at 5000 Hz (pick) or 9000 Hz (drop) on frame 9 of
goblin 0's act states, and runs the boredom counter. On frame 3 of a walk
and at the end of each cycle the goblin moves one cell in its state's
direction (states 0-9, 38; 28/29 for goblin 1) and is stood on the new
cell. A pending multState replaces the state at the end of the cycle,
moving the object by the layer's delta instead. Goblins 1 and 2 reaching
states 10/11 set `goesAtTarget`, which makes the next `goblin 1009`
report the item.

## 6. Items and the pocket

The items map holds two item ids per cell (low and high byte). The pocket
holds one object (`itemIndInPocket`, -1 empty) with its item id
(`itemIdInPocket`).

- **Pick** (`pickItem`): the object becomes type 3 (hidden after the next
  draw), its id is removed from the map, its item position cleared.
- **Place** (`placeItem`): the object is shown again at goblin 0's feet
  (14 pixels to the right when facing right), its id put in the cells at
  and above the goblin and beside it (by its facing), and its item
  position set next to the goblin.
- **Swap** (`swapItems`): the pocket object is placed where the picked one
  was (its map cells and item position), and the picked one goes into the
  pocket.

These run at frame 9 of goblin 0's act states (`treatItemPick`, from
`goblin 1009 writeTreatItem`). `writeTreatItem` returns -1 when goblin 0
uses the pocket item (action 3, frame 0 of states 10/11), the item reached
when `goesAtTarget` is set (and makes that item's object the "acted-on
object", section 8), or 0.

`goblin 2005 initGoblin` makes goblin 0 current and, with an item in the
pocket, plays its drop animation and puts the item down at frame 10 (with
a 50 Hz beep).

`manipulateMap x, y, item` (op 40) moves an item: its id is removed from
the map and put in the 2x2 cells at (x, y), (x, y-1), (x+1, y), (x+1, y-1)
(the low byte when one of them already has a high-byte item); its item
position becomes the first walkable cell of x-2 (orient 4), x+2 (0), x+1
(0), x-1 (4).

## 7. Opcodes

Operand layouts are in `docs/opcodes.md` 10.3. Results go to `v[59]`.

| cmd | op |
|---|---|
| 1-16 | set state, curFrame, nextState, multState, order, actionStartState, curLookDir, type (0 also redraws), noTick, pickable, (11 none), xPos, yPos, doAnim, relaxTime, maxTick of an object (91-106: of a goblin); the acted-on object's variable follows |
| 21-37 | get the same fields (31 none, 32 maxFrame = frames - 1) |
| 40 / 42 | manipulateMap (42: the operands are variable numbers) |
| 41 / 43 | getItem: the high byte's id if any, else the cell's word |
| 44 | setPassMap |
| 50 / 52 / 53 | goblin cell in half resolution: set (stands it, state 21), get x, get y |
| 150 | setGoblinMultState goblin, state, 0: play state (layer position), sets `noPick`; goblin, x, y: stand on cell (x, y) in state 21 |
| 152 | object `unk14` |
| 200-203 | set/get the pocket's item id and object index |
| 204 / 605 | set an item position (x, y, orient) |
| 250 | setGoblinPos goblin, x, y: stand on the cell in state 21 |
| 251 | setGoblinState: the state at its layer's own position |
| 252 | setGoblinStateRedraw: the same for an object, shown and redrawn |
| 500 | decRelaxTime: count down; at the last frame below 0, restart (relax 50-149) |
| 502 / 503 | goblin cell x / y |
| 600 | clear the path |
| 601 / 602 | goblin visible / invisible |
| 603 / 604 | rectangles of object/goblin and goblin intersect |
| 1000 / 1001 | loadObjects (string variable with the .AVJ name) / freeObjects |
| 1002 / 1003 / 1004 | animateObjects / drawObjects / loadMap |
| 1005 / 1010 | moveGoblin (action variable, use variable) / moveGoblin0 |
| 1006 | switchGoblin to the next one |
| 1008 / 1009 | loadGoblin (section 8) / writeTreatItem (action variable, result variable, use variable) |
| 1011 | setGoblinTarget: `goesAtTarget` from a variable |
| 1015 | object 10's position from two variables |
| 2005 | initGoblin |

**switchGoblin** (1006, and clicks on a goblin) only works while `v[59]`
is 0, the current goblin is on frame 0 of a normal state and not on a
ladder or rope cell, and another goblin is shown: the old goblin goes back
to state 21 and animates, the next shown one becomes current (state 18 or
19 by its facing) and `v[106]` (byte 424) follows.

## 8. The variable record

`goblin 1000` binds the goblin record to variable bytes 236-456
(`Goblin::initVarPointers`) and sets byte 456 (the pocket) to -2. Before
that the engine writes nothing there.

| byte | v[n] | content |
|---|---|---|
| 236 | 59 | goblin op result |
| 240-308 | 60-77 | current goblin: state, frame, multState, nextState, x, y, left, top, right, bottom, doAnim, order, noTick, type, maxTick, tick, actionStartState, lookDir |
| 320, 324 | 80, 81 | its pickable, relaxTime |
| 328-396 | 82-99 | the acted-on object: the same fields |
| 408, 412, 420 | 102, 103, 105 | its pickable, relaxTime, maxFrame |
| 424 | 106 | current goblin index |
| 428 | 107 | 0 |
| 432, 436 | 108, 109 | current goblin's cell |
| 440 | 110 | current goblin's maxFrame |
| 456 | 114 | object in the pocket |

`goblin 1009` saves the record (`saveGobDataToVars`), the level script
runs its puzzle logic on the variables, and `goblin 1008` loads them back
into the objects (`loadGobDataFromVars`), redrawing the acted-on object
when its type or state changed.

## 9. Not ported / differences

- The `.AVJ` + `.PAS`/`.POS` level format of the floppy releases (not
  needed for the CD version).
- ScummVM's Gob1 workaround for the goblin stuck after a reload
  (`inter_v1.cpp:891`: at INTER position 2933 with `v[285] != 0`) does
  not apply to the CD INTER.TOT: no `if` starts at that offset and INTER
  never touches `v[285]`.
- Where the original would dereference a missing state or index past its
  tables (item ids above 19, object indices from unlinked items), the port
  uses an empty state, no object or a dummy item position; none of this
  happens in the tested levels.
- A bug of the original (ScummVM has it too) is fixed: in the spiders
  level (AVT001) the ladder (item 16) is linked to the small spider's
  pillow (object 1, item 2) so the spider can be shot from below; once the
  pillow can be taken, a pick on the ladder took it under id 16 (and
  removed the ladder from the map), and that pillow never caught the flask,
  so the level could no longer be won. `GameRules::pickAliases` makes such
  a pick take item 2 (`treatItemPick`); `tests/check_gob1_fixes.sh`.
