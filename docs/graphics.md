# Gobliins 2: text, fonts, screen, palette, cursor and input

Implementation notes for the parts of the v2 engine that sit on top of the surface/sprite layer
(which is assumed done: `spriteOperation`, `invalidateRect`, `blitInvalidated`'s rect copy,
`dirtiedRect`, sprite decoding, the sprite opcodes). Source is ScummVM `engines/gob`; every
`file:line` points into ScummVM's `engines/gob` (in its source tree).

## 0. Ground rules for Gob2

- **Classes:** `Init_v2`, `Video_v2`, `Inter_v2`, `Draw_v2` (`gob.cpp:558-569`). Wherever a
  v2 method is missing, the v1 or base version applies.
- **Coordinate adjustment is a no-op.** `Draw::_needAdjust` is 2 (`draw.cpp:132`) and Gob2
  never changes it, so `adjustCoords()` returns at once (`draw.cpp:344-346`). Everything
  below ignores it. Where code branches on `_needAdjust != 2`, only the `== 2` branch matters.
- **TOT version.** All Gob2 TOTs are 1.2, so `getVersionMinor()` is 2. Any `< 2` or `>= 4` branch
  is dead.
- **`VAR(n)`** means the 32-bit variable at byte offset `n*4`.
- **Usage counts** come from running `build/gobdis` over every TOT in
  the game folder's `INTRO.STK` (1517 functions). "Not used by Gob2"
  means the opcode never appears in reachable code. Such items get a short description only.
  Opcodes that do appear: printTotText 1293, keyFunc 336 (`1`:272, `5`:46, `0`:9, plus
  50/100/1000/5000), palLoad 222, loadCursor 209, initCursorAnim 184, printText 171,
  blitCursor 109, capturePop 107, animatePalette 102, animateCursor 45, loadFont 24,
  initCursor 23, clearCursorAnim 23, capturePush 14, setMousePos 10, setRenderFlags 9,
  initScreen 3, 34 hotspot blocks.
  **Never used:** addHotspot, removeHotspot, getTotTextItemPart, animPalInit,
  loadFontToSprite/freeFontToSprite, setBackDelta, invalidate, scroll/setScrollOffset,
  freeFont, o1_setRenderFlags.

---

## 1. Text rendering

### 1.1 Finding a text item (`Resources::getTextItem`, `resources.cpp:576-596`)

The text table is either inside the TOT (header u32 at +48 is a file offset) or, when that
field is 0, in the language file `<base>.<ext>`. Gob2 always uses the language file (28-29
TOTs per language; 3 TOTs have no texts, i.e. 0xFFFFFFFF).

**Language extensions** (`resources.cpp:455-492`):

| language | 0 | 1 | 3 | 4 | 5 | other (incl. 2 = British) |
|---|---|---|---|---|---|---|
| extension | .dat | .all | .esp | .ita | .usa | .ang |

If the wanted file is missing, ScummVM falls back from British to American (and the
reverse), then to the first language that exists (`resources.cpp:495-551`).

**Table layout:**
- `i16 count` (masked with 0x3FFF).
- `count` × (`u16 offset`, `i16 size`). Offsets are relative to the start of the table data.
- An item is absent when offset is 0xFFFF, size is 0, or offset+size runs past the table.
- The id passed to printTotText is masked with 0xFFF first (`draw_v2.cpp:201`).

**Items are edited in place.** `TextItem` wraps the table memory without copying it
(`resources.cpp:81-86`), and `printTotText` writes into it in two places:
- the v<2 control-char cleanup, which is dead for Gob2;
- cmd 10.

Neither path fires for Gob2, so a read-only view is safe.

Every item in the Gob2 data ends with an extra `0x00` after the `0x01` terminator. The size
field counts that byte.

### 1.2 TOT text item binary format

All words are little-endian.

```
+0   u16 left      bit 15 = "subtitle" flag (see 1.5); the coordinate is left & 0x7FFF
+2   u16 top
+4   u16 right     inclusive
+6   u16 bottom    inclusive
+8   u8  backColor clear colour for the whole rect; 16 and 144 mean "don't fill"
+9   box list: repeated 9-byte records, ended by i16 -1 (0xFFFF), which takes 2 bytes
       i16 x1, i16 y1, i16 x2, i16 y2   relative to (left, top)
       u8  cmd: high nibble = op, low nibble = colour
then the text stream, ended by byte 0x01
then one 23-byte record per 0xBA byte in the stream (variable insertion), in stream order
then (in the Gob2 files) one 0x00 pad byte
```

**Box ops** (`draw_v2.cpp:303-327`):

| op (cmd >> 4) | operation | colour |
|---|---|---|
| 0 | `DRAW_DRAWLINE` from (x1,y1) to (x2,y2) | `_frontColor = cmd & 0xF` |
| 1 | `DRAW_DRAWBAR` (rectangle outline) | `_frontColor = cmd & 0xF` |
| 2 | `DRAW_FILLRECTABS` (filled rectangle) | `_backColor = cmd & 0xF` |
| other | ignored | |

All coordinates get `left`/`top` added. Only colours 0-15 can be expressed. Gob2 data has
exactly one box record in all languages: op 1.

**Text stream bytes** (`draw_v2.cpp:396-627`):

| byte | operands | meaning |
|---|---|---|
| 0x01 | – | end of stream. Flushes the pending run |
| 0x02 or 0x05 | `i16 dx, i16 dy` | flush, then pen = (left+dx, top+dy). Both values are identical for Gob2 |
| 0x03 | `u8 v` | flush, then font = (v >> 4) & 7 and colour = v & 0xF |
| 0x04 | `u8 c` | flush, then colour = c (full byte) |
| 0x06 | `u8 flags`, then `i16 id` if flags & 0x80, then `i16 l, i16 r, i16 t, i16 b` if flags & 0x40 | flush, then hotspot/button setup (1.4) |
| 0x07 | – | flush, then clear the button mode (`colCmd = 0`) |
| 0x08 / 0x09 | – | underline-mask on / off. **Does not flush** |
| 0x0A | `u8 n`, `n × u16` | "glyph-code string" reference (1.4) |
| 0x20 | – | space. Advances the run position |
| 0xBA | – | insert variable from the next 23-byte record (1.4) |
| anything else | – | literal character (CP850), appended to the run |

"Flush" means: if the current run is non-empty, draw it. It happens before any byte 1-7 or
10 is processed.

