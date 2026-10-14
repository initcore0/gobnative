# Gobliins 2: map, goblins and sound

Implementation notes for the walk map (`Map_v2` + `Map`), the two player goblins (`Goblin_v2`
+ `Goblin`), and sound effects. The reference is ScummVM's `engines/gob`. Citations are
`file:line` relative to ScummVM's `engines/gob` (in its source tree).
Gob2 pairs `Map_v2`, `Goblin_v2`, `Mult_v2` and `Inter_v2` (`gob.cpp:559-569`).

The scenery and mult internals (`Scenery::updateAnim`, `loadMult`, `initMult`,
`loadMultObject`, `Mult_v2::animate`, the `Mult_Object` / `Mult_AnimData` layouts) are in
`docs/animation.md`. This file refers to them by name only. Opcode operand layouts are in
`docs/opcodes.md`.

**How the claims were checked.** All 32 CD TOTs were disassembled with `build/gobdis`. Every
map resource and sound resource the scripts reference was pulled out of the EXT files and
parsed with the layouts below. Statements marked **[data]** come from that check.
**[uncertain]** marks anything that is inferred or looks like a ScummVM quirk rather than
original behaviour.

---

## 0. Conventions

- **cell**: one walk-map tile. In Gob2 a cell is 12 x 6 pixels, and the map is 26 x 33 cells
  (§1.3).
- `VAR(n)` is the 32-bit variable n (byte offset 4n). `v[n]` / `b@k` / `s[n]` are gobdis
  notation (dword variable n / byte at offset k / byte-addressed variable n).
- Goblin object i is `_mult->_objects[i]`, a `Mult_Object` (`mult.h:91`). Its per-object
  animation record is `pAnimData`, a `Mult_AnimData` (`mult.h:42`) that lives inside the
  script variables. Gob2 points it at `s[2435]` with a 40-byte stride
  (`initMult ..., v[3185], v[3260], s[2435]` in every level). Object 0's record therefore
  starts at byte 9740 and object 1's at 9780. Pixel positions are `v[3185+i]` / `v[3260+i]`.
- `Mult_Object` goblin fields and their types matter, because values wrap: `goblinX`,
  `goblinY`, `destX`, `destY`, `nearestWayPoint`, `nearestDest` are **uint8**. `gobDestX`
  and `gobDestY` are **int8** (`mult.h:101-108`). `Mult_AnimData.destX/destY` are **int8**.

---

## 1. Map (Map_v2)

### 1.1 State (`map.h:75-163`)

| field | Gob2 value / meaning |
|---|---|
| `_mapVersion` | first byte of the map resource. Always **2** in Gob2 [data] |
| `_screenWidth`, `_screenHeight` | 320, 200 for version ≤ 2 (`map_v2.cpp:121-125`). The constructor presets height 200 (`map_v2.cpp:43`) |
| `_passWidth` | **row stride of the pass map in bytes = 40** for version ≤ 2 (`map_v2.cpp:122`). It is *not* the map width |
| `_tilesWidth`, `_tilesHeight` | cell size in pixels, 12 and 6 [data] |
| `_bigTiles` | true when the high byte of the stored tilesHeight word is 0. Gob2 stores 0x0106, so **false** [data]. All the `hasBigTiles()` "-(y+1)/2" corrections are dead in Gob2 |
| `_mapWidth`, `_mapHeight` | screenWidth / tilesWidth = **26**, screenHeight / tilesHeight = **33** |
| `_passMap` | `int8*` pointing **into the script variable space**, not owned memory (§1.5) |
| `_wayPointCount`, `_wayPoints[]` | way points `{int16 x, y, notWalkable}` (`map.h:62`) |
| `_usesObliqueCoordinates` | false in Gob2. Only id 65534 sets it, and `Goblin_v2::freeObjects` clears it |
| `_itemsMap`, `_itemPoses`, `_curGoblinX/Y`, `_destX/Y`, `_nearestWayPoint/Dest`, `_loadFromAvo`, `_sourceFile` | Gob1 (`Map_v1`/`Goblin_v1`) only. Never touched by Gob2 code paths |

### 1.2 Draw op 0x50 `o2_loadMapObjects` → `Map_v2::loadMapObjects` (`inter_v2.cpp:638`, `map_v2.cpp:50`)

Script operands: `varidx passVar`, `i16 id`, then the sound list (§1.3 step 9).

1. `passVar` is a variable byte offset. `variables` = the address of that byte in variable
   space (`map_v2.cpp:60-61`).
2. If `(uint16)id >= 65520`, it is a special command on that variable block. Nothing more is
   read and the function returns (`map_v2.cpp:65-105`):
   | id | action |
   |---|---|
   | 65529 | stub (no-op in ScummVM) |
   | 65530 | for i in [0, mapW·mapH): `passMap[i] -= varByte[passVar+i]` |
   | 65531 | the same with `+=` |
   | 65532 | zero mapW·mapH bytes at passVar |
   | 65533 | `objects[u16 at passVar].animVariables` = a 16-bit variable array at passVar (later games) |
   | 65534 | tilesW, tilesH, mapW, mapH, oblique = bytes passVar+0..4 |
   | 65535 | `_passMap` = &variables[passVar] |
   | other ≥ 65520 | warning, no-op |

   65530-65532 walk the map **linearly** over mapW·mapH bytes and ignore the 40-byte stride.
   **None of these ids appears in Gob2** [data: all 46 calls use ids 30009-30014], so skip
   them.
3. Otherwise the resource `id` is loaded. If it is missing, the function returns before
   reading the sound list. That desyncs the script (see opcodes.md 8.10).

### 1.3 Map resource layout (byte exact, `map_v2.cpp:113-192`)

All multi-byte values are little-endian.

| step | bytes | content |
|---|---|---|
| 1 | `u8 version` | 2 in Gob2. 3 → passWidth 65, 640x200. 4 → 640x400, plus extra fields (not Gob2) |
| 2 | `u8 wayPointCount` | Gob2: 35-81 [data] |
| 3 | `i16 tilesWidth` | 12 |
| 4 | `i16 tilesHeightWord` | low byte = tilesHeight (6). High byte ≠ 0 means not big tiles. Gob2: 0x0106 |
| (4b) | `i16 screenW, i16 screenH` | version 4 only |
| 5 | `mapW·mapH` × `i8` | pass map, **row-major, packed with stride mapW (26)**. Gob2: 858 bytes |
| 6 | `wayPointCount` × `{i8 x, i8 y, i8 notWalkable}` | way points (sign-extended). If the resource's first byte is 1 (version 1), the count is forced to 40 |
| 7 | `i16 n1`, n1 × 14 bytes; `i16 n2`, n2 × 14 + 28 bytes | skipped by the engine. They are NUL-padded ASCII names (editor metadata: scenery or animation names), followed by unused bytes [data] |
| 8 | `i16 gobCount`, gobCount × 14 bytes (names, skipped), then gobCount × goblin-state block (§1.4) | `_goblin->_gobsCount = gobCount` (`map_v2.cpp:186`). Gob2: 2 or 3 [data] |
| 9 | **script**: `i16 soundCount`, then soundCount × `loadSound(1)` | `_goblin->_soundSlotsCount` and `_soundSlots[i]` (`map_v2.cpp:190-192`). See §3.3 |

Order of work. The engine reads steps 1-4, computes mapW/mapH, skips the pass map and reads
the way points (6). It then seeks back to step 5 and copies the pass map
(`map_v2.cpp:163-177`), and continues at step 7. The copy happens **only if**
`variables != &variables[0]`. The copy uses `setPass(x, y, byte)` for y < 33, x < 26, so
the 26-wide packed rows are spread into the 40-byte-stride buffer in variable space, and
`_passMap` = that variable address. The scripts call it in two ways [data]:

- `loadMapObjects s[63], <id> ...` on a fresh level entry (`v[790] == 0`). The pass map
  lands at byte 252 = `VAR(63)`, 33 rows × 40 bytes. Scripts read it back as the 2-D array
  `as[63](y){x}`. That confirms the 40 stride.
