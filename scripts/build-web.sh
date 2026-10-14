#!/bin/sh
# gob2native - builds the web version (WebAssembly) with Emscripten
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: scripts/build-web.sh [--serve [PORT]]
#
# Needs the Emscripten SDK: emcc on the PATH (source emsdk_env.sh), or
# $EMSDK, or ../tools/emsdk next to the repository. Emscripten fetches its
# SDL2 port by itself the first time.
#
# Builds Release in build-web/ and writes dist/web/: index.html,
# gobnative.js, gobnative.wasm and the licences. No game data is part of
# it: the page imports the player's own GOG installers into the browser's
# storage. --serve then serves dist/web on http://localhost:PORT/ (8080)
# with Python's static server, for testing on this computer.

set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)

if ! command -v emcc >/dev/null 2>&1; then
	emsdk=${EMSDK:-$repo/../tools/emsdk}
	if [ ! -f "$emsdk/emsdk_env.sh" ]; then
		echo "emcc not found: source emsdk_env.sh of the Emscripten SDK, or set EMSDK" >&2
		exit 2
	fi
	# (from its own folder: a POSIX sh can't tell where a sourced script is)
	here=$(pwd)
	cd "$emsdk"
	EMSDK_QUIET=1 . ./emsdk_env.sh >/dev/null
	cd "$here"
fi

build="$repo/build-web"
dist="$repo/dist/web"

emcmake cmake -S "$repo" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" --target gobnative --parallel

rm -rf "$dist"
mkdir -p "$dist/licences"
cp "$build/gobnative.js" "$build/gobnative.wasm" "$repo/web/index.html" "$dist/"
cp "$repo/assets/icon/favicon-32.png" "$repo/assets/icon/apple-touch-icon.png" "$dist/"
cp "$repo/LICENSE" "$dist/LICENSE.txt"
cp "$repo/third_party/imgui/LICENSE.txt" "$dist/licences/imgui.txt"
cp "$repo/third_party/fonts/nunito/OFL.txt" "$dist/licences/Nunito-OFL.txt"
port=$(find "$(em-config CACHE)/ports/sdl2" -maxdepth 2 -name LICENSE.txt 2>/dev/null | head -n 1)
[ -n "$port" ] && cp "$port" "$dist/licences/SDL2.txt"
git -C "$repo" describe --always --dirty > "$dist/VERSION.txt" 2>/dev/null || true

echo "dist/web:"
ls -l "$dist" | sed 1d

if [ "${1:-}" = "--serve" ]; then
	p=${2:-8080}
	echo "serving dist/web on http://localhost:$p/ (Ctrl+C stops)"
	exec python3 -m http.server --bind 127.0.0.1 --directory "$dist" "$p"
fi
