# Gobliins 2 bytecode: operand layout of every opcode

This catalog covers the opcodes Gobliins 2 can execute. Gobliins 2 runs the `Inter_v2`
interpreter (`gob.cpp:558-569` pairs `kGameTypeGob2` with `Inter_v2`, `Mult_v2`, `Map_v2`,
`Goblin_v2`, `Scenery_v2`, `Draw_v2`). The source is ScummVM's `engines/gob`. File:line
citations point into ScummVM's `engines/gob` (in its source tree).
`v1:` means `inter_v1.cpp`, `v2:` means `inter_v2.cpp`.

Everything below describes how many bytes each handler takes from the script stream and in
what order. It is written for a static disassembler. Where the layout depends on runtime
state, the entry says so and gives the value that applies to Gobliins 2 (VGA, 256 colours).

**Validation.** The layouts were checked against the real game. A throwaway disassembler,
written straight from this catalog, walked every function reachable from the TOT function
tables and from `callSub` targets in all 32 extracted TOT files (all version 1.2) in
a folder with the files extracted from `INTRO.STK` (GOB00-22, INTRO0, MENU, ...). It parsed
1517 functions, 15176 code blocks and 34 hotspot blocks. For every block, the operands
ended exactly at the length stored in the block header, and the opcode count matched the
header count. Section 9 says which opcodes appear in the data and which are unconfirmed.
Section 10 gives the differences of version 1 (Gobliiins, TOT version 1.1), section 11
those of version 3 (Goblins Quest 3, TOT version 1.3).

---

## 0. Notation and encoding

| token | meaning |
|---|---|
| `u8` / `i8` | 1 raw byte, unsigned or signed |
| `i16` / `u16` | 2 raw bytes, little-endian |
| `i32` | 4 raw bytes, little-endian |
| `expr` | a full expression ending at stop token `99` (0x63). Read through `evalExpr`, `evalInt`, `evalBool`, `evalString` or `readExpr` |
| `valexpr` | `readValExpr`. Takes **exactly the same bytes** as `expr` (it is `parseExpr(99)`). The two are kept apart only to mirror the source |
| `varidx` | a variable reference (`readVarIndex`, i.e. `Expression::parseVarIndex`). Layout in section 7 |
| `str` | NUL-terminated string |
| `raw[n]` | n raw bytes |
| `block` | an embedded code block (section 5.1), skipped using its length field |

**Func opcode byte → table index** (`inter.cpp:315-327`). Let `b` be the opcode byte.
- If `b >> 4` is 0x0-0xB: index = `b >> 4`, so i=0 and j=`b>>4`. **The low nibble is ignored.**
- If `b >> 4` is 0xC-0xF: i = 16 - (`b >> 4`) and j = `b & 0xF`. That gives 0xF*→i=1, 0xE*→2, 0xD*→3, 0xC*→4.

Indexes 0x0C-0x0F can't be encoded at all. Unassigned func or draw slots log a warning,
read nothing more, and carry on (`inter.cpp:76-96`). Running into one is a desync.

Some marker bytes inside control structures land on unassigned func slots: `0x40-0x4F`
(switch default), `0xB0` (repeat), and `0xC0-0xCF` (else). Inside those structures they can't
be confused with real opcodes.

---

## 1. Func table (effective v2)

`setupOpcodesFunc`: v1:97-170, overridden by v2:118-141.