**What Gob2 actually uses.** Scanning every item in USA/DAT/ANG/ALL/ITA/ESP:
- commands: only 0x02, 0x03 and 0x05;
- backColor values: 0 (most), 14, 16, 1, 5;
- font/colour pairs: font 1 colour 15 (3240×), and font 0 with colours 1, 4-11 and 15;
- characters: 0x20-0x7E plus CP850 0x80-0xAD and 0xE1 (all inside the font's 32..225 range).

So 0x04, 0x06-0x0A and 0xBA can be implemented later. Their exact semantics are still given
in 1.4.

### 1.3 `Draw_v2::printTotText(id)` step by step (`draw_v2.cpp:190-653`)

1. **Gob2 workaround** (`draw_v2.cpp:207-224`). In some **non-CD** versions, in `gob07.tot`,
   ids 24 and 31 are swapped by a small state machine (`_mayorWorkaroundStatus`). ScummVM
   applies it only when `!isCD()`. Treat it as optional. **Unsure** whether the GOG data needs it.
2. **Fetch the item.** Missing → return. Set `_pattern = 0`.
3. **Subtitle check.** `isSubtitle = byte[1] & 0x80`. If set and `_doSubtitles` is false,
   return without drawing. `_doSubtitles` is false for Gob2: only `init_v3`/`init_v4`/
   `init_fascin` set it. See 1.5 for why this matters.
4. **Header.**
   - destX = word0 & 0x7FFF, destY = word1, right = word2, bottom = word3.
   - The `RENDERFLAG_DOUBLECOORDS` and `RENDERFLAG_FROMSPLIT` variants are not Gob2.
5. **Capture push.** If `_renderFlags & RENDERFLAG_CAPTUREPUSH` (0x02): capture
   (destX, destY, right-destX+1, bottom-destY+1) and increment the per-TOT capture counter
   (`draw_v2.cpp:282-286`). Gob2 never sets this flag. Its scripts bracket printTotText with
   explicit capturePush/capturePop opcodes instead.
6. **Clear.**
   - Set `_destSpriteX/Y = destX/destY`, `_spriteRight/Bottom = right/bottom`,
     `_destSurface = 21` (back), `_backColor = byte8`, `_transparency = 1`.
   - Run `spriteOperation(DRAW_CLEARRECT)`. It fills unless the colour is 16 or 144, but
     **always** invalidates the rect.
7. **Boxes.**
   - Set `_backColor = 0`.
   - Save `_renderFlags` and clear `RENDERFLAG_NOINVALIDATE` for the duration.
   - Run each box record as in 1.2. The fields `_destSpriteX/Y` and `_spriteRight/Bottom`
     carry the absolute coordinates.
8. **Pre-scan.** Walk from the stream start to the 0x01 terminator, skipping operands
   exactly as the table in 1.2 sizes them. The 0x06 operand size is 1 + (2 if 0x80) + (8 if
   0x40). Then step one past the terminator. That position (`ptrEnd`) is the first 23-byte
   variable record (`draw_v2.cpp:330-377`).
9. **Text loop state.**
   - font = 0, colour = 0, pen = (0, 0). The pen is absolute only after the first 0x02.
   - Run buffer `str[80]`, pre-filled with spaces. Mask buffer `mask[80]`, zero.
   - `strPos = 0`, `strPos2 = -1`.
   - `_backColor = 0`, `_transparency = 1`.
10. **Flush a run** (`draw_v2.cpp:397-478`). This applies when the next byte is 1-7 or 10 and
    `strPos != 0`.
    - Terminate the run at `max(strPos, strPos2)`.
    - `width = strlen(run) * font.itemWidth`. It assumes a monospaced font; Gob2 fonts are.
    - **Button mode** (`colCmd & 0x0F` non-zero, set by cmd 6). Build the rect (penX-2,
      penY-2) to (penX+width+1, penY+charHeight+1).
      - Unless the id is -1, add hotspot `0xD000+id` there (type click).
      - Then `printTextCentered(colCmd & 0xF, rect shrunk by 2, run, font, colour)`.
    - **Normal mode.**
      - Set `_destSpriteX/Y` = pen and `_fontIndex/_frontColor` = font/colour.
      - Override them with `_subtitleFont/_subtitleColor` for subtitle items.
      - Run `spriteOperation(DRAW_PRINTTEXT)`. With `_needAdjust == 2` there is no
        bounds/truncation logic: the run is drawn as is.
      - Then for each run position whose mask byte is set, draw a 1-pixel underline
        (`DRAW_DRAWLINE`) at y = penY + charHeight, from x = penX + i*charWidth across one
        character width. Gob2 never sets mask bytes.
    - **Advance the pen** by the summed widths of the run characters. The widths come from
      the **current** `_fontIndex` field, which is the subtitle font for subtitles.
    - Reset the run.
11. **Loop end.** On 0x01, stop.
12. **Restore and finish** (`draw_v2.cpp:641-652`).
    - Restore `_renderFlags`.
    - If `RENDERFLAG_COLLISIONS` (0x04) is set: call `Hotspots::check(0, 0)` (a blocking wait
      for a key or click, see 6.6), then pop one capture if the counter is non-zero. Not Gob2.

**Draw state left behind:** `_destSurface = 21`, `_backColor = 0`, `_transparency = 1`, plus
whatever the last run set in `_fontIndex`, `_frontColor`, `_destSpriteX/Y`. The scripts don't
depend on it, but mirror it to be safe.

### 1.3b `Draw_v1::printTotText(id)` (Gobliiins, `draw_v1.cpp:154`)

Version 1 text items have the same header (absolute rectangle, back colour) and box list.
Differences: the rectangle is cleared only for back colours 0-15; the stream knows only
command 2 (`i16 x, i16 y`, relative), 3 (`u8 font << 4 | colour`), the end byte 1, letters
(drawn transparent, advancing by the font's width) and **0xBA**, which inserts a variable.
The inserts are described by 23-byte records that follow the end byte; record byte 17 is the
format (0: 32-bit number, 1: string, n >= 2: number with n decimals after a comma; bit 7:
the rest of the line flows after the insert) and bytes 18-19 the variable slot. Render flags
CapturePush / CapturePop work as in version 2. ScummVM also starts the per-level narration
track here (`cdPlayMultMusic`; not implemented yet, G1.6).

### 1.4 The commands Gob2 does not use

- **0x06 (hotspot/button).**
  - Reads `colCmd`. Sets `colId = -1`, or reads it when bit 0x80 is set.
  - With bit 0x40: reads l, r, t, b (relative to left/top) and adds hotspot
    `colId + 0xD000`, type click (2), with no key or functions, at once.
  - If `colCmd & 0x0F` is non-zero, later runs are drawn as centred "buttons" (step 10).
  - `0xD000+id` gives hotspot state 0xD (filled, disabled, type1), so the hotspot stays inert
    until a hotspot block enables it with type 12 (6.4).
- **0x0A.**
  - Writes 0xFF, then the u16 offset of this byte within the text table, then NUL into the
    run buffer.
  - Marks `n` mask bytes and skips `n` u16 glyph codes.
  - When `DRAW_PRINTTEXT` sees a string starting with 0xFF and the font is monospaced, it
    draws the glyph codes stored at that table offset instead (`draw_v2.cpp:843-852`).
    ScummVM uses only one byte of the stored offset, a latent bug.
  - Never reached in Gob2. Note that a script string beginning with byte 0xFF would also take
    this path.
- **0xBA (variable insertion)** uses the current 23-byte record `rec`. Only `rec[17]`
  (format) and `rec[18..19]` (u16 variable index; byte offset = index*4) are read.
  - `fmt & 0x7F == 0`: the decimal int32 value.
  - `fmt & 0x7F == 1`: the string at that var, at most 19 characters.
  - Otherwise: decimal, left-padded with '0' until strlen-1 ≥ fmt (a leading '-' stays first),
    then a decimal separator inserted fmt digits from the right ('.' when language is 2, else ',').
  - The text is copied into the run.
  - If `fmt & 0x80`: `strPos2` = end of the inserted text, `strPos` advances by 1 only, so later
    characters overwrite the inserted text.
  - Otherwise `strPos` advances past it, and a following space or a same-line 0x02 is folded
    into a single space (`draw_v2.cpp:599-625`).
  - Then `rec` moves on by 23.

### 1.5 The "subtitle" bit in Gob2 data: important

- In the **.USA** files, 190 dialogue items have bit 15 set on `left`, for example
  `0x8002` → x = 2.
- The **.DAT/.ANG/.ALL/.ITA/.ESP** files have none.
- ScummVM drops such items because `_doSubtitles` is false for Gob2. It maps plain "English"
  to British (.ANG, `gob.cpp:350-354`), so this rarely shows in ScummVM.
- For a native engine, **mask bit 15 and always draw** (treat `_doSubtitles` as true). The
  subtitle font/colour override in step 10 is harmless: cmd 3 inside the item sets
  `_subtitleFont/_subtitleColor` to the same values.
- **Unsure** what the original US executable did with the bit. Verify against DOSBox if possible.

### 1.6 `o2_printText` (func 0x11, `inter_v2.cpp:1089-1161`)

**Operands:** five `valexpr`s (x, y, backColor, frontColor, fontIndex), then an embedded string
that ends at byte 200 (0xC8).

**Fields set:** `_destSpriteX/Y`, `_backColor`, `_frontColor`, `_fontIndex`,
`_destSurface = 21`, `_textToPrint` (a 60-byte local buffer).
- `_transparency = 0`: opaque cells, with the background painted in backColor.
- **Exception:** backColor == 16 → `_backColor = 0`, `_transparency = 1`.

**Chunk loop.** Repeat until the next byte is 200:
1. Copy up to 59 literal bytes into the buffer, stopping before a `.` (0x2E) or byte 200.
2. If the stop byte is not 200, it is the `.` variable marker:
   - skip it;
   - peek the variable-type byte and read a full `varidx`;
   - append the value: int8/int16/int32 as signed decimal, string vars as text. Unknown types
     append nothing, and the buffer is then not NUL-terminated (latent bug);
   - skip one more byte (the separator after the var ref).
3. Otherwise terminate the buffer.
4. If `_destSpriteX < back.width && _destSpriteY < back.height`, run
   `spriteOperation(DRAW_PRINTTEXT)`. The values are compared as int, so a negative x passes;
   characters at x < 0 are then skipped one whole character at a time by `drawString`.

After the loop, skip the 200.

**Each chunk is printed separately.** `DRAW_PRINTTEXT` leaves `_destSpriteX` advanced past the
text (`draw_v2.cpp:962-963`), so chunks continue left to right. The buffer is refilled from
index 0 for every chunk.

**In Gob2** this mostly prints string variables (`"{s[3335]}"` and similar) and debug numbers.

### 1.7 `printTextCentered`, `drawButton`, `stringLength` (`draw.cpp:417-608`)

None of these is reachable in Gob2 (they are only called from cmd-6 buttons and Playtoons
`createButton`), apart from `stringLength`, which text input uses. Summary:

- **`stringLength(str, font)`.** Sums per-character widths for a proportional font, or
  strlen × width for a monospaced one. Returns 0 for a missing font. The Japanese branch is
  not Gob2.
- **`printTextCentered(id, l, t, r, b, str, font, colour)`.**
  - If the TOT's function-table entry 13 (`kFunctionCenter`, header +100+13*2) is non-zero:
    call it with VAR(17)=id, VAR(18)=l, VAR(19)=t, VAR(20)=width, VAR(21)=height, run one
    `funcBlock(0)`, then return.
  - Then, if the string is non-empty and the font exists: `_transparency = 1`, x = l +
    (width − textWidth)/2, y = t, then `DRAW_PRINTTEXT`.
- **`drawButton`.** Same centre-function call (VAR(22..25) and the multi-line `\` handling
  apply only to v≥1.4). Prints the text centred horizontally at y = top (y-centring is v≥1.4
  only).

### 1.8 `o2_getTotTextItemPart` (func 0x1A, `inter_v2.cpp:1242-1425`). Not used by Gob2

**Operands:** `i16 itemId`, `varidx dst`, `valexpr part`.
- part == -1: point `_hotspotText` at the destination string and return.
- Otherwise: zero the destination, skip the box list, then walk the stream:
  - Each run of printable text on one line, where continuation lines are 0x02/0x05 with
    |dy − currentY| ≤ 1, is one "part". Parts are counted with `curPart`. When
    `curPart == part`, the part's text is left in the destination.
  - For cmd 6 with `colCmd & 0x8F` and `-colId-1 == part`, the text after it is copied.
  - For cmd 10 with `curPart == part`, a 7-byte record `FF, u16 x, u16 y, u16 tableOffset, 00`
    is written.

Implement only if the data ever needs it.

---

## 2. Fonts (.LET)

### 2.1 File format (`Font::Font`, `video.cpp:45-64`)

```
+0  u8  width | 0x80 if a per-character width table follows the bitmaps
+1  u8  height
+2  u8  firstChar
+3  u8  lastChar
+4  bitmaps: (lastChar-firstChar+1) glyphs of itemSize bytes each
        rowBytes = (width-1)/8 + 1 ; itemSize = rowBytes * height
        rows top to bottom, bytes left to right, bit 7 = leftmost pixel, 1 = ink
then (only if flag 0x80) one u8 width per glyph
```

- **Monospaced** means "no width table" (`isMonospaced`, `video.cpp:96-98`).
- **`getCharWidth(c)`** returns the table entry for c in range, otherwise `width`.
- **`getCharWidth()`** (no argument) is always `width`.
- **`hasChar`** is the firstChar..lastChar range test.

**Checked against the real files** (a folder with the files extracted from `INTRO.STK`):
- AVT, JEUDAK, JEULET1, JEULET2, MAJ1, MAJ2 and MAJ3 all have header `08 08 20 E1`:
  8×8, chars 32..225, no width table, itemSize 8.
- File size is 4 + 194×8 = 1556 bytes. JEUDAK.LET has one stray trailing byte (1557); ignore it.
- Decoding 'A' from JEULET1 with bit 7 on the left gives the expected glyph, so the bit order
  is confirmed.
- **All Gob2 fonts are monospaced 8×8.** The proportional paths exist but are never exercised.

### 2.2 Drawing a glyph (`drawLetterRaw`, `video.cpp:100-150`)

- '\r' and '\n' draw nothing. So does a start position outside the surface (unsigned test),
  or a character outside the font range (`getCharData` returns null).
- **Per pixel:** bit set → `colour1` (the front colour). Bit clear → `colour2` (the back
  colour) **only when not transparent**. Transparent leaves the pixel untouched.
- The full `width` pixels of each row are written. Pixels past the surface edge are clipped
  per pixel.
- Colours are palette indexes (8-bit surface).

**String drawing** depends on the font (`draw_v2.cpp:835-866`).

**Monospaced** (`Font::drawString`, `video.cpp:160-178`):
- Each character is drawn only if the whole cell fits: x ≥ 0, y ≥ 0, x+w ≤ surfaceWidth and
  y+h ≤ surfaceHeight. Otherwise it is skipped entirely, but x still advances.
- Afterwards `_destSpriteX += len × width`.

**Proportional:**
- Per-character `drawLetter` with per-pixel clipping; x advances by `getCharWidth(c)`.
- The whole `width` columns are still painted. With an opaque background this overdraws the
  next cell's left part until that cell paints over it.

**After either path**, `dirtiedRect(destSurface, startX, y, endX-1, y+height-1)`, which
invalidates when the target is the back surface.

### 2.3 Font slots and who fills them

- `Draw::_fonts[16]` (`draw.h:112`). `Init::initGame` nulls 0-7 (`init.cpp:119-120`).
- **Startup** (`init.cpp:138-160`):
  - If `intro.inf` exists, each of its lines (up to 8) is a font base name, loaded into slots
    0..7 with ".let" appended.
  - Otherwise slots 0-3 get `jeulet1.let`, `jeulet2.let`, `jeucar1.let` and `jeumath.let`
    (`init.cpp:52`).
  - The GOG data has **no intro.inf**, and jeucar1/jeumath don't exist. Result:
    **slot 0 = JEULET1, slot 1 = JEULET2, slots 2-15 = null.**
  - This happens after `intro.stk` is opened (`init.cpp:93-96`), so the fonts come from
    INTRO.STK.
- **`o1_loadFont`** (func 0x4B, `inter_v1.cpp:1926-1932`).
  - Operands: `expr` filename string, `i16 slot`.
  - Calls `animateCursor(4)` (the wait cursor, 5.5) first.
  - Replaces the slot. A missing file leaves the slot **null**: the old font is deleted
    first (`draw.cpp:731-742`).
  - Gob2: REGLAGE.TOT loads JEULET1/maj1/maj2/maj3 into slot 0 (the font-style option).
- **`o1_freeFont`** (func 0x4C) nulls a slot. Unused.
- **Missing fonts.** Text drawing with a null font must be a silent no-op (`draw_v2.cpp:836-838`).
  printTotText would actually dereference null (`draw_v2.cpp:400`), so guard it.

### 2.4 Font-to-sprite (`o2_loadFontToSprite`, draw 0x30, `inter_v2.cpp:551-562`). Not used by Gob2

**Operands:** `i16 slot (0-3)`, then four (u8 value, u8 ignored) pairs: sprite, base, width,
height.

**Reset.** `playTot` sets all four entries to -1 when a TOT is loaded (`game.cpp:579-584`), and
`o1_freeFontToSprite` (draw 0x31) does the same for one entry.

**Drawing.** When `_fontIndex < 4` and `_fontToSprite[_fontIndex].sprite != -1`,
`DRAW_PRINTTEXT` blits glyph cells from that sprite instead of using the font
(`draw_v2.cpp:867-883`).
- `perRow` = 320 (if the sprite is the front/back surface) or sprite width, divided by the
  cell width.
- Character c's cell is at column `(c-base) % perRow` and row `(c-base) / perRow`.
- Transparency: colour 0 when `_transparency` is set, otherwise none.
- x advances by the cell width.

**Since Gob2 never uses it, the "-1" reset is the only part needed:** it makes `DRAW_PRINTTEXT`
always take the font path.

---

## 3. Screen, video init and presentation

### 3.1 Boot sequence and surfaces

**Engine setup** (`gob.cpp:325-337`, `gob.cpp:780-803`): 320×200, `_videoMode = 0x13`,
`_fakeVideoMode = 0x13`, `_mode = 0x14`, `_useMouse = 1`. The primary surface is a 320×200
8-bit surface.

**`Init_v2::initVideo`** (`init_v2.cpp:45-74`):
- `_frontSurface` **is** the primary surface (the same object). Drawing to sprite 20 draws
  straight into the presented framebuffer.
- `_colorCount = 256` (VGA DOS, mode 0x13).
- `_pPaletteDesc->vgaPal = _draw->_vgaPalette`.
- Cursor size defaults to 16×16 and `_transparentCursor = 1`.

**`Init::initGame`** (`init.cpp:89-160`):
- `initPrimary(0x13)` (3.3), then `_setAllPalette = true` and a full palette set (all zero, so
  black).
- Fonts (2.3).
- Then `Game::start`.

**`Game::prepareStart`** (`game.cpp:515-546`):
- Sets the palette from `_vgaPalette` (still black).
- `Draw_v2::initScreen`.
- **Fills the front surface with colour 1.** It is invisible because the palette is black.
- Mouse is warped to (152, 92) and `_inter_mouseX/Y` and `_cursorX/Y` are set to match.
- `_invalidatedCount = 0`, `_noInvalidated = true`, `_applyPal = false`,
  `_paletteCleared = false`.
- All 40 cursor anims cleared (low = -1, high = 0, delay = 0).
- `_renderFlags = 0`, backDelta = 0, `_startTimeKey = now` (used by VAR(12)).

**`Draw_v2::initScreen`** (`draw_v2.cpp:50-71`):
- Clears the scroll offsets.
- Creates a **new** 320×200 back surface in sprite 21 (zeroed) and sets `_backSurface` to it.
- If sprite 23 doesn't exist, creates the cursor strip (32×16, room for two 16×16 cursors)
  plus the 16×16 "scummvm cursor" scratch surface.
- Sets sprite 20 = front and 21 = back. Marks the whole screen dirty.

**`Draw_v2::closeScreen`** (`draw_v2.cpp:73-79`) only calls `freeSprite(21)`. Because
`freeSprite` re-points slot 21 to `_backSurface` (`draw.cpp:338-341`), this does **nothing
visible**. The next `initScreen` replaces the back surface.

**Every TOT load** (`playTot`, `game.cpp:576-602`) re-asserts sprite 20 = front,
21 = back, `_cursorSpritesBack = _cursorSprites`, and sets **`_cursorHotspotXVar = -1`**.

**Surfaces that exist in steady state:**

| slot | surface | created by |
|---|---|---|
| 20 | front (primary) | init |
| 21 | back | initScreen |
| 22 | anim surface | mult init |
| 23 | cursor strip, `count × cursorWidth` wide | initScreen / initCursor; becomes 80×16 after Gob2's `initCursor 16,16,5` |
| 30-49 | capture stack | 3.5 |
| others | script sprites | script |

Surface widths are rounded up to a multiple of 8 (`video.cpp:251-254`). Cursor and Adibou
surfaces are exempt.

### 3.2 `o2_initScreen` (draw 0x80, `inter_v2.cpp:791-884`)

**Operands:** `i16 w` (low byte = videoMode, high byte = offY, the bottom split height), then
`valexpr width`, `valexpr height`. Gob2 uses `initScreen 20, -1, -1` three times: once in
INTRO.TOT, the start TOT, and twice in MENU/MENUSPE. That is mode 0x14, offY 0, size unchanged.

1. **First, unconditionally:** clear the *physical* screen to colour 0. It does not touch any
   surface.
2. `_fakeVideoMode = videoMode`. Mode 0xD would map to `_mode`.
3. **Early return** when `videoMode == _videoMode` and `width == -1`. Gob2's second and third
   calls take this path (mode is already 0x14), so their whole effect is a black physical
   screen until the next present repaints dirty areas. Nothing else changes, and VAR(15) is
   not written either.
4. **Otherwise:**
   - Apply width/height if > 0.
   - Split: height1 = min(200, surfH−offY), height2 = offY, splitStart = surfH−offY.
   - Mouse range: 0..320 × 0..(200 − offY − 1).
   - `closeScreen` (no-op), clear the hardware palette, zero the shadow RGB arrays.
   - `_videoMode = videoMode`, `initPrimary(videoMode)` (3.3), **VAR(15) = fakeVideoMode**.
   - `_setAllPalette = true`, re-warp the mouse to `_inter_mouseX/Y`, clear the hardware
     palette again.
   - `Draw_v2::initScreen` (new zeroed back surface).
   - `setScrollOffset()`: offsets 0, one `waitRetrace`.
   - **Net effect for Gob2:** front and back both zeroed, screen black, `_vgaPalette` untouched.

**Shadow palette after initScreen.** `initPrimary` re-sets the shadow RGB from `_vgaPalette`;
the hardware palette is then cleared without touching the shadow (see 4.1).

**Not Gob2:** splits, scrolling, Lost in Time / Adibou branches.

### 3.3 `Video::initPrimary` and `initSurfDesc` (`video.cpp:218-263`)

- **`initPrimary(mode)`.** For mode 3 or -1: just record `_oldMode`. Otherwise resize the
  primary surface to `_surfWidth × _surfHeight`. **Resize always reallocates and zeroes**
  (`surface.cpp:259-270`), so the front surface goes black. Then, unless `_dontSetPalette` is
  set, call `setFullPalette(_pPaletteDesc)`.
- **`initSurfDesc(w, h, flags)`.** `PRIMARY_SURFACE` (0x80) means resize the primary.
  Otherwise allocate a new surface with the width rounded up to 8.

### 3.4 Render flags

`o2_setRenderFlags` (draw 0x0A, `inter_v2.cpp:265-278`) takes one `valexpr e`:

| e | effect |
|---|---|
| bit 15 set | `flags |= e & 0x3FFF` |
| else bit 14 set | `flags &= e & 0x3FFF` |
| else | `flags = e` |

Gob2 uses only:
- `1024+32768`: sets 0x400 (`RENDERFLAG_NOSUBTITLES`), which **no code reads**;
- `160` (0xA0): 0x80 `HASWINDOWS`, which only matters to Fascination windows and the
  `getWinFromCoord` base stub returning -1; 0x20 is undefined;
- `0`.

So **none of the behaviour-changing flags are ever set by Gob2**: NOINVALIDATE 0x1,
CAPTUREPUSH 0x2, COLLISIONS 0x4, CAPTUREPOP 0x8, USEDELTAS 0x10, BORDERHOTSPOTS 0x40,
NOBLITINVALIDATED 0x200. Store the value but you can ignore it.

**setBackDelta** (func 0x37, `inter_v1.cpp:1731-1734`) stores `_backDeltaX/Y`. They are only
applied when USEDELTAS (sprite ops) or CAPTUREPOP (hotspot coordinates) is set, which never
happens in Gob2.

### 3.5 Capture stack (`Game::capturePush/capturePop`, `game.cpp:722-789`)

This saves a rectangle of the **back surface** and restores it later. It is used around
printTotText in cutscenes (INTRO0: `capturePush 20,154,125,40; printTotText n; … capturePop`).

**State:**
- `_captureStack[20]` of rects plus `_captureCount`. Overflow is a fatal error.
- Sprite slots 30+n hold the saved pixels.
- Separately, a **per-playTot counter** (`*_pCaptureCounter`, a local in `playTot`,
  `game.cpp:556-560`) counts pushes made by scripts, so pops never cross TOT boundaries.

**Push(left, top, width, height):**
1. Record the rect (left, top, left+width, top+height). Right and bottom are exclusive here.
2. Widen horizontally to 16-pixel alignment: L = left & ~15, R = (left+width−1) | 15.
3. Create sprite 30+n of size (R−L+1) × height (zeroed).
4. Blit back(L, top, R−L+1 wide, height tall) to sprite 30+n at (0, 0), opaque.
   - This goes through `spriteOperation(DRAW_BLITSURF)` with `_needAdjust` temporarily 10.
   - It changes `_sourceSurface`, `_destSurface`, `_sprite*`, `_destSprite*` and
     `_transparency = 0`; mirror that.

**Pop(doDraw):**
1. Decrement n.
2. If doDraw: blit sprite 30+n from (left & 15, 0), size (width × height) of the recorded
   rect, to the back surface at (left, top), opaque. This goes through `spriteOperation`, so
   it **invalidates** the rect.
3. Free sprite 30+n.

**Opcodes:**
- **`o1_capturePush`** (func 0x15, `inter_v1.cpp:1426-1440`): four `valexpr`s. Ignored when
  w ≤ 0 or h ≤ 0. Otherwise push and increment the counter.
- **`o1_capturePop`** (func 0x16): only if the counter is > 0, decrement it and pop with draw.
  An unmatched pop is therefore a no-op. INTRO0 ends with two extra pops; they are safe.

**Implicit pops:**
- When a TOT's main function returns, `playTot` pops (without drawing) as many captures as its
  counter says (`game.cpp:664-665`).
- `initControlVars` zeroes the counter on every TOT load and `playTot` call (`inter.cpp:138-152`).

### 3.6 Presentation: when pixels reach the screen

There are three layers:
1. Scripts draw on the back surface (sprite 21), which calls `invalidateRect`, or directly on
   the front (sprite 20), which calls `dirtyRectsAdd`.
2. `Draw::blitInvalidated` copies invalidated rects back → front and marks them dirty
   (`draw.cpp:238-290`).
3. `Video::retrace` copies the dirty parts of the front/primary surface to the real screen and
   flips (`video.cpp:276-312`).

**`retrace(mouse = true)`** also shows the hardware cursor iff `_showCursor & 6` (5.3). For a
native SDL build, just upload the whole 320×200 framebuffer and palette each retrace. The dirty
lists are only an optimisation.

**`waitRetrace`** = retrace, then sleep so that at least ~10 ms have passed (`video.cpp:314-318`).
ScummVM's stand-in for VGA vsync. **Unsure** whether the original waited for a real 70 Hz
vertical retrace (about 14.3 ms).

**`Util::waitEndFrame`** (`util.cpp:474-502`) is the frame pacer:
- It loops {processInput, retrace} until `1000/frameRate` ms have passed since the last frame.
- More than 1 s late (or a negative delta): one retrace and resync.
- `setFrameRate` (func 0x47) sets the rate; Gob2 uses 12, and 10 twice. Rate 0 becomes 1.
- Before the first `setFrameRate`, the wait time is 0, so each call is one retrace.

**`Util::longDelay(ms)`** loops {waitRetrace, processInput, sleep 15} until ms have elapsed.

**Callers of `blitInvalidated` relevant to Gob2:**

| caller | where | notes |
|---|---|---|
| `o1_keyFunc`, every call | `inter_v1.cpp:1351-1352` | after `animPalette()` |
| `o1_animatePalette` (func 0x48) | `inter_v1.cpp:1910-1916` | blitInvalidated → waitEndFrame → animPalette → `storeKey(checkKeys(...))`. **This is Gob2's per-frame "present and pace" opcode** |
| `o2_playCDTrack` (draw 0x20) | `inter_v2.cpp:509-514` | unless flag 0x200 |
| `Hotspots::check` | `hotspots.cpp:813`, `hotspots.cpp:829-835` | once on entry; then each loop iteration when something is invalidated, either directly or through `animateCursor(-1)` |
| `Draw_v2::animateCursor` | `draw_v2.cpp:168-179` | when anything is invalidated |
| `Mult::playMult`, each frame | `mult.cpp:193-196` | then `waitEndFrame` |
| `Game::playTot` | `game.cpp:658` | after the TOT's start function returns |

**When the screen is actually updated:**
- `waitRetrace` runs in `Hotspots::check` (each iteration that blitted), `animateCursor`,
  fades, `longDelay`, `setScrollOffset` and `handleBusyWait`.
- `waitEndFrame` runs in animatePalette and playMult.
- Note that `o1_keyFunc` itself only retraces through `handleBusyWait` (3.7) or `longDelay`.
  **Recommendation:** have `keyFunc` always retrace after its `blitInvalidated`. It is
  harmless and avoids invisible frames when scripts poll slowly.

**`blitInvalidated` logic in full** (`draw.cpp:238-290`):
1. If `_cursorIndex == 4` (the wait cursor), `blitCursor()` first. This hides it (5.3).
2. If nothing is invalidated and there is no `_applyPal`, return.
3. If nothing is invalidated but `_applyPal` is set: set the palette, clear `_applyPal`, return.
4. If the cursor strip exists: `_showCursor` bit 1 = bit 0. Outside `animateCursor` this hides
   the cursor.
5. If `_applyPal`: clear the hardware palette (only if not already cleared), copy the **whole**
   back surface to the front with everything dirty (`forceBlit`), set the palette, reset the
   invalidation list, clear `_applyPal`, return. The visible result is a hard cut with no fade.
6. Otherwise copy each invalidated rect back → front and mark it dirty. Then reset the list and
   set `_noInvalidated = true`.

**gobnative's HD packs** don't change any of this: with a pack, every surface also carries an
HD layer that its own operations (blit, fill, pixel, text) keep in step, and decoded pictures get
their pack version; the front surface's layer becomes the full-colour frame (`docs/hdpacks.md`,
section 4).

### 3.7 ScummVM-only throttling to be aware of

**`Inter::handleBusyWait`** (`inter.cpp:477-486`), called by every keyFunc: if the previous
keyFunc was ≤ 20 ms ago and `_noBusyWait` is not set, run `longDelay(1)` (one retrace). Short
keyFunc delays (<20) set `_noBusyWait`. Keep something similar so tight `repeat keyFunc 1
until …` loops don't spin the CPU.

---

## 4. Palette

### 4.1 State

- **`_draw->_vgaPalette[256]`**: the **logical/target** palette, 6-bit RGB (0..63). Scripts
  load into it.
- **`_global->_red/_green/_bluePalette[256]`**: the **shadow of what was last programmed into
  the DAC**. `setFullPalette` and `setPalElem` update it; **`Util::clearPalette` does not**
  (it only blacks the hardware, `util.cpp:438-455`). Fades start from this shadow, so after a
  `clearPalette` a fade-in starts from the old shadow values, not from black. Reproduce that,
  or explicitly zero the shadow where the original did (`o2_initScreen` does, 3.2).
- **`_pPaletteDesc->vgaPal`** normally points at `_vgaPalette`. Mult fade keys re-point it at a
  mult palette and leave it there (`mult.cpp:404-417`). palLoad and `Draw::setPalette`
  re-point it.
- **6-bit to 8-bit conversion:** ScummVM shifts left by 2 (`video.h:136-140`), so 63 becomes 252.
- **`_setAllPalette`** is true after init. `_colorCount` is 256. Every "16 colours" branch is dead.

### 4.2 Setting

- **`Video::setFullPalette(desc)`** (`video.cpp:452-477`): copies all 256 entries to the shadow
  and the hardware.
- **`setPalette(desc)`**: the same but without the shadow; it would be 16 entries if
  `!_setAllPalette`.
- **`setPalElem(i, r, g, b)`**: one entry, updating the shadow and hardware.
- **`Draw::setPalette()`** (`draw.cpp:292-300`): `vgaPal = _vgaPalette`, `setFullPalette`,
  `_paletteCleared = false`.
- **`Draw::clearPalette()`**: hardware to black, once, setting `_paletteCleared`.

### 4.3 Fades (`PalAnim`, `palanim.cpp:53-163`)

**`fade(desc, fadeV, allColors)`:**
- **Step size** `_fadeValue` = −fadeV when fadeV < 0, else 2. Units are 6-bit levels.
- **Targets** = `desc->vgaPal`, or all zero when desc is null, for 256 colours.
- **allColors == 0 (normal fade):** loop { `fadeStep(0)`; `waitRetrace`; if fadeV > 0, sleep
  fadeV ms } until a step changes nothing. Then set the exact final palette: `setFullPalette(desc)`,
  or clear when desc is null.
- **allColors == 1:** fade red, then green, then blue separately (`fadeStepColor`, which
  **only touches colours 0-15**). Then the final set.
- **allColors == −1 (or anything else):** only load targets and step size. Mult then calls
  `fadeStep(1..3)` per frame.

**`fadeStep(0)`:** for each colour, move each shadow component toward its target by
`_fadeValue`, snapping to the target when closer than that. Changed entries go to the hardware
with `setPalElem`. Returns true when nothing changed.

**Visible behaviour of a script fade.** Step 2, 63 levels max → 32 changing steps plus one
final empty step. Each step costs one `waitRetrace` (about 10 ms in ScummVM), so a full
black↔image fade takes **about 0.33 s**, and the call **blocks** the script. If the original
synced to 70 Hz it would be about 0.47 s (**unsure**). Mult fade keys use `fadeV` as an extra
per-step delay in ms.

### 4.4 `o1_palLoad` (func 0x13, `inter_v1.cpp:1107-1348`)

**Operand:** `u8 cmd`. Bit 7 set means **fade now, blocking**. Bit 7 clear means **apply
later**: the new palette is shown with a hard cut at the next `blitInvalidated` that has
something invalidated, through `_applyPal` (3.6 step 5).

> **Correction:** `docs/opcodes.md` §8.3 calls bit 7 "don't apply the palette now".
> `src/script/disasm.cpp:520` prints "(deferred)" when bit 7 is set. Both are backwards.
> `_applyPal = !(cmd & 0x80)` (`inter_v1.cpp:1181`), and the closing `if (!_applyPal) fade(...)`
> (`inter_v1.cpp:1322-1347`) runs exactly when bit 7 is **set**.

Behaviour with Gob2 state (VGA, 256 colours, mode ≥ 0x13):

| cmd&0x7F | Gob2 behaviour |
|---|---|
| 48, 49, 50, 51, 52 | operands skipped (48/18/16/2/48 bytes), **return without any palette action** |
| 53 | `i16 resId`. Copy min(768, size) bytes of the resource (6-bit RGB triplets) into `_vgaPalette`. Bit 7 → `fade(desc, 0, 0)` to it. Otherwise `_applyPal = true` |
| 54 | zero `_vgaPalette`. Bit 7 → fade to black. Otherwise deferred black |
| 55 | push a copy of `_vgaPalette` (2 operand bytes ignored), `_applyPal = false`, return |
| 56 | `u8 first, u8 last`: pop entries first..last from the stack, set the palette at once. Then, because `_applyPal` was reset to false, it also runs a (no-op) fade |
| 61 | `u8 first, u8 last, i16 resId`: copy that range from the resource. Bit 7 **clear** → set at once and return. Bit 7 set → fade |
| other | no operands. Bit 7 → fade to the current `_vgaPalette`. Otherwise deferred |

**Gob2 uses only 0xB4, 0xB5 and 0xB6:**
- 0xB4 is always a no-op (cmd 52 skipped on VGA).
- The typical scene change is `palLoad 0xB4; palLoad 0xB6; keyFunc 1; …draw…; palLoad 0xB4;
  palLoad 0xB5 res N`, i.e. **fade to black (blocking), draw, fade in (blocking)**.
- `_applyPal` therefore never becomes true in Gob2, and the deferred path of
  `blitInvalidated` is dead. Implement it anyway; it is small.

### 4.5 Palette cycling (not used by Gob2)

- **`o2_animPalInit`** (func 0x17, `inter_v2.cpp:1163-1182`).
  - Operand `i16 n`, then two `valexpr` (low, high).
  - n > 0: slot n−1 rotates **up**; dir = 1: the colour at `high` moves to `low` and the rest
    shift up by one.
  - n < 0: slot −n−1 rotates **down**.
  - n == 0: all slots off (the expressions are still read).
- **`Inter_v2::animPalette`** (`inter_v2.cpp:1749-1783`) runs at the start of every keyFunc,
  in animatePalette, and after a duration-tick leave call (6.5). If any slot is active: one
  `waitRetrace`, rotate each active range in `_vgaPalette`, then `setFullPalette`.
- `initControlVars(1)` clears all slots on each TOT load (`inter.cpp:146-150`).
- Gob2 never activates a slot, so `animPalette` is a no-op. Keep the call sites.

---

## 5. Cursor

### 5.1 State (`draw.h:137-169`)

| field | meaning |
|---|---|
| `_cursorSprites` (= sprite 23) | horizontal strip; cursor/frame k occupies x = k·cursorWidth .. +cursorWidth−1 |
| `_cursorWidth/_cursorHeight` | 16/16 by default |
| `_cursorIndex` | logical cursor being shown. 0 initially; −1 has special meaning in `Hotspots::check` (6.6) and `blitCursor` |
| `_cursorAnim` | current animation frame |
| `_cursorTimeKey` | time of the last frame change |
| `_cursorAnimLow/High/Delays[40]` | per logical cursor: first frame (−1 = cursor not defined), last frame, delay in 10 ms units (0 = static, and then the frame equals the cursor index) |
| `_cursorHotspotXVar/YVar` | variable index (in VAR units) of per-cursor hotspot arrays, or −1 |
| `_cursorHotspotX/Y` | fixed hotspot when the var is −1 (−1 = none) |
| `_showCursor` | bit 0 = "inside animateCursor"; bit 1 = "visible on front". Visible on screen iff `(_showCursor & 6) != 0` |
| `_transparentCursor` | set by initCursor but **not used** by v2 drawing: colour 0 is always transparent |

### 5.2 Opcodes

**`o1_initCursor`** (draw 0x07, `inter_v1.cpp:335-394`). Operands: `varidx hx`, `varidx hy`,
`i16 w`, `i16 h`, `i16 count`.
- Always: `_cursorHotspotXVar = hx/4`, `_cursorHotspotYVar = hy/4`.
- w and h are clamped to ≥ 16; count to ≥ 2.
- **Only if** w ≠ cursorWidth, h ≠ cursorHeight, or the strip width ≠ w·count:
  - set `_transparentCursor` = (count < 0x80). If count > 0x80, subtract 0x80. Recreate the
    strip as (w·count) × h, zeroed. The 0x80 handling doesn't matter for Gob2;
  - reset all 40 anims to (−1, 0, 0), then **set `_cursorAnimLow[1] = 0`**.
- Gob2: `initCursor v[1273], v[1313], 16, 16, 5`. The first call grows the 32-wide strip to
  80×16 and resets anims. Repeated identical calls only re-set the hotspot vars, which matters
  because every TOT load resets them to −1.

**`o1_loadCursor`** (func 0x03, `inter_v1.cpp:763-787`). Operands: `i16 resId`, `i8 slot`.
- Ignored if slot·width ≥ strip width, or if the resource is missing.
- Fill slot `slot` with colour 0.
- Decode the sprite resource there with `drawPackedSprite`, opaque, at x = slot·cursorWidth, y = 0.
- **`_cursorAnimLow[slot] = 0`**, which makes cursor `slot` "defined".
- Gob2 loads slots 0-4.

**`o1_initCursorAnim`** (draw 0x08, `inter_v1.cpp:396-404`). Operands: `valexpr idx`,
`i16 low`, `i16 high`, `i16 delay`. Sets those three values and **`_showCursor = 3`**.
- Gob2 examples: `0, 0, 0, 0` and `1, 0, 1, 10` (cursor 1 alternates frames 0 and 1 every
  100 ms).

**`o1_clearCursorAnim`** (draw 0x09): `valexpr idx` → (−1, 0, 0) and **`_showCursor = 0`**.

**`o1_animateCursor`** (func 0x49): `animateCursor(1)`.

**`o1_blitCursor`** (func 0x4A): `blitCursor()`.

**`o1_setMousePos`** (func 0x46, `inter_v1.cpp:1886-1904`): two `valexpr`s →
`_inter_mouseX/Y` (minus the scroll offset, which is 0). Warps the real mouse when `_useMouse`.

### 5.3 `blitCursor` and visibility (`draw_v2.cpp:81-86`)

`blitCursor()`: if a strip exists and `_cursorIndex != −1`, copy bit 0 into bit 1. Called
outside `animateCursor` (bit 0 = 0), this **hides** the cursor. Gob2 calls it 109 times before
non-interactive stretches.

**What else changes visibility:**
- `blitInvalidated` does the same bit copy (3.6 step 4).
- `keyFunc 0` clears bit 1.
- initCursorAnim/clearCursorAnim force 3/0.
- `animateCursor` makes it visible (below).

### 5.4 `Draw_v2::animateCursor(cursor)` (`draw_v2.cpp:88-188`)

1. No strip → return. Set `_showCursor |= 1`.
2. **cursor == −1 (automatic).** Use `Hotspots::findCursor(mouseX, mouseY)` (6.7). If that
   cursor's low is −1, use 1.
3. **Cursor not defined** (low == −1): `blitCursor()` (hide), set `_cursorX/_cursorY = 0`, go
   to step 9.
4. **Same cursor as `_cursorIndex`.**
   - If its delay ≠ 0 and `now ≥ _cursorTimeKey + delay·10`: `_cursorAnim++`, wrap to low if
     outside [low, high], `_cursorTimeKey = now`.
   - Else, if nothing is invalidated and the mouse equals `_cursorX/Y`: `waitRetrace` and
     **return immediately**. Bit 0 stays set, a ScummVM quirk.
5. **Different cursor.** `_cursorIndex = it`. If delay ≠ 0: `_cursorAnim = low`,
   `_cursorTimeKey = now`.
6. **Pick the frame.** If delay ≠ 0: clamp `_cursorAnim` into [low, high] and use it as the
   frame. Otherwise the frame is the cursor index.
7. **Hotspot.** If `_cursorHotspotXVar != −1`: (VAR(hotspotXVar + `_cursorIndex`),
   VAR(hotspotYVar + `_cursorIndex`)) as u16. The arrays are indexed by the logical cursor,
   not the frame. Else the fixed X/Y. Else (0, 0).
8. **Install the image.** Copy the frame cell into the hardware cursor image (colour 0
   transparent, hotspot as computed).
   - If anything is invalidated: run `blitInvalidated()` with `_cursorIndex` temporarily −1.
     This sets bit 1 from bit 0, i.e. visible.
   - Otherwise: `_showCursor = 3`, `waitRetrace`, and if min(newY, `_cursorY`) < 50 sleep
     5 ms (a vsync-tearing heuristic).
   - `_cursorX/Y` are **not** updated on this path in v2, which makes the step 4
     "unchanged" test stale. Harmless with a hardware cursor.
9. Clear bit 0.

**Callers:**
- `Hotspots::check` with handleMouse: `animateCursor(-1)` per iteration, and `(2)` while a
  button is held.
- `playMult` with handleMouse.
- `o1_animateCursor`: `(1)`.
- The wait cursor `(4)`: o1_loadFont, o2_readData, and the start of every TOT load
  (`game.cpp:574`).

**Native recommendation.** Keep "current frame + hotspot + visible flag" as state set by this
function. At every retrace, draw the frame as an overlay at the **live** mouse position minus
the hotspot when `(_showCursor & 6) != 0`, with colour 0 transparent, and never into the front
surface. That matches ScummVM's hardware cursor, which follows the mouse even between script
calls.

### 5.5 Wait cursor

Slot 4 (Gob2 loads it with `loadCursor 7, 4`). `animateCursor(4)` shows it. The next
`blitInvalidated` calls `blitCursor` because `_cursorIndex == 4` (`draw.cpp:245-246`), which
hides it.

### 5.5b Version 1 (Gobliiins, `Draw_v1::animateCursor`, `draw_v1.cpp:59-152`)

`Draw::v1Cursor` selects these differences (set for engine version 1):
- The cursor shown is always the cell of `_cursorIndex`, never `_cursorAnim`: the animation
  counter runs but is not drawn. Gobliiins' INTRO sets `initCursorAnim 2, 1, 1, 100`, and cell
  2 of its 20x20 strip is empty, so while a button is held down in a hotspot wait
  (`animateCursor(2)`) the cursor disappears, as in ScummVM.
- `animateCursor` starts with `_showCursor = 2` and ends with 3 once a cursor is installed.
  ScummVM then sets 0 after the retrace that showed it when nothing was invalidated; our
  presenter draws the cursor at present time from `_showCursor`, so we keep 3 (visible).
- `blitCursor` only turns 2 into 0 (a cursor index with no animation hides the cursor).

### 5.6 Mouse variables (`Inter::storeMouse`, `inter.cpp:167-178`)

**`storeMouse`:** VAR(2) = `_inter_mouseX`, VAR(3) = `_inter_mouseY`,
VAR(4) = `_game->_mouseButtons` (0 none, 1 left, 2 right; both-pressed is reported as 0 by
`checkKeys`).

**`storeKey(key)`** (`inter.cpp:180-211`):
- VAR(12) = ms since `prepareStart`.
- `storeMouse`.
- VAR(1) = sound playing (0/1).
- VAR(0) = key in short form (6.8).
- If key ≠ 0, the key buffer is flushed.

**Who updates `_inter_mouseX/Y`:** `Game::checkKeys` (which polls the mouse position into them)
and `o1_setMousePos`. `prepareStart` sets them to (152, 92).

---

## 6. Hotspots and input

### 6.1 Data (`hotspots.h`, `hotspots.cpp:43-201`)

**Table:** 250 `Hotspot` slots. A slot is **free / end marker** when `left == 0xFFFF`.
**Almost every loop stops at the first free slot**, so a hole made by `remove()` hides all
later entries from those loops. `add()` fills the first free slot (or updates the slot with
the same id), so holes get refilled.

**Fields:** `id`, `left`, `top`, `right`, `bottom` (inclusive), `flags`, `key`, `funcEnter`,
`funcLeave`, `funcPos`, plus the owning `Script*` for pos/leave. In Gob2 there is one script
per TOT; saving the current script is enough.

**id:** low 12 bits = index/number; bits 12-15 = **state**.
- 8 = filled (local block hotspots: `0x8000 | i`)
- 4 = disabled
- 2 = type2 (globals from addHotspot, id ≥ 0)
- 1 = type1 (globals with negative id, and printTotText buttons)
- 0xC = filled + disabled

**flags:**
- bits 0-3 = type;
- bits 4-6 = button (0 left, 1 right, 2 any, other none);
- bits 8-11 = window (0 for Gob2);
- bits 12-15 = cursor index for `findCursor`;
- bit 7 = "special": `recalculate(false)` skips it.

**Types:**

| type | meaning |
|---|---|
| 0 | hover-only (enter/leave), never clickable |
| 1 | move: hover enter/leave **and** clickable |
| 2 | click |
| 3-10 | text inputs (3/4 plain, 5/6 with an expected-answer list, 7/8 same without `cleanupStr`, 9/10 float). Odd = no leave block, even = has a leave block |
| 11 / 12 | "enable" pseudo-entries |
| 20 | click that also marks the input-match hotspot |
| 21 | click with an enter block and no leave |

**Helper predicates:**
- `isIn` compares as **int16** (`hotspots.cpp:162-175`). Gob2 relies on this to park hotspots
  at negative coordinates.
- `isFilledEnabled`: state & 0xC == 8.
- `isFilledNew`: state == 8 exactly.

**Current-hover state:** `_currentKey`, `_currentId`, `_currentIndex`, `_currentX/Y`.
`_shouldPush` and a stack of saved hotspot sets.

### 6.2 Basic operations

- **`clear()`**: every slot free, `_currentKey = 0`.
- **`add(...)`** (`hotspots.cpp:250-286`): the first slot that is free **or** has an identical
  id is overwritten. If the stored id matches ignoring the disabled bit, the stored id is kept,
  so the disabled state survives the update. The current script is recorded. A full table is
  fatal.
- **`remove(id)`**: free every slot with exactly this id.
- **`removeState(s)`**: free every slot whose state equals s.
- **`push(all, force)`** (`hotspots.cpp:396-468`).
  - No-op unless `_shouldPush` or force.
  - Moves the selected entries (scanning up to the first free slot) to a new stack entry and
    frees them. Selection: all = 1 everything; 0 those with id ≥ 20, which in practice is
    everything; 2 only disabled ones.
  - Also saves the current key/id/index/x/y and `_shouldPush`, then resets them.
- **`pop()`**: appends the saved entries after the first free slot and restores the saved
  current/hover state. Running out of space is fatal.
- **`recalculate(force)`** (`hotspots.cpp:314-394`): for each entry with `funcPos != 0` (and
  not flagged 0x80 unless forced):
  - call the script at `funcPos` and re-read 4 `valexpr` (left, top, w, h), plus a flags
    `valexpr` for state 0xA;
  - clamp negative left/top by shrinking w/h;
  - store right = left+w−1 and bottom = top+h−1;
  - CAPTUREPOP deltas are not Gob2.
  - `funcPos` points at the coordinate expressions inside the block, so this re-evaluates
    them in place.
- **`call(offset)`** (`hotspots.cpp:519-538`): run one code block at offset as a subroutine.
  - Set `_shouldPush = true`.
  - Run `funcBlock(0)`. Any hotspot block evaluated inside pushes the outer set.
  - Pop until the stack depth is back where it was.
  - Set `_shouldPush = false` and `recalculate(false)`.
- **`enter(i)`**: for state 9 or 0xA, VAR(17) = −(id & 0xFFF). Record the mouse position in
  `_currentX/Y`. If funcEnter ≠ 0, `call(funcEnter)`.
- **`leave(i)`**: for state 9 or 0xA, VAR(17) = id & 0xFFF. If funcLeave ≠ 0, `call(funcLeave)`.

**TOT switching:** `totSub` / `switchTotSub` / `loadTot` push the whole set (force) before the
sub-TOT and clear+pop after (`game.cpp:1009-1020`, `game.cpp:1029-1043`,
`game.cpp:1109-1115`).

### 6.3 `checkMouse(type)` (`hotspots.cpp:647-737`)

Both modes scan from slot 0 to the first free slot, skip disabled entries, and test window 0.
The mouse is `_inter_mouseX/Y`.

**Move:**
- Take the first entry with type ≤ 1 whose rect contains the mouse.
- Return its `key`, and its id/index through the out parameters.

**Click:**
- Take the first entry with type ≥ 1 that contains the mouse and whose button matches
  `_game->_mouseButtons`.
- For types 1 and 2, return its key. For other types, return 0, but id/index are still set.
- If nothing matches: return `kKeyEscape` (0x011B) when the button state is not exactly left
  (so **right-click on nothing acts as Escape**), else 0.

### 6.4 Hotspot block (type 2): `Hotspots::evaluate` (`hotspots.cpp:1653-1810`)

The byte layout is known to you. Runtime behaviour:

**1. Setup.**
- `push(0)`. This only happens when nested inside a hotspot callback; at top level
  `_shouldPush` is false.
- `endIndex` = first free slot. The block's new entries go there.
- Header: `handleMouse` = byte0, stored in `_game->_handleMouse`. `duration` = byte1 seconds
  converted to ms. bytes 3/4 = `idx1`/`idx2`, byte 5 = recalc flag. byte 2 is Fascination-only.
- **If idx1 or idx2 is non-zero:** duration = byte1·10 ms, and byte1 == 100 means 2 ms. The
  value is kept in `timeVal`.
- VAR(16) = 0.

**2. Add the entries** with `evaluateNew` (`hotspots.cpp:1322-1573`). Entry i gets id
`0x8000|i`. The id field read from the script goes into a local table `ids[i]`.
- **Coordinates.** Type bit 0x80 means 4 `valexpr`s, and `funcPos` = the position of those
  expressions so `recalculate` can redo them. Otherwise 4 raw u16. Bit 0x40 means an extra
  window byte (not Gob2). right = left+w−1, bottom = top+h−1.
- **Type 0:** skip 6 bytes; enter block, leave block. key = 0xA000+i, flags = 0.
- **Type 1:** `i16 key`, `i16 id`, `i16 btnFlags`; enter block, leave block. key 0 becomes
  0xA000+i. flags = 1 | btnFlags<<4.
- **Type 2 / 20:** `i16 key`, `i16 id`, `i16 btnFlags`; leave block only. **key 0 stays 0.**
  Type 20 also records `inputId = i`.
- **Type 21:** like type 2 but the block is the enter function and there is no leave.
- **Inputs 3-10:**
  - Clear the key buffer.
  - key = `varidx` of the string var; `i16 font`, `u8 back`, `u8 front`. Types 5-8 add
    `u16 len` + the expected-answers blob, which is skipped.
  - With a monospaced font, right = left + w·charWidth − 1 (w counts characters).
  - Even types have a leave block. left == 0xFFFF means "absent": skip its leave block.
- **Types 11/12** add nothing. They skip 6 bytes and **enable** every existing entry in state
  0xE (type 11) or 0xD (type 12). Each such entry's funcEnter and funcLeave are set to the
  following block. Then that block is skipped.

**3. Recalc.** If the recalc flag is set, `recalculate(true)`.

**4. Prepare.** `_forceHandleMouse = 0`, flush the key buffer.

**5. Main loop**, while id == 0 and not terminating:
- **Get input.**
  - If the block has inputs: `key = handleInputs(...)` (6.9) and VAR(55) = current input
    index. On Return: `findFirstInputLeave` (the first enabled input with a leave block),
    then break.
  - Otherwise: `key = check(handleMouse, −duration, id, index)` (6.6). With duration 0 there
    is no timeout.
- **`convertSpecialKey`:** a key with low byte ≥ 0x20 and a scan code of 2..11 (the digit
  row) is rewritten to the digit character, keeping the high byte. With ScummVM's key
  translation this almost never fires.
- **`evaluateFind`** (`hotspots.cpp:1575-1651`):
  - id already set (click): done.
  - key ≠ 0: `findKey` returns the first filled-enabled entry with `key == k` or
    `key == 0x7FFF` (catch-all). If none, `findKeyCaseInsensitive` compares `toupper(k & 0xFF)`
    with entries whose key < 0x100. **Unmatched keys are dropped and the loop continues.**
  - Timeout (key 0, id 0, duration ≠ 0):
    - **idx1 ≠ 0 → `leaveNthPlain(idx1)`.** Take the idx1-th entry with state exactly 8,
      counting from `endIndex`. Run `storeMouse`. If VAR(16) is still 0: VAR(16) = its
      `ids[]` value, `call(its funcLeave)`, `animPalette()`, and shrink the remaining budget:
      `duration = clamp(timeVal − callTime, 2, timeVal)`. If VAR(16) is non-zero after the
      call, the block **finishes**. Otherwise id is reset and the loop goes on.
    - idx2 ≠ 0: take the idx2-th plain entry as if clicked.
    - neither: take the first state-8 entry as if clicked, call the hovered entry's funcLeave
      if any, `_currentKey = 0`.
- **Found an entry with funcLeave == 0** (type 21 or input types): run `storeMouse`, VAR(16) =
  id (6.5), `call(funcEnter)` if any, VAR(16) = 0, id = 0, and keep looping.
- **Found an entry with funcLeave:** exit the loop.

**6. After the loop.**
- If `(id & 0xFFF) == inputId`: `matchInputStrings` (6.9).
- If handleMouse == 1: `blitCursor` (hide).
- **Normal exit:** **seek the script to the found entry's funcLeave** and run `storeMouse`. If
  VAR(16) is 0, set VAR(16) = id. The interpreter's `callSub` loop then executes that leave
  block as the **continuation** of the current function.
- **Duration-finished or terminate:** mark the script finished, so the enclosing function
  returns.
- Remove the block's entries (`remove(0x8000|i)`).
- **Disable** every remaining entry in state 9 or 0xA.

### 6.5 VAR(16) and VAR(17)

**VAR(16)** ("current hotspot", `setCurrentHotspot` at `hotspots.cpp:2269-2283`):
- 0 while waiting and during hover callbacks.
- When an entry is chosen: its script `id` (from `ids[]`) for local entries (state 8),
  otherwise `id & 0xFFF`.
- So **inside a leave block, VAR(16) ≠ 0 means "clicked or selected" and VAR(16) == 0 means
  "mouse moved out"**.
- Gob2 depends on this. GOB00's helper 0x397C starts with `if v[16] != 0`, and type-1 object
  hotspots share one leave block for both the walk-there click and the hover-out.
- Note that VAR(16) also holds the language at TOT start (`game.cpp:639-648`); scripts copy
  it away first.

**VAR(17):** set by `enter`/`leave` only for global states 9/0xA (−id on enter, id on leave),
and used for input matching (6.9).

**VAR(2..4)** come from `storeMouse` before each enter/leave continuation. VAR(4) still holds
the button that caused the click (1 = left, 2 = right), even though `check` already waited for
the release.

**The Gob2 game screen block** (all GOBxx TOTs) is `handleMouse 1, duration 8, idx1 1`.
- Entry 0 is an off-screen 1×1 click hotspot whose leave block is the per-tick game logic
  (GOB00: `call 0x1AA0`, which runs the goblins, CD track and timers).
- So the block **calls entry 0's leave function about every 80 ms** (minus the time the tick
  took) while it waits for hover/click/keys.
- A tick that sets VAR(16) ≠ 0 ends the block.
- Entry 1 is a full-screen type 2 with button flag 1 (right click).
- Then come the type 0/1 object hotspots, with expression coordinates recalculated after
  every callback.
- MENU uses duration 1 / idx1 1 (10 ms ticks), and also blocks with duration 0 (wait forever).

### 6.6 `Hotspots::check(handleMouse, delay, &id, &index)` (`hotspots.cpp:779-963`)

1. If `delay ≥ −1`: forget the hover (`_currentKey/Id/Index = 0`) without calling leave.
2. If handleMouse:
   - when `_cursorIndex == −1` and nothing is hovered, look up the move hotspot and `enter` it;
   - then `animateCursor(-1)`.
3. `blitInvalidated`, `waitRetrace`, note the start time.
4. **Loop:**
   - Terminate/quit → return 0, hiding the cursor if handleMouse.
   - **`checkHotspotChanged()`** (`hotspots.cpp:739-777`): run `checkMouse(move)`. If the key
     differs from `_currentKey`: `leave(old)` when valid (key ≠ 0, state has bit 8), switch
     current, `enter(new)` when valid. This is how type 0/1 hover enter/leave blocks run.
   - If something is invalidated: `animateCursor(-1)` (handleMouse) or `blitInvalidated`;
     then `waitRetrace`.
   - `checkKeys` (6.8) updates the mouse into `_inter_mouseX/Y` and the buttons into
     `_game->_mouseButtons`, and returns a key.
   - **No mouse handling but a button down:** `waitMouseRelease(0)`, key = **3**.
   - **Key ≠ 0:** hide the cursor if handleMouse & 1, clear id/index, `leave(current)` if
     valid, clear current, break.
   - **handleMouse and a button down:** `animateCursor(2)`. If delay > 0, sleep delay ms;
     else if handleMouse & 1, `waitMouseRelease(1)` (animating the cursor while held).
     `animateCursor(-1)`. Then `key = checkMouse(click)`.
     - **A hit** (key or id set): hide the cursor if handleMouse & 1 and (delay ≤ 0 or no
       buttons). If a *different* hotspot was hovered, `leave(it)`. Clear current, break.
     - **handleMouse & 4:** return 0.
     - **Otherwise:** `leave(current)` if any, re-hover with `checkMouse(move)` + `enter`.
   - **handleMouse and no button:** `checkHotspotChanged()` again.
   - **delay == −2** with no key and no button: break with 0.
   - If handleMouse: `animateCursor(-1)`.
   - **Timeout:** delay < 0, no key, no button, and more than −delay ms since start → break
     with id 0.
   - Sleep 10 ms.
5. Return the key.

**Clicking the hovered hotspot** does not call its leave here; the evaluate continuation runs
it once with VAR(16) set.

**`handleMouse` values in Gob2:** 0, 1, 2.
- Bit 0 (value 1) waits for the button release before testing the click and hides the cursor
  afterwards.
- Value 2 tests while the button is still down.

**keyFunc 0** (`o1_keyFunc` case 0, `inter_v1.cpp:1370-1381`):
- `_showCursor &= ~2`, `longDelay(1)`.
- `key = check(0, 0)`: wait forever for a key, or a click, which returns 3. Hover callbacks
  still fire.
- `storeKey(key)`, flush keys.

### 6.7 `findCursor(x, y)` (`hotspots.cpp:1812-1881`, Gob2 part only)

Scan up to the first free slot, skipping window ≠ 0 and disabled entries, keeping only those
whose rect contains (x, y):
- If the entry's flag cursor nibble is 0: an input type (≥ 3) → **3**, stop. Otherwise, if its
  button isn't "right" and nothing is chosen yet → 1.
- Otherwise, if nothing is chosen yet → that nibble.

Return 0 when nothing matched. `animateCursor` maps an undefined result to 1.

### 6.8 Keyboard, mouse and keyFunc

**`Game::checkKeys(&x, &y, &buttons, handleMouse)`** (`game.cpp:864-904`):
1. `processInput`.
2. If a mult is loaded and VAR(58) ≠ 0: advance and play one mult frame (VAR(57) = base frame).
   That belongs to the mult docs.
3. Stop a timed sound if due.
4. Read the mouse position and buttons. **Both buttons together are reported as none.**
5. Pop one key from the 16-entry buffer and translate it (0 if empty).

**Button encoding** (`util.h:46-52`): 0 none, 1 left, 2 right, 3 both, 4 "any" (hotspot
matching only). `Util::_mouseButtons` tracks press/release events; `Game::_mouseButtons` is
the value scripts see.

**Key codes** (`Util::translateKey`, `util.cpp:211-251`): BIOS-style `scan<<8 | ascii`.

| key | code |
|---|---|
| Backspace | 0x0E08 |
| Space | 0x3920 |
| Return | 0x1C0D |
| Escape | 0x011B |
| Delete | 0x5300 |
| Up | 0x4800 |
| Down | 0x5000 |
| Right | 0x4D00 |
| Left | 0x4B00 |
| F1..F10 | 0x3B00..0x4400 |
| printable ASCII 32..127 | plain ASCII, no scan code |
| Latin-1 160..255 | converted to CP850 |

- **ScummVM maps F5 to Escape** (`util.cpp:229`). Gob2's MENU has an F5 hotspot (key 0x3F00,
  the icon bar at y = 0). **A native port should send 0x3F00 for F5.**
- Ctrl+F/G/P are ScummVM-only (speed/pause).

**Short codes** stored in VAR(0) by `storeKey`:

| key | VAR(0) |
|---|---|
| Up | 0x0B |
| Down | 0x0A |
| Right | 0x09 |
| Left | 0x08 |
| Escape | 0x1B |
| Backspace | 0x19 |
| Delete | 0x1A |
| other, low byte ≠ 0 | key & 0xFF (so Return = 13, Space = 32) |
| other, low byte 0 (F-keys) | full value |

Hotspot key fields are compared against the **full** codes, not the short ones.

**`o1_keyFunc`** (func 0x14, `inter_v1.cpp:1350-1424`). Every call first runs `animPalette()`,
`blitInvalidated()` and `handleBusyWait()`. Then it reads `i16 cmd`:

| cmd | effect |
|---|---|
| 0 | 6.6 |
| −1 | nothing else |
| 1 | **poll**. `forceMouseUp(true)`: if the script already saw the current button state, clear it, so a press is reported in exactly one keyFunc 1. Then `checkKeys` into `_inter_mouseX/Y` and `_game->_mouseButtons`, then `storeKey` (VAR 0-4, 12, 1) |
| 2 | `processInput`, raw key into VAR(0) without shortening, flush. Not Gob2 |
| other | update the PC speaker; if cmd < 20, sleep cmd ms and set `_noBusyWait`; else `longDelay(cmd)` (retraces while waiting) |

Gob2 uses 1, 5, 0, 50, 100, 1000 and 5000.

**`waitMouseRelease(draw)`** (`util.cpp:402-414`): loop `checkKeys` into locals until no
button is down, running `animateCursor(2)` when draw ≠ 0. It does **not** clear
`_game->_mouseButtons`.

### 6.9 Text input (high level; Gob2 has one, in MENU/MENUSPE: type 4, string `s[532]`, font 0, back 0, front 8, block handleMouse 0)

**`handleInputs`** (`hotspots.cpp:1227-1320`):
- Redraws every enabled input (fill the rect with back colour, text vertically centred, font
  colour front).
- Zeroes VAR(17..56).
- Edits the current input with `updateInput`. Return moves to the next input, or returns
  `kKeyReturn` when there is only one. Up/Down move between inputs. A click on another input
  switches to it. F1-F10 return.

**`updateInput`** (`hotspots.cpp:972-1225`) edits the string variable in place.
- Field length is width / charWidth characters for monospaced fonts.
- The cursor is an underline (monospaced) or a 1-pixel bar (proportional), blinking with two
  300 ms checks per cycle.
- VAR(56) = cursor position.
- Left/Right move (falling through to Up/Down at the ends), Backspace/Delete edit, printable
  characters insert. When full, the last character is overwritten. Float types only accept
  `0-9 - . , +`.
- An overall `duration` budget (decremented by 600 per blink cycle) aborts the edit.

**`matchInputStrings`** (`hotspots.cpp:2338-2370`), run only for a type-20 trigger:
- For answer-list inputs, compare the typed string with each NUL-separated expected answer
  (after `cleanupStr` for types < 7). On a match, increment VAR(17) and set VAR(17+n) = 1.
- Other inputs get VAR(17+n) = 2.
- Finally VAR(17) = (all matched).

### 6.10 Global hotspots: `o2_addHotspot` / `o2_removeHotspot` (func 0x18/0x19). Not used by Gob2

**`addHotspot`** (`inter_v2.cpp:1184-1227`):
- Operands: `valexpr id`, then (with `funcPos` = this position) `valexpr left`, `top`, `w`,
  `h`, `flags`, then `i16 key`.
- key 0 becomes |id| + 41960.
- Negative left/top are clamped by shrinking.
- id < 0: add `0xD000 − id` (state 0xD) with the rect snapped out to multiples of 4 and +3
  margins.
- Else: add `0xE000 + id` (state 0xE).
- Both start **disabled**. A block's type 11/12 entry enables them for that block's duration,
  and `evaluate` disables them again afterwards.

**`removeHotspot`** (`inter_v2.cpp:1229-1240`): `valexpr id`.
- −2: remove all with state 0xD.
- −1: remove all with state 0xE.
- Otherwise remove id `0xE000 + id`.

---

## 7. Open questions and discrepancies

1. **The USA subtitle bit (1.5).** Should .USA items with bit 15 on `left` be drawn? I
   recommend yes (mask and draw). This needs confirming against the original US executable.
2. **palLoad bit 7 naming.** `docs/opcodes.md` §8.3 and `src/script/disasm.cpp:520` label
   bit 7 as "deferred". The code makes bit 7 mean an **immediate blocking fade**, and bit 7
   clear mean deferred to the next `blitInvalidated` (4.4). Fix the label and doc text.
3. **Retrace timing.** ScummVM's `waitRetrace` is a 10 ms sleep. The original probably waited
   for the 70 Hz VGA vertical retrace. This affects fade length (about 0.33 s vs 0.47 s) and
   the hotspot loop rate.
4. **Is the GOG data "CD"?** The Fingus/mayor id swap in GOB07 (1.3 step 1) is applied by
   ScummVM only to non-CD Gob2. Check whether the text ids in this GOB07 need it.
5. **keyFunc presentation.** ScummVM's `o1_keyFunc` only retraces indirectly (busy-wait
   throttle or `longDelay`). I recommend retracing on every keyFunc. Verify that no visual
   difference (e.g. intermediate frames meant to stay hidden) results.
6. **Mouse-button edge semantics** (`forceMouseUp(true)` in keyFunc 1) are a ScummVM
   reconstruction. The original read the button state level-triggered via INT 33h. Some
   `repeat keyFunc 1 until v[4] == 0` loops might behave differently. Test the intro skip and
   menus.
7. **Hardware vs software cursor.** The original v2 engine drew the cursor into the front
   buffer only when `animateCursor` ran. ScummVM uses a free-moving hardware cursor. The
   overlay recommendation (5.4) follows ScummVM.
8. `printTotText` dereferences the font without a null check (`draw_v2.cpp:400`). Harmless
   with Gob2 data (fonts 0/1 always loaded), but add a guard.

## 9. Screens larger than the display, the split bar and scrolling (Goblins 3)

Goblins 3 (`Inter_v3`, `Init_v3`) draws its rooms on screens larger than the 320x200
display and keeps a text bar at the bottom. The engine's version is in
`src/engine/interpreter_v3.cpp` (`initScreenV3`, `scrollV3`, `setScrollOffsetV3`,
`pollMouseV3`, `evaluateScroll`) and `src/engine/draw.{h,cpp}` (`initScreen(w, h, split)`,
`screenSurface()`); after `o2_initScreen`, `o2_scroll`, `o2_setScrollOffset`
(`inter_v2.cpp:791-950`), `Video::retrace` (`video.cpp:276`), `Util::setScrollOffset`,
`Util::getMouseState`, `Util::processInput` and `Game::evaluateScroll` (`game.cpp:804`).

- **initScreen** (draw 0x80): `i16` whose high byte is the split's height and low byte the
  video mode, then `val width, val height` (-1: unchanged). The rooms use `0x2814, 472,
  200`: video mode 0x14, a 472x200 screen, a 40-row split. EMAP1012 uses `0x2814, 320,
  240`; menus and cutscenes `0x14, 320, 200`. Both the front and the back surface get that
  size (in the original the front surface is the "primary" surface). The split starts at
  row `height - split` (160 or 200). The mouse is limited to rows `0..199 - split` (159):
  the pointer cannot enter the bar, which only shows text. The screen palette is cleared,
  the scroll offset reset, and VAR 15 is the video mode from then on (written at every
  script load, as the original's `_fakeVideoMode`).
- **The display** shows the front surface's window at the view offset (`viewX, viewY`,
  the original's `Video::_scrollOffsetX/Y`) in rows `0..199 - split`, and the front
  surface's rows from the split start, unscrolled, in the bottom `split` rows. The scripts'
  offset (`scrollX, scrollY`, `Draw::_scrollOffsetX/Y`) becomes the view offset whenever
  the original calls `Util::setScrollOffset`. `Draw::screenSurface()` composes this view
  (with its own HD layer, copied from the front surface's changed tiles or in full after a
  scroll) and returns it; for 320x200 screens it is the front surface itself.
- **setScrollOffset** (draw 0x82): `val x, val y`. x = -1 toggles edge scrolling off and on
  (`_preventScroll`) and writes the scripts' offset to VAR 2 and 3 (the scripts read it
  there, where the mouse position normally is); else the offset is set, clamped to
  `0..width - 320` (`0..320` on a 320-wide screen, as in the original) and shown.
- **scroll** (draw 0x81): `val startX, startY, endX, endY, stepX, stepY` (clamped to the
  scroll range): moves the offset step by step, each step shown about 10 ms after the
  previous one (the original's `waitRetrace`). A zero step towards a different end would
  loop forever in the original; the engine ignores such a call.
- **The mouse**: the scripts see the pointer on the screen surface (display position plus
  the view offset, VAR 2/3, hotspots); setMousePos (func 0x46) takes surface coordinates
  and moves the pointer to them minus the offset. A pointer outside the limits is moved
  back to them.
- **Edge scrolling** (`Game::evaluateScroll`, from the hotspot loop when it handles the
  mouse, in video modes 0x14/0x18, unless prevented): after a pointer move, x = 0 scrolls
  left by half a cursor width (at least one pixel), y = 0 up likewise; the cursor touching
  the right edge (x + cursor width >= 320) scrolls right and puts the pointer back at
  320 - cursor width, the bottom edge of the area above the bar likewise downwards. The
  original sees that move as a new one, so a touch of the right edge scrolls on to the end
  (8 pixels a frame with the 16-pixel cursor: 19 frames for the 152 pixels of a room).
  WORKAROUND (ScummVM): in EMAP1008 every move evaluates the scroll at once.
- Text cells for sharp text and the Tools tab's outlines follow the view offset.

Checked: `gobrun ../goblins3 --press-every 5 --press-stop 60 --move 90 319 100` scrolls
EMAP1000 by 152 pixels within a second, with the bar unchanged; `test_hdrender` covers a
472x200 screen with a split.