- `loadMapObjects v[0], <id> ...` when `v[790] != 0` (re-entering a level). Here
  `variables == &variables[0]`, so the pass map is **neither copied nor re-pointed**. The
  previous `_passMap` pointer and the current variable contents stay. Everything else
  (way points, goblin states, sounds) is reloaded. **Edge case:** if this runs before any
  `s[63]` load in the session, `_passMap` is null and `getPass` returns 0 everywhere. A
  native engine should keep `_passMap` = &VAR(63) across levels, as ScummVM does by never
  resetting it.

Every Gob2 map resource parses to exactly its end with this layout [data].

### 1.4 Goblin-state block (`Map_v2::loadGoblinStates`, `map_v2.cpp:197-251`)

One block per goblin object index 0..gobCount-1. It fills `_objects[index].goblinStates`,
an array of 101 pointers to `Mult_GobState` (`mult.h:80`):

- `i8 indices[100]`. For slot i in 0..99, `indices[i]` is a **state number** or -1 (absent).
  Slot order is only the order of the records that follow. State numbers are **not** equal
  to slot numbers in general [data: 575 equal, 2066 not]. Gob2 state numbers go up to 98.
- Then, for each slot with indices[i] ≠ -1, in slot order:
  `i16 animation`, `i16 layer`, `u8 dataCount`, then dataCount × 9-byte sound records:
  `u8 recIndex` (skipped. Equals the record number 0..9 in 1386 of 1392 records [data]),
  `i8 sndItem`, `u8 0` (skipped), `u8 sndFrame`, `u8 0` (skipped), `i16 freq`, `i8 repCount`,
  `u8 speaker`. The skipped zero bytes look like the high bytes of 16-bit item/frame values
  **[uncertain]**.
- `goblinStates[state][0]` = {animation, layer, dataCount}. `goblinStates[state][1..dataCount]` =
  {sndItem, sndFrame, freq, repCount, speaker}. A state that is absent stays null.
- Gob2 data [data]: sndItem 0-24 (an index into `_soundSlots`), sndFrame 1-35, freq
  2000-12000 Hz, repCount 1 or 2, speaker always 0. Animation 0-3. Goblin 0 always has
  state 8 → layer 45 and state 9 → layer 44. Goblin 1 has 8 → 49 and 9 → 48 (the hard-coded
  layers in §2.7 depend on this).

`loadGoblinStates` allocates without freeing earlier arrays. Only `freeGoblins` frees them
(§2.3).

### 1.5 Pass map semantics

- Storage: `_passMap[y * 40 + x]`, int8, inside variable space (`map.h:151`, `map.cpp:122-144`).
- `getPass(x, y)` returns 0 when `_passMap` is null or the cell is outside
  [0, 26) x [0, 33). Otherwise it returns `_passMap[y*_passWidth + x]`. `setPass` uses the
  same bounds check and silently ignores writes outside them.
- Pixel ↔ cell, as used by Gob2 code:
  - mouse → cell (`goblin.cpp:1802-1803`): `cx = mouseX / 12`, `cy = mouseY / 6`.
  - cell → sprite position (`placeObject`, `setState`, `moveAdvance`, `o2_loadMultObject`):
    `posX = cx * 12` and `posY = (cy + 1) * 6 - h`. Here h = `_animBottom - _animTop` of the
    state's animation layer, frame 0, measured with `Scenery::updateAnim(..., flags 0)`.
    The sprite's bottom edge sits on the bottom of cell row cy.
  - drawing order: `animData.order = cy` (`goblin_v2.cpp:78`, `:158`).
- Cell values in the Gob2 data: **0, 1, 18, 19, 20** [data]. No script writes the pass
  map [data].
  | value | meaning (from the code that tests it) |
  |---|---|
  | 0 | blocked. `getDirection` / `checkDirectPath` treat any nonzero value as walkable |
  | 1 | floor |
  | 18, 19, 20 | climbable: "ladders, ropes, stairs" (comment at `goblin_v2.cpp:609`). A value > 17 triggers the climb-layer swap and destination snapping (§2.7). 19/20 also trigger the "don't stop mid-ladder" snapping in `move` (§2.3) |
  | 3, 6 | ladders for `getDirection` (`map.cpp:202-212`). Not in the Gob2 data, so dead |
  | 10, 11 | 640-wide maps only (`goblin_v2.cpp:250-325`, `goblin.cpp:1821-1826`). Dead |
- Way points [data]: `notWalkable` is always 0 or -1 in Gob2. It is **never 1**, so every
  "blocking way point" test (`map.cpp:489-497`, `map_v2.cpp:281`) is inert in Gob2. All way
  points lie inside the map.

### 1.6 `Map::getDirection(x0, y0, x1, y1)` (`map.cpp:178-347`)

The result is a `Direction` (`map.h:50`; the values double as arrow keycodes).

1. Already at (x1, y1) → None. If `x1 < 0 || x1 > mapW || y1 < 0 || y1 > mapH` → None.
   The test is `>`, not `>=`.
2. rel = Up/Down from the sign of y1 - y0, OR-ed with Left/Right from the sign of x1 - x0.
3. Ladder rule (cell value 3 or 6 only, so dead in Gob2): if rel contains Up and the cell
   above is nonzero → N. If rel contains Down and the cell below is nonzero → S.
4. Otherwise the first candidate whose target cell is nonzero wins. If none is, None:
   | rel | candidates in order |
   |---|---|
   | Left | W |
   | Right | E |
   | Up | N, NW, NE |
   | Down | S, SW, SE |
   | Right+Up | NE, N, E |
   | Right+Down | SE, S, E |
   | Left+Up | NW, N, W |
   | Left+Down | SW, S, W |

`moveDirection` (`map.cpp:438-479`) applies the step: NW = (-1, -1), N = (0, -1), and so on.

### 1.7 Path primitives

- **`checkDirectPath(obj, x0, y0, x1, y1)`** (`map.cpp:481-510`): simulates greedy
  `getDirection` steps from (x0, y0). Returns **1** when it reaches (x1, y1) and **3** when
  `getDirection` gives None. With obj ≠ null, every iteration first returns 3 if the next
  way point toward `nearestDest` has notWalkable == 1 (inert in Gob2). The walk always ends:
  each step strictly lowers 2·|dy| + |dx| or |dx|.
- **`findNearestWayPoint(x, y)`** (`map.cpp:349-370`): scans the way points in order and
  **stops at the first one outside the map** (`break`, not `continue`). Returns the index
  with the smallest Manhattan distance. Ties go to the **later** index (`<=`). Returns -1 if
  there is none.
- **`Map_v2::findNearestToGob(obj)`** / **`findNearestToDest(obj)`** (`map_v2.cpp:253-265`):
  `obj->nearestWayPoint` = nearest to (goblinX, goblinY), `obj->nearestDest` = nearest to
  (destX, destY). Each is assigned only if the result ≠ -1.
- **`Map_v2::optimizePoints(obj, x, y)`** (`map_v2.cpp:267-290`). No-op if there are no way
  points. If `nearestWayPoint < nearestDest`: for i = nearestWayPoint up to nearestDest
  (inclusive), if `checkDirectPath(obj, x, y, wp[i]) == 1` then nearestWayPoint = i. So it
  picks the **furthest** point along the chain that can be reached directly. Otherwise:
  for i = nearestWayPoint down to nearestDest, stop if wp[i].notWalkable == 1 (inert), and
  set nearestWayPoint = i whenever the point can be reached directly.
  checkDirectPath reads `obj->nearestWayPoint` while the loop is changing it. That only
  affects the inert notWalkable test.
