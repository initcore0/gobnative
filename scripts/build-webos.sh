#!/bin/sh
# gob2native - cross-builds for LG webOS TVs
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: scripts/build-webos.sh [--tv root@ADDRESS]
#
# Needs webosbrew's native SDK (arm-webos-linux-gnueabi_sdk-buildroot,
# unpacked and relocated): $WEBOS_SDK, or
# ../tools/arm-webos-linux-gnueabi_sdk-buildroot next to the repository.
#
# Builds Release in build-webos/: gobcore, the test_* programs and the
# command-line tools for the TV's 32-bit ARM. With --tv, copies them to
# /tmp/gobnative-test on a rooted TV over SSH and runs the unit tests
# there (nothing outside that folder is touched; /tmp is cleared when the
# TV restarts).

set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
tv=
while [ $# -gt 0 ]; do
	case $1 in
	--tv) tv=$2; shift 2 ;;
	*) echo "usage: $0 [--tv root@ADDRESS]" >&2; exit 2 ;;
	esac
done

sdk=${WEBOS_SDK:-$repo/../tools/arm-webos-linux-gnueabi_sdk-buildroot}
if [ ! -f "$sdk/share/buildroot/toolchainfile.cmake" ]; then
	echo "webOS SDK not found: set WEBOS_SDK to the unpacked arm-webos-linux-gnueabi_sdk-buildroot" >&2
	exit 2
fi
sdk=$(cd "$sdk" && pwd)

build="$repo/build-webos"
cmake -S "$repo" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/webos.cmake" -DWEBOS_SDK="$sdk" \
	-DCMAKE_BUILD_TYPE=Release >/dev/null
jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
cmake --build "$build" -j "$jobs"

tests=$(cd "$build" && ls test_*)
tools="gobrun gobdis gobinstall resdump stktool"

# Only the C library and the TV's own libraries may be needed at run time.
for exe in $tests $tools; do
	"$sdk/bin/arm-webos-linux-gnueabi-readelf" -d "$build/$exe" | awk '/NEEDED/ { print $5 }' | tr -d '[]' |
	while read -r lib; do
		case $lib in
		libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1|ld-linux.so.3) ;;
		*) echo "$exe needs $lib" >&2; exit 1 ;;
		esac
	done
done

[ -n "$tv" ] || exit 0

dir=/tmp/gobnative-test
ssh "$tv" "mkdir -p $dir"
(cd "$build" && COPYFILE_DISABLE=1 tar --no-xattrs -cf - $tests $tools) | ssh "$tv" "tar -xf - -C $dir"
fail=0
for t in $tests; do
	if ssh "$tv" "cd $dir && ./$t" >/dev/null 2>&1; then
		echo "pass  $t"
	else
		echo "FAIL  $t"
		fail=1
	fi
done
exit $fail
