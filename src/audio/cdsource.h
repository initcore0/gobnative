// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where CD music comes from: a stretch of the CD audio image (the GOG
// release's Track1.mp3), or a replacement music file (MP3 or WAV) that a
// music pack provides for a track. Either way the mixer pulls 44.1 kHz
// stereo 16-bit frames from a CdSource.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gob {

class CdSource {
public:
	virtual ~CdSource() = default;
	// Fills up to `frames` interleaved stereo frames at 44.1 kHz. Returns
	// fewer (eventually 0) once the stream has ended.
	virtual size_t read(int16_t *out, size_t frames) = 0;
};

// The CD audio image: one constant bitrate MP3 laid out by CD sector
// (75 sectors per second, starting at sector 0), indexed by MPEG frame so a
// position can be reached without decoding everything before it.
class CdImage {
public:
	bool load(std::vector<uint8_t> mp3);
	bool empty() const { return !_data || _data->frames.empty(); }
	// A stream starting `startSeconds` into the image, or null.
	std::unique_ptr<CdSource> open(double startSeconds) const;

	struct Data {
		std::vector<uint8_t> mp3;
		std::vector<size_t> frames; // byte offset of each MP3 frame
	};

private:
	std::shared_ptr<const Data> _data; // shared with the open streams
};

// A music file: MP3 (any MPEG audio dr_mp3 decodes) or WAV (PCM 8/16/24/32
// bit, or 32/64-bit float), mono or stereo, at any sample rate; converted to
// 44.1 kHz stereo (windowed-sinc resampling). Null if it can't be read, with
// the reason in `error`. The whole file is read here (a WAV is kept as 16-bit
// samples, an MP3 compressed), so the source's read(), which runs in the
// audio callback, never touches the disk: call this on the engine side.
std::unique_ptr<CdSource> openMusicFile(const std::string &path, std::string *error = nullptr);

// Format of a music file without decoding it all ("WAV, 22050 Hz mono,
// 0:42"); empty `error` when the file looks playable.
struct MusicFileInfo {
	std::string description;
	std::string error;
};
MusicFileInfo probeMusicFile(const std::string &path);

} // namespace gob