- **`findNearestWalkable(&cx, &cy, mouseX, mouseY)`** (`map.cpp:372-436`). Used only by
  `move()` for a mouse click on a blocked cell. Here mapW = 320/12 and mapH = 200/6.
  1. Left: the smallest i in 1..cx with cell (cx-i, cy) walkable. If found,
     cand = (i-1)·12 + mouseX%12 + 1. `distance = i` is set **unconditionally**: cx+1 if
     nothing was found. dir = left.
  2. Right: the smallest i with cx+i < mapW and a walkable cell.
     cand = i·12 - mouseX%12. It replaces the current best if it is strictly smaller
     (or if there is no best). Sets dir = right, distance = i.
  3. Down: the same with 6 and mouseY. cand = i·6 - mouseY%6 → dir = down.
  4. Up: i in 1..cy, cand = (i-1)·6 + mouseY%6 + 1 → dir = up if strictly smaller.
  5. Apply: cx -= d / cx += d / cy += d / cy -= d.

  If nothing walkable exists in any direction, the result is cx = -1. `move()` maps that
  back to the goblin's own cell.
- **Gob1 only, skip**: `checkLongPath`, `getItem/setItem/placeItem`, `loadMapsInitGobs`,
  `Map_v1::*`.

### 1.8 VARs (map)

- Written: the pass-map bytes at the `loadMapObjects` variable operand (VAR(63)..,
  33 rows × 40 bytes).
- Read: VAR(17) only for map version 4 (not Gob2).
- `VAR(56)` belongs to the goblin code (§2.9).

---

## 2. Goblins (Goblin_v2)

### 2.1 Representation

- A Gob2 goblin is **not** a `Goblin::Gob_Object`. `_goblins[]`, `_objects[]`, the
  `_curGob*VarPtr` and `_destItem*VarPtr` variable pointers and the `_gobPositions` table are
  Gob1-only. A Gob2 goblin is a mult object `_mult->_objects[i]` with
  `goblinStates != null`, for i < `_gobsCount`.
  - Object 0 = goblin 1 (Fingus) and object 1 = goblin 2 (Winkle). The names are from the
    game, not the code. Both are always present [data].
  - In 12 levels a third state block exists for object 2 (`gobCount` 3; the block has 0, 7 or 24 states)
    [data]. Scripts drive object 2 with `moveGoblin ..., 2`, `stopGoblin 2` and
    `placeGoblin 2, ...`. `handleGoblins` only knows objects 0 and 1.
- `_gobsCount` (`goblin.h:178`): -1 = no goblins loaded (the constructor sets it,
  `goblin_v2.cpp:43`). Otherwise it is the map's gobCount. `Mult_v2::animate` routes
  animType 100/101 objects to the goblin code only when `_gobsCount >= 0`
  (`mult_v2.cpp:1169`).
- Goblin-only engine state (`goblin.h:190-198`): `_soundSlotsCount`, `_soundSlots[60]`,
  `_gob1Busy`, `_gob2Busy`, `_gob1RelaxTimeVar`, `_gob2RelaxTimeVar` (variable *numbers*),
  `_gob1NoTurn`, `_gob2NoTurn`, and `_rotStates[4][4]` (§2.6).
- Per-goblin `Mult_Object` fields: `goblinX`, `goblinY` (current logical cell), `destX`,
  `destY` (current leg target: the final cell or a way point), `gobDestX`, `gobDestY` (final
  target), `nearestWayPoint`, `nearestDest`, `goblinStates`.
- Per-goblin `Mult_AnimData` fields used by the goblin code (names only; offsets are in
  animation.md): `animation`, `layer`, `frame`, `animType` (100 = walking goblin,
  101 = state-animated, non-walking), `order`, `isPaused`, `isStatic`, `maxTick`,
  `maxFrame`, `newCycle`, `state`, `nextState`, `newState`, `curLookDir`, `isBusy`,
  `pathExistence`, `destX`, `destY`, `framesLeft`, `stateType`.
  - **`isBusy` is the "selected / player-controlled goblin" flag** in Gob2. The scripts
    write 1 to the selected goblin and 0 to the other when the player switches goblins
    [data: GOB01 0x3C68, 0x3DB6]. Do not confuse it with `_gob1Busy` / `_gob2Busy`, which
    come from VAR(474) / VAR(1407) ("goblin is performing an action").

### 2.2 How goblins come into being

1. `initMult` (animation.md) creates the objects. `goblinX = goblinY = 1`.
2. `loadMapObjects` fills `goblinStates` for objects 0..gobCount-1 and loads the goblin
   sound slots (§1.3, §1.4).
3. `o2_loadMultObject` with animType 100 or 101 and objIndex < `_gobsCount` runs the goblin
   branch (`inter_v2.cpp:419-477`). Gob2 scripts use
   `loadMultObject i, cellX, cellY, anim, STATE, 0, 100, ...`, where **the `layer` slot
   carries the initial state number** (29 or 28) [data]:
   - animType 100 (`:419-455`): goblinX = destX = gobDestX = `posX % 256`, and the same for
     Y. framesLeft = maxFrame, nextState = newState = -1, pathExistence = 0, isBusy = 0,
     state = the layer slot value, layer/animation = `goblinStates[state][0]`. Then
     `posX = goblinX*12` and `posY = (goblinY+1)*6 - h`.
     **[uncertain]** ScummVM measures h with `updateAnim(<state number>, 0, animation, ...)`,
     that is, it passes the *state number* as the layer index rather than the state's
     layer. That looks like a bug. Measuring the state's real layer is probably what the
     original does, but this is unverified.
   - animType 101 (`:457-476`): nextState = newState = -1, state = the layer slot value,
     layer/animation come from goblinStates. If pos == (1000, 1000), pos = the layer's
     posX/posY. No grid snapping. Used once (GOB15 object 2) [data].
   - The goblin branch dereferences `goblinStates[layerSlot]` without a null check. Valid
     data never hits a null.

### 2.3 Opcodes

All are draw ops (`inter_v2.cpp:96-103`). The operands are in opcodes.md table 2.

**0x51 `o2_freeGoblins` → `Goblin_v2::freeObjects`** (`goblin_v2.cpp:50-64`). It sets
`_usesObliqueCoordinates = false`. If `_gobsCount >= 0`, it frees each object's
goblinStates storage, then calls `freeSoundSlot` on every `_soundSlots[i]` that doesn't have
bit 0x8000 set, then sets `_gobsCount = -1`. ScummVM leaves the `goblinStates` pointers
dangling; a native engine should null them. It is also called automatically when a TOT
finishes (`game.cpp:668`). Gob2: once per level (23 calls) [data].

**0x52 `o2_moveGoblin(destX, destY, index)` → `Goblin::move`** (`inter_v2.cpp:646`,
`goblin.cpp:1784-1849`):
1. gobDestX/Y = dest, and animData.destX/Y = dest.
2. If **animData.isBusy ≠ 0 and dest == (-1, -1)**: the target is the mouse.
   cx = mouseX/12 and cy = mouseY/6. If cell (cx, cy) is blocked, call
   `findNearestWalkable`. Then `gobDest = (c == -1) ? goblin's own cell : c`, copied into
   animData.destX/Y.
   If isBusy == 0, the (-1, -1) destination stays as it is, and int8 -1 becomes uint8 255
   in destX. The Gob2 scripts only issue `moveGoblin -1, -1, n` for the selected goblin
   [data: 27 sites].
3. `VAR(56) = 0`.
4. p = getPass(gobDest). If p == 11 on a 640-wide map, gobDestY++ (dead in Gob2).
5. If p is 19 or 20 (tall ladder): `down` = how many consecutive cells starting at gobDestY,
   going down, equal p. `up` = the same going up. The ScummVM variables are misnamed
   `ladderTop` / `ladderBottom`. gobDestY becomes `gobDestY - up` if up ≤ down, else
   `gobDestY + down`. That is the first non-p cell beyond the nearer end of the run; on a
   tie it picks the upper end. animData.destY is set to the same value.
6. `initiateMove(obj)` (§2.4).