| idx | i.j | byte(s) | handler (file:line) | operands, in order | control flow / notes |
|---|---|---|---|---|---|
| 0x00 | 0.0 | 0x00-0x0F | o1_callSub v1:695 | `u16 offset` | **CALL** to absolute file offset `offset` in the current TOT. The target is a block (type 1 or 2, section 5). Offset < 128 is ignored. See 6.1 |
| 0x01 | 0.1 | 0x10-0x1F | o1_callSub v1:695 | `u16 offset` | same as 0x00 |
| 0x02 | 0.2 | 0x20-0x2F | o1_printTotText v1:759 | `i16 textId` | draws text item from TOT/locale text table (no further script reads) |
| 0x03 | 0.3 | 0x30-0x3F | o1_loadCursor v1:763 | `i16 resId`, `i8 cursorIndex` | |
| 0x04 | 0.4 | 0x40-0x4F | — unassigned — | | |
| 0x05 | 0.5 | 0x50-0x5F | o1_switch v1:790 + Inter_v2::checkSwitchTable v2:156 | special (6.5) | **SWITCH**. Case bodies are blocks |
| 0x06 | 0.6 | 0x60-0x6F | o1_repeatUntil v1:811 | `block`, `u8 marker`, `expr cond` | **REPEAT…UNTIL** (6.4) |
| 0x07 | 0.7 | 0x70-0x7F | o1_whileDo v1:849 | `expr cond`, `block` | **WHILE** (6.3) |
| 0x08 | 0.8 | 0x80-0x8F | o1_if v1:886 | `expr cond`, `block then`, `u8 elseMarker`, [`block else`] | **IF/ELSE** (6.2). The marker byte is always consumed |
| 0x09 | 0.9 | 0x90-0x9F | o2_assign v2:1040 | `varidx dest`, [`u8 99`, `u8 count`], `count × expr` | special (8.1). count defaults to 1 |
| 0x0A | 0.10 | 0xA0-0xAF | o1_loadSpriteToPos v1:1013 | `i16 spriteRes`, `valexpr x`, `valexpr y`, `u8 flags`, `u8 ignored` | flags bit0 = transparency. destSurface = (flags>>1)-1, and 101 if negative. A Gob1-EGA-only workaround (v1:1019) doesn't apply to Gob2 |
| 0x0B | 0.11 | 0xB0-0xBF | — unassigned — | | 0xB0 is the repeat marker |
| 0x0C-0x0F | — | (none) | — not encodable — | | |
| 0x10 | 1.0 | 0xF0 | — unassigned — | | |
| 0x11 | 1.1 | 0xF1 | o2_printText v2:1089 | `5 × valexpr` (x, y, backColor, frontColor, font), then inline text format | special (8.2) |
| 0x12 | 1.2 | 0xF2 | o1_loadTot v1:1092 | if next byte has bit 7 set: `u8 (skipped)`, `expr name`; else `u8 len`, `raw[len] name` | **LOAD TOT**: sets the next TOT (name + ".tot") and `_terminate=1`. The current block stops after this opcode and the TOT is unloaded. Treat as terminal |
| 0x13 | 1.3 | 0xF3 | o1_palLoad v1:1107 | `u8 cmd`, then cmd-dependent raw data | special (8.3) |
| 0x14 | 1.4 | 0xF4 | o1_keyFunc v1:1350 | `i16 cmd` | 0 = check hotspots, 1 = check keys, 2 = check key, -1 = nop, other = delay |
| 0x15 | 1.5 | 0xF5 | o1_capturePush v1:1426 | `4 × valexpr` (left, top, width, height) | |
| 0x16 | 1.6 | 0xF6 | o1_capturePop v1:1442 | — | |
| 0x17 | 1.7 | 0xF7 | o2_animPalInit v2:1163 | `i16 index`, `valexpr low`, `valexpr high` | all three branches (index >0, ==0, <0) read both expressions |
| 0x18 | 1.8 | 0xF8 | o2_addHotspot v2:1184 | `valexpr id`, `valexpr left`, `valexpr top`, `valexpr width`, `valexpr height`, `valexpr flags`, `i16 key` | Adds a hotspot without enter/leave functions. It remembers the position of `left` and later re-evaluates left..height (+flags when id ≥ 0) via `Hotspots::recalculate` hotspots.cpp:314. No layout impact |
| 0x19 | 1.9 | 0xF9 | o2_removeHotspot v2:1229 | `valexpr id` | |
| 0x1A | 1.10 | 0xFA | o2_getTotTextItemPart v2:1242 | `i16 textItem`, `varidx destStr`, `valexpr part` | reads the text resource only, not the script |
| 0x1B-0x1D | 1.11-1.13 | 0xFB-0xFD | — unassigned — | | |
| 0x1E | 1.14 | 0xFE | o1_drawOperations v1:1455 | `u8 drawOp`, then that draw opcode's operands (table 2) | |
| 0x1F | 1.15 | 0xFF | o1_setcmdCount v1:1463 | `u8 newCount` | Replaces the enclosing block's command count and resets the counter to 0. After it, exactly `newCount` more commands run (the block header's count no longer applies). Never seen in data |
| 0x20 | 2.0 | 0xE0 | o1_return v1:1468 | — | **RETURN**: ends the current block (and the function). Outside a top-level call it also sets `_break` |
| 0x21 | 2.1 | 0xE1 | o1_renewTimeInVars v1:1476 | — | |
| 0x22 | 2.2 | 0xE2 | o1_speakerOn v1:1480 | `valexpr frequency` | |
| 0x23 | 2.3 | 0xE3 | o1_speakerOff v1:1500 | — | |
| 0x24 | 2.4 | 0xE4 | o1_putPixel v1:1507 | `i16 surface`, `valexpr x`, `valexpr y`, `valexpr color` | |
| 0x25 | 2.5 | 0xE5 | o2_goblinFunc v2:1427 | `i16 cmd`, `i16 paramCount`, then gob opcode `cmd`'s operands (table 3), unless cmd == 101 | special (8.9) |
| 0x26 | 2.6 | 0xE6 | o1_createSprite v1:1569 | `3 × (i16 or valexpr)`, `i16 flags` | special (8.4): one peek decides raw or expression for all three |
| 0x27 | 2.7 | 0xE7 | o1_freeSprite v1:1595 | `i16 index` | |
| 0x28-0x2F | 2.8-2.15 | 0xE8-0xEF | — unassigned — | | |
| 0x30 | 3.0 | 0xD0 | o1_returnTo v1:1599 | — | **BREAK** out of the innermost repeat/while (6.6) |
| 0x31 | 3.1 | 0xD1 | o1_loadSpriteContent v1:1617 | `i16 spriteRes`, `i16 destSurface`, `i16 transparency` | |
| 0x32 | 3.2 | 0xD2 | o1_copySprite v1:1627 | `(i16 or valexpr) src`, `(i16 or valexpr) dest`, `6 × valexpr` (left, top, width, height, destX, destY), `i16 transparency` | special (8.4): src and dest are each decided by their own peek |
| 0x33 | 3.3 | 0xD3 | o1_fillRect v1:1672 | `i16 surface`, `5 × valexpr` (x, y, width, height, color) | |
| 0x34 | 3.4 | 0xD4 | o1_drawLine v1:1699 | `i16 surface`, `5 × valexpr` (x1, y1, x2, y2, color) | |
| 0x35 | 3.5 | 0xD5 | o1_strToLong v1:1711 | `varidx srcStr`, `varidx dest` | |
| 0x36 | 3.6 | 0xD6 | o1_invalidate v1:1722 | `i16 surface`, `4 × valexpr` | |
| 0x37 | 3.7 | 0xD7 | o1_setBackDelta v1:1731 | `valexpr dx`, `valexpr dy` | |
| 0x38 | 3.8 | 0xD8 | o1_playSound v1:1736 | `valexpr slot`, `valexpr repCount`, `valexpr frequency` | |
| 0x39 | 3.9 | 0xD9 | o2_stopSound v2:1441 | `valexpr` | |
| 0x3A | 3.10 | 0xDA | o2_loadSound v2:1454 → loadSound(0) v2:1669 | `valexpr slot`, `i16 id`, [`raw[9] filename` if id == -1] | special (8.5) |
| 0x3B | 3.11 | 0xDB | o1_freeSoundSlot v1:1788 → Game::freeSoundSlot game.cpp:791 | `valexpr slot` | |
| 0x3C | 3.12 | 0xDC | o1_waitEndPlay v1:1792 | — | |
| 0x3D | 3.13 | 0xDD | o1_playComposition v1:1796 | `varidx data`, `valexpr frequency` | |
| 0x3E | 3.14 | 0xDE | o2_getFreeMem v2:1458 | `varidx`, `varidx` | |
| 0x3F | 3.15 | 0xDF | o2_checkData v2:1471 | `expr filename`, `varidx result` | |
| 0x40 | 4.0 | 0xC0 | — unassigned — | | 0xC0-0xCF serve as the else marker after an if body |
| 0x41 | 4.1 | 0xC1 | o1_cleanupStr v1:1831 | `varidx str` | |
| 0x42 | 4.2 | 0xC2 | o1_insertStr v1:1838 | `varidx str`, `expr insert`, `valexpr pos` | |
| 0x43 | 4.3 | 0xC3 | o1_cutStr v1:1850 | `varidx str`, `valexpr pos`, `valexpr size` | |
| 0x44 | 4.4 | 0xC4 | o1_strstr v1:1861 | `varidx str`, `expr needle`, `varidx result` | |
| 0x45 | 4.5 | 0xC5 | o1_istrlen v1:1875 | `varidx str`, `varidx result` | |
| 0x46 | 4.6 | 0xC6 | o1_setMousePos v1:1886 | `valexpr x`, `valexpr y` | |
| 0x47 | 4.7 | 0xC7 | o1_setFrameRate v1:1906 | `valexpr` | |
| 0x48 | 4.8 | 0xC8 | o1_animatePalette v1:1910 | — | |
| 0x49 | 4.9 | 0xC9 | o1_animateCursor v1:1918 | — | |
| 0x4A | 4.10 | 0xCA | o1_blitCursor v1:1922 | — | |
| 0x4B | 4.11 | 0xCB | o1_loadFont v1:1926 | `expr filename`, `i16 index` | |
| 0x4C | 4.12 | 0xCC | o1_freeFont v1:1934 | `i16 index` | |
| 0x4D | 4.13 | 0xCD | o2_readData v2:1505 | `expr filename`, `varidx dest`, `valexpr size`, `expr offset` | offset is read with evalInt, still a stop-99 expression |
| 0x4E | 4.14 | 0xCE | o2_writeData v2:1585 | `expr filename`, `varidx src`, `valexpr size`, `expr offset` | |
| 0x4F | 4.15 | 0xCF | o1_manageDataFile v1:1993 | `expr filename` | empty string closes the archive |

v1 handlers overridden by v2, and so not reachable: o1_assign (0x09), o1_printText (0x11),
o1_animPalInit (0x17), o1_goblinFunc (0x25), o1_stopSound (0x39), o1_loadSound (0x3A),
o1_getFreeMem (0x3E), o1_checkData (0x3F), o1_readData (0x4D), o1_writeData (0x4E).

---

## 2. Draw table (reached via func 0x1E + `u8 drawOp`)

`setupOpcodesDraw`: v1:61-95, then v2:64-116 overrides and adds entries. Any drawOp not
listed is unassigned.

