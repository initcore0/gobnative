// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/image.h"

#include <filesystem>

namespace gob {

// Writes an 8-bit indexed BMP of `width` x `height` pixels.
bool writeBmp(const std::filesystem::path &path, const uint8_t *pixels, int width, int height, const Palette &pal);

} // namespace gob
