# Gobliins 2: scenery, mult and object animation

Implementation notes for the animation layer of the v2 engine: `Scenery` (anim/static
resources and piece composition) and `Mult_v2` (mult objects, keyframed mults, the per-tick
`animate`). It sits on top of things that are assumed done: `spriteOperation`, sprite slots,
invalidation, `blitInvalidated`, the interpreter and operand layouts (`docs/opcodes.md`), and
`docs/graphics.md`. Goblin movement (`Goblin_v2`), the map (`Map_v2`) and sound are covered
elsewhere; here they appear only as named calls.

Every `file:line` points into ScummVM's `engines/gob` (in its source tree).

## 0. Ground rules

- **Classes** (`gob.cpp:550-554`): `Mult_v2`, `Scenery_v2`, `Goblin_v2`, `Map_v2`. `Scenery_v2`
  adds nothing: its `loadAnim` calls the **base** `Scenery::loadAnim` directly
  (`scenery_v2.cpp:38-40`). That skips `Scenery_v1::loadAnim`'s "wait for the CD track to end"
  (`scenery_v1.cpp:40-49`). `Mult_v2` overrides every virtual in `Mult` (`mult.h:340-364`).
  `playMult` and the palette/sound/text key helpers are base `Mult` code.
- **TOT version 1.2** everywhere, so `getVersionMinor()` is 2. Every `> 2` / `>= 3` branch is
  dead (IMD keys, the redraw-skip optimisation in `animate`).
- **`animDataSize` is 10** in every Gob2 TOT (header byte 56, `totfile.cpp:93`, copied to
  `_inter_animDataSize` at `game.cpp:628`). So an object's anim-data record is 40 bytes wide.
- **`adjustCoords` is a no-op** (graphics.md 0). Ignore every call to it.
- **`VAR(n)`** is the 32-bit variable at byte offset `n*4`. In gobdis output, `v[n]` is
  `VAR(n)`, `b@N` is the byte at offset N, `s[n]{k}` is byte `n*4+k`, and `as[n](i){k}` is
  byte `n*4 + i*4*animDataSize + k` (`expression.cpp:734-741`).
- **Surfaces**: 20 front, 21 back, 22 anim surface (`draw.h:61-63`). Scenery picture sprites
  are allocated downward from slot 19 (1.3).
- **Usage counts** come from `build/gobdis` over all 32 TOTs (section 3).

---

## 1. Scenery

### 1.1 State (`scenery.h:38-159`)

Ten anim slots and ten static slots, both indexed 0..9.

| field | meaning |
|---|---|
| `_animPictCount[10]` | number of pictures in anim slot i. **0 = slot free** |
| `_animResId[10]` | resource id of the anim (used by search mode) |
| `_animations[10]` | `{layersCount, layers[], pieces[pict][piece], piecesCount[pict]}` |
| `_animPictToSprite[70]` | draw sprite slot of picture p of anim i at `[7*i + p]`. **Max 7 pictures per anim** |
| `_staticPictCount[10]` | same for statics, but **-1 = slot free** |
| `_staticResId[10]`, `_statics[10]`, `_staticPictToSprite[70]` | same as the anim versions |
| `_spriteRefs[20]`, `_spriteResId[20]` | refcount and source sprite resource of draw slots 0..19 used as picture sprites. A picture whose sprite resource is already loaded shares the slot |
| `_curStatic`, `_curStaticLayer` | the static that `updateStatic` redraws over objects (1.6) |
| `_maxDrawOrder` | **40** for Gob2 (`scenery.cpp:60-69`) |
| `_toRedrawLeft/Top/Right/Bottom` | bounding box output of `updateAnim`, and also its clip window input for flag 4 (1.5) |
| `_animLeft/Top/Right/Bottom` (Scenery's, not Mult's) | rectangle of the **first** piece drawn by the last bounds-computing `updateAnim` (1.5) |
| `_pCaptureCounter` | points to playTot's capture counter (flag 1 only, dead) |

There are two different sets of `_animLeft/_animTop` fields: `Scenery::_animLeft/Top/Right/Bottom`
(a piece rectangle) and `Mult::_animLeft/_animTop/_animWidth/_animHeight` (the mult area set by
initMult). Name them differently in your code.

**Reset at every TOT load.** `playTot` calls `Mult::initAll` (`game.cpp:593`), which calls
`Scenery::init` (`mult.cpp:114-120`, `scenery.cpp:94-111`). That sets every anim count to 0,
every static count to -1, every sprite ref to 0 and resId to -1, and `_curStatic` and
`_curStaticLayer` to -1. **Nothing is freed**: the picture sprites stay allocated in their
draw slots. INTRO0 leaves its three mult anims loaded (its `freeMultKeys 1` is a no-op, see 2.6),
so in ScummVM, slots 17-19 stay occupied after the intro. Gob2 scripts only ever use script
sprite 0 (`createSprite 0` / `freeSprite 0`), so which slot a picture ends up in doesn't
matter. **Recommendation:** on TOT load, free all scenery slots and their sprites, then reset.

### 1.2 Resource formats

All little-endian. "Picture" means one sprite image that pieces are cut from.

**Piece table** (the `pictDescId` resource, `Scenery::loadPieces`, `scenery.cpp:1297-1315`):
`N = size / 8` records, each `i16 left, i16 right, i16 top, i16 bottom`. These are
**inclusive** pixel coordinates inside the picture sprite. Some records are all `-1` (unused
filler). For example, INTRO0 res 30002 starts with `(0,31,0,33)` followed by a `-1` record.

**Anim resource** (`Scenery::loadAnim`, `scenery.cpp:477-516`):

```
i16 layersCount
u16 layerOffset[layersCount]       absolute offsets from the start of the resource
layer @ layerOffset[i]:
  +0  i16 unknown0                 values 2..15 in the data. Only read back by getAnimLayerInfo (unused)
  +2  i16 posX                     default position, used when an object's posX is the sentinel 1000
  +4  i16 posY
  +6  i16 animDeltaX               added to the object position at each cycle end for animType 1
  +8  i16 animDeltaY
  +10 s8  transp                   non-zero means colour 0 is transparent (blit mode 3)
  +11 i16 framesCount              may be 0 (empty layer)
  +13 frame pieces, 5 bytes each, framesCount frames back to back:
      u8 pictByte, u8 pieceIndex, s8 destX, s8 destY, s8 notFinal
```

- A frame is a run of pieces. `notFinal == 1` means another piece of the same frame follows.
  Any other value ends the frame. In the data, the last piece of the last frame of a layer is
  usually `-1` and other frame ends are `0`.
- **pictByte** packs three things: bits 0-3 are `pictIndex + 1` (0 means "no picture": the
  piece is skipped, which works as an empty frame), bits 4-5 extend destY and bits 6-7 extend
  destX (1.5 step 4). The extension bits **are used** in the data (for example GOB19 res 30001
  has 426 such pieces), so implement them.
- The loader counts all pieces first (by scanning notFinal), then stores them in one flat
  array per layer (`scenery.cpp:496-515`). Frame f is found at run time by skipping f runs.
- Layers with `framesCount == 0` exist (26 of 60 in GOB00 res 30001). Their `transp` byte holds
  garbage (78, -48 seen). Only test `!= 0`.
- Byte example: INTRO0 res 30001 begins `05 00 0c 00 9b 00 …`. That is 5 layers, and layer 0
  is at offset 12.

**Static resource** (`Scenery::loadStatic`, `scenery.cpp:156-183`). **Gob2 never loads one**,
so this is for completeness:

```
i16 layersCount
u16 layerOffset[layersCount]
layer: i16 backResId (overwritten, see below), i16 planeCount,
       planeCount × { u8 pictIndex (1-based), u8 pieceIndex, u8 drawOrder, i16 destX, i16 destY, s8 transp }   (8 bytes)
```

The resource's `backResId` is replaced by the i-th entry of the back list in the script
descriptor (`scenery.cpp:181-182`).

**Script-side descriptor.** Byte layout is in opcodes.md table 2 and 8.7:
`expr index, [statics: i16 backCount, backs[]], i16 picsCount, i16 resId,
picsCount × {i16 pictDescId, i16 width, i16 height, i16 sprResId}`.

### 1.3 `loadAnim(search)` / `loadStatic(search)` (`scenery.cpp:437-559`, `113-224`)

