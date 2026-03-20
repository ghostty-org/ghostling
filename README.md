# Ghostling - Minimal libghostty Terminal

Ghostling is a demo project meant to highlight a minimum
functional terminal built on the libghostty C API in a single C file.

The example uses Raylib for windowing and rendering. It is single-threaded
(although libghostty-vt supports threading) and uses a 2D graphics renderer
instead of a direct GPU renderer like the primary Ghostty GUI. This is to
showcase the flexibility of libghostty and how it can be used in a variety of
contexts.

> [!IMPORTANT]
>
> The Ghostling terminal isn't meant to be a full featured, daily use
> terminal. It is a minimal viable terminal based on libghostty. Still,
> it supports a lot more features than even the average terminal emulator!

## Features

Despite being a minimal, thin layer above libghostty, look at all the
features you _do get_:

- Resize with text reflow
- Full 24-bit color and 256-color palette support
- Bold, italic, and inverse text styles
- Unicode and multi-codepoint grapheme handling (no shaping or layout)
- Keyboard input with modifier support (Shift, Ctrl, Alt, Super)
- Kitty keyboard protocol support
- Mouse tracking (X10, normal, button, and any-event modes)
- Mouse reporting formats (SGR, URxvt, UTF8, X10)
- Scroll wheel support (viewport scrollback or forwarded to applications)
- Scrollbar with mouse drag-to-scroll
- Focus reporting (CSI I / CSI O)
- And more. Effectively all the terminal emulation features supported
  by Ghostty!

### What Is Coming

These features aren't properly exposed by libghostty-vt yet but will be:

- Kitty Graphics Protocol
- OSC clipboard support
- OSC title setting

This list is incomplete and we'll add things as we find them.

### What You Won't Ever Get

libghostty is focused on core terminal emulation features. As such,
you don't get features that are provided by the GUI above the terminal
emulation layer, such as:

- Tabs
- Multiple windows
- Splits
- Session management
- Configuration file or GUI
- Search UI (although search internals are provided by libghostty-vt)

These are the things that libghostty consumers are expected to implement
on their own, if they want them. This example doesn't implement these
to try to stay as minimal as possible.

## Building

Requires CMake 3.11+, a C23-capable compiler, and Zig 0.15.x on PATH.
Raylib is fetched automatically via CMake's FetchContent if not already installed.

```sh
cmake -B build -G Ninja
cmake --build build
./build/ghostling
```

For a release (optimized) build:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

After the initial configure, you only need to run the build step:

```sh
cmake --build build
```
