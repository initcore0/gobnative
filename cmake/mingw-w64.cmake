# gob2native - cross-compiling for 64-bit Windows with MinGW-w64
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
#         -DSDL2_DIR=/path/to/SDL2-2.x.y/x86_64-w64-mingw32/lib/cmake/SDL2
#
# scripts/build-windows.sh does this and packages the result. The compilers
# are found on the PATH (Homebrew's mingw-w64, or a Linux distribution's)
# or under MINGW_PREFIX.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TRIPLE x86_64-w64-mingw32 CACHE STRING "MinGW-w64 target triple")
if(DEFINED ENV{MINGW_PREFIX})
	set(_mingw_bin "$ENV{MINGW_PREFIX}/bin/")
else()
	set(_mingw_bin "")
endif()

set(CMAKE_C_COMPILER ${_mingw_bin}${MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${_mingw_bin}${MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER ${_mingw_bin}${MINGW_TRIPLE}-windres)

# Libraries and headers come from the target's sysroot and the paths given
# on the command line (SDL2_DIR), never from the build machine.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