1. Evaluate `index`. Read picsCount and resId (statics read the back list first).
2. **Search mode** (only from loadMult, `search = 1`):
   - Anim: start with `index = 10`. Scan slots 0..9. If a slot is in use with the same resId,
     skip `8 × that slot's picsCount` script bytes and **return the slot number i (no +100)**.
     Otherwise remember the lowest free slot (`scenery.cpp:455-466`). The evaluated expr is
     ignored. If no slot is free, the index stays 10 and overflows (never happens in Gob2).
   - Static: the same, except the starting index is the evaluated expr, and a free slot is
     taken only if it is lower than that (`scenery.cpp:133-145`).
3. Store picsCount and resId in the slot. **No check that the slot is already in use**, so a
   used slot would leak. Gob2 frees before reloading.
4. Load the resource. **If it is missing, return 0 at this point**, without reading the
   per-picture records. This desyncs the script, and the slot keeps the count from step 3.
5. Parse layers (1.2).
6. For each picture: read pictDescId and load its piece table. Read width, height and sprResId.
   - If some draw slot 0..19 already has `_spriteResId == sprResId`, share it and increment
     its refcount.
   - Otherwise take the **highest slot ≤ 19 whose sprite pointer is null**, scanning down
     with no lower bound. Set refcount 1, record the resId, create a `width × height` sprite
     there (`initSpriteSurf(…, flags 2)`), clear it, and decode resource `sprResId` into it at
     (0,0) with transparency 0 (`DRAW_LOADSPRITE`). Destination is not 21, so nothing is
     invalidated.
   - Record the slot in `_animPictToSprite[7*index + p]`.
7. Return `index + 100` (freshly loaded).

The return value matters only to loadMult: `>= 100` means "loaded by this mult, free it in
freeMultKeys" (2.5).

### 1.4 `freeAnim(index)` / `freeStatic(index)` (`scenery.cpp:561-592`, `226-258`)

`index == -1` means "evaluate an expr from the script" (the opcode path). Return if the slot is
free (anim count 0, static count -1). For each picture: drop its piece table and decrement the
sprite refcount. At 0, `freeSprite(slot)` and set resId to -1. Then drop the layers and mark
the slot free. `_animResId` is not cleared, which is harmless because search checks the count
first.

### 1.5 `updateAnim(layer, frame, anim, flags, drawDeltaX, drawDeltaY, doDraw)` (`scenery.cpp:1032-1245`)

This is the core routine. It composes one frame of one layer, optionally draws it to the
**back surface (21)**, and/or computes its bounding box.

**Guards** (`scenery.cpp:1073-1083`). Return **without touching any output** if anim is
outside 0..9, the slot is free, layer < 0, layer ≥ layersCount, or frame ≥ framesCount (so
every frame of a 0-frame layer). Callers then read the **stale** `_toRedraw*` left over from
the previous call, possibly for another object. Reproduce this by keeping `_toRedraw*` as
persistent engine state.

**Flags:**

| bit | effect |
|---|---|
| 1 | capture: run itself with doDraw = 0 to get bounds, then `capturePush` that rect and increment `*_pCaptureCounter` (`scenery.cpp:1085-1096`). Only reachable through draw 0x12, which Gob2 never uses. **Dead** |
| 2 | clip each piece to the **mult area** (`Mult::_animLeft/_animTop/_animWidth/_animHeight`) |
| 4 | "redraw inside a window". On entry, intersect the caller-preset `_toRedraw*` with the mult area (`scenery.cpp:1105-1111`). Clip pieces to that window (only if bit 2 is clear). **Bounds are not recomputed.** |
| 8 | no effect in Scenery (the video-object path of other games) |
| bit 4 clear | `_toRedrawLeft = -12345` ("empty"), and bounds are recomputed from the drawn pieces |

Values used in Gob2: **0** (measure only: goblin code, loadMultObject, getObjAnimSize),
**8** (measure in `animate`), **10** = 8|2 (draw, clipped to the mult area, measuring),
**12** = 8|4 (redraw clipped to another object's rect).

**Algorithm:**
1. Locate the frame: skip `frame` runs of pieces (`scenery.cpp:1101-1103`).
2. Set up the flag-4 window or the -12345 reset (above). Blit mode = `layer.transp ? 3 : 0`.
3. For each piece of the run (do…while `notFinal == 1`):
4. **Position.** `hx = (pictByte >> 6) & 3` and `hy = (pictByte >> 4) & 3`. destX = s8 destX,
   then **add `hx*128` if destX ≥ 0, else subtract it** (the extension grows the magnitude
   away from zero). The same for destY with hy (`scenery.cpp:1127-1139`).
5. Add the origin: `drawDeltaX == 1000` ? `layer.posX` : `drawDeltaX`, and the same for Y
   (`scenery.cpp:1141-1149`). **1000 is a sentinel meaning "the layer's own position."** Gob2
   objects loaded with pos (1000, 1000) keep it in their VARs and rely on this (3.4).
6. `p = (pictByte & 15) - 1` (unsigned). Skip the piece if `p ≥ picsCount`, the piece table is
   missing, or `pieceIndex ≥ piecesCount[p]`. Fetch `left, right, top, bottom`.
7. **Clip** (source rect and dest move together):
   - bit 2: if destX < areaLeft, then `left += areaLeft - destX` and `destX = areaLeft`. If the
     right edge `destX + (right-left)` ≥ `areaLeft + areaWidth`, shrink `right` so the last
     column is `areaLeft + areaWidth - 1`. The same for top/bottom with the area's top and
     height (`scenery.cpp:1167-1187`).
   - else bit 4: the same, against the window `_toRedrawLeft..Right / Top..Bottom` (inclusive
     edges) (`scenery.cpp:1188-1204`).
   - Skip the piece if `left > right` or `top > bottom`.
8. **Draw** if doDraw: blit `(left, top, right-left+1, bottom-top+1)` from the picture's
   sprite slot to surface 21 at (destX, destY) with mode 3/0 (`scenery.cpp:1209-1222`).
   spriteOperation invalidates the rect. Mode 3 means colour 0 is transparent
   (`draw_v2.cpp:759-763`).
9. **Bounds** if bit 4 is clear: the piece covers `destX..destX+right-left`,
   `destY..destY+bottom-top` (inclusive, after clipping). For the first piece that gets here,
   set `_toRedraw*` **and** Scenery's `_animLeft/Top/Right/Bottom` to that rect. For later
   pieces, take the union into `_toRedraw*` only (`scenery.cpp:1224-1242`).
   So **Scenery `_animTop/_animBottom` describe the first visible piece, not the frame.** Goblin
   code uses `_animBottom - _animTop` as an object height (goblin docs). Keep this behaviour.
10. If no piece survives, `_toRedrawLeft` stays -12345 while Right/Top/Bottom keep stale values.

There is **no flip** in Gob2's scenery. The only `0x80` "mirror" logic (`clipInRect`,
`scenery.cpp:594-625`) is used by the Adibou video-object path. The only commands inside frame
data are notFinal and the pictByte bits.

### 1.6 Statics: `renderStatic`, `updateStatic`, `loadCurLayer`. **All effectively dead in Gob2**

