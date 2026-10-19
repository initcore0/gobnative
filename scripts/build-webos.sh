#!/bin/sh
# gob2native - cross-builds for LG webOS TVs
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: scripts/build-webos.sh [--test root@ADDRESS] [--install root@ADDRESS]
#
# Needs, next to the repository (or where the variables say):
# - webosbrew's native SDK (arm-webos-linux-gnueabi_sdk-buildroot,
#   unpacked and relocated): $WEBOS_SDK, or
#   ../tools/arm-webos-linux-gnueabi_sdk-buildroot;
# - SDL2 from webosbrew's SDL-webOS (a release's SDL2-x.y.z-webos.tar.gz,
#   unpacked; not the -abi one): $SDL2_WEBOS, or the newest
#   ../tools/sdl2-webos/SDL2-*;
# - ares-package from the webOS CLI (@webos-tools/cli): on the PATH, or
#   ../tools/webos-cli/node_modules/.bin.
#
# Builds Release in build-webos/: gobcore, the test_* programs, the
# command-line tools and gobnative for the TV's 32-bit ARM, and writes
#   dist/webos/                       the package's files
#   dist/<app id>_<version>_arm.ipk   the package
#   dist/<app id>.manifest.json       its Homebrew Channel manifest
# The version is $GOB_VERSION, else the latest vX.Y.Z tag's X.Y.Z (webOS
# packages take only numbers), else appinfo.json's.
# --test copies the tests and tools to /tmp/gobnative-test on a rooted TV
# over SSH and runs the unit tests there (nothing outside that folder is
# touched; /tmp is cleared when the TV restarts). --install installs the
# package on a rooted TV (as the Homebrew Channel does) and starts it.

set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
test_tv=
install_tv=
while [ $# -gt 0 ]; do
	case $1 in
	--test) test_tv=$2; shift 2 ;;
	--install) install_tv=$2; shift 2 ;;
	*) echo "usage: $0 [--test root@ADDRESS] [--install root@ADDRESS]" >&2; exit 2 ;;
	esac
done

sdk=${WEBOS_SDK:-$repo/../tools/arm-webos-linux-gnueabi_sdk-buildroot}
if [ ! -f "$sdk/share/buildroot/toolchainfile.cmake" ]; then
	echo "webOS SDK not found: set WEBOS_SDK to the unpacked arm-webos-linux-gnueabi_sdk-buildroot" >&2
	exit 2
fi
sdk=$(cd "$sdk" && pwd)

sdl=${SDL2_WEBOS:-}
if [ -z "$sdl" ]; then
	for d in "$repo"/../tools/sdl2-webos/SDL2-*; do
		[ -d "$d" ] && sdl=$d
	done
fi
if [ -z "$sdl" ] || [ ! -f "$sdl/lib/cmake/SDL2/SDL2Config.cmake" ]; then
	echo "SDL-webOS not found: set SDL2_WEBOS to an unpacked SDL2-x.y.z-webos release" >&2
	exit 2
fi
sdl=$(cd "$sdl" && pwd)

PATH=$PATH:$repo/../tools/webos-cli/node_modules/.bin
command -v ares-package >/dev/null || { echo "ares-package not found (npm install @webos-tools/cli)" >&2; exit 2; }

build="$repo/build-webos"
cmake -S "$repo" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/webos.cmake" -DWEBOS_SDK="$sdk" \
	-DSDL2_DIR="$sdl/lib/cmake/SDL2" -DCMAKE_BUILD_TYPE=Release >/dev/null
jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
cmake --build "$build" -j "$jobs"

tests=$(cd "$build" && ls test_*)
tools="gobrun gobdis gobinstall resdump stktool"

# Only the C library and the TV's own libraries may be needed at run time;
# gobnative also needs the GPU's and SDL2 (in the package).
for exe in $tests $tools gobnative; do
	"$sdk/bin/arm-webos-linux-gnueabi-readelf" -d "$build/$exe" | awk '/NEEDED/ { print $5 }' | tr -d '[]' |
	while read -r lib; do
		case $exe:$lib in
		*:libc.so.6|*:libm.so.6|*:libpthread.so.0|*:libdl.so.2|*:librt.so.1|*:ld-linux.so.3) ;;
		gobnative:libSDL2-2.0.so.0|gobnative:libGLESv2.so.2) ;;
		*) echo "$exe needs $lib" >&2; exit 1 ;;
		esac
	done