| op | handler (file:line) | operands, in order | notes |
|---|---|---|---|
| 0x00 | o1_loadMult v1:289 → Mult_v2::loadMult mult_v2.cpp:63 | `i16 resId`, [`u8 multIndex` if resId & 0x8000], then **data-dependent** scenery/sound/IMD descriptors | special (8.6). The length depends on the contents of the mult resource |
| 0x01 | o2_playMult v2:245 | `i16 checkEscape` | bit0 = escape allowed, value>>1 = mult index |
| 0x02 | o2_freeMultKeys v2:254 | `u16 multIndex` | |
| 0x07 | o1_initCursor v1:335 | `varidx hotspotXVar`, `varidx hotspotYVar`, `i16 width`, `i16 height`, `i16 count` | |
| 0x08 | o1_initCursorAnim v1:396 | `valexpr index`, `i16 low`, `i16 high`, `i16 delay` | |
| 0x09 | o1_clearCursorAnim v1:406 | `valexpr index` | |
| 0x0A | o2_setRenderFlags v2:265 | `valexpr flags` | |
| 0x10 | o1_loadAnim v1:420 → Scenery::loadAnim scenery.cpp:437 (search=0) | `expr sceneryIndex`, `i16 picsCount`, `i16 resId`, `picsCount × {i16 pictDescId, i16 width, i16 height, i16 sprResId}` | 8 bytes per picture. See 8.7 for the edge case |
| 0x11 | o1_freeAnim v1:424 → Scenery::freeAnim scenery.cpp:561 | `expr index` | |
| 0x12 | o1_updateAnim v1:428 | `expr deltaX`, `expr deltaY`, `expr animation`, `expr layer`, `expr frame`, `i16 flags` | |
| 0x13 | o2_multSub v2:280 → Mult_v2::multSub mult_v2.cpp:391 | `valexpr flagsAndIndex`, then `4 × valexpr` | The 4 trailing expressions are read on both paths (mult loaded or not): 3 anim-object numbers + start frame |
| 0x14 | o2_initMult v2:284 | `i16 left`, `i16 top`, `i16 width`, `i16 height`, `i16 objCount`, `varidx posXVar`, `varidx posYVar`, `varidx animDataVar` | |
| 0x15 | o1_freeMult v1:541 | — | |
| 0x16 | o1_animate v1:545 | — | |
| 0x17 | o2_loadMultObject v2:398 | `valexpr objIndex`, `valexpr posX`, `valexpr posY`, then 11 slots, each either `u8 99` (keep the field) or `valexpr` | special (8.8) |
| 0x18 | o1_getAnimLayerInfo v1:572 | `expr anim`, `expr layer`, `4 × varidx` (dx, dy, unk, frames) | |
| 0x19 | o1_getObjAnimSize v1:591 | `expr objIndex`, `4 × varidx` (left, top, right, bottom) | |
| 0x1A | o1_loadStatic v1:626 → Scenery::loadStatic scenery.cpp:113 (search=0) | `expr sceneryIndex`, `i16 backCount`, `backCount × i16 backResId`, `i16 picsCount`, `i16 resId`, `picsCount × {i16 pictDescId, i16 width, i16 height, i16 sprResId}` | See 8.7 |
| 0x1B | o1_freeStatic v1:630 → Scenery::freeStatic scenery.cpp:226 | `expr index` | |
| 0x1C | o2_renderStatic v2:495 | `valexpr index`, `valexpr layer` | |
| 0x1D | o2_loadCurLayer v2:504 | `valexpr static`, `valexpr layer` | |
| 0x20 | o2_playCDTrack v2:509 | `expr trackName` | |
| 0x21 | o2_waitCDTrackEnd v2:516 | — | overrides o1_getCDTrackPos |
| 0x22 | o2_stopCD v2:523 | — | |
| 0x23 | o2_readLIC v2:527 | `expr filename` | ".LIC" is appended |
| 0x24 | o2_freeLIC v2:534 | — | |
| 0x25 | o2_getCDTrackPos v2:538 | `varidx pos`, `varidx name` | |
| 0x30 | o2_loadFontToSprite v2:551 | `i16 index`, then `u8 sprite, u8 pad, u8 base, u8 pad, u8 width, u8 pad, u8 height, u8 pad` | 10 bytes total. Pad bytes are skipped (they're the high bytes of 16-bit values) |
| 0x31 | o1_freeFontToSprite v1:687 | `i16 index` | |
| 0x40 | o2_totSub v2:564 | `u8 lenOrFlag`; if bit7 set → `expr name`, else `raw[lenOrFlag] name`; then `u8 flags` | **CALL TOT** (6.7). (lenOrFlag & 0x7F) must be ≤ 13 |
| 0x41 | o2_switchTotSub v2:597 | `i16 envIndex`, `i16 function` | **CALL into a stacked TOT environment** (6.7) |
| 0x42 | o2_pushVars v2:607 | `u8 count`, then per item: if next byte is 25 or 28 → `varidx` + `u8 (skipped, 99 in data)`; else `expr` | |
| 0x43 | o2_popVars v2:629 | `u8 count`, `count × varidx` | |
| 0x50 | o2_loadMapObjects v2:638 → Map_v2::loadMapObjects map_v2.cpp:50 | `varidx`, `i16 id`; if (u16)id < 65520: `i16 soundCount`, `soundCount × {i16 sndId, [raw[9] name if sndId == -1]}` | special (8.10) |
| 0x51 | o2_freeGoblins v2:642 | — | |
| 0x52 | o2_moveGoblin v2:646 | `valexpr destX`, `valexpr destY`, `valexpr index` | |
| 0x53 | o2_writeGoblinPos v2:656 | `varidx varX`, `varidx varY`, `valexpr index` | |
| 0x54 | o2_stopGoblin v2:667 | `valexpr index` | |
| 0x55 | o2_setGoblinState v2:673 | `valexpr index`, `valexpr state`, `valexpr type` | |
| 0x56 | o2_placeGoblin v2:778 | `valexpr index`, `valexpr x`, `valexpr y`, `valexpr state` | |
| 0x80 | o2_initScreen v2:791 | `i16 modeWord` (low byte = video mode, high byte = offY), `valexpr width`, `valexpr height` | |
| 0x81 | o2_scroll v2:886 | `6 × valexpr` (startX, startY, endX, endY, stepX, stepY) | |
| 0x82 | o2_setScrollOffset v2:922 | `valexpr x`, `valexpr y` | |
| 0x83 | o2_playImd v2:951 | `expr name`, `8 × valexpr` (x, y, startFrame, lastFrame, breakKey, flags, palStart, palEnd) | |
| 0x84 | o2_getImdInfo v2:1000 | `expr name`, `5 × varidx` (x, y, frames, width, height) | |
| 0x85 | o2_openItk v2:1018 | `expr name` | |
| 0x86 | o2_closeItk v2:1026 | — | |
| 0x87 | o2_setImdFrontSurf v2:1034 | — | empty handler |
| 0x88 | o2_resetImdFrontSurf v2:1037 | — | empty handler |

Unassigned drawOps include 0x03-0x06, 0x0B-0x0F, 0x1E, 0x1F, 0x26-0x2F, 0x32-0x3F,
0x44-0x4F, 0x57-0x7F and 0x89-0xFF. They read nothing more.

---

## 3. Gob table (reached via func 0x25)

`o2_goblinFunc` (v2:1427) reads `i16 cmd` then `i16 paramCount`. If cmd == 101 it stops
there. Otherwise it dispatches `cmd` here (`Inter::executeOpcodeGob` inter.cpp:98).
`Inter_v2::setupOpcodesGob` (v2:143-154) **doesn't chain to Inter_v1's table**. The
~70 `o1_*` goblin opcodes from v1:172-244 (and their `cmd>90`/`cmd>110` remapping in
o1_goblinFunc) are therefore **unreachable in Gobliins 2**.

| cmd | handler (file:line) | operands after cmd + paramCount | notes |
|---|---|---|---|
| 0 | o2_loadInfogramesIns v2:1614 | `i16 varNum` | the file name is the string in variable `varNum` (byte offset varNum*4) + ".INS" |
| 1 | o2_startInfogrames v2:1639 | `i16 (ignored)` | |
| 2 | o2_stopInfogrames v2:1645 | `i16 (ignored)` | |
| 10 | o2_playInfogrames v2:1626 | `i16 varNum` | string variable + ".DUM" |
| 100 | o2_handleGoblins v2:1659 | `6 × i16`: gob1NoTurnVar, gob2NoTurnVar, gob1RelaxTimeVar, gob2RelaxTimeVar, gob1BusyVar, gob2BusyVar | These are raw variable **numbers**, not varidx. Numbers 1, 2, 5 and 6 are read immediately as `VAR(n)`. Numbers 3 and 4 are stored and used later by Goblin_v2. The only gob call seen in the data, always with paramCount = 6 |
| 101 | (handled in o2_goblinFunc) | nothing | not dispatched |
| 500 | o2_playProtracker v2:1651 | — | |
| 501 | o2_stopProtracker v2:1655 | — | |
| any other | unimplemented | `raw[paramCount × 2]` skipped | inter.cpp:112 |

**Implemented gob handlers ignore `paramCount`.** They read only what the table says, so a
paramCount that disagrees with the handler would desync. In the data, paramCount always
matched.

---

## 4. TOT file and function structure (context)

- The 128-byte header holds 14 `u16` function entry offsets at file offset 100 + 2*i
  (`totfile.cpp:100`). [0] is the start function run when the TOT loads (`game.cpp:632`).
  [13] is the "center text" helper that `Draw::printTextCentered` / `drawButton` run with
  `funcBlock` (`draw.cpp:461-472`, `519-541`). Others are used by `o2_switchTotSub` / `o2_totSub`
  (6.7). Unused entries are 0.
- Bytecode lives in [128, scriptEnd), where scriptEnd = min(file size, text table offset,
  resource table offset). A seek below 128 marks the script finished
  (`Script::seek` script.cpp:102-126).
- A **function** is a single block at its entry offset: a code block (type 1) or a
  hotspot block (type 2). `Inter::callSub` (inter.cpp:348-368) keeps executing blocks while
  the script isn't finished. A type-1 block always marks the script finished when it ends
  (inter.cpp:345), so in practice a function is one block.
  - In the data, every hotspot block was a whole function, and the bytes right after
    each one were the start of another function (a callSub target or table entry).
    Disassemble each function from its entry, and collect callSub / function-table
    targets as new entry points.

---

## 5. Block layouts

### 5.1 Code block (type 1). Interpreted by `Inter::funcBlock` (inter.cpp:232-346)

| offset | size | field |
|---|---|---|
| +0 | u8 | block type = `0x01` (skipped without checking in funcBlock) |
| +1 | u8 | cmdCount = number of opcodes to execute. 0 = empty block (finishes immediately) |
| +2 | u16 | `len`. **Total block length = len + 2**, measured from +0 (so `len` counts from +2 to the end) |
| +4 | … | cmdCount opcodes |

- funcBlock **doesn't use `len`**. It runs exactly `cmdCount` opcodes, unless one of them
  returns, breaks, terminates, or resets the count via 0x1F.
- Every skip uses `len`: `Script::skipBlock` (script.cpp:132) and the inline equivalents in
  if/while/repeat/switch/hotspots all seek to start + u16@(start+2) + 2.
- Data check: for all 15176 blocks, parsing `cmdCount` opcodes ended exactly at start+len+2.

### 5.2 Hotspot block (type 2). Interpreted by `Hotspots::evaluate` (hotspots.cpp:1653) and `evaluateNew` (hotspots.cpp:1322)

`Inter::callSub` jumps here when the block-type byte is 2 (inter.cpp:357-361). **There's
no length field.** To skip the block you have to walk every entry.

Header (8 bytes):

| offset | size | field |
|---|---|---|
| +0 | u8 | `0x02` |
| +1 | u8 | `count`: number of hotspot entries that follow |
| +2 | u8 | handleMouse |
| +3 | u8 | duration: whole seconds. If +5 or +6 is non-zero, units of 10 ms instead, and the value 100 then means 2 ms |
| +4 | u8 | leaveWindowIndex (used only by Fascination; ignored for Gob2) |
| +5 | u8 | hotspotIndex1 |
| +6 | u8 | hotspotIndex2 |
| +7 | u8 | needRecalculation (non-zero → recalculate all hotspots) |

Then `count` entries. Each entry:

1. `u8 type`.
2. If `type & 0x40`: `u8 windowNum` follows, and 0x40 is removed from type.
3. Coordinates:
   - if `type & 0x80`: `valexpr left`, `valexpr top`, `valexpr width`, `valexpr height`.
     The position of `left` is saved for later re-evaluation.
   - else: `u16 left`, `u16 top`, `u16 width`, `u16 height` (8 raw bytes). left == 0xFFFF
     means "no position".
4. `type &= 0x7F` (only bits 0-5 remain), then by type:

| type | name (hotspots.h:45-58) | remaining bytes of the entry |
|---|---|---|
| 0 | kTypeNone | `raw[6]` (skipped), `block enter`, `block leave` |
| 1 | kTypeMove | `i16 key`, `i16 id`, `i16 flags`, `block enter`, `block leave` |
| 2 | kTypeClick | `i16 key`, `i16 id`, `i16 flags`, `block leave` |
| 3 | kTypeInput1NoLeave | `varidx dest`, `i16 fontIndex`, `u8 backColor`, `u8 frontColor` |
| 4 | kTypeInput1Leave | as 3, then `block leave` |
| 5 | kTypeInput2NoLeave | as 3, then `u16 n`, `raw[n]` (input format/initial string) |
| 6 | kTypeInput2Leave | as 5, then `block leave` |
| 7 | kTypeInput3NoLeave | same as 5 |
| 8 | kTypeInput3Leave | same as 6 |
| 9 | kTypeInputFloatNoLeave | same as 3 |
| 10 | kTypeInputFloatLeave | same as 4 |
| 11 | kTypeEnable2 | `raw[6]` (skipped), `block` (becomes enter and leave of every re-enabled 0xE-state hotspot, e.g. ones made by func 0x18 with id ≥ 0) |
| 12 | kTypeEnable1 | `raw[6]` (skipped), `block` (as 11, for 0xD-state hotspots, e.g. func 0x18 with id < 0) |
| 20 | (click, also sets inputId) | same as 2 |
| 21 | kTypeClickEnter | `i16 key`, `i16 id`, `i16 flags`, `block enter` |
| 13-19, 22-63 | — | **nothing more is read** (the switch falls to its default case). Undefined and never seen in data |

Rule for input types 3-10: a leave block is present iff `(type & 1) == 0`. That holds both
with and without coordinates (hotspots.cpp:1509-1527).

Embedded blocks inside entries are skipped with `skipBlock` (u16 at +2, plus 2), so they
must be **type-1 code blocks**. A hotspot block has no length at +2. In the data all 13202
nested blocks (if/while/repeat/switch bodies and hotspot enter/leave blocks) were type 1.

Control flow of a hotspot block:
- Building the entries doesn't execute anything. Then the engine waits for input.
- An **enter** block runs as a subroutine (`Hotspots::call` hotspots.cpp:519 → `funcBlock(0)`)
  and control comes back to the wait loop.
- When a hotspot is chosen, the engine **jumps** (seeks) to that hotspot's **leave** block,
  and `callSub` carries on from there. The leave block replaces the rest of the function.
  A leave offset of 0, or a timeout, just finishes the function (hotspots.cpp:1782-1792).
- So nothing after the last entry runs in sequence. Treat the hotspot block as terminal
  and disassemble every enter and leave block as a sub-block.
- In Gob2 data the type histogram was: 1 = 462, 2 = 98, 0 = 30, 4 = 2. None used the 0x40 window flag.

---

## 6. Control-flow opcodes in detail

### 6.1 Call (func 0x00 / 0x01, o1_callSub v1:695)
`u16 offset`. Pushes the return position, seeks to `offset`, and runs blocks there via
`callSub(2)` until finished, then returns. Offset < 128 is ignored. A Gob2-specific
copy-protection skip (offsets 1722/1746 in intro0.tot) doesn't change the layout.
Tail-call case: if this is the last command of a block that runs with retFlag 2, the return
address is dropped and the target block replaces the current function. The layout is the same.

### 6.2 If / else (func 0x08, o1_if v1:886)
Layout: `expr cond` · `block THEN` · `u8 marker` · [`block ELSE` if `marker >> 4 == 0xC`].

- The marker byte **is always consumed**, even when there is no else. In the data it was
  `0x00` (no else, 7366×) or `0xC0` (else, 886×).
- True: THEN runs as a nested block (push/pop), then skip THEN, read the marker, and if an
  else is present skip ELSE.
- False: skip THEN, read the marker, and if an else is present run ELSE then skip it.
- Next instruction = after ELSE if present, else right after the marker.
- Tail position (last command, retFlag 2): the engine leaves the script at the start of the
  chosen block and lets `callSub` run it as the remainder of the function. Same layout.

### 6.3 While (func 0x07, o1_whileDo v1:849)
Layout: `expr cond` · `block BODY`. Loop: evaluate cond; if true, run BODY and seek back to
cond; if false, skip BODY. A break/terminate inside BODY skips to after BODY. No extra
marker byte.

### 6.4 Repeat…until (func 0x06, o1_repeatUntil v1:811)
Layout: `block BODY` · `u8 marker` · `expr cond`. Loop: run BODY, seek to
BODY_start + (len+2) + 1 (this skips the marker), evaluate cond, and repeat while cond is
false. The marker was always `0xB0` in the data (491×). The next instruction is right
after `cond`.

### 6.5 Switch (func 0x05, o1_switch v1:790 with Inter_v2::checkSwitchTable v2:156)
Layout:
1. `varidx` (the switch variable; the first byte gives its width: int8, int32 or int16).
2. Repeated case groups:
   - `i8 n`. If n == -5 (0xFB), the case list ends.
   - Otherwise `n` case values, each encoded as one of:
     - `0x13` + `i32` + 1 byte → 6 bytes (immediate int32)
     - `0x14` + `i16` + 1 byte → 4 bytes (immediate int16)
     - `0x15` + `i8`  + 1 byte → 3 bytes (immediate int8)
     - anything else → a full `expr` (stop 99)

     The trailing byte after an immediate is the expression terminator 99. The engine
     skips it blindly. In the data it was always 99 (2366×), so a value that starts with
     an immediate but continues with operators would desync the engine.
   - then `block CASEBODY` (skipped with len+2).
3. After the -5: if the next byte has `byte >> 4 == 4` (0x40 in data, 709×), it's a
   default: `u8 marker`, then `block DEFAULT`. Otherwise **nothing is consumed**.

Execution: the first matching case body (or DEFAULT) runs as a nested block. Execution then
continues after the whole switch (after DEFAULT if present, else after the -5 byte). If
nothing matches and there's no default, the body call is a no-op. Tail position works the
same way as for if.

### 6.6 Return / break
- 0x20 `o1_return`: finishes the current block, and so the function. Under a nested
  if/switch body or a loop body it also sets `_break` so the enclosing blocks stop.
- 0x30 `o1_returnTo`: directly inside a repeat/while body (retFlag 1) it breaks that loop.
  In a deeper nested block it records the nesting level and breaks out to that loop.
  At nesting level 0 it does nothing.
- 0x12 `o1_loadTot`: sets `_terminate`. The current block ends after this opcode, and the
  engine loads the new TOT.
- Neither return nor break takes operands.

### 6.7 Calls into other TOTs
- **draw 0x40 o2_totSub** (v2:564 → Game::totSub game.cpp:947). Name = raw characters
  (count = low 7 bits of the first byte) or a string expression (bit 7 set). Then `u8 flags`.
  Loads `<name>.TOT` in a new environment, runs it, and returns. Flags 16/17 are prefetch
  no-ops. Bit 0x80 = reuse an existing environment of the same TOT. Bit 0x20 = don't push
  hotspots. In the data, flags were 2 and 3.
- **draw 0x41 o2_switchTotSub** (v2:597 → Game::switchTotSub game.cpp:1069). `i16 index`,
  `i16 function`. Runs `function` inside an already stacked environment, then returns.
  In `Game::playTot` (game.cpp:689-700), function > 13 is an absolute offset; otherwise
  entry `function + 1` of that TOT's function table is used.

### 6.8 Expression-level calls
None. Expressions never call script code.

---

## 7. Expression and variable-reference byte layout (for skipping)

Token values come from `expression.h`. The byte-exact skipper is `Expression::skipExpr`
(expression.cpp:111-208). `parseExpr` (expression.cpp:1056) consumes the same bytes, with
one exception noted under OP_FUNC.

### 7.1 Expression = sequence of tokens up to the stop token (99 unless noted)

| token | following bytes |
|---|---|
| 1-8, 11 (NEG ADD SUB BITOR MUL DIV MOD BITAND NOT) | none |
| 9 BEGIN_EXPR `(` | none (raises the nesting level) |
| 10 END_EXPR `)` | none (lowers the nesting level) |
| 30-37 (OR AND LESS LEQ GREATER GEQ EQ NEQ) | none |
| 14 (var base: direct offset) | `i16 offset`, `u16 size`, optional `u8 97` |
| 15 (var base: offset from array) | `i16 offset`, `u16 elemSize`, `u8 dimCount`, `raw[dimCount] dims`, `dimCount × index-expr (stop 12)`, optional `u8 97` |
| 16 ARRAY_INT8, 26 ARRAY_INT32, 27 ARRAY_INT16 | `i16 base`, `u8 dimCount`, `raw[dimCount] dims`, `dimCount × index-expr (stop 12)` |
| 28 ARRAY_STR | as 16, then optional `u8 13` + char-index-expr (stop 12) |
| 17 VAR_INT16, 18 VAR_INT8, 23 VAR_INT32, 24 VAR_INT32_AS_INT16 | `u16 varNum` |
| 25 VAR_STR | `u16 varNum`, then optional `u8 13` + char-index-expr (stop 12) |
| 19 IMM_INT32 | `i32` |
| 20 IMM_INT16 | `i16` |
| 21 IMM_INT8 | `i8` |
| 22 IMM_STR | `str` (NUL-terminated) |
| 29 FUNC | `u8 funcId` (0/1/6 = sqrt, 5 = sqr, 7 = abs, 10 = rand), then argument tokens up to the closing `10` |
| 99 (or the stop token) | end of expression (consumed) |
| any other byte (12, 13, 97, …) | consumed, no operand |

- The stop token is consumed. Index sub-expressions use stop 12 (OP_END_MARKER).
- **OP_FUNC caveat**: `skipExpr` ends the argument at the first *unmatched* `10`, but
  `parseExpr(10)` returns at the first `10` of any kind. The two disagree only if the
  argument contains parentheses. Gob2 data has just 2 FUNC tokens (both rand, no inner
  parentheses).
- The `or`/`and` short-circuit inside parseExpr skips with skipExpr, which consumes the same bytes.

### 7.2 varidx (`Expression::parseVarIndex` expression.cpp:619 with `getVarBase` expression.cpp:549)

1. Prefix loop, while the next byte is 14 or 15:
   - `14`: `i16 offset`, `u16 size`. If the next byte is 97, consume it and continue the
     loop. Otherwise **the reference ends here** (just the prefix).
   - `15`: `i16 offset`, `u16 elemSize`, `u8 dimCount`, `raw[dimCount]`, `dimCount × expr(stop 12)`.
     Then the same 97 rule.
2. `u8 type`, then:
   - 16/26/27/28: `i16 base`, `u8 dimCount`, `raw[dimCount]`, `dimCount × expr(stop 12)`.
     For 28 only, optional `u8 13` + `expr(stop 12)`.
   - 17/18: `u16 varNum`.
   - 23/24/25: `u16 varNum`. For 25 only, optional `u8 13` + `expr(stop 12)`.
   - anything else: just that 1 byte (returns offset 0). Never seen in data.

Handlers that branch on "destination type" (o2_assign, o2_printText, the v2 switch) look
at the **first** byte of the varidx.

Observed in the data: type bytes 23 (17205×), 18 (2661×), 25 (1072×), 28 (466×), 26 (310×).
No 14/15 prefixes occurred.

---

## 8. Irregular opcodes in full

### 8.1 func 0x09 o2_assign (v2:1040)
`varidx dest` (remember its first byte as destType). Then, if the next byte is `99`:
consume it and read `u8 count`; otherwise count = 1. Then `count × expr`.

Value i goes to dest + i × element size, where the size is 1, 2, 4, or 4 for the
int32-as-int16 type. For string destinations (types 25/28) every iteration writes `dest`
itself. In the data, count was always 11 when present (335×).

### 8.2 func 0x11 o2_printText (v2:1089)
`5 × valexpr` (x, y, backColor, frontColor, fontIndex), then a sequence of chunks
terminated by the byte `200` (0xC8).

Chunk loop:
1. Copy literal bytes until the next byte is `'.'` (0x2E) or `200`, at most 59 per chunk.
2. If the next byte isn't `200`:
   - consume one byte (the `.`).
   - If the following byte is a variable type (16, 17, 18, 23, 24, 25, 26, 27 or 28):
     `varidx`, whose value is printed.
   - Then consume **one more byte**. This was always 99 in the data (224×). If the byte
     after `.` isn't a variable type, this second consumption eats that byte instead.
3. Repeat while the next byte isn't `200`, then consume the `200`.

A literal `.` can't appear in the text. Edge case: if 59 literal bytes appear with no `.`
or `200`, the engine treats the 60th byte as the separator. This never happens in the data.
(v1's o1_printText differs and isn't used by Gob2.)

### 8.3 func 0x13 o1_palLoad (v1:1107)
`u8 cmd`. Bit 7 set = fade to the new palette immediately (blocking); bit 7 clear = apply it with a hard cut at the next `blitInvalidated` (see docs/graphics.md). The data that follows depends on
`cmd & 0x7F`, and for some commands on runtime video state. With Gob2's VGA state
(colour count 256, video mode ≥ 0x13) the sizes are:

| cmd&0x7F | following bytes |
|---|---|
| 48 (0x30) | `raw[48]` (always skipped) |
| 49 | `raw[18]` (skipped unless in EGA modes 5/7. In those modes ScummVM peeks 16 bytes and then consumes either 48 or 16 bytes, which is inconsistent. Not relevant on VGA) |
| 50 | `raw[16]` (skipped when 256 colours) |
| 51 | `raw[2]` (skipped for mode < 0x64) |
| 52 | `raw[48]` (skipped when 256 colours. Otherwise 16 RGB triples, also 48 bytes) |
| 53 | `i16 resId` (palette resource) |
| 54 | nothing (clear palette) |
| 55 | `raw[2]` (push palette, bytes ignored) |
| 56 | `u8 first`, `u8 last` (pop palette range) |
| 61 | `u8 first`, `u8 last`, `i16 resId` (skipped as 4 bytes when mode < 0x13) |
| any other | nothing |

Only 0xB4 (52), 0xB5 (53) and 0xB6 (54) appear in the Gob2 data.

### 8.4 func 0x26 o1_createSprite (v1:1569) and func 0x32 o1_copySprite (v1:1627)
"Raw or expression" is decided by peeking the byte **at offset +1** from the current position:
- If that byte is `0`, the operand is a raw `i16`. Otherwise it's a `valexpr`.

createSprite does the peek **once** and applies it to all three of index, width and height
(3 × i16 or 3 × valexpr), then reads `i16 flags`. copySprite peeks separately for src and
for dest, then reads 6 valexpr and an `i16 transparency`.

A disassembler must replicate this test exactly. An expression whose second byte is 0
(e.g. `0x15 0x00 0x63`, immediate 0) gets read as a raw i16 by the engine. Seen in the data:
2 createSprite, both raw. copySprite: 36.

### 8.5 func 0x3A o2_loadSound → `Inter_v2::loadSound(0)` (v2:1669)
`valexpr slot` (negative = AdLib), `i16 id`. If id == -1: `raw[9]` filename. Otherwise the
id is a resource. The search variant (`loadSound(1)`, used by 8.6 and 8.10) has **no slot
expression**: `i16 id`, then `raw[9]` if id == -1. It also returns before reading the
9 bytes if no free sound slot exists (runtime edge case).

### 8.6 draw 0x00 o1_loadMult → Mult_v2::loadMult (mult_v2.cpp:63) + loadImds (mult_v2.cpp:254). **DATA-DEPENDENT**
Script bytes: `i16 resId`. If resId & 0x8000: `u8 multIndex`. Then resId &= 0x7FFF.

The rest depends on the **mult resource** `resId`. In Gob2 these were all EXT resources
30000+, some LZSS-packed. Parse the resource as follows; the script reads are interleaved
where marked **[S]**:

1. `s8 sc`, `s8 ac`. Compute staticCount = (u8)(sc+1) and animCount = (u8)(ac+1).
   hasImds = staticCount & 0x80, then staticCount &= 0x7F.
2. For each static: **[S]** one scenery-static descriptor, same layout as draw 0x1A:
   `expr`, `i16 backCount`, `backCount × i16`, `i16 picsCount`, `i16 resId`, `picsCount × 8 bytes`.
   The resource advances 14 bytes per static.
3. For each anim: **[S]** one scenery-anim descriptor, same layout as draw 0x10:
   `expr`, `i16 picsCount`, `i16 resId`, `picsCount × 8 bytes`. The resource advances 14 bytes per anim.
   These run with search=1: if a static/anim with the same resId is already loaded, the
   engine skips 8 × (previously stored picsCount), which is the same size in practice.
4. Resource only:
   - `i16 frameRate`
   - `i16 n` + n×4 static keys
   - 4 × (`i16 n` + n×10 anim keys)
   - 240 bytes of fade palettes
   - `i16 n` + n×7 pal-fade keys
   - `i16 n` + n×80 pal keys
   - `i16 n` + n × (4 + (hasImds ? 0 : 24)) text keys
5. Sound keys: `i16 n`, then for each key the resource has `i16 frame`, `i16 cmd`,
   `raw[6]`, `raw[2]`, and `raw[24]` if !hasImds.
   - cmd 1 or 4: **[S]** peek `u16 resId`. If an earlier sound key in this list had the
     same resId (only keys with cmd 1/4 carry one), **[S]** skip 2 bytes. Otherwise
     **[S]** `loadSound(1)`: `i16 id`, plus `raw[9]` if id == -1.
   - cmd 3: **[S]** `raw[4]`.
   - cmd -1 or other: nothing.
6. If hasImds: **[S]** `i16 n`, `raw[2n]`. Then, only if the TOT version minor (header
   byte 41 minus '0') ≥ 3: resource `i16 m` + m×20 bytes, then **[S]** `i16 k`, and
   `raw[14k]` if k > 0. Gob2 TOTs are version 1.2, so this second part is skipped.
7. If the resource can't be loaded, the engine returns right after the resId/index bytes
   and consumes nothing else (desync).

In the data: 25 loadMult calls. All had hasImds set and staticCount 0 with 1-6 anims;
sound-key cmds were 1 (6346×) and -1. Every one parsed exactly with this procedure.

### 8.7 draw 0x1A o1_loadStatic / draw 0x10 o1_loadAnim (scenery.cpp:113 / 437)
The layouts are in table 2. Edge case: the engine reads the header (expr, counts, resId)
and then loads the resource. If the resource is missing, it returns **before** reading the
per-picture records (picsCount × 8 bytes), and the script desyncs. A static disassembler
should always consume picsCount × 8.

### 8.8 draw 0x17 o2_loadMultObject (v2:398)
`valexpr objIndex`, `valexpr posX`, `valexpr posY`, then 11 slots. Each slot: if the next
byte is `99`, consume that one byte (the field is left unchanged); otherwise read a
`valexpr`. Note that v1 peeks a u16 instead; that version isn't used by Gob2.

### 8.9 func 0x25 o2_goblinFunc (v2:1427)
`i16 cmd`, `i16 paramCount`. If cmd == 101, nothing more. Otherwise the operands listed in
table 3 follow, or `raw[2 × paramCount]` for an unimplemented cmd.

### 8.10 draw 0x50 o2_loadMapObjects → Map_v2::loadMapObjects (map_v2.cpp:50)
`varidx`, `i16 id`.
- If (u16)id ≥ 65520 (special commands 65529-65535 that act on the variable): nothing more.
- Otherwise the map resource is loaded and parsed (resource only), then the script gives
  `i16 soundCount` and soundCount × `loadSound(1)` records: `i16 sndId`, plus `raw[9]` if sndId == -1.
- Runtime edge cases: a missing map resource returns before soundCount (desync), and the
  no-free-slot case from 8.5 applies.
- Seen 46× in the data, all parsed exactly.

### 8.11 Other small irregulars
- func 0x0A o1_loadSpriteToPos: two raw bytes after the two expressions; the second is ignored.
- draw 0x30 o2_loadFontToSprite: the four values are bytes, each followed by an ignored byte.
- draw 0x42 o2_pushVars: a string or string-array reference (first byte 25 or 28) is
  stored as raw variable memory and followed by one skipped byte (99 in data). Anything
  else is an `expr` whose integer value is pushed.
- func 0x12 o1_loadTot: if the first byte has bit 7 set it's skipped and an `expr` follows.
  Otherwise the first byte is the raw name length.

---

## 9. Coverage and open questions

**Opcodes seen in the Gob2 data, where the layout parsed exactly:**
- func: 00 01 02 03 05 06 07 08 09 11 12 13 14 15 16 1E 20 21 22 23 25 26 27 30 31 32 33 34 35 38 39 3B 3E 3F 45 46 47 48 49 4A 4B 4D 4E 4F
- draw: 00 01 02 07 08 09 0A 10 11 13 14 15 16 17 19 1D 20 22 23 25 40 41 42 43 50 51 52 53 54 55 56 80
- gob: 100
- hotspot entry types: 0 1 2 4

**Not seen in the data. Taken from the source only, so less certain:**
- func 0A 17 18 19 1A 1F 24 36 37 3A 3C 3D 41 42 43 44 4C
- draw 12 18 1A 1B 1C 21 24 30 31 81-88
- gob 0 1 2 10 500 501
- hotspot types 3, 5-12, 20, 21

**Ambiguities, where the layout isn't fixed by the bytes alone:**
1. **draw 0x00 loadMult**: the length depends on the mult resource contents (8.6). You
   can't skip it without parsing the resource.
2. **func 0x13 palLoad**: sizes depend on runtime video mode and colour count. The table
   in 8.3 is for Gob2 VGA. cmd 49 on EGA modes is inconsistent in ScummVM.
3. **func 0x26 / 0x32**: raw vs expression is the engine's peek-at-offset-+1 heuristic (8.4).
4. **Unimplemented func/draw slots and hotspot types 13-19/22+** consume nothing beyond
   their opcode or type bytes. That desyncs the engine too, so treat them as invalid code.
5. **Gob ops**: implemented ones ignore paramCount. A mismatch would desync the engine.
6. **OP_FUNC arguments containing parentheses**: skipExpr and parseExpr disagree (7.1).
   Doesn't occur in Gob2.
7. **v2 switch immediates** assume a 99 right after the immediate value (6.5). Verified for all data.
8. **Runtime early returns** that would desync the real engine: loadStatic/loadAnim/
   loadMult/loadMapObjects with a missing resource, and loadSound(1) with no free slot.
   A static disassembler should ignore these.
9. **o2_printText**: the byte consumed after a `.` placeholder is unconditional. It's 99
   after a variable reference, but would eat the next byte if a `.` isn't followed by a
   variable type.

---

## 10. Version 1 (Gobliiins, TOT version 1.1)

Gobliiins runs `Inter_v1` (with `Mult_v1`, `Map_v1`, `Goblin_v1`, `Scenery_v1`,
`Draw_v1`). The table indexing, the expression encoding, the block layouts and the
hotspot blocks are the same as above. `gobdis` picks the layouts from the TOT header's
version byte; the engine's version 1 hooks are in `src/engine/interpreter_v1.cpp`.

**Validation.** `gobdis ../gobliiins1 --all` walks the 25 TOTs of the GOG CD version
(769 functions, 6726 code blocks, 94 hotspot blocks, 20946 instructions) with 0 errors.

### 10.1 Func table differences

| Index | Version 1 | Difference from version 2 |
|---|---|---|
| 0x05 | switch | `checkSwitchTable` (v1:246) reads the variable as 32 bits whatever its type, evaluates **every** case expression (no immediate shortcuts), and the **last** matching case wins. The default is taken only when no case matched. Same bytes. |
| 0x09 | assign (v1:987) | No `99 count` repeat prefix. Only 32-bit and string destinations are written; int8/int16 destinations are ignored. |
| 0x11 | printText (v1:1038) | Transparent background for any backColor >= 16 (v2: == 16). Only 32-bit and string variables are inserted. No destination bounds check. Same bytes. |
| 0x17 | animPalInit (v1:1449) | `i16 dir, val low, val high`: one palette-cycling channel. Same bytes; unused by Gobliiins. |
| 0x18-0x1A | (none) | addHotspot, removeHotspot, getTotTextItemPart do not exist. |
| 0x39 | stopSound (v1:1777) | `val`: stops AdLib and the sound-effect voice, whatever the sign. |
| 0x3A | loadSound (v1:2688) | `val slot, u16 id`; id 0xFFFF is followed by a 9-byte file name. Same bytes as v2. Unused by Gobliiins (its sounds come from the .AVO files). |
| 0x3E | getFreeMem (v1:1808) | `var var`. **Does not write `v[16]`**, which is the language in Gobliiins. |
| 0x3F | checkData (v1:1820) | `expr var`: writes 50 or -1. **Does not write `v[16]`.** |
| 0x4D | readData (v1:1948) | `expr file, var, val size, val offset`: int16 size and offset (v2: `expr` offset; same bytes). Game files only. |
| 0x4E | writeData (v1:1978) | Same operands; writes nothing, sets `v[1] = 0`. |

Gobliiins uses neither loadSound, playSound, readData, writeData, checkData nor
animPalInit; getFreeMem appears in every level's quit hotspot.

### 10.2 Draw table differences

| Op | Version 1 | Operands |
|---|---|---|
| 0x01 | playMult (v1:293) | `i16`: the whole word is the escape flag; there is one mult (v2: bit 0 escape, bits 1-3 the mult index). |
| 0x02 | freeMultKeys (v1:330) | `i16`, ignored: frees the current mult's keys. |
| 0x0A | setRenderFlags (v1:416) | `val`, assigned as is (v2: 0x8000 = or, 0x4000 = and). |
| 0x21 | **getCDTrackPos** (v1:659) | No operands. Waits a tick, then `v[5]` = the CD position in frames (75/s), or 32767 when nothing plays. In v2, 0x21 is waitCDTrackEnd and getCDTrackPos is 0x25 with two `var` operands. |
| 0x30 | loadFontToSprite (v1:679) | `i16 index, i16 sprite, i16 base, i16 width, i16 height` (same size as v2's). |
| 0x13, 0x23-0x25, 0x40-0x43, 0x50-0x56, 0x80-0x88 | (none) | The v2 additions (multSub, LIC, totSub, pushVars, goblin draw ops, screen, IMD, ITK) do not exist. |

The mult loader (draw 0x00, `Mult_v1::loadMult`) reads the resource id as a plain
`i16` (no 0x8000 index flag, no IMD keys). Its interleaved script reads differ from 8.6
only in the sound keys: cmd 1/4 load a sound as in v2 (into slots 19, 18, ...), cmd 3
skips **6** bytes (v2: 4) and cmd 5 skips `freq * 2` bytes. Draw 0x17 loadMultObject
tests for a skipped field with a 16-bit peek (`99, 0`) and skips one byte.

### 10.3 Goblin functions (func 0x25, `o1_goblinFunc` v1:1516)

`i16 cmd, i16 paramCount`, then an object prefix chosen by the cmd range:

| cmd | Prefix | Then runs |
|---|---|---|
| 1-16 | `i16 object, i16 value` | setter `cmd` on object |
| 21-37 | `i16 object` | getter `cmd` on object |
| 91-106 | `i16 goblin, i16 value` | setter `cmd - 90` on goblin |
| 111-127 | `i16 goblin` | getter `cmd - 90` on goblin |

Getters write their result to `v[59]`. Then the op reads its own `i16` operands:

| Words | Ops |
|---|---|
| 0 | setters 1-10, 12-16; getters 21-30, 32-37; 202 getItemIdInPocket, 203 getItemIndInPocket, 600 clearPathExistence, 1001 freeObjects, 1002 animateObjects, 1003 drawObjects, 1004 loadMap, 1006 switchGoblin, 1008 loadGoblin, 1010 moveGoblin0, 2005 initGoblin |
| 1 | 52 getGoblinPosXH, 53 getGoblinPosYH, 200 setItemIdInPocket, 201 setItemIndInPocket, 500 decRelaxTime, 502 getGoblinPosX, 503 getGoblinPosY, 601 setGoblinVisible, 602 setGoblinInvisible, 1000 loadObjects, 1011 setGoblinTarget |
| 2 | 41 getItem, 43 getItemIndirect, 152 setGoblinUnk14, 251 setGoblinState, 252 setGoblinStateRedraw, 603 getObjectIntersect, 604 getGoblinIntersect, 1005 moveGoblin, 1015 setGoblinObjectsPos |
| 3 | 40 manipulateMap, 42 manipulateMapIndirect, 44 setPassMap, 50 setGoblinPosH, 150 setGoblinMultState, 250 setGoblinPos, 1009 writeTreatItem |
| 4 | 204 setItemPos, 605 setItemPos |

In all 3415 goblin calls of the GOG data, prefix plus operands is exactly `paramCount`
words; `gobdis` appends `[paramCount N]` where the two would disagree. The ops run in
`GoblinsV1::goblinFunc` (`engine/goblins_v1.cpp`; what they do is in
`docs/goblins-v1.md` section 7). A cmd without a handler is skipped by `paramCount`
and traced as a stub. Gobliiins also calls 2000-2004 (paramCount 0, from the F3-F9
keys of every level), which have no handler in ScummVM either: they are skipped
without a stub trace.

### 10.4 Copy protection

ScummVM skips Gobliiins' colour-chart screen by ignoring `callSub(3905)` in the start TOT
(v1:720). The GOG CD INTRO.TOT contains no such call (no `00/01 41 0F` bytes at all): it
plays the intro and the Coktel logo, sets `v[286] = 200` and loads INTER, which starts
AVT003. The protection texts are still in INTRO.TOT's text table, but no code shows them,
so no rule is needed.


## 11. Version 3 (Goblins Quest 3, TOT version 1.3)

Goblins 3 runs `Inter_v3` (with `Mult_v2`, `Draw_v2`, `Map_v2`, `Goblin_v3`,
`Scenery_v2`, `Init_v3`). Its operand layouts equal version 2's (`gobdis ../goblins3
--all`: 39 TOTs, 2034 functions, 54205 instructions, 0 errors), so this section lists
behaviour only. The engine's version 3 hooks are in `src/engine/interpreter_v3.cpp`; they
also hold the version 2 opcodes that Gobliins 2 never uses, so its code path stays as it
is.

### 11.1 Differences from version 2

| Opcode | Version 3 |
|---|---|
| func 0x22/0x23 speakerOn/Off (v3:76) | The footstep hack (frequency 50: a 5 ms click, the next speakerOff ignored); the shared implementation already does it. |
| func 0x32 copySprite (v3:103) | After the copy, a copy to the front surface calls `Video::sparseRetrace(20)`: the screen is shown after every 21st such copy, or when the last one was more than a second ago. The rooms dissolve to a close-up with 800 random 4x4 and 8x8 copies; the engine shows a frame at each retrace (about 38 frames). |
| goblin 100 | **wobble** (`Draw::wobble`, draw.cpp:680), no operands: the back surface slides onto the screen with its rows shifted by a decaying sine wave (amplitude 32, one less every frame with bit 4 of the frame count set: 64 frames) while the palette fades in. There is no handleGoblins and no protracker (500, 501); other goblin ops are skipped by paramCount. The CD version's scripts call no goblin op at all. |

### 11.2 Version 2 opcodes first used by Goblins 3

| Opcode | Behaviour |
|---|---|
| func 0x02 printTotText | The complete `Draw_v2::printTotText` (draw_v2.cpp:189): besides 2/5 (move), 3 (font and colour) it has 4 (colour), 6 (a clickable area: hotspot `0xD000 + id`, its text centred in a box), 7, 8/9 (underline), 10 (glyph reference) and 0xBA (variable). **Bit 15 of the first word marks a subtitle**: drawn only when subtitles are on, in the font and colour its own command 3 set. ScummVM forces subtitles on for the CD version because some texts that must show are flagged; the engine does too (`Interpreter::subtitles`). Goblins 3's texts use only commands 2, 3 and 5; 483 of its 1438 English text items are flagged. |
| func 0x18 addHotspot (v2:1184) | `val id`, then `val left, val top, val width, val height, val flags` (re-read by recalculation from that position) and `i16 key` (0: 41960 + abs(id)). id >= 0: hotspot `0xE000 + id`; id < 0: `0xD000 - id` on a 4-pixel grid with a 3-pixel margin. Unused by Goblins 3. |
| func 0x19 removeHotspot (v2:1229) | `val id`: -2 removes the hotspots of state 0xD, -1 those of state 0xE, others `0xE000 + id`. Unused by Goblins 3. |
| func 0x1A getTotTextItemPart (v2:1242) | `i16 item, var string, val part`: copies a part of a text item into the string: the text of clickable area `-part - 1`, or the part-th line (texts joined by moves on the same line), or for a glyph reference 0xFF and its position. Part -1 names the hotspot text (text-to-speech; ignored). Goblins 3 builds its item names (`as[7418]`) and dialogue choices with it. |
| func 0x17 animPalInit (v2:1163) | `i16 n, val low, val high`: palette cycling on 8 channels. n > 0: colours low..high rotate upwards by one entry each frame on channel n - 1 (the highest becomes the lowest), n < 0 downwards on channel -n - 1, 0 stops them all. The rotation (`Inter_v2::animPalette`) runs at the start of keyFunc and after animatePalette's frame wait, and is shown at once; a script file load stops it. EMAP1004 uses it. |
| func 0x3A loadSound (v2:1669) | `val slot` (negative: AdLib, ignored here), `i16 id`; id -1 is followed by a 9-byte file name, loaded from NAME.SND. Goblins 3 loads room sounds into slots 2 and 3 by resource id. |
| draw 0x1A-0x1D loadStatic, freeStatic, renderStatic, loadCurLayer | As in version 1 (shared `Scenery` statics), with `val` operands for renderStatic and loadCurLayer. |
| draw 0x85 openItk (v2:1018) | `expr name` (".ITK" added): opens a video or speech archive in the next free archive slot, not as a base archive. Goblins 3's INTRO opens the speech archive by language (FRGOB3, DEGOB3, USGOB3, ITGOB3; Spanish reads the archive's number from VOICE.INF) and IMD.ITK. A missing speech archive falls back to USGOB3.ITK (`GameRules::archiveFallback`; the GOG version ships only that one). |
| draw 0x86 closeItk (v2:1026) | Closes the archive in the last slot that is not a base archive. The rooms open IMD.ITK again and close it when they leave. |
| draw 0x87/0x88 setImdFrontSurf, resetImdFrontSurf | No-ops. |
| draw 0x83 playImd, 0x84 getImdInfo | `expr name, val x, val y, val startFrame, val lastFrame, val breakKey, val flags, val palStart, val palEnd` and `expr name, var x, var y, var frames, var width, var height`: `docs/imd.md` 4. |
| draw 0x81-0x82 scroll, setScrollOffset | Milestone G3.4 (docs/port-plan.md). |

**Archive slots** (`DataFiles`, as `DataIO`): an archive takes the first free slot and
later slots take priority. Archives opened by manageDataFile are base archives; those of
openItk are not. INTRO also sets render flag 0x400 (`RENDERFLAG_NOSUBTITLES`) for the
languages that have speech; ScummVM ignores it, and so does the engine.
