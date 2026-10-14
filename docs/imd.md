# IMD video

Coktel Vision's IMD format holds Goblins Quest 3's cutscenes, in-room animations and
talking heads (with the speech as their sound). The decoder is `src/formats/imd.{h,cpp}`
(`ImdDecoder`, pure, no engine dependencies); `tools/gobvideo.cpp` lists, checks and
decodes the videos; `tests/test_imd.cpp` covers every feature with synthetic videos.
Format knowledge comes from ScummVM's `video/coktel_decoder.cpp` (`IMDDecoder`; not in
the sparse checkout: `git -C .../reference/scummvm show HEAD:video/coktel_decoder.cpp`).

    build/gobvideo /path/to/goblins3 --list            # every IMD: size, position, frames, sound
    build/gobvideo /path/to/goblins3 --check-all       # decode them all
    build/gobvideo /path/to/goblins3 INTRO /tmp/intro  # frames as BMP, the sound as WAV
    build/gobvideo /path/to/goblins3/MDOS.IMD /tmp/m   # a loose file

## 1. Where the videos are

| Container | Videos |
|---|---|
| IMD.ITK | 45: 5 full-screen cutscenes (INTRO, INT9, INT120, INT121, INTERV) and in-room animations |
| USGOB3.ITK | 462: the speech, as talking heads (most 61x68 or 92x75 at 7 fps) and 7 larger clips |
| MDOS.IMD | loose, 320x200, 400 frames, 8.7 kHz sound; no script plays it (a launcher logo) |

ITK files are STK archives (`formats/stk.h`); the scripts open them with openItk
(`docs/opcodes.md` 11.2). `gobvideo --check-all` decodes all 508 videos (18660 frames)
without an error.

## 2. Header

All values little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | 0 (handle) |
| 2 | u8 | version, 2 or more (Goblins 3: 2 and 4) |
| 3 | u8 | features (below) |
| 4 | u16 | frame count |
| 6 | i16 x, i16 y | default position (where the video is drawn) |
| 10 | i16 w, i16 h | size |
| 14 | u16 | flags (used by the player) |
| 16 | u16 | offset of the first frame |
| 18 | 768 | palette, 6-bit VGA RGB |

Then, in this order, each only when present:

- **Standard rectangle** (version 3 on): `u16 count` (0 or 1), then `i16 x, y, w, h`. Frames
  without their own rectangle draw there.
- **Frame offset table position** (version 4 on): `u32`; 0 means none (feature 0x200).
- **Frame rectangle table position** (feature 0x80): `u32`.
- **Sound** (feature 0x40): `i16 rate` (absolute value), `i16 slice size`, `i16 slice
  count` (negative n means -n - 1; at most 40). The frame rate is rate / slice size
  (22050 / 3150 = 7 fps for the speech); videos without sound run at 12 fps.
- **Buffer sizes** (feature 0x20): `u16 a, u16 b`, or `u16 0, u32 a, u32 b`. The frame
  buffers hold max(a, b, w * h + 1000) bytes.

The offset table holds one `u32` per frame; the rectangle table one `i16 left, top,
right, bottom` (inclusive) per frame, left -1 meaning none.

## 3. Frames

A frame is a sequence of `u16` commands, read in this order:

1. **Breaks** (`cmd & 0xFFF8 == 0xFFF0`). 0xFFF0 is followed by 2 bytes to skip and
   another command. 0xFFF1 ends the frame; 0xFFF2 `u16 n` and 0xFFF3 `u32 n` skip n bytes
   and end the frame (no sound, no video).
2. **Sound.** 0xFF01 (start): `slice size * slice count` bytes, the first slices; playback
   starts with this frame. 0xFF00: one slice. Any other command: the frame has a silent
   slice. Either sound command is followed by the next command. Samples are 8-bit unsigned
   PCM (the decoder hands them out signed).
