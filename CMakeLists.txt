cmake_minimum_required(VERSION 3.19)
project(ghostling C)

# Generate compile_commands.json for clangd/IDE support.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Ghostty's CMake wrapper caches ZIG_EXECUTABLE. When the flake changes the Zig
# package, an existing build tree can otherwise keep using the old store path.
# Resolve Zig from the active environment before adding Ghostty so builds follow
# the flake/dev-shell selection.
find_program(GHOSTLING_ZIG_EXECUTABLE zig REQUIRED NO_CACHE)
set(ZIG_EXECUTABLE "${GHOSTLING_ZIG_EXECUTABLE}" CACHE FILEPATH "Path to the Zig compiler" FORCE)

# --- Raylib ------------------------------------------------------------------
# Use a system-installed raylib if available, otherwise fetch from GitHub.
set(RAYLIB_VERSION 5.5)
find_package(raylib ${RAYLIB_VERSION} QUIET)
if (NOT raylib_FOUND)
  FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
  )
  FetchContent_GetProperties(raylib)
  if (NOT raylib_POPULATED)
    set(FETCHCONTENT_QUIET NO)
    FetchContent_MakeAvailable(raylib)
  endif()
endif()

# --- libghostty-vt -----------------------------------------------------------
# Virtual terminal emulator library from Ghostty. Requires `zig` on PATH.
# The Ghostty CMakeLists.txt delegates to `zig build lib-vt` to produce the
# shared library. See https://ghostty.org/docs/install/build for the
# required Zig version.
FetchContent_Declare(ghostty
  URL https://release.files.ghostty.org/libghostty-vt/1.0.1/libghostty-vt-1.0.1.tar.gz
  URL_HASH SHA256=333dfce97641ef9b536bb9e08907ad86abaaada50e8f7417b70d9284adef37aa
)
FetchContent_MakeAvailable(ghostty)

# --- Embed font as a C header ------------------------------------------------
# Convert the TTF font into a C byte-array header so we don't rely on C23
# #embed (which GCC < 15 doesn't support).
set(FONT_INPUT "${CMAKE_SOURCE_DIR}/fonts/JetBrainsMono-Regular.ttf")
set(FONT_HEADER "${CMAKE_BINARY_DIR}/font_jetbrains_mono.h")
add_custom_command(
  OUTPUT "${FONT_HEADER}"
  COMMAND ${CMAKE_COMMAND}
    -DINPUT=${FONT_INPUT}
    -DOUTPUT=${FONT_HEADER}
    -DARRAY_NAME=font_jetbrains_mono
    -P ${CMAKE_SOURCE_DIR}/bin2header.cmake
  DEPENDS "${FONT_INPUT}" "${CMAKE_SOURCE_DIR}/bin2header.cmake"
  COMMENT "Generating font_jetbrains_mono.h"
)

# --- Our Project -------------------------------------------------------------
add_executable(${PROJECT_NAME} main.c "${FONT_HEADER}")
target_compile_features(${PROJECT_NAME} PRIVATE c_std_11)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
target_link_libraries(${PROJECT_NAME} raylib ghostty-vt)

# macOS requires these system frameworks for raylib's windowing/input/rendering.
if (APPLE)
  target_link_libraries(${PROJECT_NAME} "-framework IOKit")
  target_link_libraries(${PROJECT_NAME} "-framework Cocoa")
  target_link_libraries(${PROJECT_NAME} "-framework OpenGL")
endif()

# Linux requires libutil for forkpty().
if (UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} util)
endif()
