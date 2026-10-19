cmake_minimum_required(VERSION 3.20)
project(gobnative CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(MSVC)
	# UTF-8 sources, standard C++ (two-phase lookup, __cplusplus), M_PI,
	# no min/max macros from windows.h, fopen without the _s nagging.
	add_compile_options(/W3 /utf-8 /permissive- /Zc:__cplusplus /EHsc)
	add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
	set(GOB_NO_WARNINGS /w)
else()
	add_compile_options(-Wall -Wextra)
	set(GOB_NO_WARNINGS -w)
	if(WIN32)
		add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
	endif()
endif()

if(EMSCRIPTEN)
	# The web build (scripts/build-web.sh, README "Web"): only the app, with
	# SDL2 from Emscripten's ports and C++ exceptions as WebAssembly
	# exceptions (the engine throws on bad data; coroutines need nothing).
	add_compile_options(-fwasm-exceptions)
	add_link_options(-fwasm-exceptions)
	add_library(SDL2::SDL2 INTERFACE IMPORTED)
	set_target_properties(SDL2::SDL2 PROPERTIES INTERFACE_COMPILE_OPTIONS "-sUSE_SDL=2"
	                      INTERFACE_LINK_OPTIONS "-sUSE_SDL=2")
endif()

if(WEBOS)
	# The LG webOS TV build (cmake/webos.cmake, scripts/build-webos.sh):
	# __WEBOS__ turns on SDL-webOS's own API in its headers.
	add_compile_definitions(GOB_WEBOS __WEBOS__)
endif()

if(WIN32)
	enable_language(RC)
	# The C++ runtime linked in statically (MinGW's and MSVC's): the
	# programs need no DLLs but Windows' own and SDL2.dll.
	# 8 MB of stack for the main thread as on macOS (Windows' default is 1 MB).
	if(MINGW)
		add_link_options(-static-libgcc -static-libstdc++ -static -Wl,--stack,8388608)
	else()
		add_link_options(/STACK:8388608)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	endif()
	# The SDL2 config adds -mwindows to every program; the console tools
	# must stay console programs (gobnative is made a GUI program below).
	set(SDL2_NO_MWINDOWS ON)
endif()

# Windows: every program gets a manifest with UTF-8 as its code page (so
# paths are UTF-8 in std::string, fopen and std::filesystem alike, as on
# macOS), long path support and the modern common controls.
function(gob_windows_program target)
	if(WIN32)
		if(MSVC)
			target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/win/gobnative.manifest)
			target_link_options(${target} PRIVATE /MANIFESTUAC:NO) # the manifest has its own
		else()
			target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/win/manifest.rc)
		endif()
	endif()
endfunction()

add_library(gobcore STATIC
	src/formats/stk.cpp
	src/formats/tot.cpp
	src/formats/anim.cpp
	src/formats/avo.cpp
	src/formats/imd.cpp
	src/data/datafiles.cpp
	src/gfx/image.cpp
	src/script/expression.cpp
	src/script/exprprint.cpp
	src/script/disasm.cpp
	src/engine/game.cpp
	src/engine/rules.cpp
	src/engine/interpreter.cpp
	src/engine/interpreter_v1.cpp
	src/engine/interpreter_v3.cpp
	src/engine/cdaudio.cpp
	src/engine/draw.cpp
	src/engine/hotspots.cpp
	src/engine/scenery.cpp
	src/engine/mult.cpp
	src/engine/mult_v1.cpp
	src/engine/goblins.cpp
	src/engine/goblins_v1.cpp
	src/engine/saves.cpp
	src/engine/saves_v3.cpp
	src/engine/levels.cpp
	src/engine/levels_v1.cpp
	src/engine/levels_v3.cpp
	src/engine/inspect.cpp
	src/engine/skip.cpp
	src/engine/video.cpp
	src/engine/hdrender.cpp
	src/gfx/surface.cpp
	src/gfx/bmp.cpp
	src/gfx/png.cpp
	src/gfx/smooth.cpp
	src/gfx/hdpack.cpp
	src/gfx/pixelfilter.cpp
	src/gfx/font.cpp
	src/audio/mixer.cpp
	src/audio/cdsource.cpp
	src/audio/musicpack.cpp
	src/audio/resample.cpp
	src/audio/dr_mp3_impl.cpp
	src/install/lzma.cpp
	src/install/hash.cpp
	src/install/inno.cpp
	src/install/gameinstall.cpp
	src/install/zip.cpp
)
target_include_directories(gobcore PUBLIC src)
if(NOT EMSCRIPTEN)
	# std::thread (HD pack prefetch); glibc before 2.34 needs libpthread
	# named (webOS).
	find_package(Threads REQUIRED)
	target_link_libraries(gobcore PUBLIC Threads::Threads)
