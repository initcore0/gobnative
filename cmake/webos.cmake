# gob2native - cross-compiling for LG webOS TVs (32-bit ARM)
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   cmake -S . -B build-webos -DCMAKE_TOOLCHAIN_FILE=cmake/webos.cmake \
#         -DWEBOS_SDK=/path/to/arm-webos-linux-gnueabi_sdk-buildroot
#
# scripts/build-webos.sh does this. The SDK is webosbrew's native toolchain
# (buildroot, GCC 12, arm-webos-linux-gnueabi), unpacked and relocated
# (relocate-sdk.sh); its folder is WEBOS_SDK, or $WEBOS_SDK, or
# ../tools/arm-webos-linux-gnueabi_sdk-buildroot next to the repository.
# Its sysroot has glibc 2.12, so the programs run on every webOS release;
# the TVs' own libstdc++ is older than C++20 needs, so the C++ runtime is
# linked in statically.

if(NOT WEBOS_SDK)
	if(DEFINED ENV{WEBOS_SDK})
		set(WEBOS_SDK "$ENV{WEBOS_SDK}")
	else()
		get_filename_component(WEBOS_SDK
			"${CMAKE_CURRENT_LIST_DIR}/../../tools/arm-webos-linux-gnueabi_sdk-buildroot" ABSOLUTE)
	endif()
endif()
if(NOT EXISTS "${WEBOS_SDK}/share/buildroot/toolchainfile.cmake")
	message(FATAL_ERROR "webOS SDK not found at ${WEBOS_SDK}: set WEBOS_SDK")
endif()
# (kept for try_compile, which reads the toolchain file again)
set(WEBOS_SDK "${WEBOS_SDK}" CACHE PATH "webOS native SDK (arm-webos-linux-gnueabi_sdk-buildroot)")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES WEBOS_SDK)

# (before the SDK's file, which sets them only when they aren't set)
set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc" CACHE STRING "Linker flags for executables")

include("${WEBOS_SDK}/share/buildroot/toolchainfile.cmake")

set(WEBOS ON)