Gob2 usage [data]: index 0 (160×), 1 (157×), 2 (246×). The scripts pair
`placeGoblin i, x, y, s` with `moveGoblin x, y, i` to the same cell to park a goblin.

**0x53 `o2_writeGoblinPos(varX, varY, index)`** (`inter_v2.cpp:656`): writes
`goblinX` / `goblinY` (the logical cell, uint8 widened to 32 bits) into the two variables.
Used with indices 0, 1, 2 [data].

**0x54 `o2_stopGoblin(index)`** (`inter_v2.cpp:667`): `pathExistence = 4`, nothing else.
The goblin finishes its current half-step, then switches to its standing state, and
pathExistence becomes 5 (§2.6). Indices 0/1/2 [data].

**0x55 `o2_setGoblinState(index, state, type)`** (`inter_v2.cpp:673-776`). It sets
`stateType = type` **first**, then returns if goblinStates or `goblinStates[state]` is null:
- type 0 (`:695-709`): frame = 0, state = state, layer/animation from goblinStates,
  **pos = that animation layer's posX/posY** (absolute, no grid), isPaused = isStatic = 0,
  newCycle = framesCount. Gob2: 2× (GOB00, object 2) [data].
- type 11 (`:748-771`): the same state switch, but the position is grid-snapped from
  goblinX/goblinY (posX = gx·12, posY = (gy+1)·6 - h). **Gob2 uses this 352 times**
  [data]. Common states: 31, 28, 19, 16, 35, 36 and script-computed values.
- types 1/4/6 (`:711-746`): keep the visual anchor. They measure the old frame-0 box and the
  old layer's animDeltaX/Y, switch state, measure again, and shift pos by the size delta plus
  the old delta. **Not used by Gob2 scripts** [data].
- stateType then shapes `isPaused` in `moveAdvance` / `Goblin::animate`: 0/1 clear isPaused
  every tick, 4 pauses at frame 0, 6 pauses at the end of the cycle, anything else (11)
  leaves isPaused alone.

**0x56 `o2_placeGoblin(index, x, y, state)` → `Goblin_v2::placeObject(null, 0, index, x, y, state)`**
(`inter_v2.cpp:778`, `goblin_v2.cpp:66-126`): goblinX/Y = (x, y), order = y.
- state == -1: frame = 0, isPaused = isStatic = newCycle = 0. It measures the *current*
  animation/layer frame 0 and grid-snaps the position. **No initiateMove.** Gob2 uses this
  for non-goblin objects 6, 8, 10, 12 [data: 8 sites].
- state ≥ 0 with `goblinStates[state]` present: state/layer/animation from goblinStates,
  frame = 0, isPaused = isStatic = 0, newCycle = framesCount, grid-snapped position, then
  `initiateMove(obj)`. A Gob3 workaround at `:110-112` is irrelevant.
- state ≥ 0 but no such state (or the object has no goblinStates): only
  `initiateMove(obj)`.
- `initiateMove` uses the old `gobDestX/Y`. placeGoblin alone would make the goblin walk
  back toward its previous target, which is why the scripts follow it with
  `moveGoblin x, y, i`.
- Gob2 states used: 8, 9, 28, a script-computed state (`s[2435]{v[1396]}` = the object's
  own animData.state byte), and -1 [data].

### 2.4 `Goblin_v2::initiateMove(obj)` (`goblin_v2.cpp:128-142`)

1. `destX/Y = gobDestX/Y` (int8 → uint8).
2. `findNearestToDest`, then `findNearestToGob`.
3. `optimizePoints(obj, goblinX, goblinY)`.
4. `pathExistence = checkDirectPath(obj, goblin → gobDest)`, so 1 (direct) or 3 (needs way
   points).
5. If 3: `destX/Y = wayPoint[nearestWayPoint]`. `getWayPoint` asserts that the index is in
   range.

### 2.5 Per-frame hook in `Mult_v2::animate` (`mult_v2.cpp:1150-1177`)

In the "advance animations" pass, after all objects have been drawn, each object that is
not `isStatic` and not `isPaused` is handled. When `maxTick == tick`, tick resets to 0 and:
- animType < 100, or `_gobsCount < 0` → `newCycleAnim` (animation.md);
- animType 100 → **`Goblin_v2::moveAdvance(obj)`**;
- animType 101 → **`Goblin::animate(obj)`** (§2.8).

Otherwise tick++. Gob2 goblins use maxTick 0, so they advance on every `animate` call
[data]. A paused or static goblin is frozen completely, path following included.

During a scripted `multSub` sequence the mult key frames set the object's animType to 1, so
the goblin code is bypassed. The stop frame restores animType from animTypeBak
(`mult_v2.cpp:1299`, `:1392`). While track k is running, `VAR(18+k)` holds its frame, and
-1 otherwise (`mult_v2.cpp:1400`).

Draw-order hook (`mult_v2.cpp:1069-1089`): when `_gobsCount >= 0`, objects that have
goblinStates and the same `order` are re-sorted so that the one with the larger `newBottom`
comes later. On equal bottoms, the one with isBusy == 1 is swapped later.

### 2.6 `Goblin_v2::moveAdvance(obj)` (`goblin_v2.cpp:340-511`)

Returns at once if `goblinStates` is null. Per call:

1. `movePathFind(obj)` (below), then `playSounds(obj)` (§3.7).
2. fc = framesCount of the current (animation, layer). If !isPaused, frame++.
3. stateType: 0/1 → isPaused = 0; 4 → isPaused = 1 if frame == 0; 6 → isPaused = 1 if
   frame >= fc.
4. curLookDir from state:
   | state(s) | curLookDir |
   |---|---|
   | 0 1 7 13 16 23 40 41 | 0 (west/left) |
   | 2 15 18 21 26 38 | 2 (north/up, back to the viewer) |
   | 3 4 5 12 19 22 42 43 | 4 (east/right) |
   | 6 14 17 20 27 39 | 6 (south/down, facing the viewer) |
   | 8 9 28 29 | unchanged. If pathExistence == 4 it becomes **5** |
5. newState switch (`:429-450`): if newState ≠ -1, frame == fc and newState ≠ state, switch
   to newState, add the old layer's animDeltaX/Y to pos, frame = 0, and **return**. Gob2
   scripts never write newState, so it stays -1 and this path is dead [data].
6. Half-step: if `isMovement(state)` (states 0-7, 38, 39) and `frame == (fc+1)/2`:
   s = nextState. (gx, gy) = (goblinX, goblinY + 1) **before** moving. `advMovement(s)`.
   If state ≠ s: switch to s (layer/animation from goblinStates, frame 0) and re-anchor
   `posX = gx·12` and `posY = gy·6 - h(s)`.
7. If frame < fc, return.
8. End of cycle: s = nextState. Switch to s (layer, animation, frame 0, state). Take
   (gx, gy) before moving, call `advMovement(s)`, and re-anchor pos from (gx, gy) as in
   step 6.

**`advMovement(s)`** (`goblin_v2.cpp:683-726`): moves the logical cell for the walking
states only. 0: x-1. 1: x-1, y-1. 2 and 38: y-1. 3: x+1, y-1. 4: x+1. 5: x+1, y+1.
6 and 39: y+1. 7: x-1, y+1. Anything else: no move.

So one walk cycle moves **two cells**: one at mid-cycle and one at the end. The logical cell
(goblinX/Y) is always one step ahead of the cell the sprite is anchored to. The walk frames
themselves carry the in-between pixel offsets. Copy this exactly; the re-anchoring uses the
pre-move cell.

**`movePathFind(obj)`** (`goblin_v2.cpp:144-338`). It sets newCycle = fc,
animData.destX/Y = gobDestX/Y and order = goblinY. Then, with (gx, gy) = goblinX/Y,
(dx, dy) = destX/Y and (fx, fy) = gobDestX/Y:

- pathExistence **1** (direct): dir = getDirection(g → d). If dir == None, pathExistence = 0.
  If g == f, pathExistence = 4. The second test wins.