3. **Palette** 0xFFF4: 2 bytes skipped, a 768-byte palette, then the next command.
4. **Jump** 0xFFFD `i16 frame` (only with an offset table): the commands of that frame
   follow at once (the frame number becomes the target's minus one, as ScummVM has it).
   No Goblins 3 video jumps.
5. **Video:** 0xFFFC `u32 n`, or any other non-zero command n: a chunk of n + 2 bytes. 0:
   no video in this frame.

The sound is buffered ahead: the first slices are the sound of the first frames, so the
sound ends (slice count - 1) slices after the last frame.

### 3.1 Video chunks

The chunk draws the frame's rectangle: the whole video for the first frame and frames
without a rectangle of their own or a standard one; else its own (from the table) or the
standard rectangle. The rectangle is clipped to the video's area and the destination
surface.

The first byte is the type:

- bit 4: 16 palette entries follow first: `u8 index`, 48 bytes of RGB;
- bit 7: the pixel data is LZ77-packed (3.2);
- the rest selects the block layout:

| Type | Layout |
|---|---|
| 0x01 | sparse: per row, count bytes; bit 7 set: (count & 0x7F) + 1 literal pixels follow, else count + 1 pixels are left as they are |
| 0x02 | whole: every pixel, row by row |
| 0x42 | quarter wide: each byte fills 4 pixels |
| other with low nibble 2 | half high: whole rows, each drawn twice |
| other | half-high sparse |

Goblins 3 uses 0x01 (1540 chunks), 0x02 (10326), 0x81 (2504) and 0x82 (139).

### 3.2 LZ77

`u32 unpacked size`, then optionally the marker `0x1234 0x5678` (u16 each) for the
extended variant, then groups of 8 items, each led by a flag byte read from bit 0 up: a set
bit is a literal byte, a clear bit a `u16` reference: offset = low byte + (high byte's high
nibble << 8), length = high byte's low nibble + 3. The extended variant reads a length of
18 as a following byte + 18 (the standard variant would do that for 0, which cannot
happen). The 4 KiB window starts filled with spaces, with its write position at 4078
(standard) or 273 (extended); every output byte is also written to the window.

## 4. Engine use

`src/engine/video.cpp` plays them as ScummVM's `VideoPlayer` does for the primary video
(`o2_playImd`, `o2_getImdInfo`; `videoplayer.cpp`).

**playImd** (draw 0x83): `expr name` (cut to 8 characters; ".IMD" added), then `val x, y,
startFrame, lastFrame, breakKey, flags, palStart, palEnd`.

- The flags pick the target: 0x80 the front surface, 0x800 sprite `x` (drawn at 0, 0),
  0x100 none (sound only), else the back surface. A same-named video stays open.
  x and y place the video (-1 keeps the IMD's default position). Opening writes VAR 7
  (the frame count); a missing file writes VAR 11 = -1.
- lastFrame -1 closes the video afterwards; startFrame -2 opens it and plays frame 0
  only, without closing; startFrame -1 plays nothing (an empty name with -1: close).
- The palette command is `1 << (flags & 0x3F)`: 1 none; 2 the video's palette after every
  frame; 4 also into the game palette at the first frame, then a fade (instant in effect,
  as the blit applies it); 8 at once at the first frame; 16 a fade after the first frame.
  `palStart..palEnd` limits the entries copied.
- Every frame: the rectangles drawn are blitted from a decoding buffer (which mirrors the
  target, so holes in sparse frames keep what was there) into the target, and invalidated
  and shown for the back surface; VAR 11 = the frame. Escape cancels (ScummVM makes the
  break key Escape for every video), and so does the Skip button: the sound stops and the
  video seeks to its last frame.
- Timing: with sound, frame n is due n / rate seconds after the first frame (7 fps for the
  speech, 10 or 7 for the cutscenes); without, each frame waits 1 / setFrameRate seconds.
  Playback blocks the script on the game clock; a video counts as a cutscene, so it plays
  at 1x whatever the game speed, in step with its sound.
- The sound track (`ImdDecoder::soundTrack()`, the frames' sound in order) plays on the
  mixer's video voice from the frame with the start command; closing the video stops it
  (the last slices, buffered ahead, are cut, as in the original). A video from the speech
  archive (the one openItk opened for the language, `_voiceArchive`) is speech: it plays at
  the Speech volume, and not at all with the speech option off (`Interpreter::speech`,
  the Audio tab's "Speech on", `gobrun --no-speech`); the subtitles always show. Other
  videos' sound (the cutscenes') plays at the Effects volume.
- WORKAROUND (ScummVM): CAIL1 and CAIL2 keep the cursor and don't wait between frames.

**getImdInfo** (draw 0x84): `expr name, var x, var y, var frames, var width, var height`:
the IMD's default position, frame count and size, or -1 in all five when it is missing.

Goblins 3 calls playImd with startFrame 0 and lastFrame -1 only: cutscenes with palette
command 4 or 8 (INTRO from DEMO, the INTn cutscenes), talking heads on the back surface
next to the subtitle (`s[89]`, placed at the speaker), and sound-only videos (flag 0x100:
the coin toss PILE/FACE, ...). Videos in mults (IMD keys) are milestone G3.5.