endif()
target_include_directories(gobcore SYSTEM PRIVATE third_party/dr_mp3)
set_source_files_properties(src/audio/dr_mp3_impl.cpp PROPERTIES COMPILE_OPTIONS "${GOB_NO_WARNINGS}")

if(NOT EMSCRIPTEN) # the tools and tests are for the desktop
add_executable(stktool tools/stktool.cpp)
target_link_libraries(stktool PRIVATE gobcore)

add_executable(resdump tools/resdump.cpp)
target_link_libraries(resdump PRIVATE gobcore)

add_executable(gobexport tools/gobexport.cpp tools/frames.cpp)
target_link_libraries(gobexport PRIVATE gobcore)

find_package(SDL2 REQUIRED)
if(TARGET SDL2::SDL2main)
	# Windows: SDL2main (WinMain, UTF-8 argv) calls into SDL2, so SDL2 must
	# follow it on the link line.
	set_property(TARGET SDL2::SDL2main APPEND PROPERTY INTERFACE_LINK_LIBRARIES SDL2::SDL2)
endif()
add_executable(gobview tools/viewer.cpp)
target_link_libraries(gobview PRIVATE gobcore SDL2::SDL2)
if(TARGET SDL2::SDL2main)
	target_link_libraries(gobview PRIVATE SDL2::SDL2main)
endif()

enable_testing()
add_executable(test_expression tests/test_expression.cpp)
target_link_libraries(test_expression PRIVATE gobcore)
add_test(NAME expression COMMAND test_expression)
add_executable(test_png tests/test_png.cpp)
target_link_libraries(test_png PRIVATE gobcore)
add_test(NAME png COMMAND test_png)
add_executable(test_hdpack tests/test_hdpack.cpp)
target_link_libraries(test_hdpack PRIVATE gobcore)
add_test(NAME hdpack COMMAND test_hdpack)
add_executable(test_hdrender tests/test_hdrender.cpp)
target_link_libraries(test_hdrender PRIVATE gobcore)
add_test(NAME hdrender COMMAND test_hdrender)
add_executable(test_mixer tests/test_mixer.cpp)
target_link_libraries(test_mixer PRIVATE gobcore)
add_test(NAME mixer COMMAND test_mixer)
add_executable(test_detect tests/test_detect.cpp)
target_link_libraries(test_detect PRIVATE gobcore)
add_test(NAME detect COMMAND test_detect)
add_executable(test_avo tests/test_avo.cpp)
target_link_libraries(test_avo PRIVATE gobcore)
add_test(NAME avo COMMAND test_avo)
add_executable(test_imd tests/test_imd.cpp)
target_link_libraries(test_imd PRIVATE gobcore)
add_test(NAME imd COMMAND test_imd)
add_executable(test_install tests/test_install.cpp)
target_link_libraries(test_install PRIVATE gobcore)
add_test(NAME install COMMAND test_install)
add_executable(test_prefs tests/test_prefs.cpp src/frontend/prefs.cpp src/frontend/ini.cpp)
target_include_directories(test_prefs PRIVATE src)
add_test(NAME prefs COMMAND test_prefs)
add_executable(test_touch tests/test_touch.cpp src/frontend/touch.cpp)
target_include_directories(test_touch PRIVATE src)
add_test(NAME touch COMMAND test_touch)

add_executable(gobinstall tools/gobinstall.cpp)
target_link_libraries(gobinstall PRIVATE gobcore)

add_executable(gobvideo tools/gobvideo.cpp)
target_link_libraries(gobvideo PRIVATE gobcore)

add_executable(gobdis tools/disasm.cpp)
target_link_libraries(gobdis PRIVATE gobcore)

add_executable(gobrun tools/runner.cpp tools/roomguide.cpp)
target_link_libraries(gobrun PRIVATE gobcore)
endif() # NOT EMSCRIPTEN

# Dear ImGui, for the front end's overlay.
add_library(imgui STATIC
	third_party/imgui/imgui.cpp
	third_party/imgui/imgui_draw.cpp
	third_party/imgui/imgui_tables.cpp
	third_party/imgui/imgui_widgets.cpp
	third_party/imgui/backends/imgui_impl_sdl2.cpp
	third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui SYSTEM PUBLIC third_party/imgui third_party/imgui/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 ${CMAKE_DL_LIBS}) # its GL loader dlopens on Linux
target_compile_options(imgui PRIVATE ${GOB_NO_WARNINGS})
if(EMSCRIPTEN OR WEBOS)
	target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3) # WebGL 2, the TV's GPU