- pathExistence **3** (way points):
  - g == f → pathExistence = 4, d = f, dir = None.
  - Otherwise, if checkDirectPath(obj, g → f) == 1, then d = f. pathExistence stays 3 and
    becomes 4 on arrival.
  - Otherwise, if g == d (the current way point has been reached):
    - nearestWayPoint > nearestDest: optimizePoints(obj, g). d = wp[nearestWayPoint]. If
      checkDirectPath(obj, g → d) == 3, then `VAR(56) = 1` and pathExistence = 0. Then, if
      still nearestWayPoint > nearestDest, nearestWayPoint--.
    - nearestWayPoint < nearestDest: the mirror image (++).
    - equal: if checkDirectPath(g → f) == 3 **and** getPass(f) ≠ 0, then
      d = wp[nearestWayPoint] and `VAR(56) = 1`, with pathExistence still 3. Edge case: the
      goblin is already standing on that way point, so it idles forever with pathExistence 3
      and never reaches 5. Otherwise pathExistence = 1 and d = f.
  - Otherwise (still walking toward the way point) d is unchanged.
  - dir = getDirection(g → d).
- pathExistence 0, 4 or 5: dir = None.
- destX/Y = d is stored back. Then nextState is chosen:
  | dir | nextState (320-wide map; the 640-only overrides are dead in Gob2) |
  |---|---|
  | NW | 1 |
  | N | 2 if curLookDir == 2, else rotateState(curLookDir, 2) |
  | NE | 3 |
  | W | rotateState(curLookDir, 0) |
  | E | rotateState(curLookDir, 4) |
  | SW | 7 |
  | S | 6 if curLookDir == 6, else rotateState(curLookDir, 6) |
  | SE | 5 |
  | None | curLookDir 0 → 8, 2 → 29, 4 → 9, 6 → 28. Any other curLookDir leaves nextState unchanged |

**`rotateState(from, to)` = `_rotStates[from/2][to/2]`** (`goblin.cpp:140`,
`goblin_v2.cpp:44-47`). The rows and columns are W, N, E, S:

| from \ to | W | N | E | S |
|---|---|---|---|---|
| W | 0 (walk W) | 18 | 19 | 20 |
| N | 13 | 2 (walk N) | 12 | 14 |
| E | 16 | 15 | 4 (walk E) | 17 |
| S | 23 | 21 | 22 | 6 (walk S) |

The off-diagonal entries are turn animations. The curLookDir table above confirms each
one's final facing: 18 W→N, 19 W→E, 20 W→S, 13 N→W, 12 N→E, 14 N→S, 16 E→W, 15 E→N,
17 E→S, 23 S→W, 21 S→N, 22 S→E. Turn states aren't movement states: the turn plays to the
end of its cycle, and the next cycle starts the walk. The curLookDir values the scripts
write are 0, 4 and 6 [data]. A curLookDir ≥ 8 would index past the table.

**Gob2 state numbers** (the meanings come from the tables above; the animations are data):

| state | meaning |
|---|---|
| 0-7 | walk W, NW, N, NE, E, SE, S, SW (one half-step each) |
| 8 / 9 | stand facing W / E (layers 45 / 44 for goblin 0 and 49 / 48 for goblin 1) |
| 12-23 | turns (table above) |
| 24, 25, 26 | boredom idles (§2.7). 26 also counts as "facing N" in step 4 |
| 28 / 29 | stand facing S (toward the viewer) / N |
| 30-98 | script-driven actions via setGoblinState / placeGoblin. 38-43 are only auto-selected on 640-wide maps |

### 2.7 Gob op 100 `o2_handleGoblins` → `Goblin_v2::handleGoblins` (`inter_v2.cpp:1659-1667`, `goblin_v2.cpp:513-672`)

Operands: six raw variable numbers. Gob2 always uses `1695, 1696, 1879, 1880, 474, 1407`
[data: one call per level, in the per-frame main-loop function]. The engine sets
`_gob1NoTurn = VAR(1695) ≠ 0`, `_gob2NoTurn = VAR(1696) ≠ 0`,
`_gob1RelaxTimeVar = 1879`, `_gob2RelaxTimeVar = 1880`, `_gob1Busy = VAR(474) ≠ 0` and
`_gob2Busy = VAR(1407) ≠ 0`. Then, with obj0/obj1 and anim0/anim1 (shown for goblin 1;
goblin 2 is symmetric with its own variables and layers 48/49):

1. **Boredom**: if `!anim0.isBusy` (not selected):
   - If `!_gob1Busy && anim0.isStatic == 0`: when `VAR(1879) == 0` and state == 28, the
     state becomes 24 + random(0..2), via `setState(0, ...)` below, and VAR(1879) = 100.
     Otherwise VAR(1879)--. It keeps decrementing past 0 (uint32 wrap), so boredom only
     fires on an exact 0 while in state 28. The scripts reset it to 100 themselves.
   - If the (possibly new) state is 8, 9 or 29, then curLookDir = 6. The next movePathFind
     idle choice is then 28 (face the viewer).
2. If `isBusy == 1 && isStatic == 0 && state ∈ {28, 29}`, then curLookDir = 0.
3. If `VAR(18) ≠ -1` (mult track 0 is running): layer 44 → curLookDir 4, layer 45 →
   curLookDir 0, and isBusy == 0 → curLookDir 6 (that last rule wins). Goblin 2 uses
   VAR(19) and layers 48/49.
4. **Auto-turn** (only when `VAR(18) == -1` and `!_gob1NoTurn`): if layer == 45 (standing
   W) && curLookDir == 4 && pathExistence == 5, `setState(0, 19)` (turn W→E). If layer == 44
   (standing E) && curLookDir == 0 && pathExistence == 5, `setState(0, 16)` (turn E→W).
   ScummVM's comments on these two lines have the directions swapped.
5. **Climb layers**: if getPass(goblin cell) is 18-20, `updateLayer1(anim0)`
   (`goblin.cpp:1851`) forces the layer by state: 2→8, 6→9, 17→26, 18→32, 21→22, 22→20,
   23→21. Goblin 2 uses `updateLayer2` (`goblin.cpp:1886`): 2→10, 6→11, 17→29, 18→35,
   21→25, 22→23, 23→24. It changes only `layer`, not `animation`.
6. **Ladder destination fix-up**, using anim0.destX/Y (int8). If goblin 1's destination is
   outside [0, 39]², **return at once**, which also skips goblin 2's fix-up. Otherwise, if
   getPass(dest) > 17: walk destY up (when the goblin is below the destination, gy > destY)
   or down (otherwise) until the cell is ≤ 17, then step back one cell. That lands on the
   end cell of the ladder. If the cell to its left is 0: destX++ when the cell to the right
   is nonzero. Otherwise (left is walkable): destX--. Then `move(destX, destY, 0)`. If both
   sides are blocked, the destination stays on the ladder end and `move` is re-issued every
   frame. Goblin 2 does the same check without the early return.

**`Goblin::setState(index, state)`** (`goblin.cpp:1683-1715`): returns if the state is
absent. Otherwise layer/animation from goblinStates, state, frame = 0, isPaused =
isStatic = 0, newCycle = framesCount, then a grid-snapped position from goblinX/Y. It does
not change nextState or pathExistence. **[uncertain]** ScummVM measures with
`updateAnim(..., doDraw = 1)`, which also blits frame 0 onto the back surface. A native
engine can measure without drawing; the next `animate` redraws the object anyway.

### 2.8 `Goblin::animate(obj)` for animType 101 (`goblin.cpp:1717-1782`)

Returns if isStatic. newCycle = fc of `goblinStates[state]`. Then `playSounds`. If !isPaused,
frame++. Then the stateType rules from §2.6 step 3. While newState == -1 and frame ≥ fc:
if framesLeft ≤ 0, framesLeft = maxFrame and frame = 0; otherwise framesLeft--. If frame is
still ≥ fc: with newState ≠ -1, switch to it (adding the delta of the *old* layer); without
it, frame-- (hold the last frame). The effect: the state's animation loops, holding its last
frame for maxFrame ticks between loops. Gob2 has a single user (GOB15 object 2) [data].