- **`renderStatic(index, layer)`** (`scenery.cpp:260-327`). Return if layer ≥ layersCount
  (index isn't range-checked). If `backResId != -1`, decode that sprite resource into 21 at
  (0,0), transparency 0. Then for order 0..39, draw every plane whose `drawOrder == order` as a
  piece blit (pictIndex is 1-based) to 21 at (destX, destY), mode `transp ? 3 : 0`. No clipping.
- **`updateStatic(orderFrom, index, layer)`** (`scenery.cpp:329-420`). Same plane loop but
  only for orders ≥ orderFrom, no background, and each piece clipped to the `_toRedraw*`
  window. Returns if index ≥ 10 or layer ≥ layersCount.
- **`updateStatic(orderFrom)`** (`scenery.cpp:422-435`), called by `animate` after drawing each
  object (with `order + 1`) so foreground statics are repainted over it:
  - `_curStatic == -1` → nothing.
  - `< 10000`: static `low byte`, layer `low byte of _curStaticLayer`. If the high byte is
    non-zero, also static `high-1` with layer `high byte`.
  - `≥ 10000`: statics 0..(cur-10001), layer 0.
- **`o2_loadCurLayer`** (draw 0x1D, `inter_v2.cpp:504-507`) sets both from two valexprs.
- **`o2_renderStatic`** (draw 0x1C, `inter_v2.cpp:495-502`) calls renderStatic.

**Why it's dead in Gob2:** no TOT uses loadStatic, freeStatic or renderStatic. All 25
loadCurLayer calls are `-1, -1`. Every mult has `staticCount 0`, and the only place outside
loadCurLayer that sets `_curStatic` is a static key with layer ≥ 0 (`mult_v2.cpp:607-625`).
Gob2's static keys are only -1 and -2, and -2 sets `_curStatic = -1`. **Implement
`updateStatic(order)` as a no-op guarded by `_curStatic == -1`.**

### 1.7 Info opcodes

- **`o1_getAnimLayerInfo`** (draw 0x18, `inter_v1.cpp:572-589` → `scenery.cpp:1247-1269`):
  `expr anim`, `expr layer`, 4 varidx. Writes animDeltaX, animDeltaY, unknown0 and framesCount
  as 32-bit values. Asserts on a bad index. **Unused by Gob2.**
- **`o1_getObjAnimSize`** (draw 0x19, `inter_v1.cpp:591-624`): `expr obj`, then varidx left,
  top, right, bottom.
  - If obj is outside `[0, _objCount)`: all four `_toRedraw*` = 0.
  - Else: if the object's `isStatic == 0`, run `updateAnim(layer, frame, animation, flags 0,
    *posX, *posY, doDraw 0)` (unclipped bounds, so posX/posY = 1000 means the layer position).
    **If isStatic != 0, nothing is recomputed** and the stale `_toRedraw*` is returned.
  - Then clamp Left and Top to ≥ 0 (not Right/Bottom). A -12345 Left therefore becomes 0.
  - Write all four as 32-bit VARs.
  - Gob2 uses it 164 times, on objects 0 and 1 (the goblins) and 2. The results go into
    v[4771..4781].

### 1.8 Draw opcode summary

| op | handler | effect | Gob2 |
|---|---|---|---|
| 0x10 loadAnim | `inter_v1.cpp:420` → `loadAnim(0)` | 1.3 | 79 (slots 0, 1, 2, sometimes 3) |
| 0x11 freeAnim | `inter_v1.cpp:424` → `freeAnim(-1)` | 1.4 | 79 |
| 0x12 updateAnim | `inter_v1.cpp:428-444` | `expr dx, dy, anim, layer, frame`, `i16 flags`, doDraw 1 | **0** |
| 0x18 getAnimLayerInfo | `inter_v1.cpp:572` | 1.7 | **0** |
| 0x19 getObjAnimSize | `inter_v1.cpp:591` | 1.7 | 164 |
| 0x1A loadStatic | `inter_v1.cpp:626` → `loadStatic(0)` | 1.3 | **0** |
| 0x1B freeStatic | `inter_v1.cpp:630` → `freeStatic(-1)` | 1.4 | **0** |
| 0x1C renderStatic | `inter_v2.cpp:495` | 1.6 | **0** |
| 0x1D loadCurLayer | `inter_v2.cpp:504` | 1.6 | 25, always `-1, -1` |

---

## 2. Mult (v2)

### 2.1 Objects and where they live

**Engine-side object** `Mult_Object` (`mult.h:91-126`). Fields that matter for Gob2:

| field | meaning |
|---|---|
| `pPosX`, `pPosY` | references to **32-bit VARs** (`VariableReference`). Reads are passed to int16 parameters, so they truncate |
| `pAnimData` | pointer to a **byte record in VAR memory** (below) |
| `tick` | frame-hold counter compared with `maxTick` |
| `lastLeft/Right/Top/Bottom` | rect drawn last time (after mult-area clipping). `lastLeft == -1` means "not on screen" |
| `newLeft/Top/Right/Bottom`, `needRedraw` | per-animate scratch |
| `goblinStates`, `goblinX/Y`, `destX/Y`, `gobDestX/Y`, … | goblin code only |
| `videoSlot`, `animName`, `animVariables` | other games. `videoSlot` stays 0 |

**`Mult_AnimData`** (`mult.h:42-78`) is packed and **35 bytes**. It occupies the first 35
bytes of a 40-byte slot (animDataSize 10 × 4). Object i's record is at byte
`animDataVar + i*40`. In Gob2 `animDataVar` is always `s[2435]`, i.e. **byte 9740**, so
object i's record starts at **byte 9740 + 40·i** (object 0 = `s[2435]`, object 1 = `s[2445]`,
…). Bytes 35..39 of each slot are unused.

The "script" column comes from the field-index constants that every level defines in its init
function (for example GOB00 `0x45CF`: v[782]=1, v[1354]=2, v[466]=3, v[467]=4, v[468]=5,
v[1896]=6, v[469]=11, v[1397]=12, v[1396]=13, v[471]=14, v[464]=15, v[463]=16, v[472]=17,
v[473]=18) and from a scan of all byte accesses (R = read, W = written).

| off | type | field | written by the engine | script |
|---|---|---|---|---|
| 0 | s8 | animation (scenery anim slot 0..9) | keys (2.7/2.10), loadMultObject, newCycleAnim type 2, goblin code | R/W |
| 1 | u8 | layer | the same | R/W |
| 2 | u8 | frame (**8-bit: wraps above 255**) | keys → 0, newCycleAnim ±1/0 | R/W |
| 3 | s8 | animType (2.9) | keys → 1, multSub end → animTypeBak, newCycleAnim | R/W (values 100, 101, 6, 0, 1, 5) |
| 4 | s8 | order (draw priority) | drawAnims (playMult keys only). **advanceObjects doesn't set it** | R/W |
| 5 | s8 | isPaused | keys → 0, newCycleAnim | R/W |
| 6 | s8 | isStatic: 0 = animated, 1 = hidden/idle, 2 = ignored completely | initMult → 1, keys, newCycleAnim type 5 | R/W (the "hide" switch) |
| 7 | s8 | maxTick (hold each frame maxTick+1 ticks) | keys → 0 | loadMultObject only |
| 8 | s8 | maxFrame | — (goblin) | loadMultObject only |
| 9 | u8 | newLayer | — | loadMultObject only |
| 10 | s8 | newAnimation | — | loadMultObject only |
| 11 | u8 | intersected (index of an overlapping object, 200 = none) | every animate | constant defined |
| 12 | u8 | newCycle | newCycleAnim: 0 mid-cycle, 1 on wrap. Goblin code sets it to framesCount | R (`newCycle-1` compared with frame) |
| 13 | s8 | state | goblin | R/W |
| 14 | s8 | nextState | goblin | |
| 15 | s8 | newState | goblin; animType 7 consumes it in animate | |
| 16 | s8 | curLookDir | goblin | R/W (0/4/6) |
| 17 | s8 | isBusy | goblin; newCycleAnim type 11 | R/W |
| 18 | s8 | pathExistence | goblin | R/W |
| 19 | s8 | destX | goblin | R |
| 20 | s8 | destY | goblin | R |
| 21 | s8 | framesLeft | goblin / type 11 | |
| 22 | s8 | stateType | o2_setGoblinState | |
| 23 | s8 | animTypeBak | multSub | |
| 24 | s8 | redrawAnimation | every animate (copy of 0) | |
| 25 | u8 | redrawLayer | every animate | |
| 26 | u8 | redrawFrame | every animate | |
| 27-34 | | destXBak, destYBak, gobDest*, field_1F..22 | goblin / oblique ordering only | |

**Positions:** object i's X is **VAR(posXVar/4 + i)** and its Y is **VAR(posYVar/4 + i)**
(`inter_v2.cpp:340-345`). In Gob2 that is **VAR(3185 + i)** and **VAR(3260 + i)**. Scripts
also keep shadow copies (VAR 3200+, 3275+, 3903+, 3918+, …) and copy them back.

Implement `pAnimData` as a byte offset into the VAR array and read/write the fields as bytes.
Don't cache them: scripts poke them between ticks.

### 2.2 `o2_initMult` (draw 0x14, `inter_v2.cpp:284-396`)

Operands: `i16 left, top, width, height, objCount`, `varidx posX, posY, animData`.
1. Store the mult area `Mult::_animLeft/_animTop/_animWidth/_animHeight` and `_objCount`.
2. If objects exist and the count changed: free the old objects, render list and order array
   (with a warning). If the count is unchanged, **the existing objects are kept as they are**.
3. If there are no objects: allocate `_renderObjs[objCount]`, `_orderArray[objCount]` and
   zeroed `_objects[objCount]`. For each object, set up the posX/posY/animData references
   (2.1). In VAR memory, **only `isStatic` is written (= 1)**; all other anim-data bytes keep
   whatever the VARs held. Engine-side: tick 0, last* = -1, goblinX/Y = 1.
4. If an anim surface exists and the size changed: free sprite 22.
5. If there's no anim surface: create sprite 22 (`animWidth × animHeight`, flags 0).
6. **Snapshot the background:** blit back-surface rect (animLeft, animTop, w, h) into 22 at
   (0,0), transparency 0. Not invalidated.

Surface 22 is the **clean background** that `animate` erases objects with. So the script must
have drawn the background into 21 before initMult. Gob2 levels do exactly that:
`loadSpriteContent 30007,101,0`, then the overlay `30008,101,1`, then initMult. Scripts never
draw into 22 afterwards. Only playMult's static keys refresh it (2.10).

Gob2 always passes left = top = 0, width 320, height **165** (22 TOTs, plus INTRO0 and FINAL)
or **168** (GOB16), with 15 objects (levels) or 5 objects (INTRO0, FINAL).

### 2.3 `o2_loadMultObject` (draw 0x17, `inter_v2.cpp:398-493`)

Operands: valexpr obj, valexpr posX, valexpr posY, then 11 slots for anim-data **bytes 0..10**
(animation … newAnimation). A slot holding the byte 99 keeps the old value; otherwise it's a
valexpr stored as a byte.
1. Write posX and posY into the VARs, then the 11 bytes.
2. animType **100** and `obj < _gobsCount`: goblin placement. posX/posY are **tile coords**,
   and the code converts them to pixels using the map and a measuring `updateAnim` (flags 0,
   no draw). Goblin docs.
3. animType **101** and `obj < _gobsCount`: the `layer` byte is treated as a goblin state,
   mapped through `goblinStates[state][0]` to (animation, layer). If pos is (1000, 1000), it
   is replaced by the layer's posX/posY. Then a measuring updateAnim. Goblin docs.
4. Other animTypes: the only special case is pos (-1234, -4321), which closes an object video
   (other games). **Otherwise nothing more happens: pos 1000/1000 stays 1000/1000 in the VARs**
   and works through the updateAnim sentinel.

Gob2 data (178 calls): frame, maxTick, maxFrame, newLayer and newAnimation are always 0. The
keep-marker 99 is never used. animType is 6 (113×), 100 (52×, goblins 0/1 at tile coords such
as (1, 26)), 0 (12×) or 101 (1×). Pos is (1000, 1000) 123 times. isStatic is 0 (167×) or 1;
isPaused is 0 or 1 (4×). Order is 0 for most objects and 1..26 for the rest.

### 2.4 `o1_freeMult` (draw 0x15, `inter_v1.cpp:541` → `mult.cpp:131-156`)

Free all objects (only the references: **the VARs are untouched**), the render list and the
order array. Drop the anim surface and `freeSprite(22)`. 24 uses: every level, plus INTRO0.

Also note `Mult::initAll` at TOT load (`mult.cpp:114-120`). It sets `_objects = null` **without
freeing** and drops the surface reference. Then `zeroMultData()` (`mult.cpp:163-165`) clears
`_multData` **but not `_multDatas[]`**. The INTRO0 mult data survives in `_multDatas[0]` until
the next `loadMult` overwrites it. That's harmless in Gob2, because `animate` needs objects, and
the next TOT that creates objects (a level) loads its own mult into index 0 first.
**Recommendation:** on TOT load, free the objects, sprite 22 and all eight mult datas.

### 2.5 `loadMult` (draw 0x00, `mult_v2.cpp:63-252`)

The byte layout and script interleaving are in opcodes.md 8.6. What each part **means**:

1. `index` = optional byte (bit 15 of resId). **Always 0 in Gob2.** Allocate a fresh
   `Mult_Data`, store it in `_multDatas[index]` and make it current. Any data already in that
   slot is overwritten without being freed. Initialise `animObjs[0][i] = i` (track i drives
   object i) and `animKeysFrames[i] = -1` (no sequence running).
2. **Counts byte:** `staticCount = (s8+1)`, `animCount = (s8+1)`. Bit 7 of staticCount is the
   "new format" flag (ScummVM calls it hasImds): **set in all 25 Gob2 mults** (first byte
   0x7F). It means text and sound keys have no 24-byte padding, and that the script carries an
   extra sprite list (step 9).
3. **Scenery list:** for each static and then each anim, `loadStatic(1)` / `loadAnim(1)` from
   the **script** (search mode). The resource skips 14 bytes per entry (all zero in the data).
   Results go to `staticIndices[i]` / `animIndices[i]`, with `staticLoaded` / `animLoaded` =
   "this mult loaded it" (return ≥ 100, then minus 100).
   - Gob2: statics 0. Anims 1-6. In levels **every anim is already loaded** by the level's
     loadAnim, so search returns the existing slot and `animLoaded` is false. Order and
     duplicates differ from the slot order: GOB00 gives animIndices = {2, 0, 1} and GOB16 gives
     {0, 1, 3, 1, 1, 1}. INTRO0 (3 anims) and FINAL (1 anim) load theirs fresh.
4. **frameRate** (i16). Applied only by `playMultInit` when no anim surface exists. Gob2 always
   runs initMult first, so **this value is unused**. The script's `setFrameRate` governs.
   Values are 10 or 16.
5. **Static keys** `{i16 frame, i16 layer}`. At `frame`, playMult changes the background (2.10):
   - layer ≥ 0: render static layer L, where L counts across the mult's statics.
   - layer -1: no-op.
   - layer ≤ -2: decode sprite resource `spriteList[-layer-2]` (step 9) into 21.
6. **Anim keys:** 4 tracks, each `i16 count` + `{u16 frame, i16 layer, i16 posX, i16 posY,
   i16 order}`. At `frame`, the object driving the track gets its position, layer and order
   set and starts playing (animType 1, frame 0). layer -1 hides it. **Key layer numbers are
   global across the mult's anim list:** subtract `layersCount(animIndices[0])`,
   `layersCount(animIndices[1])`, … until the number fits in one anim.
7. **Fade palettes** (5 × 16 RGB = 240 bytes), **pal-fade keys** `{frame, fade, palIndex,
   s8 flag}` and **pal keys** `{frame, cmd, rates[4], 2 × unknown, subst[16][4]}` (80 bytes).
   These drive `doFadeAnim` / `prepPalAnim` / `doPalAnim` (`mult.cpp:288-438`), which only run
   inside playMult. Gob2: pal-fade keys 0 everywhere. Pal keys appear only in level mults
   (GOB04/12/13/15, 1 each), which never playMult. **Dead in Gob2.**
8. **Text keys** `{i16 frame, i16 cmd}`. Gob2: **all 962 have cmd 1.** In playMult, cmd 1 at
   the current frame makes playMult **return to the script** with VAR(57) = that frame (2.10).
   In multSub, any text key frame acts as the **end of a sequence** (2.7). cmd 0 and cmd 3
   only keep playMult running.
9. **Sound keys** `{frame, cmd, freq, fadeLength, repCount, 2 skipped}`. cmd 1/4 loads a sample
   from the script (`loadSound(1)`, de-duplicated by resId). The slot is kept in `sndSlot[]`
   and `soundIndex = slot & 0x7FFF`. cmd -1 = stop. Played by `doSoundAnim` (2.10, 2.7). Sound
   docs cover the sample side. Gob2 cmds: 1 and -1. INTRO0 has none, FINAL has 64, levels have
   hundreds.
10. **New-format tail** (`loadImds`, `mult_v2.cpp:254-307`): script `i16 n` + n × i16 → the
    `execPtr` sprite list used by static-key layers ≤ -2. With TOT 1.2 the function **returns
    here**. The remaining 12 resource bytes and all IMD keys are never read, so IMD playback is
    dead. INTRO0's list is `{30010}`, a 320×200 sprite.
11. **TOT 1.3 (Goblins 3)** goes on: resource `i16 n` + n × 20 bytes (skipped), script `i16
    files` + files × 14-byte IMD names (NUL-padded); when files > 0, 2 resource bytes, then
    4 tracks of IMD keys, each `i16 count` + `{i16 frame, i16 imdFile, i16 field4, i16 field6,
    u16 flags, i16 palFrame, i16 lastFrame, s8 palStart, s8 palEnd}` (16 bytes). The engine
    reads them into `MultData::imdFiles` / `imdKeys` (`engine/mult.cpp`). Goblins 3's
    mults have no statics. Playback: 2.13.

### 2.6 `o2_freeMultKeys` (draw 0x02, `inter_v2.cpp:254-263` → `mult_v2.cpp:309-367`)

`u16 index`. If `_multDatas[index]` is null, nothing happens. Otherwise: make it current, free
the statics and anims it loaded itself (`*Loaded` flags), free its key arrays, free the sound
slots it loaded (those without bit 15), and, if `_animDataAllocated`, free the objects too
(freeMult). Then null every `_multDatas[]` entry that points at it, and null `_multData`.
Gob2: `freeMultKeys 0` in every level (frees no anims: the level's freeAnim does that), and
`freeMultKeys 1` in INTRO0, **which is a no-op** (index 1 was never loaded).

### 2.7 `o2_multSub` and the sequence engine (draw 0x13; `mult_v2.cpp:391-520`, `1265-1415`)

This is how levels play their keyframed sequences: goblin and object animations synchronised
with sound. It never calls playMult. `animate` runs it one frame per tick.

**`multSub(w)`**, where `w = valexpr` and 4 more valexprs follow:
- `multIndex = w >> 12` (always 0). `notifyNewAnim()` resets the frame-pacer start
  (`util.cpp:470-472`). If that mult isn't loaded: read and discard the 4 valexprs, return.
- **Slot** (0..3) = 3 if `w & 0x200`, 2 if `& 0x100`, 1 if `& 0x80`, else 0.
- If `w & 0x400`: reverse playback, and track 0's object becomes the sentinel 1024 ("none").
  Gob2 never sets 0x400, so don't implement reverse.
- Otherwise `animObjs[slot][0] = w & 0x7F` (the object driven by **track 0**).
  `animObjs[slot][1..3]` = the next 3 valexprs (objects for tracks 1..3; -1 = none).
- `start` = the 4th valexpr. `animKeysFrames[slot] = start` and **`VAR(18 + slot) = start`**.
- **start == -1 (abort):** for each object in the slot (not -1 or 1024), set
  `animType = animTypeBak`. Return.
- **stop frame** = the smallest text-key frame strictly greater than start, or 32000 if there
  is none (`mult_v2.cpp:462-469`).
- Back up `animTypeBak = animType` for each of the slot's objects.
- For each track t: `animKeysIndices[slot][t]` = index of the first key in track t with
  `frame ≥ start` (or 0).
- IMD index setup: **dead** (no IMD keys).

**`advanceAllObjects()`** runs first in every `animate`. For each loaded `_multDatas[i]`, it
makes that data current and calls `advanceObjects(slot)` for slots 0..3, then restores
`_multData`.

**`advanceObjects(slot)`:**
1. `f = animKeysFrames[slot]`. If -1, return (idle slot).
2. For each track t whose object `o = animObjs[slot][t]` is valid: scan track t's keys from
   `animKeysIndices[slot][t]`. Stop at the first key with `frame > f`; skip keys with
   `frame < f`. On a key with `frame == f`:
   - layer > -1: save j as the new scan start. Set pos = (key.posX, key.posY), frame 0,
     **animType 1**, isStatic 0, isPaused 0, maxTick 0, tick 0. Resolve the global layer
     against `animIndices[]` (2.5 step 6) to get animation and layer. **Break.** (order is not
     touched.)
   - layer == -1: `isStatic = 1` (hide) and keep scanning.
3. IMD keys: dead.
4. `doSoundAnim(f)`: play or stop every sound key of this mult whose frame equals f
   (`mult.cpp:440-461`). cmd 1/4: `blasterStop(0)`, then `blasterPlay(sample[soundIndex],
   repCount, freq, fadeLength)` if the sample exists. cmd -1: if something is playing,
   `blasterStop(fadeLength)`. **Sound keys aren't per track.** Two slots on the same frame
   number would both trigger them.
5. **`VAR(22) = f`.**
6. If `f == stop`: set f = -1 and restore every slot object's `animType = animTypeBak`.
   Else f++.
7. `animKeysFrames[slot] = f` and **`VAR(18 + slot) = f`**.

So the keys at `start` take effect on the first animate after multSub, and the keys at `stop`
(the text-key frame) are still applied before the sequence ends. While a sequence runs, its
objects are animType 1. Afterwards they get their previous type back (100 for goblins, 6 for
props), keeping the last key's position, animation and layer.

**Script side:** the typical pattern is `multSub …; repeat { call tick; animate;
animatePalette } until v[18] == -1` (or v[19] for slot 1). Scripts test VAR(18..21) == -1
constantly (about 1600 references), and a save-game restore re-issues multSub from saved values
(`multSub av[17](5+4i), …, av[17](i)`). VAR(22) is never read by Gob2 scripts.

Gob2 flag words (1120 calls): `0` (503×), `128+1` (456×), `1` (46×), others such as `256+n` and
`512+n`. Track 1..3 objects are mostly -1, sometimes 2..14. Start frames go up to 5000.

### 2.8 `animate()` (draw 0x16 `o1_animate`, `inter_v1.cpp:545` → `mult_v2.cpp:872-1204`)

This runs once per game tick in levels (212 call sites), and once per frame inside playMult.
Return at once if there are no objects (or no order array). Steps:

**A. Sequences.** `advanceAllObjects()` (2.7).

**B. Render list** (`mult_v2.cpp:892-944`). For i in 0..objCount-1:
- The oblique-coordinates order correction (`898-934`) needs `_map->_usesObliqueCoordinates`.
  That is only set by `loadMapObjects` command 65534 (`map_v2.cpp:95`), which Gob2 never
  issues (and `Goblin_v2::freeObjects` clears it, `goblin_v2.cpp:51`). **Dead.**
- `intersected = 200`.
- If `isStatic != 2` and (`isStatic == 0` or `lastLeft != -1`): append to `_renderObjs`.
  Remember the object index.

**C. Dirty rects** (`946-1026`). For each listed object: needRedraw = 0, new = (1000, 1000, 0, 0).
- isStatic 0, not paused, and `maxTick == tick` (it will advance this tick): needRedraw = 1.
  Measure with `updateAnim(layer, frame, animation, flags 8, posX, posY, no draw)`. new = that
  box if `lastLeft == -1`, else the union with last. (The "unchanged, skip redraw" test at
  `981-990` needs version minor > 2: **dead**.)
- isStatic 0 but waiting (paused or tick < maxTick): if never drawn (`lastLeft == -1`),
  needRedraw = 1 and new = the measured box. Else new = last, no redraw.
- isStatic ≠ 0 but still on screen (`lastLeft != -1`): needRedraw = 1 and new = last. This
  erases it.
- Copy layer/frame/animation into `redraw*` (bytes 24-26).
- If needRedraw or isStatic == 0: fold `order` into minOrder/maxOrder (initial 100 / 0).

**D. Erase** (`1028-1055`). For each object with needRedraw and `lastLeft != -1`:
set `lastLeft = -1`. Clamp the new rect's left/top to the mult area's left/top (right and
bottom are not clamped; the 22-surface size clips them). If the rect is non-empty, blit from
**surface 22** at `(x - animLeft, y - animTop)` to **surface 21** at (x, y), transparency 0.
This invalidates the rect.

**E. Draw order** (`1057-1067`). For order = minOrder..maxOrder, in render-list order, append
the objects with that order that need a redraw or are animated (isStatic == 0).

**F. Goblin tie-break** (`1069-1089`), only if `_gobsCount ≥ 0`. For each listed object that
has goblinStates, swap it with any later object of **equal order** whose newBottom is smaller,
or equal while the first object's isBusy == 1. Lower feet are drawn first.

**G. Draw** (`1091-1148`), in draw order:
- Object **not** needing a redraw but animated (isStatic 0): for every object B in the list
  that needs a redraw and whose new rect overlaps this one's new rect, set
  `_toRedraw* = B.new` and redraw this object with flags 12 (clipped to B's rect ∩ the mult
  area, bounds untouched). Then `updateStatic(order+1)`. This repairs the parts of still
  objects that step D erased.
- Object needing a redraw:
  - isStatic ≠ 0 (being erased): `_toRedraw* = new`. Nothing is drawn.
  - else draw with **flags 10** (clipped to the mult area, bounds measured). If something was
    drawn, `last* = _toRedraw*` (the **clipped** box); otherwise `lastLeft = -1`.
  - Then `updateStatic(order+1)` (a no-op in Gob2, 1.6).

**H. Advance** (`1150-1177`). For each listed object with isStatic 0:
- animType 7 with `newState != -1`: layer = newState, frame 0, newState = -1, unpause.
  (Goblin state switching.)
- Paused → skip.
- If `maxTick == tick`: tick = 0, then:
  - animType < 100, or `_gobsCount < 0`: **`newCycleAnim`** (2.9).
  - animType 100: `Goblin::moveAdvance(obj, …)` (goblin docs).
  - animType 101: `Goblin::animate(obj)` (goblin docs).
- else tick++.

**I. Intersections** (`1179-1202`). For each ordered pair of different, animated, on-screen
objects whose `last*` rects overlap: `B.intersected = index of A`. With several overlaps, the
last one wins.

`animate` itself **never presents**. Everything lands in surface 21 plus the invalidation list.
Things to note:
- Frames are drawn **before** they advance: the frame shown this tick is the one the object
  held when animate began (after sequence keys were applied in step A).
- Goblins 0 and 1 are just objects with animType 100. Their frame/position stepping is goblin
  code, but erase, ordering and drawing are the same as for every other object.

### 2.9 `newCycleAnim(obj)` (`mult_v2.cpp:695-870`)

1. If `animation ≥ 0` and layer ≥ layersCount: return (nothing changes). (Negative animation
   = video object, other games.)
2. animType 4: frame = 0, isPaused = 1, return. ("Stopped" state.)
3. animType 12 becomes 11. animType 11: busy countdown. If isBusy and `animTypeBak == 1`:
   decrement framesLeft, and at 0 call `Goblin::setGoblinState(obj, isBusy)` and set
   isBusy = 0, animTypeBak = 0, animType = 10. Return. Types 10/11/12 are set only by v7 code
   (`goblin_v7.cpp:75`, `inter_v7.cpp:418-446`), so this is **dead in Gob2**.
4. Unless animType 8: frame++ (the u8 wraps).
5. If `frame < framesCount`: newCycle = 0, return.
6. End of cycle, by animType:

| animType | at cycle end | Gob2 use |
|---|---|---|
| 0 | frame = 0 (loop) | props (12 loadMultObject) |
| 1 | frame = 0, pos += (animDeltaX, animDeltaY). A pos still at the 1000 sentinel stops being one | set by every sequence key |
| 2 | frame = 0, animation = newAnimation, layer = newLayer | not seen |
| 3 | animType = 4, frame = 0 (plays once and then rests on frame 0) | not seen |
| 5 | isStatic = 1, frame = 0 (plays once and disappears) | scripts set it (1×) |
| 6, 7 | frame-- (back to the last frame), isPaused = 1 (plays once and holds) | **the common prop type (113×)** |
| 8 | frame isn't advanced, so this never triggers (frozen) | not seen |
| 10 | oblique-map goblin step. Without oblique coordinates it only warns, then updates goblin fields and calls `Goblin::initiateMove` | dead (v7 only) |
| other | nothing | |

7. newCycle = 1.

### 2.10 `o2_playMult` and the playMult loop (draw 0x01; `inter_v2.cpp:245-252`, `mult.cpp:167-265`)

Operand `i16 w`: the current mult = `_multDatas[w >> 1]` and checkEscape = `w & 1`. Then
`playMult(start = VAR(57), end = -1 → 32767, checkEscape, handleMouse 0)`. Gob2 uses only
`playMult 1`, in INTRO0 and FINAL.

**Init.** If `_multData` is null, return. `_frame = start`. If start == -1, call
**`playMultInit`** (`mult_v2.cpp:522-594`): reset the palette-animation state and remember the
current palette pointer. If **no anim surface exists** (no initMult was done), it creates its
own 4 objects in private storage, a full-screen surface 22 snapshotted from 21, applies the
mult frameRate, and sets `_animDataAllocated`. **In Gob2 initMult always ran first**, so only
`_animDataAllocated = false` happens. Then `_frame = 0`.

**Each frame** (do…while):
1. `stop = true`.
2. If **VAR(58) == 0** (always true in Gob2):
   - **`drawStatics`** (`mult_v2.cpp:596-642`). If the last static key's frame > `_frame`, set
     stop = false. For each static key at `_frame` with layer ≠ -1:
     - layer ≥ 0: find the static containing global layer L (counting across staticIndices),
       set `_curStatic` / `_curStaticLayer`, and renderStatic.
     - layer ≤ -2: decode `spriteList[-layer-2]` into 21 at (0,0), transparency 0, and set
       `_curStatic = -1`.
     - Then **copy all of 21 into 22** (offset (0,0), clipped to 22's size). This is the only
       other writer of the background snapshot.
   - **`drawAnims`** (`mult_v2.cpp:644-693`). If any track's last key frame > `_frame`, set
     stop = false. For each track t and each key with `frame == _frame`: the object is
     `animObjs[0][t]` (identity unless a multSub on slot 0 remapped it).
     - layer ≠ -1: pos = key pos, frame 0, **order = key.order**, animType 1, isPaused 0,
       isStatic 0, maxTick 0, tick 0. Layer resolved globally (2.5 step 6).
     - layer -1: isStatic = 1.
3. **`animate()`** (2.8).
4. handleMouse ? `animateCursor(-1)` : **`blitInvalidated()`** (Gob2: blitInvalidated).
5. If VAR(58) == 0, **`drawText`** (`mult.cpp:267-286`). For each text key at `_frame`:
   cmd 0 → stop = false; cmd 1 → **stopNoClear = true and `frameStart = 0`**; cmd 3 →
   stop = false (its script hook is unimplemented in ScummVM).
6. Palette keys (`prepPalAnim`, `doPalAnim`) and fade keys (`doFadeAnim`): no keys in Gob2.
7. **`doSoundAnim(_frame)`** (2.7 step 4).
8. If `_frame ≥ end`: stopNoClear = true (never with end 32767).
9. If a blaster sample is playing: stop = false.
10. `processInput`. If checkEscape and the key is Escape: stop = true. Then
    **`waitEndFrame()`** (paced by the script's `setFrameRate`, graphics.md 3.6).
11. `_frame++`. Loop while `!stop && !stopNoClear`.

**Exit:**
- **stopNoClear** (a text key cmd 1 was hit): `VAR(57) = _frame - frameStart - 1`. That is the
  text-key frame, because frameStart is 0 and `_frame` was already incremented.
- **stop** (Escape, or nothing left to play): if `_animDataAllocated`, free the private
  objects and surface (not in Gob2). Stop the blaster with fade 10. `VAR(57) = -1`.

**Script protocol (INTRO0, FINAL):** set `v[57] = -1` and `v[58] = 0`, then
`repeat { playMult 1; switch v[57] { case F: …; v[57]++ } } until v[57] == -1`. Each return at
text key F hands control to the script (print a caption, pop a capture, fade). `v[57]++` makes
the next playMult resume at F+1. Because `_frame != -1`, playMultInit doesn't run again. The
script ends the loop by setting -1 itself (INTRO0 at 790, FINAL at 259) or by Escape.
- INTRO0 text keys: 1, 11, 51, 65, 105, 130, 168, 193, 240, 337, 380, 397, 437, 530, 574,
  593, 636, 790. Its `case 50` and `case 85` can never be hit (dead script code).
- Static keys: INTRO0 has (0, -2) → sprite 30010 into 21 and 22 at frame 0, plus (794, -1).
  FINAL has only -1 keys.
- Anim keys: INTRO0 has 104 keys over tracks 0-3 (frames 0..794, orders 0/2/8/9). FINAL has
  16 keys.
- "CD sync" is purely in the script: INTRO0 starts CD track `g2intro`, polls `getCDTrackPos`
  until > 840, and then runs the mult paced by `setFrameRate 10`. Case 1 polls again until
  > 1190 before fading in. playMult itself knows nothing about the CD.

### 2.11 The `checkKeys` VAR(58) hook (`game.cpp:869-878`; graphics.md 6.8)

If a mult is current and VAR(58) ≠ 0, every `checkKeys` does the following:
- Cycle `frameStart` through 0..VAR(58)-1: increment it, wrapping to 0 when it reaches
  VAR(58)-1.
- Play exactly one frame with `playMult(frameStart + VAR(57), the same value, escape 1,
  handleMouse)`.

The result is a looping background mult driven by input polling. **Gob2 only ever writes
VAR(58) = 0** (INTRO0, FINAL), so this is dead. Keep the guard.

### 2.12 Rendering pipeline per tick

```
            initMult / static key                      animate()
 back(21) ──── snapshot ────▶ anim surface(22)   D: erase  22 ──▶ 21 (old ∪ new rect, invalidates)
                                                  G: draw pieces  pictures(≤19) ──▶ 21 (invalidates)
 21 ── blitInvalidated ──▶ front(20) ── retrace/waitEndFrame ──▶ screen