endif()
if(WEBOS)
	target_link_libraries(imgui PUBLIC GLESv2)
endif()
if(APPLE)
	target_link_libraries(imgui PUBLIC "-framework OpenGL")
endif()

# The front end's font (third_party/fonts/nunito), compiled in as byte arrays.
set(GOB_EMBEDDED)
foreach(pair IN ITEMS "nunitoRegular:Nunito-Regular.ttf" "nunitoBold:Nunito-Bold.ttf")
	string(REPLACE ":" ";" pair "${pair}")
	list(GET pair 0 name)
	list(GET pair 1 file)
	set(out ${CMAKE_BINARY_DIR}/embedded/${name}.cpp)
	add_custom_command(OUTPUT ${out}
		COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/third_party/fonts/nunito/${file} -DOUTPUT=${out}
		        -DNAME=${name} -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
		DEPENDS ${CMAKE_SOURCE_DIR}/third_party/fonts/nunito/${file} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
		COMMENT "Embedding ${file}" VERBATIM)
	list(APPEND GOB_EMBEDDED ${out})
endforeach()
# ... and the window icon (assets/icon, drawn by scripts/make-icon.py).
set(out ${CMAKE_BINARY_DIR}/embedded/windowIcon.cpp)
add_custom_command(OUTPUT ${out}
	COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/assets/icon/window-icon.png -DOUTPUT=${out}
	        -DNAME=windowIcon -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
	DEPENDS ${CMAKE_SOURCE_DIR}/assets/icon/window-icon.png ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
	COMMENT "Embedding window-icon.png" VERBATIM)
list(APPEND GOB_EMBEDDED ${out})

# Platform front end (SDL2, OpenGL, ImGui); the engine in gobcore stays portable.
add_executable(gobnative
	tools/gobnative.cpp
	${GOB_EMBEDDED}
	src/frontend/appicon.cpp
	src/frontend/gl.cpp
	src/frontend/ini.cpp
	src/frontend/overlay.cpp
	src/frontend/overlay_tabs.cpp
	src/frontend/library.cpp
	src/frontend/prefs.cpp
	src/frontend/presenter.cpp
	src/frontend/settings.cpp
	src/frontend/sharptext.cpp
	src/frontend/theme.cpp
	src/frontend/tools_tab.cpp
	src/frontend/touch.cpp
)
target_link_libraries(gobnative PRIVATE gobcore imgui SDL2::SDL2)
if(EMSCRIPTEN)
	# gobnative.js + gobnative.wasm, loaded by web/index.html. The app's
	# folder is an IndexedDB file system (IDBFS); no threads (no
	# cross-origin isolation needed); 4 MB of stack as the desktop's
	# 8 MB main threads have more than they need; 32 MB of memory to
	# start with (the library needs little; a game grows it, phones are
	# short of memory).
	target_sources(gobnative PRIVATE src/frontend/web.cpp)
	set_target_properties(gobnative PROPERTIES SUFFIX ".js")
	target_link_options(gobnative PRIVATE
		-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=32MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=4MB
		-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sGL_ENABLE_GET_PROC_ADDRESS=1
		-sFORCE_FILESYSTEM=1 -lidbfs.js -sENVIRONMENT=web
		--js-library ${CMAKE_SOURCE_DIR}/web/library_gobnative.js)
	set_property(TARGET gobnative APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/library_gobnative.js)