### 2.9 What scripts read and write

- `pathExistence`. **0** = no path or stuck. **1** = walking directly. **3** = walking via
  way points. **4** = arrived (or stopGoblin), still finishing the cycle. **5** = arrived and
  standing (state 8/9/28/29). The scripts poll `== 5` and `== 0`
  [data: `b@9758`, `b@9798`, `b@9838`].
- Also polled or written through animData [data]: `curLookDir` (writes 0/4/6, ~600
  accesses), `isStatic` (hides goblins), `animType` (100/101/6), `isBusy` (selection),
  `state`, `frame`, `layer`, `animation`, `destX/Y`, `newCycle`, `order`. Example: footstep
  clicks key off `animation == 0 && layer ≤ 19 && frame ∈ {2, 5}` (§3.11).
- `VAR(56)`: set to 0 by `move()` and to 1 when way-point navigation fails (`goblin.cpp:1816`,
  `goblin_v2.cpp:187/201/215`). **No Gob2 script reads it** [data].
- `VAR(18)`, `VAR(19)`: read by handleGoblins (mult track frames, written by Mult).
- VAR(1879), VAR(1880): read and written (relax timers). VAR(1695), VAR(1696), VAR(474) and
  VAR(1407) are read.

### 2.10 Dead code for Gob2 (skip)

These are all Gob1-only (`Goblin_v1`/`inter_v1` gob table, unreachable from `Inter_v2`):
`doMove`, `moveInitStep`, `moveTreatRopeStairs`, `moveCheckSelect`, `moveFindItem`,
`adjustDest`, `targetItem`, the item and pocket code, `Gob_Object` and its state machine,
`showBoredom`, `switchGoblin`, `drawObjects` and `animateObjects`. Also dead: `newState`
handling in moveAdvance, the stateType 1/4/6 branches of setGoblinState, everything gated on
screen width 640, pass values 3/6/10/11, way-point notWalkable == 1, animType 10/11/12
(oblique Gob3 coordinates, `mult_v2.cpp:734-863`), and `Goblin::setGoblinState` (a warning
stub).

---

## 3. Sound

### 3.1 Sample slots

- `Sound::_sounds[60]` (`sound/sound.h:53`, `:160`) is an array of `SoundDesc`. A slot is
  empty when it has no data. `sampleGetBySlot(slot)` returns null outside 0..59
  (`sound.cpp:97`).
- `SoundDesc` (`sounddesc.h:43`): `_type` (SND/WAV/ADL), data and size, `_frequency` (Hz),
  `_repCount`, `_id` (the resource id it was loaded from), `_flag`.
  `isId(id)` = loaded and `_id == id`.
- `sampleFree(desc)` (`sound.cpp:138-158`): for a non-ADL sample it stops the blaster only if
  that sample is the one currently playing (`SoundBlaster::stopSound(0, desc)`), then frees
  the data.
- At the end of every TOT (`game.cpp:667-678`), `freeObjects`, `blasterStop(0)` and a free of
  every SND/WAV slot. Samples never outlive their TOT except through a totSub.

### 3.2 SND sample format (`sounddesc.cpp:145-157`) [data: all 399 goblin samples check out]

| offset | size | meaning |
|---|---|---|
| 0 | u8 | flag. Gob2 uses 0x88 (332×) or 0x00 (67×). `_flag = byte & 0x7F`, or 8 if the byte is 0. Otherwise unused. ScummVM then zeroes this byte |
| 0-3 | u32 BE with byte 0 zeroed | sample count, i.e. a 24-bit big-endian length in bytes 1-3. ScummVM clamps it to resource size - 6. Gob2: always exactly size - 6 |
| 4 | u16 **BE** | sample rate in Hz, clamped to ≥ 4700 (`MAX(..., 4700)`). Gob2 rates are 2845-10896 Hz. The four rates below 4700 (2845, 3815, 3913, 4643) get raised by ScummVM's clamp **[uncertain whether the original clamps]** |
| 6 | n bytes | **signed 8-bit mono PCM**. ScummVM reads the bytes as `int8` (`soundmixer.cpp:67-72`). The data is centred on 0x00 (histograms peak at 0x0_ and 0xF_) [data] |

The SND samples are EXT resources (ids 30010+). **No loose .SND/.ADL/.INS/.DUM/.MDY/.TBR file
exists in the CD INTRO.STK** (283 files: TOT, EXT, text, CAT, LET, GDR, LIC, DAT), nor in the
floppy DISK*.STK [data].

### 3.3 `Inter_v2::loadSound(search)` (`inter_v2.cpp:1669-1747`)

- search = 0 (func 0x3A `o2_loadSound`): `valexpr slot`. A negative slot means
  **type ADL, slot = -slot**. Then `i16 id`.
- search = 1 (used by loadMapObjects and loadMult): `i16 id`. Look for a slot whose sample
  `isId(id)`. If one exists, use it and set `slotIdMask = 0x8000` ("shared, the other owner
  frees it"). If not, take the **highest-numbered empty slot** (59 downward). If none is
  free, warn and **return 0 without reading a 9-byte name** (a desync in theory; Gob2 never
  uses names).
- Then `sampleFree(slot)`. The shared case is freed too, and **the same resource is loaded
  again** into that slot.
- id == -1: `raw[9]` filename + ".ADL" or ".SND" from disk. Never used in Gob2 [data].
- Otherwise `getResource(id)` and `SoundDesc::load(type, res)`. `_id = id`. Returns
  `slot | slotIdMask`, or **0** on failure. A failure therefore looks like slot 0. That is
  harmless in Gob2, because all of the referenced resources exist [data].

### 3.4 Who loads sounds in Gob2 [data]

- **`loadMapObjects`** loads the per-level goblin sound list: 3-25 SND resources
  (`... sounds 30010 30011 ...`) into `_goblin->_soundSlots[]`, indexed by the state
  records' `sndItem`. `freeGoblins` frees the slots that aren't shared.
- **`loadMult`** sound keys with cmd 1 or 4 (`mult_v2.cpp:195-243`). Each distinct key
  `resId` gets one `loadSound(1)`. A repeated resId within the same mult reuses the
  `soundIndex` of the earlier key and consumes 2 script bytes. `_multData->sndSlot[]` keeps
  the returned values. `soundIndex = slot & 0x7FFF`. `freeMultKeys` frees the slots that
  aren't shared (`mult_v2.cpp:338-340`).
- **`o2_loadSound`** (func 0x3A): **never used by the CD version.** The floppy version uses
  it as `loadSound slot -1, 3000x` (ADL music into slot 1) in INTRO, MENU and INTRO0. The CD
  build replaced the AdLib music with CD audio.

### 3.5 Playback model (`sound/soundmixer.cpp`, `sound/soundblaster.cpp`, `sound.cpp:455-539`)

There is a single SFX voice. A new sample replaces the old one, provided the old one was
stopped first.

- `blasterPlay(desc, repCount, freq, fadeLength = 0)` → `SoundBlaster::playSample` →
  `SoundMixer::play`. It is **ignored if a sample is still playing** (`soundmixer.cpp:150`).
  Every caller therefore does `blasterStop(0)` first.
  - `setSample` (`soundmixer.cpp:105-144`): if freq ≤ 0, use the sample's own rate.
    Linear-interpolated resampling at `freq` Hz.
  - Repeat semantics (`checkEndSample`, `:156-168`): the mixer's repCount r plays the sample
    max(r, 1) times. r = -1 loops forever. So 0 and 1 both mean "once".
  - fadeLength > 0 means a **fade-in**: volume ramps linearly 0 → full over
    fadeLength / 10 seconds.
- `blasterStop(fadeLength)` (`SoundBlaster::stopSound` → `SoundMixer::stop`, `:82-97`):
  0 stops at once. n > 0 means a linear fade-out over n / 10 seconds, then stop. During a
  fade-out the mixer still counts as playing, so a `play` issued then is dropped.
- `blasterPlayingSound()` = 1 while a sample is playing (or fading).
- `blasterWaitEndPlay(interruptible = false, stopComp = true)` (`sound.cpp:521-539`): ends any
  composition, busy-waits (200 ms polls) until playback finishes, then stops. `VAR(57) = -1`
  is written only in the interruptible + Escape case, so never from `o1_waitEndPlay`.
- Compositions (`blasterPlayComposition`, `soundblaster.cpp:77-139`) chain up to 50 slots,
  each played once. **Unused by Gob2.**

gobnative's mixer (`src/audio/mixer.cpp`) keeps this model (one voice, the same repeat, fade
and "still playing" rules) and offers two renderings of the 8-bit data: *original* holds each
sample until the next (zero-order hold, like the Sound Blaster DAC), *enhanced* interpolates with
a Kaiser-windowed sinc through a low-pass at 0.42 of the playback rate, lets the filter ring out
after the last pass without counting as playing, and fades a cut-off voice over 2 ms.