```

- **Objects are drawn only into the back surface (21)**, always through `spriteOperation`
  BLITSURF, so invalidation is automatic (`draw.cpp:315-324`).
- **Background restore always reads surface 22**, the snapshot taken at initMult (or refreshed
  by a playMult static key). Anything the script later draws into 21 inside the mult area is
  erased wherever an object moves, unless it also goes into 22, which Gob2 never does. Scripts
  draw text and panels below y = 165, outside the area. Restores can't reach there because
  surface 22 is only 165 (168) lines tall.
- **Reaching the screen:**
  - In playMult: `blitInvalidated` right after animate, then `waitEndFrame` (retraces).
  - In levels: `animate` runs inside the hotspot block's duration-tick leave call (the
    first hotspot of the main block, `duration 8`, calls the tick function, which ends with
    `animate`; graphics.md 6.4-6.5). `Hotspots::check` then blits and retraces when anything is
    invalidated (graphics.md 6.6). In the scripted sequence loops, `animatePalette` presents and
    paces (`blitInvalidated → waitEndFrame`).

---

### 2.13 IMD keys (TOT 1.3, Goblins 3; `mult_v2.cpp:1206-1394`)

`multSub` also finds, per track, the first IMD key at or after the start frame
(`imdKeysIndices`). Each `advanceObjects` frame then, for every track whose object is not
-1 (whether or not an object is behind it), after the track's anim keys:

1. an IMD key at exactly this frame with a file (`imdFile` < -1: file `-imdFile - 2` of the
   mult's list) makes it the track's video and stops the others (`imdIndices`); a key
   with `imdFile` -1 stops the track's video;
2. a track with a video plays the frame `frame - key.frame` of the key's loop, through
   `Mult_v2::playImd`: flags `key.flags >> 8` (0x20 means the front surface), palette
   entries `palStart..palEnd`, the loop from `palFrame` (-1: 0) to `lastFrame` (-1: the
   last frame), the frame taken modulo the loop's length, palette command 8, position
   VAR 55/56 when render flag 0x100 is set (else the IMD's own). It plays in the primary
   video slot, which stays open from one mult frame to the next (`docs/imd.md` 4); the
   cursor is hidden meanwhile (CAIL1/2 keep it). The original waits for the video
   frame's time there; the engine doesn't, since the room's own `animatePalette` then
   paces the frames instead (it waits only for what is left of its frame), and Goblins 3's
   mult videos are silent and run at the script's frame rate.
3. A key with a file index below 0 (`imdFile` -1 kept as the track's video) ends
   `advanceObjects` at once, before the sound keys and the frame step, as in the original.

When the sequence reaches its stop frame every track's video is dropped. Goblins 3 runs
no sequence backwards, so the reverse-direction key choice is not implemented.

Mults with IMD files: EMAP1002 (TROUA-C, Blount falling into the hole), EMAP1006 (OYA,
PAPILLON), EMAP1010 (CAIL1/2, CHAUD, PREND), INTER0 (INTER21-23, INTERV: Blount's
barrel flight; INTER22.IMD is missing from the data), INTER3 (the comic panels BDA..BDFIN,
CHUTE) and FINAL (ANGE).

**Version 3 redraw skip** (`mult_v2.cpp:981`): in `animate`, an advancing object whose
new rectangle equals the last one and whose animation, layer and frame equal the ones it
was drawn with is not redrawn.

## 3. What Gob2 actually uses

From gobdis over all 32 TOTs (EDIT, FINAL, GOB00-22, INTER, INTRO, INTRO0, JOKER, MENU,
MENUSPE, REGLAGE). There were 0 disassembly errors.

### 3.1 Opcode counts

| opcode | count | where | parameters |
|---|---|---|---|
| loadMult (0x00) | 25 | GOB00-22, INTRO0, FINAL | index 0 always. New-format flag set, statics 0, anims 1-6, frameRate 10/16 |
| playMult (0x01) | 2 | INTRO0, FINAL | `1` (mult 0, escape allowed) |
| freeMultKeys (0x02) | 24 | levels `0`, INTRO0 `1` (no-op) | |
| multSub (0x13) | 1120 | GOB00-22 | 2.7 |
| initMult (0x14) | 25 | same TOTs as loadMult | `0,0,320,165,15` ×22, `0,0,320,168,15` (GOB16), `0,0,320,165,5` (INTRO0, FINAL). Vars always `v[3185], v[3260], s[2435]` |
| freeMult (0x15) | 24 | levels + INTRO0 | |
| animate (0x16) | 212 | GOB00-22 | |
| loadMultObject (0x17) | 178 | GOB00-22 | 2.3 |
| loadAnim / freeAnim (0x10/0x11) | 79 / 79 | GOB00-22 | slots 0, 1, 2 always, slot 3 in 10 levels. Slot 0 is TOT resource 0 (76 layers, the goblin anims) |
| getObjAnimSize (0x19) | 164 | GOB00-22 | objects 0, 1, 2 |
| loadCurLayer (0x1D) | 25 | | `-1, -1` |
| updateAnim, getAnimLayerInfo, loadStatic, freeStatic, renderStatic | **0** | | |

### 3.2 Level life cycle (GOB00 as the template)

1. **Init function (`0x2573`):**
   - loadAnim 0 (res 0), 1 (30001), 2 (30004).
   - Background `loadSpriteContent 30007,101,0` + overlay `30008,101,1`.
   - `initMult 0,0,320,165,15,…`, `loadCurLayer -1,-1`.
   - `loadMapObjects` (map/goblins).
   - `loadMult 30030`: anims found by search, sound samples loaded.
2. **Objects:** `loadMultObject` 0 and 1 (goblins, animType 100, tile coords). Props 2..14
   with pos (1000, 1000) and animType 6 or 0.
3. **Main loop:** hotspot block with a duration tick. The tick function updates
   getObjAnimSize, writeGoblinPos, game logic, `handleGoblins` (goblin func 100), and ends with
   `animate`.
4. **Scripted events:** `multSub …` then `repeat { call tick; animate; animatePalette } until
   v[18]==-1`, or wait on `v[1695]==1 && v[18]==-1` flags inside the main loop.
5. **Exit (`0x2B20`):** `freeMultKeys 0`, `freeGoblins`, `freeAnim 0..2(3)`, `freeMult`.

### 3.3 Mult resource facts (all 25 parsed with the procedure in opcodes.md 8.6)

- All have the new-format flag and `staticCount 0`. Anims: 1 (FINAL), 2-6.
- Static keys: only layers -1 and -2 (`-2` in INTRO0 at frame 0 and in GOB00/08/15/16/17/18;
  in levels these are never executed because levels never playMult).
- Pal-fade keys: none. Pal keys: one each in GOB04/12/13/15 (never executed).
- Text keys: 962, all cmd 1.
- Sound key cmds: 1 and -1 only. Some freq values are negative (for example -6 in GOB16):
  sound docs.

### 3.4 Dead paths you can skip

- Static scenery entirely (1.6). Keep `updateStatic` as a guarded no-op and `loadCurLayer` as
  a setter.
- updateAnim flag 1 (capture), draw 0x12, draw 0x18.
- Mult index ≠ 0 and the reverse multSub (0x400 / 1024 sentinel).
- IMD keys, `loadImds` beyond the sprite list, `playImd`, object videos
  (`updateAnimObjectVideo`, `openObjVideo`, the -1234/-4321 case).
- `playMultInit`'s private-object path (`_animDataAllocated`) and the mult frameRate.
- Palette keys, fade keys, text cmd 0/3, the checkKeys VAR(58) hook.
- The oblique-coordinate ordering in animate, animType 10 handling, and the version > 2
  redraw skip.

### 3.5 Implementation checklist

1. Scenery slots + piece tables + picture sprites (1.2-1.4) with the shared-sprite refcount.
2. `updateAnim` exactly as in 1.5, with **persistent `_toRedraw*` and Scenery `_anim*`**, the
   1000 sentinel, the pictByte extension bits, and flags 0/8/10/12.
3. VAR-backed objects (2.1, 2.2), loadMultObject (2.3; the goblin branches call into goblin
   code), freeMult.
4. loadMult semantic tables (2.5), freeMultKeys.
5. multSub + advanceObjects (2.7), with VAR(18+slot) / VAR(22) writes and animTypeBak
   restore.
6. animate A-I (2.8) + newCycleAnim (2.9).
7. playMult loop (2.10) with drawStatics (layer ≤ -2 path), drawAnims, drawText cmd 1, and
   doSoundAnim.
8. TOT-load reset: free scenery, objects, surface 22 and all mult datas (1.1, 2.4).

---

## 4. Open questions and uncertainties

1. **Stale `_toRedraw*` reads.** These happen in getObjAnimSize for static objects, in
   animate's measuring for 0-frame layers or out-of-range frames, and when every piece is
   clipped away. ScummVM returns values left over from earlier calls. This doc assumes the DOS
   original did the same (shared globals). If something visibly misbehaves (an object's dirty
   rect jumping to another object's box), check this first.
2. **animType 10/11/12** in newCycleAnim: no Gob2 script or loadMultObject sets them, and
   the engine sets them only in v7 code (`goblin_v7.cpp`, `inter_v7.cpp`). They are treated as
   dead here, but the goblin docs should confirm that nothing in the v2 goblin path produces
   them.
3. **Field meanings in bytes 27-34** of the anim data are goblin/oblique-only guesses in
   ScummVM. Gob2 scripts never touch them.
4. **`unknown0`** in anim layers (values 2..15): its meaning is unknown, and nothing in the
   Gob2 runtime path reads it.
5. The **12 trailing bytes** of every new-format mult resource (after the sound keys) aren't
   read by v1.2. They look like the (empty) IMD sections of later versions.
6. **Scenery sprite slots leaking across TOTs** (1.1) is ScummVM behaviour. Freeing on TOT load
   is assumed safe because Gob2 scripts address only sprite 0, 20-23 and 30-49 directly.

---

## 5. Version 1 (Gobliiins): statics and Mult_v1

Gobliiins (TOT version 1.1) draws its rooms and its three mults (the intro and the Coktel
logo in INTRO.TOT, the ending in FINAL.TOT) with **statics** and plays mults with
`Mult_v1` (`mult_v1.cpp`, `mult.cpp:167`). The implementation is in
`src/engine/scenery.cpp` (statics) and `src/engine/mult_v1.cpp` (loader, player, renderer).

### 5.1 Statics (`scenery.cpp:113-436`)

- **Descriptor in the script** (loadStatic, draw 0x1A, and every static of a mult):
  `expr index, i16 backCount, i16 back[backCount], i16 picsCount, i16 resId`, then per
  picture `i16 pieceTable, i16 width, i16 height, i16 spriteRes` (the same picture records
  as loadAnim). With search, a static already holding `resId` is reused (its picture records
  skipped) and the index drops to the lowest free slot below it.
- **Resource:** `i16 layersCount`, `u16 offset[layersCount]`, and at each offset
  `i16 back` (ignored: the script's `back[i]` replaces it), `i16 planeCount`, then 8-byte
  planes `u8 pictIndex (1-based), u8 pieceIndex, u8 drawOrder, i16 destX, i16 destY,
  i8 transp`.
- Picture sprites are shared with the animations by sprite resource id (sprites 19, 18, ...).
- **renderStatic(index, layer)** (draw 0x1C): draws the back picture (if not -1) at 0,0 into
  the back surface, then the planes for order 0 .. maxDrawOrder - 1 (100 in Gobliiins, 40 in
  Gobliins 2 and Goblins 3), transparent when `transp` is set.
- **updateStatic(orderFrom)**: redraws the planes of order >= orderFrom of the current static
  (`curStatic`/`curStaticLayer`, set by loadCurLayer draw 0x1D or by the mult's static keys)
  clipped to the `toRedraw*` window. `curStatic` >= 10000 means layer 0 of statics
  0 .. curStatic - 10001; a high byte names a second static (minus 1) and layer.
- freeStatic (draw 0x1B) takes an `expr` index.

### 5.2 Loading a mult (`Mult_v1::loadMult`)

The script gives the resource id as a plain `i16`. The resource: `i8 staticCount - 1,
i8 animCount - 1`, the statics (each followed by 14 skipped resource bytes) and animations
(likewise), loaded with search from the script's descriptors, then `i16 frameRate`,
static keys (`i16 count`, 4 bytes each: frame, layer), four anim key tracks (10 bytes:
frame, layer, posX, posY, order), five 16-colour fade palettes (240 bytes), palette fade keys
(7 bytes: frame, fade, palIndex, i8 flag), palette keys (80 bytes: frame, cmd, rates[4],
4 unknown bytes, subst[16][4]), text keys (28 bytes: frame, cmd, 24 unknown) and sound keys
(36 bytes: frame, cmd, freq, fadeLength, repCount, 26 unknown). Sound keys with cmd 1/4
read an `u16` resource id from the script (0xFFFF: plus a 9-byte name) unless an earlier
key named the same id; each new sound goes into slot 19, 18, ... cmd 3 skips 6 script
bytes, cmd 5 skips `freq * 2`.

Scenery_v1::loadAnim first lets a playing CD track run to its end and stops it. The engine
also stops the CD at every TOT load in Gobliiins (`game.cpp:588`).

### 5.3 playMult (draw 0x01)

The whole operand is the escape flag. From `VAR(57)`; -1 means from frame 0 after
playMultInit, which (when no anim surface exists, i.e. no initMult ran) sets the frame rate
from the mult, makes four objects whose positions and anim data live outside the script
variables, and copies the back surface into a full-screen anim surface. Per frame, while
`VAR(58) == 0`:
1. **Static keys** of this frame: the layer counts across the mult's statics; render it,
   make it the current static, and copy the back surface into the anim surface. A key later
   than the frame keeps the mult running.
2. **Anim keys**: track t drives object t; a key sets its position, order, layer (counted
   across the mult's animations), animType 1 and frame 0, or (layer -1) makes it static.
3. `animate()` (5.4), then present.
4. **Text keys**: cmd 1 returns to the script (`VAR(57)` = this frame), cmd 0 and 3 keep it
   running.
5. **Palette keys**: cmd -1 ends palette cycling and restores the palette; others start it.
   Each of the four channels cycles its colour chain (`subst[step][channel]`, 1-based, 0 ends
   it) every `rates[channel]` frames.
6. **Fade keys**: the first 16 colours become fade palette `palIndex`, at once (fade 0), with a
   blocking fade (step `-fade` if negative, else 2) or, with flag bit 0, a fade that runs during
   the next frames.
7. Sound keys, as in version 2; a playing sample keeps the mult running.

When the mult ends without a text key, its own objects and the anim surface are freed and
`VAR(57)` becomes -1. freeMultKeys (draw 0x02, operand ignored) frees the mult's loaded
statics, animations and sound slots.

### 5.4 The version 1 renderer (`Mult_v1::animate`, draw 0x16)

Per object: an object that is animating and due this tick (`tick == maxTick`) is measured
(updateAnim flags 0) and its dirty box is the union with its last box; a still object with a
last box keeps it (the original then marks object 0, not itself, for redraw when it is
static). Dirty boxes of redrawn objects are restored from the anim surface. Then for each
order from the lowest to the highest seen, redrawn objects are drawn (updateAnim flags 2,
recording their new box) and the static planes of the following orders are redrawn over
them; still objects that overlap a redrawn box are repaired inside it (flags 4). Finally
due objects advance: animType 4 pauses, others go through newCycleAnim (types 0, 1 (move by
the layer's delta), 2, 3, 5, 6 as in version 2, no goblin types). `intersected` is the first
other moving object whose box overlaps.

Other version 1 draw ops: updateAnim (0x12: `expr dx, dy, animation, layer, frame, i16
flags`, drawn), getAnimLayerInfo (0x18: writes the layer's deltaX, deltaY, unknown0 and
frame count), loadMultObject (0x17: x, y and 11 anim-data bytes, a 16-bit 99 skips a field),
initMult and freeMult as in version 2.

