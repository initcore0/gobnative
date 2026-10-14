# HD packs and sharp text

gobnative can take upscaled versions of the game's pictures ("HD packs")
made by the player from their own copy of the game with AI upscalers, and
draws the game's text at window resolution ("sharp text"). This document
covers the exporter (`gobexport`), the pack format and its loader
(`src/gfx/hdpack.*`), the HD compositor that shows packs in the game
(`src/gfx/hdlayer.h`, `Surface`'s layer, `src/engine/hdrender.*`, the
presenter's HD source), the edge smoothing (`src/gfx/smooth.*`), the
sharp text layer (`Draw::TextCell`, `src/frontend/sharptext.*`, the
presenter's text pass) and the tools for repainting rooms with an image
model (`gobrun --guide-at`, `gobexport --frames` / `--import-frames`).

Status: export, pack format, loading and validation, the HD compositor
(with packs switched live, room-only packs and filters for pictures without
HD art), sharp text and the repainting tools (alignment guide, per-frame
export and import) are done.

Nothing derived from the game data is part of this repository: exports,
packs and screenshots stay on the player's machine.

## 1. Exporting the pictures

    build/gobexport /path/to/gobliins2 ~/gob2-export        # about a second
    build/gobexport /path/to/gobliins2 ~/gob2-export --only GOB05 --bleed 16

Every resource of every TOT/EXT that decodes as a sprite is written, one
folder per script file:

| file | content |
|---|---|
| `<TOT>/<id>.png` | RGB, with the palette that applies to it. For pictures drawn with transparency the transparent pixels (colour 0) are filled by bleeding: each pass gives every unfilled pixel next to a filled one the weighted average of its filled 8-neighbours (sides count twice); after `--bleed` passes (default 8) the rest gets the mean opaque colour. Upscalers then see no dark halo around shapes. |
| `<TOT>/<id>.mask.png` | 8-bit gray, 255 = opaque, 0 = transparent; only for pictures drawn transparently. Not to be upscaled. |
| `<TOT>/<id>.json` | metadata (below) |
| `index.json` | every picture (TOT, id, size, kind, transparency, file), the identical pairs and counts per kind |
| `README.txt` | the workflow for the player |

Metadata per picture: `tot`, `resource`, `width`, `height`, `kind`,
`transparent`, `transparent_pixels`, `colors_used`, `palette` (`tot`,
`resource`, `source`: how it was chosen), `files`, `same_as` (an identical
earlier picture: same pixels and palette), `uses` (every script operation
that loads it: opcode, bytecode offset, surface/transparency, or for
animation pictures the anim resource, picture index and sprite size) and
`pieces` (for animation sheets: every rectangle `[left, top, right,
bottom]`, inclusive, of the piece tables the game cuts frames out of the
sheet with; animation.md 1.2).

**Kinds** come from how the scripts load a picture (`ResourceUse` in
`src/script/disasm.h`, collected while disassembling):

| kind | loaded by | transparent |
|---|---|---|
| `animation` | a picture of a `loadAnim` / `loadMult` scenery descriptor: the sheet the animation pieces are cut from | yes |
| `cursor` | `loadCursor` | yes |
| `overlay` | `loadSpriteContent` into the back surface with transparency (the room's foreground layer, e.g. 30008) | yes |
| `background` | `loadSpriteContent` into the back surface without transparency, or a mult's static-key sprite | no |
| `sprite` | `loadSpriteContent` into another sprite (MENU's icon bar) | yes |
| `other` | no script loads it (kept for completeness) | if it has colour 0 and is not full-screen |

**Palettes.** Rooms load their palette with `palLoad 53` (or 61) and fade it
in after drawing. For each picture: the first palette loaded after it in the
same function; else the room's palette (the one its TOT loads most often:
in GOB08, 10, 12, 14, 18 and 21 that is res 3, while res 2 belongs to the
world intermission picture shown on arrival); INTER's three random pictures
0, 1, 2 use palettes 3, 4, 5 (fixed table); pictures no script uses take a
palette stored right after them if there is one (INTRO0 4 and 5), else the
room's; MENU, MENUSPE and REGLAGE bring no palette (they are drawn over the
running room) and are exported with GOB00's.

**Gobliiins** (`gobexport /path/to/gobliiins1 ...`; the game is detected
from the folder). Its scripts also draw pictures with `loadSpriteToPos`
(the goblin portraits, the skull menu, INTER's pictures) and keep each
level's room in a static (`loadStatic` / a mult's static descriptor: the
back pictures and the plane pictures); for version 1 scripts the
disassembler records these as `loadSpriteContent`-like uses and animation
pictures, so they get kinds and piece tables. Palettes: a level loads up to
three, once each (the story text's for the six levels with narration, the
room's, the AGAIN/LOAD/EXIT screen's), so "the most often" doesn't work:
the room's palette is the one the level fades in after `goblin 1000
loadObjects`, and the level's cursors take it too (they are drawn over the
room, also on the AGAIN screen). INTER's three random pictures share
INTER's palette (no fixed table), and INTRO picture 1 (the voodoo doll) is
drawn over the feast with palette 0. There is no inherited palette. The
export's README names the game's folders (AVT00-AVT022, INTRO, INTER,
FINAL, `gobnative/Gobliiins/HDPacks`). Statistics: 415 pictures (306 with
masks, 167 identical to an earlier one): 36 backgrounds, 94 animation
sheets, 68 cursors, 133 overlays, 84 other (mostly INTRO's unused copy
protection symbols).

**Export statistics** (GOG data): 271 pictures in 6.8 MB: 43 backgrounds
(2.51 Mpixel), 83 animation sheets (2.17 Mpixel), 114 cursors, 23
overlays (1.47 Mpixel), 2 sprites, 6 other; 226 with masks; 177 are
identical to an earlier one (the five cursors of every room, MENU's icon
bar), so 94 distinct pictures. Sizes: 118 up to 32x32, 150 are 320 wide
(full-screen backgrounds and overlays, and the animation sheets, which are
320 wide and 15-168 high).

## 2. Workflow and recommended upscaling

1. `gobexport` into a folder.
2. Upscale the `.png` files (not the `.mask.png`) at 2x, 3x, 4x or 8x
   (pictures may differ; 1x for recoloured pictures), writing into a new
   folder with the same
   sub-folders and names (chaiNNer: load images from the folder with
   subdirectories, skip `*.mask.png`, upscale, save with the relative path).
   - Backgrounds and overlays: a model trained on painted / illustrated art;
     these are painted 256-colour pictures with dithering.
   - Animation sheets: upscale each sheet as one image, never single frames
     (the game cuts frames out of the sheet by fixed rectangles, so the sheet
     must keep its layout and scale exactly). A pixel-art or cartoon model
     suits the small characters better.
   - Cursors are 10-16 pixels: any model, or leave them out.
   - Masks are rebuilt automatically from the original pictures.
3. Put the result into `<pref dir>/HDPacks/<pack name>/` (on macOS
   `~/Library/Application Support/gobnative/Gobliins2/HDPacks/`), keeping
   `<TOT>/<id>.png`.
4. Overlay, Video tab, "HD pack": choose the pack; problems are listed and
   the game is shown through the HD compositor (section 4).

Repainting pictures with an image model (which redraws rather than
upscales) is covered in section 7: objects must stay aligned with the
repainted background, and animation frames are repainted one by one.

`gobexport <gamedir> --check-pack <packdir>` prints the same check (and
decodes every picture). `gobexport <gamedir> --scale-export <exportdir>
<packdir> 4` builds a plain nearest-neighbour pack from an export, to try
the pipeline without an AI model.

## 3. Pack format

A folder with `<TOT>/<id>.png` (TOT folder names in any case, id in
decimal) and optionally `<TOT>/<id>.mask.png`. JSON, TXT, MD and INI files
are ignored, so a pack can carry the export's metadata. Pictures may be any
PNG format (8/16-bit, gray, RGB, palette, alpha, interlaced; the decoder in
`src/gfx/png.*` handles all standard formats). Rules checked when a pack is
opened (PNG headers only, so opening is fast):

| problem | severity |
|---|---|
| file not in `<TOT>/` or name not a number | warning, ignored |
| the game has no such picture | error, ignored |
| size not exactly 1, 2, 3, 4 or 8 times the original (same factor both ways) | error, ignored |
| unreadable PNG | error, ignored (also when decoding fails later) |
| mask whose size is neither the picture's nor the original's | warning, mask ignored |
| pictures at different factors | info: shown at the largest, the others enlarged |
| other files | info |

**Alpha.** Every picture becomes RGBA8. The alpha comes from the original
1x picture: colour 0 (transparent when the game draws with transparency) is
alpha 0, enlarged by nearest neighbour (`HdAlpha::Nearest`, the default:
the exact silhouettes the game uses), or with the edge smoothing of
`gfx/smooth.h` (`HdAlpha::Smooth`). If the pack has an upscaled
`<id>.mask.png` of the picture's size, it is used instead, thresholded at
`maskThreshold` (default 128) to stay crisp. The colours are the pack's
(the bled colours under transparent pixels simply get alpha 0).
Backgrounds get the same alpha; renderers ignore it for opaque blits.

**Identical pictures.** A picture missing from the pack uses the HD version
of an identical picture (same size and indexed pixels) that the pack has, so
the cursors only need upscaling once. The match ignores palettes: rooms
show identical cursors and the icon bar with the same colours, but a pack
author who recolours one copy should include every copy
(`HdPackOptions::identicalFallback` turns this off).

## 4. The HD compositor

### 4.1 Loader API

`src/gfx/hdpack.h`, engine-side (no SDL/GL), thread-safe:

```cpp
GameImages originals(gameDir);                      // original 1x pictures, cached TotFiles
HdPack pack;
pack.open(dir, [&](const std::string &tot, int id) { return originals(tot, id); }, options);
pack.issues(); pack.scale(); pack.maxScale(); pack.keys(); pack.contains({"GOB00", 30007});
std::shared_ptr<const HdImage> img = pack.find("GOB00", 30007);  // lazy decode, LRU cache
pack.prefetch("GOB05");                             // decode a TOT's pictures on 4 threads
```

`find()` decodes on first use (about 5 ms for a 4x full-screen picture)
outside the lock, so other lookups go on meanwhile; a lookup of a picture
being decoded waits for that decode. Results stay in an LRU cache bounded by
`memoryCap` (256 MB). `prefetch()` queues a script file's pictures for four
worker threads. The compositor calls it when the first picture of a new TOT
is drawn, so a room's pictures decode in parallel while it draws: the
longest engine frame of a room load went from 140 ms to about 50 ms.

### 4.2 Layers

Every `Surface` can carry an `HdLayer` (`src/gfx/hdlayer.h`) at the
compositor's scale N. The layer is kept in step by the surface's own
operations, so nothing in the engine has to know about it: `blit` (opaque,
transparent, mirrored, clipped, overlapping self-blits), `fillRect`,
`putPixel` (and so text, lines, circles), `clear`. A blit from a surface
with a layer copies the HD rectangle ×N; from one without (a picture
decoded without the hook, a saved sprite) it enlarges the source's indexed
pixels. Per HD pixel the layer holds 32 bits (R, G, B, kind) and the
palette index; per surface pixel `nat`, the indexed picture the mirrored
operations left. Kinds:

| kind | colour |
|---|---|
| live | the screen palette at `idx` (primitives, text, pictures the pack lacks) |
| HD | the stored colour, mapped through the palette's fades (4.4) |
| repaired | live, restored from the surface after a mismatch (4.5) |
| mix | two palette indices blended by a weight, in the screen palette (smoothed pictures without HD art, 4.10; tinted like live pixels) |

`Draw::setHdScale(N)` attaches a layer to every sprite (front 20, back 21,
anim 22, cursor strip 23, captures 30+, picture sprites, the scripts' own
sprites); `createSprite` and `initScreen` attach to new ones. The front
layer also tracks dirty 16x8 tiles. Transparent blits skip HD pixels with
index 0, exactly as the game skips colour 0.

### 4.3 Pictures

Every picture decoded from a resource goes through `Draw::decodePicture`,
which calls `Draw::pictureHook` before the picture is drawn: `LoadSprite`
(backgrounds, overlays, mult static keys, MENU's icon bar), `loadAnim`'s
picture sprites (animation sheets: pieces are cut by blits, so they map ×N
by themselves) and `loadCursor`; each tells how the picture is used
(`PictureUse`: screen, sprite, animation, cursor). `HdRenderer::decorate`
looks the picture up as (running TOT, resource id) and gives it a layer:

- From the pack: the HD image, resampled to N when its own scale differs
  (nearest when N is a multiple, averaging when larger, bilinear for 3x in
  a 2x/4x/8x compositor). Indices come from the original pixel under each
  HD pixel; for pictures drawn transparently the pack's alpha decides: an HD
  pixel outside it gets index 0, one inside it over an originally
  transparent pixel takes a neighbouring original index. With the default
  nearest alpha this is the exact original silhouette; `hd_smooth_edges`
  uses the smoothed alpha.
- Not in the pack, or not taken from it (`hd_pack_scope = rooms`, "Apply
  pack to: Room backgrounds only": only pictures drawn into the screen
  surfaces, i.e. backgrounds, overlays and mult static keys, which gobexport
  calls `background` and `overlay`, take the pack's image): the original,
  enlarged by `hd_sprite_filter` (section 4.10).

The compositor runs at `pack.maxScale()`: mixed packs lose nothing, a 1x
pack (recoloured pictures) runs at 320x200 with the normal filters.

### 4.4 Colours, fades and palettes

The scripts of the GOG version use only three palette commands
(`gobdis --all`): 53 (load a palette resource and fade to it, 68 uses), 54
(fade to black, 43) and 52 (EGA data, ignored, 111); never 61 (partial
palettes), 55/56 (push/pop) or the "apply later" bit. Rooms fade to black,
draw, load their palette and fade in; all loads inside a room reload the
same palette. So no room changes its palette other than by fades, and the
palette that HD pictures are shown under at full brightness is the last
non-black staged palette (`Draw::vgaPalette`), the *reference*.

Each frame the screen palette is compared with the reference:

| screen palette | HD pixel colour |
|---|---|
| all black | black |
| = reference | the pack's colour |
| every channel `max(ref - d, 0)` (a fade toward black) | `max(c - d, 0)` per channel |
| every channel `min(ref, e)` (a fade up from black) | `min(c, e)` |
| anything else | `screen[idx] + (c - ref[idx])`: the HD detail over the new colour |

The two fade rules are the game's own fade arithmetic (`Draw::fadeStep`
moves every channel by the same step), so with a nearest-neighbour pack a
fading frame equals the original to within 0.12/255 on average. The last
rule re-tints HD pictures for palette changes the game doesn't make; live
pixels always use the screen palette.

### 4.5 Repairs

`HdRenderer::update` first compares the front surface with its layer's
`nat`: any pixel that differs was changed without going through the
surface operations (none are known), and its HD block is repaired from the
surface as a live pixel. So the picture can't go wrong, only less sharp,
and `--hd-tint` / "Show what the pack doesn't cover" tints repaired pixels
red and live ones blue. In every test run 0 pixels needed repairing.

### 4.6 The frame

`update()` then recolours the dirty tiles (all of them when the palette
mapping changed, e.g. every fade step) into an N×320 x N×200 RGBA frame
and reports the changed rectangles. The front end takes the sharp text's
ink out of it (each glyph's clean background, in the screen palette) and
draws the cursor from the cursor strip's layer at N×; both are marked so
they are uploaded now and recoloured next frame. The presenter's source
texture is N×320x200 and only the changed rectangles are uploaded. With an
HD frame the pixel-art upscalers (Scale2x/3x, xBR), which expect 320x200
pixel art, are skipped and sharp bilinear scales to the viewport (nearest
and bilinear work as chosen). The CRT filter imitates the original monitor
and shows the original frame (and no sharp text, as before). The text pass
reads 320x200 maps, so it doesn't care about the source size.

### 4.7 Performance

M1 Pro, 4x pack (271 pictures), window at 4x, rooms GOB01-GOB22 with
goblins walking (`--hd-stats`):

| per frame | mean | p95 | max |
|---|---|---|---|
| engine (mirroring included) | 0.2-0.45 ms | 0.15-0.4 ms | 50 ms when a room loads its pictures |
| compositor (repair check, recolouring, text, cursor) | 0.3-0.6 ms | 1.6 ms | 2.7 ms |
| upload (CPU side) | 0.25-0.4 ms | 1.0 ms | 1.5 ms |

Most frames change a few tiles (median under 0.1 ms each); the p95 frames
are fade steps, which recolour and upload all 1280x800 pixels. Native
engine frames take 0.02 ms (max 2 ms).

### 4.8 Verification

`--hd-dump-at T PREFIX` writes the HD frame and the 320x200 frame; reducing
the HD frame by averaging and comparing with the 320x200 one measures the
structural alignment. With a nearest-neighbour pack (`--scale-export`) the
difference is exactly 0 in every dump: GOB04 and GOB05 with goblins walking
(23 dumps 0.3 s apart), the icon bar opening and closing, the intro's first
60 seconds and room fades (within 0.12/255 while fading), a mixed 2x/4x
pack; a 1x inverted pack shows inverted. With the AI pack (nxbrz 4x) the
mean difference is 3.9-4.4/255, about 1% of pixels off by more than 48
(edges the upscaler redrew), the same before, during and after walking and
menus (no trails); frames before and after the icon bar differ only where
the 320x200 frames differ. Coverage: everything but the text strip and the
hotspot line comes from the pack (87-100% of the screen).

Headless: `gobrun <gamedir> --hd-pack DIR --hd-dump-at T PREFIX` runs the
same compositor without a window (the pack opened as gobnative opens it,
pixel sources tracked, `HdRenderer::update` every frame) and prints the
mean difference and the number of colour values more than 48 off for each
dump, besides writing `PREFIX_native.png` and `PREFIX_hd.png`.
`tests/check_hd_gob1.sh` uses it for Gobliiins with a 4x nearest pack made
by `--scale-export` from a fresh export: 40 dumps (the intro, the doll, the
title, the first level while a click script solves it, its access code,
INTER, the next level, a quick load, the game over and access code
screens, the ending, a story text and every level's room) are all exactly
0, which also checks the export's palettes: a picture exported with
another palette than the one the game shows it in comes out in the wrong
colours.

### 4.9 Limitations

- Rebuilt layers keep at most two picture entries per pixel (4.11): where
  a transparent edge pixel lands on a pixel that already has two, the
  older one is dropped. With the default exact silhouettes nothing is lost;
  with smoothed outlines a rebuilt frame can differ from the incremental
  one in a few edge pixels (under 0.005% in the tests) until they are
  redrawn.
- Switching to a pack decodes the pictures on screen: 30-60 ms at 4x,
  150-250 ms at 8x (the pack's decoding; with the pictures cached, as for
  an option change, about 55 ms at 8x).
- The game's own lines, fills and 8x8 text stay pixels (text is drawn
  sharply by the sharp text layer); the text strip is a fill.
- Room loads take up to about 50 ms longer while the first pictures decode.
- Fades recolour the whole frame on the CPU (1.6 ms); a shader could do it.

### 4.10 Pictures without HD art

`hd_sprite_filter` ("Pictures without HD art", `HdSpriteFilter`) enlarges
pictures the pack lacks or doesn't apply to:

| value | result |
|---|---|
| `original` | big pixels (nearest), live |
| `scale2x` | Scale2x / Scale3x (4x = 2x twice, 8x = three times) on palette indices, live (the former `hd_native_smooth`) |
| `smooth-edges` | the presenter's xBR-like "Smooth edges" shader ported to the CPU (`gfx/pixelfilter.*`) at the compositor's factor: corners where an edge runs diagonally are filled with the neighbour up to a 45-degree (or shallower / steeper) line, anti-aliased over one HD pixel; edges are found with the colours of the reference palette (4.4) |
| `smooth` | bilinear |

The smoothing filters write *mixes* (`HdLayer::Mix`: two palette indices
and the second one's weight; bilinear keeps the two strongest of its four
samples, exact wherever at most two colours meet). A mix is coloured from
the screen palette like a live pixel, so palettes and fades apply exactly;
its `idx` is the original pixel under it. For pictures drawn transparently
colour 0 is never mixed in, so the silhouette is the original one; with
`hd_smooth_edges` the outline is smoothed instead (`gfx/smooth.h`, as for
HD sprites). Results are cached by content (128 MB at most), so a picture
loaded again costs a copy; at 8x the filters don't lengthen a room load
measurably (the pack's decoding dominates). Smoothed edges depend on the
palette: rooms decode their pictures before loading their palette, so
when the reference palette changes, the pictures smoothed under another
one are made again and only their pixels in the layers are rebuilt (4.11),
15 ms at 4x, 25 ms at 8x, once per room during the fade from black.

### 4.11 Switching packs

A pack, a scale, None, `hd_pack_scope`, `hd_sprite_filter` or
`hd_smooth_edges` can be changed at any moment (room, menus, icon bar,
mid-animation, intro) and shows in the next frame, exactly as if it had
been chosen before the room was drawn. The composite surfaces (back 21,
the animation snapshot 22, the front 20, captures) hold pictures drawn
long ago, so the layers are rebuilt from *pixel sources*
(`gfx/pixelsource.h`):

- The front end turns on `Draw::setTrackSources`. Every decoded picture is
  registered in `Draw` (script file, resource id, transparency, use, the
  original pixels; identical loads share an entry) and its pixels are
  marked as pixel (x, y) of picture id.
- Every surface keeps two entries per pixel, maintained by the same
  operations as the layer: `top`, a picture reference (with a mirror flag
  and whether the picture pixel is colour 0) or "live" (fills, text,
  lines); and `under`, set only where a transparent blit put a picture's
  edge pixel (one with an opaque and a clear neighbour) over something
  else, because the HD version of an edge pixel may leave part of its block
  clear (smoothed outline, pack mask) or fill part of a clear pixel's block
  (Scale2x, smoothed outline). Opaque copies carry both entries, transparent
  copies the top one; interior pixels need one entry, clear interior pixels
  change nothing.
- `HdRenderer::attach` (also used for switching) collects the pictures the
  surfaces refer to, decorates them again for the new pack, scale and
  options on four threads (the pack decodes outside its lock) and makes
  every layer anew (`Draw::rebuildHd`, `Surface::rebuildHd`): live pixels
  from the indices; a referenced pixel's block from the picture's new
  layer (mirrored if needed), the `under` block first with the top one
  over it, skipping colour 0, as the transparent blit would. A pixel whose
  sources don't explain its index (changed behind the operations' back) is
  made live, so nothing stale can show. Switching to None detaches the
  compositor and the 320x200 frame is shown.
- The reference palette survives attaching during a fade (`Draw::litPalette`,
  the last non-black palette applied or faded to); the presenter follows
  the frame size by itself.

Cost: scanning the sources is about 1 ms; at 4x a switch takes 30-60 ms
(4-10 pictures, most of it decoding pack pictures), at 2x 10 ms, at 8x
150-250 ms; tracking costs the engine a copy of 8 bytes per blitted pixel.

Verification (`--hd-switch-at T NAME`, `--hd-option-at T KEY VALUE`,
`--hd-dump-at`; sharp text off so the HD frame holds the text):

- Room started with the nxbrz 4x pack vs the same room started without a
  pack and switched to it 6 s later, frames 3 s after the switch: GOB01,
  04, 05, 08, 17, 21 identical (mean difference 0.000); GOB13 differs only
  where the 320x200 frames differ (an animation's phase, 0.16% of pixels).
  The same with a 2x nearest pack switched to the 4x AI pack (GOB04), the
  AI pack switched to the 4x nearest pack (GOB13), and in the intro (4 s,
  20 s, 30 s): identical except where animation phases differ (and next to
  them: the AI pack's sheets change around a changed pixel).
- The same game moment (the overlay pauses the game): a dump, a switch to
  another pack or scale and back, a dump: bit-identical in the intro, GOB13,
  GOB05 with the icon bar open, GOB21 with the tint view, at 8x, with
  smoothed outlines, with a partial pack and Scale2x, with goblins having
  walked (8x room-only pack with smooth-edges and smooth sprites).
- With the 4x nearest pack every switched frame averages down to the
  320x200 frame exactly (0.000), also room-only with `original` sprites
  while goblins walk and the icon bar opens. Switching to None shows the
  320x200 frame.
- Changing `hd_pack_scope` and `hd_sprite_filter` live (8x) gives the same
  frame as setting them before the room (0.000).

## 5. Edge smoothing (`src/gfx/smooth.*`)

Used for the glyphs and for `HdAlpha::Smooth`. A 1-bit shape is resampled
in continuous coordinates: each pixel splits into four corner quadrants,
each decided from the pixel and its neighbours around that corner. A set
pixel whose corner sticks out (both side neighbours clear, diagonal clear)
loses a triangle there: cut through the middle of both sides (45 degrees)
when a diagonal stroke runs past the corner, a small rounding (0.3)
otherwise. A clear pixel between two set side neighbours gains a triangle:
the big one when the diagonal is clear (a staircase), a small fillet (0.2)
in an inner corner. Cuts and fills of neighbouring pixels lie on the same
lines, so a one-pixel staircase becomes a straight diagonal band and
diagonal strokes keep their thickness. `glyphDistanceField` renders the
shape at 16 samples per pixel and runs an exact Euclidean distance
transform (Felzenszwalb-Huttenlocher) to get a signed distance field.

## 6. Sharp text

Setting `sharp_text` in `[video]` (on by default since the HD compositor:
the layering proved robust over HD frames), "Sharp text" in the Video tab,
`--sharp-text` for one run. Not used with the CRT filter (its curvature and
scanlines can't be followed).

**Recording (engine, `Draw::drawText`).** All game text goes through
`DRAW_PRINTTEXT` (printTotText, printText) or the text input field, which
now call `Draw::drawText` (Gobliiins' 6x8 fonts included). When it draws on the front or back surface with
an 8x8-or-smaller monospaced font, every glyph cell is recorded as a
`Draw::TextCell`: position, size, ink colour, the glyph's ink bits, the 64
pixels the cell holds right after drawing (`expect`) and the background
without the text (`clean`: the back colour for opaque text; for transparent
text the pixels before drawing, or the background of an earlier cell at the
same place that was still on screen, so a reprinted label stays clean). A
cell is `pure` when only its own ink differs from the clean background (not
an earlier glyph showing through); a new cell replaces any cell at the
same position; spaces are not kept; at most 2048 cells. The front end turns
recording on (`Draw::recordText`); gobrun leaves it off.

**Validation (every frame, `Draw::visibleText`).** A cell is drawn sharply
only while the front surface (what the player sees) holds exactly its
`expect` pixels. Whatever overwrites a cell (a fill, a sprite, an
animation, a capture restore, other text, the input field's cursor line)
makes the check fail and the native pixels show; text drawn on the back
surface shows once it has been copied to the front; a restore that brings
the very same pixels back brings the sharp glyph back. No drawing
operation has to report anything, so nothing can be missed: when unsure,
the native pixels win.

**Compositing (front end).** `SharpText::build` replaces the ink pixels of
every visible cell in the 320x200 RGBA frame by the cell's clean background
(before the cursor is drawn) and fills two frame-sized maps: per game pixel
the glyph's atlas slot and the pixel's position in the cell, and the ink
colour (from the current palette, so fades work). Cursor pixels are masked
out of the map. The filtered frame is drawn as usual; then the presenter's
text pass covers the viewport: each output pixel looks up its game pixel in
the map and, inside a glyph cell, samples the glyph's distance field
(8 samples per glyph pixel, +-1 pixel spread, kept in a 2112x2112 atlas,
one slot per distinct glyph bitmap, made on first use in about a
millisecond) and blends the ink colour with coverage over one output pixel.
The result sits exactly where the native text was, at any window size and
with 4:3 correction.

Verified with screenshots (intro captions, the room's dialogue line,
hotspot names at the bottom changing from one object to another, the icon
bar's disk menu opening and closing, with sharp bilinear and xBR) against
the same frames without sharp text.

Debug options used for that: `--mouse-at T X Y` and `--click-at T X Y`
(move the mouse, or click, at game pixel X, Y at time T),
`--hdpacks-dir DIR`, `--hd-pack NAME`; for the compositor `--hd-tint`,
`--hd-native-smooth`, `--hd-stats`, `--hd-dump-at T PREFIX`,
`--hd-switch-at T NAME` (NAME may be `none`) and
`--hd-option-at T KEY VALUE` (`hd_sprite_filter` or `hd_pack_scope`).

## 7. Repainting a room with an image model

An upscaler keeps every shape where it was; an image model asked to repaint
a background redraws it, and things move a little. That matters wherever
the game draws something over the background at a fixed pixel position:
the objects (flowers in a planter, a bottle on a wall, people on a bench)
are separate animation frames placed by the scripts, and they still use
their original art (or the pack's version of their sheet). And animation
sheets can't be repainted whole: the game cuts every frame out of a sheet
by exact piece rectangles, so a repainted sheet scrambles them. The tools
below show what has to stay aligned and let frames be repainted one by one.

Workflow: guide → repaint the background keeping the outlined areas in
place → export the frames → repaint the frames you want → import them into
the pack → play. Nothing these tools write goes into the repository.

### 7.1 Alignment guide (`gobrun --guide-at`, `tools/roomguide.*`)

    build/gobrun <gamedir> --start MENU --level-at 1 GOB01 --guide-at 2 DIR \
        [--guide-pack PACKDIR] [--guide-scale N] [--guide-hotspots]

From time T on, gobrun waits until the room is idle (the player in control,
no scripted sequence or skip running, for half a second), captures it,
writes the guide and ends the run (about half a second for GOB01). The
scale defaults to the pack's (else 4).

| file | content |
|---|---|
| `guide-original.png` | the room as the game shows it, enlarged by nearest neighbour; every drawn object outlined in its own colour with its index and name; with `--guide-hotspots` the hotspots dashed in cyan with their names |
| `guide-pack.png` | the same outlines over the room as the pack shows it: every screen pixel that comes from a picture the pack has (the repainted background, a repainted sheet) is that picture pixel's HD block, the rest the original. Shows at once where a repaint moved things away from the objects drawn over it |
| `objects-layer.png` | only the objects' pixels (everything drawn from animation sheets, goblins included, as visible on screen), transparent elsewhere; lay it over a repainted background in an image editor |
| `objects.json` | per object: `index`, `name` (as the game shows it on hover; `name_from_game`), `goblin`, `anim_slot`, `anim_resource`, `layer`, `frame`, `order`, `position`, `sheets`, `rect` and `rect_scaled` (inclusive), `visible_pixels`, `hotspot`, `frames_folder` (where `--frames` puts its frames) and its `pieces` (sheet, piece, source rect, screen position); then the hotspots with names and rects |

How: gobrun turns on pixel source tracking (`Draw::setTrackSources`, 4.11)
before the game starts, so every screen pixel tells which pixel of which
picture it shows. Objects come from the inspection API
(`Interpreter::objects()`: last drawn rect and the animation, layer and
frame drawn there, `Interpreter::scenery()` for the anim's pieces); an
object's visible pixels are the screen pixels that show exactly the sheet
pixel one of its pieces puts there (so overlapping objects separate, and an
object hidden under an identical one has 0 and is not outlined). Names:
gobrun moves the mouse onto each object (a visible pixel near its middle
that no earlier hotspot covers) and each hotspot, lets the room react,
and reads the hover line back from the recorded text cells
(`Draw::recordText`), mapping glyphs to characters through the game's
fonts; between probes the mouse rests outside every hotspot so the line is
cleared.

GOB01 (idle): 0 Fingus and 1 Winkle (goblins, anim 0), 2 Notable (only his
head and arm are an object; his body is background), 3 Old men, 4 Flowers
(anim 30004 layer 5, sheet 30006, rect 216,119-243,132), 6 Sausage, 8
Bottle; object 14 is a copy of 3 hidden under it.

Gobliiins draws its goblins and objects itself (GoblinsV1, not mult
objects), so for it `Interpreter::objects()` lists the goblins as 0-3 and
the level's objects as 100 + n (their animation, the layer of their state,
frame, position and last drawn rectangle; hidden ones have no rectangle).
A level started from INTER, e.g. `gobrun /path/to/gobliiins1 --start INTER
--set-var 0 286 100 --set-var 0 296 10 --guide-at 3 DIR`, gives AVT003's
three goblins and objects 100 (the horn on the gate), 101 (the gate), 104
(the rabbit), 105 and 106 (birds). Gobliiins shows no names on hover (only
the item carried), so names stay empty.

### 7.2 Frame export (`gobexport --frames`, `tools/frames.*`)

    build/gobexport <gamedir> --frames OUTDIR [--only TOT] [--guide GUIDEDIR]

For every animation resource a script file loads (the scenery descriptors
found by the disassembler give each anim's pictures: sheet and piece
table), every layer with frames and every frame:

| file | content |
|---|---|
| `<TOT>/anim<A>/layer<L>/frame<F>.png` | the frame as the game composes it (`Scenery::updateAnim`: pieces in order, colour 0 transparent for transparent layers), RGBA with the sheet's palette, cropped to the frame's bounding box |
| `.../preview.png` | the layer's frames side by side, each placed in the union of all frames (so they line up as in the game) over a checkerboard, numbered |
| `.../layer.json` | `anim`, `layer`, `position` (the layer's default), `transparent`, `sheets`, and per frame its `file`, `size`, `offset` (of its top-left from the layer's origin) and `pieces`: `order`, `sheet`, `piece_table`, `piece`, `source` rect in the sheet (inclusive), `dest` in the frame |
| `<TOT>/layers.txt` | with `--guide`: the objects on screen when the room is idle, with the frame file each shows and every other layer/frame drawn with the same pieces; then all layers (frames, sheets, position, objects) |
| `README.txt` | the rules below, for the player |

Folders are per animation resource, not per sheet: of GOB01's 149 layers
with frames many combine sheets (a goblin carrying an object takes pieces
from the goblin sheet 30000 and the room's sheets), and the layer numbers
belong to the anim. GOB01: 1892 frames, about a second.

### 7.3 Repainting frames

- Any size, but the frame's composition stays where it is: the same
  framing, nothing moved, cropped or resized inside the image. The import
  scales the image to the frame's size times the pack's scale; the game
  draws exactly that rectangle at a fixed position.
- Same folders and file names; only the frames that were repainted.
- Transparent or flat backgrounds both work (see alpha below).
- The same piece is often used by many frames (layers.txt lists them): the
  repaints should agree, or the import picks one (7.4).

### 7.4 Import (`gobexport --import-frames`)

    build/gobexport <gamedir> --import-frames FRAMESDIR EXPORTDIR PACKDIR [--scale N]
        [--alpha | --key-color RRGGBB [--key-tolerance T]] [--only TOT]

FRAMESDIR holds the repainted frames in the export's layout, EXPORTDIR is
the picture export (`gobexport <gamedir> <dir>`), PACKDIR the pack. The
frame layout is rebuilt from the game data (the same code as the export).

1. **Scale**: `--scale`, else the scale of the pack's existing sheets
   (they must agree), else 4.
2. **Fitting**: each image is resized to exactly N × the frame
   (Lanczos-3 on premultiplied alpha, averaging when shrinking; an image
   of exactly that size is used as is). An aspect ratio more than 3% off is
   reported. With `--key-color` pixels within the tolerance (default 24 per
   channel) of the key become transparent.
3. **Framing check**: the frame and the original are compared at twice the
   original size by normalised cross-correlation of their luminance edges
   (over grey where transparent) for shifts up to 3 original pixels; a
   clearly better shift is reported ("looks shifted by +2.0,+1.0 original
   pixels"). Recolouring doesn't trigger it (an inverted frame correlates
   best unshifted), moving the content does.
4. **Base sheet**: the pack's `<TOT>/<sheet>.png` if it has one (so
   repeated imports and repainted sheets build up), else the export's
   sheet enlarged by nearest neighbour (exactly what `--scale-export`
   makes), else the game's sheet.
5. **Pieces**: every piece of every repainted frame is an *instance*,
   ranked by how much of it the frame shows (opaque pixels not covered by a
   later piece of the same frame), then by anim, layer and frame. In rank
   order, each instance writes the HD blocks (N×N) of the sheet pixels it
   shows that no better instance wrote. So a piece shared by several frames
   comes from the frame where it is most visible, pixels a frame covers with
   a later piece come from another frame that shows them, and pixels no
   repainted frame shows keep the base. Where a lower-ranked frame shows a
   pixel differently (by 6/255 or more on average over its block) the
   difference is reported per piece: the frame kept and the frames
   overruled.
6. **Alpha**: by default only the sheet pixels that are opaque in the
   original are written, and no mask: the loader rebuilds the alpha from the
   original silhouette (section 3), so the repaint's background never shows
   and outlines stay the game's; where the repaint is clear inside the
   original outline, the nearest painted colours fill in (reported when
   over 5% of the outline). `--alpha` (the PNGs' own transparency) or
   `--key-color` also write the blocks of clear original pixels that no
   piece covers and a `<sheet>.mask.png` at N× (started from the pack's mask
   or the original silhouette), which the loader then uses (thresholded at
   128). An existing pack mask in silhouette mode is left alone (and
   reported, since it decides the outline).
7. The sheet is written as RGB, and the pack checked as `--check-pack` does.

Identical pictures (section 3): sheet 30000 (the goblins) is the same in
every room, so goblin frames imported in GOB01 also show in rooms whose
pack folder has no 30000 of its own.

### 7.5 Verification

GOB01, with frames made from the export by mechanical transformations only
(nearest-neighbour 4x enlargement, colour inversion, a shift; scratch
folders):

- All 1892 frames enlarged 4x by nearest neighbour and imported into an
  empty pack: sheets 30000, 30003 and 30006 equal the `--scale-export`
  sheets exactly (0 pixels differ), no warnings.
- The flowers (anim30004/layer5/frame0) inverted: exactly the 3200 HD
  pixels (200 opaque original pixels) of piece 42 of sheet 30006 changed,
  nothing outside it. In gobnative (`--hd-dump-at`, room started with a
  nearest pack with and without that sheet) the HD frames differ in exactly
  3200 pixels, all at 219-242 x 119-132 inside the flowers' rect, each the
  inverse of the other run's; the 320x200 frames are identical and the
  nearest pack's HD frame averages down to them exactly.
- The flowers frame shifted by 2 and 1 original pixels: "looks shifted by
  +2.0,+1.0 original pixels = +8,+4 at 4x".
- Winkle's idle layer (25 frames) with odd frames inverted: conflicts
  reported for the 8 shared pieces, e.g. "kept anim0/layer73/frame0, where
  it is most visible, over anim0/layer73 frames 1,3,5,...,17".
- `--alpha` with transparent nearest frames writes a mask equal to the
  original silhouette; a 1000x500 frame on white is fitted to 112x56
  (silhouette mode: changes only inside the piece; `--key-color FFFFFF`
  writes a mask).
- The guide with the vision pack shows the repainted planter shifted from
  the original flowers drawn over it.

### 7.6 Limitations

- The guide shows one moment: objects that appear later (after an action)
  or move are not in it; run it again at another time or after `--click`s.
- Names are what the hover line shows; some hotspots show none, and
  objects without a name are "object N" (goblins "goblin N").
- A repaint can only change pixels inside the original silhouette (default)
  or inside the frame's piece rectangles (`--alpha`, `--key-color`):
  frames can't grow beyond their pieces, and a background repaint that
  moved something must be fixed in the background.
- Pieces shared by frames get one version: frames that disagree lose
  (reported), and pieces always covered keep their previous pixels.
- The framing check only sees shifts of up to 3 original pixels and needs
  some structure in the frame; rescaled or rotated content isn't detected.