### 3.6 Script sound opcodes

| op | handler | behaviour | Gob2 CD use [data] |
|---|---|---|---|
| func 0x38 `playSound(slot, repCount, freq)` | `o1_playSound` `inter_v1.cpp:1736-1775` | `_soundEndTimeKey = 0`. Return if the slot is empty. If repCount < 0 (needs `_soundFlags ≥ 2`; Gob2 sets 0x400D, `gob.cpp:339`): k = -repCount, end time = now + `calcLength(max(k-1, 1), f, fade)` ms (f = freq, or the sample rate if 0), and `_soundStopVal = calcFadeOutLength(f)` (`sounddesc.cpp:136-143`). `Game::checkKeys` later fades the sound out at that time (`game.cpp:882-886`). An ADL sample → `adlibLoadADL`, repeating = repCount-1, play. An SND sample → `blasterStop(0)`, then `blasterPlay(sample, repCount - 1, freq)`. **Script repCount 0 → mixer -1 = loop forever**, 1 or 2 → once, n → n-1 times **[uncertain whether that off-by-one is original]** | 3×, always `playSound 1, 0, 0` (MENU music toggle, FINAL). Slot 1 is never loaded in the CD version, so this is a **no-op** |
| func 0x39 `stopSound(expr)` | `o2_stopSound` `inter_v2.cpp:1441-1452` | expr < 0 → `adlibStop()` only (the blaster is untouched). Otherwise `blasterStop(expr)` (fade in tenths of a second). Clears `_soundEndTimeKey` | 6×, always `-1`, which is an AdLib stop and a no-op in the CD version |
| func 0x3A `loadSound` | §3.3 | | not used |
| func 0x3B `freeSoundSlot(slot)` | `o1_freeSoundSlot` → `Game::freeSoundSlot` `game.cpp:791` | `sampleFree(slot)` | 4×, slot 1 (already empty) |
| func 0x3C `waitEndPlay` | `inter_v1.cpp:1792` | `blasterWaitEndPlay()` | not used |
| func 0x3D `playComposition(var, freq)` | `inter_v1.cpp:1796` | 50 int16 slot numbers from the dword variables starting at var; -1 ends the list | not used |

So in the CD version **every SFX comes from the goblin state sounds (§3.7) and the mult sound
keys (§3.8)**, plus PC-speaker clicks (§3.11). Music is CD audio.

### 3.7 Goblin state sounds: `Goblin::playSounds(obj)` (`goblin.cpp:1644-1681`)

Called at the start of every `moveAdvance` (after movePathFind, **before** frame++), and in
`Goblin::animate`. For each record i = 1..dataCount of `goblinStates[state]`, with
sndItem ≠ -1 or speaker ≠ 0, and with `animData.frame == sndFrame`:
- speaker == 0: `blasterStop(0)`. If sndItem < `_soundSlotsCount`, then
  `blasterPlay(sample in slot _soundSlots[sndItem] & 0x7FFF, repCount, freq)`. The record's
  freq **overrides** the sample's header rate. repCount 1 or 2 plays once or twice.
- speaker ≠ 0: `speakerOn(freq, repCount * 10 ms)`. Not in the Gob2 data.

Because frame is compared before it is incremented, the sound fires on the tick that
advances away from frame sndFrame. That is the call right after the frame was drawn.
`Goblin::playSound(snd, rep, freq)` (`goblin.cpp:207`) is Gob1-only.

### 3.8 Mult sound keys (`Mult_SndKey`, `mult.h:164`; `Mult::doSoundAnim`, `mult.cpp:440-461`)