endif()
if(WEBOS)
	# The package (cmake --install build-webos --prefix <folder>, then
	# ares-package; scripts/build-webos.sh): gobnative with SDL-webOS's
	# libSDL2 in lib/ (the TVs' own SDL2 is too old), appinfo.json, icons
	# and the licences.
	target_sources(gobnative PRIVATE src/frontend/webos.cpp)
	set_target_properties(gobnative PROPERTIES BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
	install(TARGETS gobnative RUNTIME DESTINATION .)
	get_target_property(_sdl2_lib SDL2::SDL2 IMPORTED_LOCATION_RELEASE)
	if(NOT _sdl2_lib)
		get_target_property(_sdl2_lib SDL2::SDL2 IMPORTED_LOCATION)
	endif()
	install(FILES ${_sdl2_lib} DESTINATION lib RENAME libSDL2-2.0.so.0)
	install(FILES webos/appinfo.json webos/icon80.png webos/icon130.png DESTINATION .)
	install(FILES LICENSE DESTINATION .)
	install(FILES third_party/imgui/LICENSE.txt DESTINATION licences RENAME imgui.txt)
	install(FILES third_party/fonts/nunito/OFL.txt DESTINATION licences RENAME Nunito-OFL.txt)
	find_file(GOB_SDL2_LICENSE LICENSE.txt PATHS ${SDL2_DIR}/../../../share/licenses/SDL2 NO_DEFAULT_PATH
	          NO_CMAKE_FIND_ROOT_PATH)
	if(GOB_SDL2_LICENSE)
		install(FILES ${GOB_SDL2_LICENSE} DESTINATION licences RENAME SDL2.txt)
	endif()
endif()
if(WIN32)
	# A GUI program (no console window); the file dialogs need COM.
	set_target_properties(gobnative PROPERTIES WIN32_EXECUTABLE ON)
	target_sources(gobnative PRIVATE src/frontend/win32.cpp win/icon.rc)
	target_link_libraries(gobnative PRIVATE SDL2::SDL2main ole32 shell32 uuid)
endif()

# macOS: build/Gobnative.app, the same program in an app bundle with its
# icon (Dock, Finder); build/gobnative stays for the command line, the
# scripts and the tests.
if(APPLE AND NOT EMSCRIPTEN)
	execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR} describe --tags --always
	                OUTPUT_VARIABLE GOB_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(NOT GOB_VERSION)
		set(GOB_VERSION "0")
	endif()
	configure_file(macos/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist @ONLY)
	set(GOB_APP ${CMAKE_BINARY_DIR}/Gobnative.app/Contents)
	add_custom_command(TARGET gobnative POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${GOB_APP}/MacOS ${GOB_APP}/Resources
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gobnative> ${GOB_APP}/MacOS/gobnative
		COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/Info.plist ${GOB_APP}/Info.plist
		COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/assets/icon/gobnative.icns ${GOB_APP}/Resources/gobnative.icns
		COMMENT "Gobnative.app" VERBATIM)
endif()

# The names from before the rename keep working: build/gob2native and
# build/gob2view are links to gobnative and gobview.
if(NOT WIN32 AND NOT EMSCRIPTEN)
add_custom_command(TARGET gobnative POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E create_symlink gobnative gob2native
	WORKING_DIRECTORY $<TARGET_FILE_DIR:gobnative>)
add_custom_command(TARGET gobview POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E create_symlink gobview gob2view
	WORKING_DIRECTORY $<TARGET_FILE_DIR:gobview>)
endif()

if(WIN32)
	# The Windows package (cmake --install build --prefix <folder>; used by
	# scripts/build-windows.sh and the GitHub workflow): the programs,
	# SDL2.dll and the licences, all in one folder.
	set(GOB_PROGRAMS gobnative gobinstall gobrun gobview gobexport gobvideo gobdis stktool resdump)
	install(TARGETS ${GOB_PROGRAMS} RUNTIME DESTINATION .)
	install(FILES $<TARGET_FILE:SDL2::SDL2> DESTINATION .)
	install(FILES README.md docs/install.md win/README-Windows.txt DESTINATION .)
	install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
	install(FILES third_party/imgui/LICENSE.txt DESTINATION licences RENAME imgui.txt)
	install(FILES third_party/fonts/nunito/OFL.txt DESTINATION licences RENAME Nunito-OFL.txt)
	file(READ third_party/dr_mp3/dr_mp3.h _drmp3)
	string(FIND "${_drmp3}" "This software is available as a choice" _begin)
	string(SUBSTRING "${_drmp3}" ${_begin} -1 _drmp3)
	string(FIND "${_drmp3}" "*/" _end)
	string(SUBSTRING "${_drmp3}" 0 ${_end} _drmp3)
	file(WRITE ${CMAKE_BINARY_DIR}/dr_mp3-licence.txt "dr_mp3 (David Reid)\n\n${_drmp3}\n")
	install(FILES ${CMAKE_BINARY_DIR}/dr_mp3-licence.txt DESTINATION licences RENAME dr_mp3.txt)
	find_file(GOB_SDL2_LICENSE LICENSE.txt PATHS ${SDL2_DIR}/.. ${SDL2_DIR}/../.. ${SDL2_DIR}/../../..
	          ${SDL2_DIR}/../../../.. NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
	if(GOB_SDL2_LICENSE)
		install(FILES ${GOB_SDL2_LICENSE} DESTINATION licences RENAME SDL2.txt)
	endif()

	foreach(t IN ITEMS stktool resdump gobexport gobview gobinstall gobvideo gobdis gobrun gobnative
			test_expression test_png test_hdpack test_hdrender test_mixer test_detect test_avo test_imd
			test_install test_prefs)
		gob_windows_program(${t})
	endforeach()
endif()