done

# The package.
pkg="$repo/dist/webos"
rm -rf "$pkg"
cmake --install "$build" --prefix "$pkg" >/dev/null
"$sdk/bin/arm-webos-linux-gnueabi-strip" "$pkg/gobnative"
(cd "$repo" && git describe --always --dirty 2>/dev/null || echo unknown) > "$pkg/VERSION.txt"
field() { sed -n "s/.*\"$1\": *\"\([^\"]*\)\".*/\1/p" "$pkg/appinfo.json"; }
id=$(field id)
version=${GOB_VERSION:-$(cd "$repo" && git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null | sed 's/^v//')}
if printf '%s' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
	sed -i.bak "s/\"version\": *\"[^\"]*\"/\"version\": \"$version\"/" "$pkg/appinfo.json"
	rm -f "$pkg/appinfo.json.bak"
fi
version=$(field version)
rm -f "$repo/dist/${id}_"*.ipk "$repo/dist/$id.manifest.json"
ares-package --no-minify --outdir "$repo/dist" "$pkg" >/dev/null
ipk=$(ls "$repo/dist/${id}_"*.ipk)
echo "$ipk"

# The Homebrew Channel's manifest (webosbrew's format): the package next to
# it in a GitHub release (ipkUrl is relative), its hash and size.
source_url=${GOB_SOURCE_URL:-https://github.com/initcore0/gobnative}
icon_url=$(printf '%s' "$source_url" | sed 's|//github.com/|//raw.githubusercontent.com/|')/main/webos/icon130.png
sha=$( (command -v sha256sum >/dev/null && sha256sum "$ipk" || shasum -a 256 "$ipk") | cut -d' ' -f1)
size=$(wc -c < "$ipk" | tr -d ' ')
cat > "$repo/dist/$id.manifest.json" <<EOF
{
  "id": "$id",
  "version": "$version",
  "type": "$(field type)",
  "title": "$(field title)",
  "appDescription": "$(field appDescription)",
  "iconUri": "$icon_url",
  "sourceUrl": "$source_url",
  "rootRequired": false,
  "ipkUrl": "$(basename "$ipk")",
  "ipkHash": { "sha256": "$sha" },
  "ipkSize": $size
}
EOF
echo "$repo/dist/$id.manifest.json"

if [ -n "$test_tv" ]; then
	dir=/tmp/gobnative-test
	ssh "$test_tv" "mkdir -p $dir"
	(cd "$build" && COPYFILE_DISABLE=1 tar --no-xattrs -cf - $tests $tools) | ssh "$test_tv" "tar -xf - -C $dir"
	fail=0
	for t in $tests; do
		if ssh "$test_tv" "cd $dir && ./$t" >/dev/null 2>&1; then
			echo "pass  $t"
		else
			echo "FAIL  $t"
			fail=1
		fi
	done
	[ $fail = 0 ] || exit 1
fi

if [ -n "$install_tv" ]; then
	# The Homebrew Channel's way: the TV's app installer, with the package
	# in /tmp; it replaces an installed version's files and keeps the rest
	# (data/). luna-send answers only on a terminal (-tt).
	remote=/tmp/$(basename "$ipk")
	scp -q "$ipk" "$install_tv:$remote"
	ssh -tt "$install_tv" "luna-send -n 40 -w 60000 luna://com.webos.appInstallService/dev/install \
		'{\"id\":\"$id\",\"ipkUrl\":\"$remote\",\"subscribe\":true}' | \
		grep -m1 -oE '\"state\": \"(installed|[^\"]*fail[^\"]*)\"|errorText.*'; rm -f $remote; \
		luna-send -n 1 luna://com.webos.applicationManager/launch '{\"id\":\"$id\"}' >/dev/null; exit" | tr -d '\r'
fi