Resource fields per key: `frame`, `cmd`, `freq`, `fadeLength`, `repCount`, then 2 skipped
bytes (values 0-8 in the data, purpose unknown) and, without IMDs, 24 more skipped bytes.
At each frame N, every key with `frame == N`:
- cmd 1 or 4 (identical at playback): `blasterStop(0)`. If the sample at `soundIndex`
  exists, `blasterPlay(sample, repCount, freq, fadeLength)`: plays repCount times
  (0 or 1 = once, -1 = forever), at freq Hz (freq ≤ 0 → the sample's own rate), with a
  fade-in over fadeLength/10 s.
- cmd -1: if something is playing, `blasterStop(fadeLength)` (fade-out over fadeLength/10 s).
- cmd 3: only consumes 4 script bytes at load. Never in Gob2.

Gob2 data [data, 25 mults]: 6346 cmd-1 keys and 411 cmd -1 keys. Key repCount 1-100
(mostly 1). fadeLength is **always 0 on play keys**, and 0-85 on stop keys. freq is mostly
500-18765 Hz, but **159 keys have freq between -15 and 0**. ScummVM treats those as "use the
sample rate". **[uncertain]** The original may have meant a relative pitch.

Call sites: `playMult` calls it every mult frame (`mult.cpp:205`), and `playMult` keeps
looping while `blasterPlayingSound()` (`mult.cpp:210-211`). When a non-continuing playMult
ends, `blasterStop(10)` (`mult.cpp:259-260`). For `multSub` tracks,
`Mult_v2::advanceObjects` calls it with the track's frame (`mult_v2.cpp:1376`). It scans
**all** of that mult's sound keys against the track frame. These are mult internals; see
animation.md.

### 3.9 VARs (sound)

- `VAR(1)` = `blasterPlayingSound()`, written by `Inter::storeKey` (`inter.cpp:184-188`),
  which runs from `keyFunc 0/1` (`inter_v1.cpp:1374`, `:1392`) and `animatePalette`
  (`:1914`). **No Gob2 script reads VAR(1)** [data].
- `VAR(14)` = `_soundFlags` (0x400D) at each TOT start (`game.cpp:640`). MENU tests bits
  16, 8 and 4 to decide whether to offer the music toggle [data].
- `VAR(57)`: only `blasterWaitEndPlay(interruptible)`, which Gob2 never reaches.

### 3.10 AdLib, Infogrames, Protracker

- Gob ops 0/1/2/10 (Infogrames .INS/.DUM) and 500/501 (Protracker "mod.babayaga")
  (`inter_v2.cpp:1614-1657`). They are only instantiated on the Amiga (`sound.cpp:62-65`)
  and **never appear in the Gob2 scripts** (gob op 100 is the only one) [data]. Skip.
- AdLib: reachable only through an ADL-typed slot (loadSound with a negative slot, then
  playSound) and `stopSound(-1)`. **The CD version never loads one**, so the AdLib code is
  dead and `stopSound -1` / `playSound 1,0,0` / `freeSoundSlot 1` can be no-ops. (The floppy
  version loads ADL resources 30000-30009 from INTRO/MENU/INTRO0 into slot 1 and loops them
  with `playSound 1, 0, 0`. That is out of scope.)

### 3.11 PC speaker (`inter_v1.cpp:1480-1505`, `sound/pcspeaker.cpp:42-53`)

- func 0x22 `speakerOn(freq)`: start a square wave at freq Hz, with unlimited length.
  **ScummVM workaround for Gob2:** freq == 50 → length 5 ms, and the next `speakerOff` is
  ignored.
- func 0x23 `speakerOff`: stop the tone (unless it is being ignored).
- `keyFunc n` for n outside {-1, 0, 1, 2} also calls `speakerOnUpdate(n)`: if a tone is
  playing, it stops after n ms (`inter_v1.cpp:1415`). It then delays n ms (n < 20:
  `Util::delay`; otherwise `longDelay`).
- Gob2 use [data]: `speakerOn 50; keyFunc 5; speakerOff` (46×) is the **goblin footstep
  click**. It plays when a goblin's `animation == 0`, `layer` is 0-19 and `frame` is 2 or 5
  (GOB01 0x3570). The goblins share one click per frame (v[3749]). Also
  `speakerOn 500/100; keyFunc 50` (a MENU beep pair, 2×) and `speakerOn 1000; keyFunc 100`
  (MENU, 2×). A native engine can synthesise a 5 ms 50 Hz square-wave click. Since the half
  period of 50 Hz is 10 ms, that click is a single 5 ms rectangular pulse; gobnative's
  enhanced mode gives it 0.5 ms raised-cosine edges and a 4 kHz low-pass.

---

## 4. What Gob2 actually executes [data]

Counts are over all 32 CD TOTs, reachable code only (gobdis).

| opcode | count | parameters seen |
|---|---|---|
| draw 0x50 loadMapObjects | 46 | `s[63]` / `v[0]`, id 30009-30014, 3-25 sounds (2 per level, either/or on `v[790]`) |
| draw 0x51 freeGoblins | 23 | |
| draw 0x52 moveGoblin | 564 | index 0/1/2, dest = cells or (-1, -1) for the mouse |
| draw 0x53 writeGoblinPos | 69 | index 0/1/2 |
| draw 0x54 stopGoblin | 502 | index 0/1/2 |
| draw 0x55 setGoblinState | 354 | type 11 (352), type 0 (2) |
| draw 0x56 placeGoblin | 667 | state 8/9/28/animData.state/-1. Objects 0-2, and 6/8/10/12 with state -1 |
| draw 0x17 loadMultObject with animType 100/101 | 53 | objects 0/1 (+2), initial state 28/29 in the layer slot |
| gob 100 handleGoblins | 23 | always `1695, 1696, 1879, 1880, 474, 1407` |
| gob 0/1/2/10/500/501 | 0 | |
| func 0x38 playSound | 3 | `1, 0, 0` (slot empty, no-op) |
| func 0x39 stopSound | 6 | `-1` |
| func 0x3B freeSoundSlot | 4 | `1` |
| func 0x3A loadSound, 0x3C waitEndPlay, 0x3D playComposition | 0 | |
| func 0x22/0x23 speakerOn/Off | 52/52 | 50 (46×), 100, 500, 1000 |
| draw 0x00 loadMult | 25 | sound keys cmd 1 / -1 only |

Minimum native implementation: Map_v2 load (version 2 only), the getDirection,
checkDirectPath, findNearestWayPoint, optimizePoints and findNearestWalkable primitives,
Goblin_v2 move / initiateMove / movePathFind / moveAdvance / advMovement / handleGoblins /
setState / animate(101), placeObject, setGoblinState types 0 and 11, SND loading into 60
slots, one SFX voice with repeat, rate override and fades, the goblin state sounds, the mult
sound keys, and a PC-speaker click. The rest can be stubbed.

---

## 5. Open questions

1. `o2_loadMultObject` (animType 100/101) and possibly the original measure the frame height
   with the state number as the layer index (§2.2). Is that a ScummVM bug? Compare the
   goblin's initial Y position against the original.
2. `Goblin::setState` blits frame 0 to the back surface (doDraw = 1) in ScummVM (§2.7). The
   original behaviour is unknown.
3. o1_playSound's `repCount - 1` mapping (0 = loop forever) can't be checked against Gob2
   data, because the only call hits an empty slot.
4. Mult sound keys with freq ≤ 0 (159 keys): ScummVM plays them at the sample rate. The
   original meaning is unknown.
5. ScummVM clamps SND rates up to 4700 Hz; this affects 4 goblin samples. Was that clamp in
   the original driver?
6. The two skipped bytes in each mult sound key and the byte-0 index in goblin sound records:
   are they ever meaningful?
7. The handleGoblins early return when goblin 1's destination is outside [0, 39] (§2.7
   step 6) also skips goblin 2's ladder fix-up. ScummVM does this; the original probably
   does too.

## 6. Goblins 3: map version 3 and Goblin_v3

Goblins 3 uses the same draw ops (0x50-0x56) and mult hooks; its maps are version 3 and
`Goblin_v3` adds a few states. Code: `src/engine/goblins.cpp` (branches on `_ver >= 3`
or on the map's width, so Gobliins 2's path is unchanged).

- **Map version 3** (first byte of the map resource): the map covers 640 pixels (`640 /
  tilesWidth` columns; 20-pixel tiles: 32 columns, 4-pixel rows: 50 rows), and the pass map
  is spread into variable space with rows **65** bytes apart (version 2: 320 and 40). The
  tile height word has its high byte set (no "big tiles"). `findNearestWalkable` scans the
  640-pixel width too. Pass values in the rooms: 1 floor, 10 and 11 stairs (11 the second
  cell of each step), 20 ladders and ropes, 19 another ladder type. EMAP1009, EMAP1015 and
  EMAPBB15 have maps of another kind (their values look like heights).
- **Movement states** (`Goblin_v3::isMovement`, `advMovement`): besides 0-7 and the ladder
  states 38/39 (one cell up or down), 26/27 move one cell up or down (pass 19) and the
  stairs states 40-43 move one cell sideways and **two** cells up or down: 40 up-left, 41
  down-left, 42 up-right, 43 down-right.
- **movePathFind** (`goblin_v2.cpp:278-334`, for maps 640 wide): on a stairs cell (pass
  10), a north-west/north-east/south-west/south-east step whose cell two rows up/down and one
  column over is stairs too becomes 40/42/41/43; north on stairs picks 40 or 42 the same
  way (else 2); north and south on pass 20 become 38/39, on pass 19 26/27.
- **moveGoblin** (`Goblin::move`): a destination on pass 11 (the second cell of a stair
  step) moves one row down, so the goblin doesn't keep walking on wide stairs.
- **moveAdvance** queued state: when newState is set and the cycle ends on another state,
  that state takes over (moved by the layer's delta) instead of the next walking step.
  The Gobliins 2 port never needed it; it runs for version 3 only.
- **placeGoblin** (`Goblin_v3::placeObject`): nothing for an object without goblin states;
  the standing states set the look direction (8 west, 9 east, 28 south, 29 north).
  WORKAROUND (ScummVM bug #5760): in EMAP1018, Blount placed at 3,33 in state 9 with layer
  27 of animation 0 and anim type 1 gets anim type 100, so he can walk again.
- Goblin op 100 is wobble (`docs/opcodes.md` 11.1); there is no handleGoblins.

The rooms' scripts redirect a walk at certain cells to keep the goblin on its route (tables
in `av[1076]`): in EMAP1000 a click on the mast first takes Blount to the foot of the rope
(10,30) and a second one up the rope.

Checked with `gobrun ../goblins3 --press-every 5 --press-stop 60 --click ... --print-objects`
in EMAP1000: up the zigzag stairs from 6,21 to 4,10 (states 5, 2, 40, 42, 40, 0), down
again (43, 41, 43), up and down the rope in column 11 (38 from 11,24 to 11,6; 39 back), and
up the pass-19 ladder in column 13 (26).
