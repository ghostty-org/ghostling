# Ghostling

## Building

- Requires CMake 3.19+, Ninja, a C compiler, and Zig 0.17.x on PATH
- Configure: `cmake -B build -G Ninja`
- Build: `cmake --build build`
- Release build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
- Run: `./build/ghostling`
- Clean: `cmake --build build --target clean`

## Code Conventions

- C (not C++), single-file project in `main.c`
- Never put side-effect calls inside `assert()` — removed in release builds
- Comment heavily — explain *why*, not just *what*

## Libghostty API Reference

- The main header is `build/_deps/ghostty-src/zig-out/include/ghostty/vt.h`
- These are generated/fetched during the build; run a build first if they
  don't exist

## Updating Libghostty

- Update CMakeLists.txt first to point to the new version
- Clean the build folder immediately to avoid stale libghostty builds
- After cleaning, perform a rebuild to test for any API changes
