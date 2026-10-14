#!/bin/sh
# gob2native - cross-builds the Windows package with MinGW-w64
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: scripts/build-windows.sh [SDL2 MinGW folder]
#
# Needs the MinGW-w64 cross compilers (x86_64-w64-mingw32-g++; Homebrew's
# mingw-w64, or a Linux distribution's) and SDL2's MinGW development
# package (SDL2-devel-2.x.y-mingw, unpacked). The SDL2 folder is the
# argument, or $SDL2_MINGW, or the newest ../tools/sdl2-mingw/SDL2-* next to
# the repository.
#
# Builds Release in build-win/, checks that the programs import no DLL
# beyond Windows' own and SDL2.dll, and writes
#   dist/gobnative-windows-x64/       the programs, SDL2.dll, README, licences
#   dist/gobnative-windows-x64.zip
# The C++ runtime is linked statically; the programs use the Universal C
# Runtime that comes with Windows 10 and 11.

set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
triple=x86_64-w64-mingw32

sdl=${1:-${SDL2_MINGW:-}}
if [ -z "$sdl" ]; then
	for d in "$repo"/../tools/sdl2-mingw/SDL2-*; do
		[ -d "$d" ] && sdl=$d
	done
fi
if [ -z "$sdl" ] || [ ! -f "$sdl/$triple/lib/cmake/SDL2/sdl2-config.cmake" ]; then
	echo "usage: $0 <SDL2 MinGW folder> (the unpacked SDL2-devel-2.x.y-mingw, with $triple/ in it)" >&2
	exit 2
fi
sdl=$(cd "$sdl" && pwd)
command -v $triple-g++ >/dev/null || { echo "$triple-g++ not found (install mingw-w64)" >&2; exit 2; }

build="$repo/build-win"
dist="$repo/dist"
name=gobnative-windows-x64
out="$dist/$name"

cmake -S "$repo" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/mingw-w64.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DSDL2_DIR="$sdl/$triple/lib/cmake/SDL2" >/dev/null
jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
cmake --build "$build" -j "$jobs"

rm -rf "$out" "$dist/$name.zip"
cmake --install "$build" --prefix "$out" >/dev/null
for exe in "$out"/*.exe; do
	$triple-strip "$exe"
done
(cd "$repo" && git describe --always --dirty 2>/dev/null || echo unknown) > "$out/VERSION.txt"

# Every imported DLL must be Windows' own or in the folder.
system='^(kernel32|user32|gdi32|shell32|ole32|oleaut32|advapi32|comdlg32|imm32|winmm|version|setupapi|opengl32|msvcrt|api-ms-win-.*)\.dll$'
missing=0
for exe in "$out"/*.exe "$out"/*.dll; do
	for dll in $($triple-objdump -p "$exe" | awk '/DLL Name:/ { print $3 }'); do
		lower=$(printf '%s' "$dll" | tr 'A-Z' 'a-z')
		if ! printf '%s' "$lower" | grep -Eq "$system" && [ ! -f "$out/$dll" ]; then
			echo "$(basename "$exe") needs $dll, which is not in the package" >&2
			missing=1
		fi
	done
done
[ $missing = 0 ] || exit 1
# gobnative is a GUI program (subsystem 2), the tools console programs (3).
for exe in "$out"/*.exe; do
	sub=$($triple-objdump -p "$exe" | awk '/^Subsystem/ { print $2 + 0 }')
	want=3
	[ "$(basename "$exe")" = gobnative.exe ] && want=2
	if [ "$sub" != $want ]; then
		echo "$(basename "$exe"): subsystem $sub, expected $want" >&2
		exit 1
	fi
done

(cd "$dist" && rm -f "$name.zip" && zip -qr "$name.zip" "$name")
echo "$out/"
ls -l "$out"
echo "$dist/$name.zip ($(du -h "$dist/$name.zip" | cut -f1))"
