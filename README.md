# Ghostling - Minimal libghostty Terminal

Ghostling is a demo project meant to highlight a minimum
functional terminal built on the libghostty C API in a single C file.

The example uses Raylib for windowing and rendering. It is single-threaded
(although libghostty-vt supports threading) and uses a 2D graphics renderer
instead of a direct GPU renderer like the primary [Ghostty](https://ghostty.org) GUI. This is to
showcase the flexibility of libghostty and how it can be used in a variety of
contexts.

> [!IMPORTANT]
>
> The Ghostling terminal isn't meant to be a full featured, daily use
> terminal. It is a minimal viable terminal based on libghostty. Still,
> it supports a lot more features than even the average terminal emulator!

<p align="center">
  <img src="demo.gif" alt="Ghostling Demo" />
</p>

## What is Libghostty?

Libghostty is an embeddable library extracted from [Ghostty's](https://ghostty.org) core,
exposing a C and Zig API so any application can embed correct, fast terminal
emulation.

Ghostling uses **libghostty-vt**, a zero-dependency library (not even libc) that
handles VT sequence parsing, terminal state management (cursor position,
styles, text reflow, scrollback, etc.), and renderer state management. It
contains no renderer drawing or windowing code; the consumer (Ghostling, in
this case) provides its own. The core logic is extracted directly from Ghostty
and inherits all of its real-world benefits: excellent, accurate, and complete
terminal emulation support, SIMD-optimized parsing, leading Unicode support,
highly optimized memory usage, and a robust fuzzed and tested codebase, all
proven by millions of daily active users of Ghostty GUI.

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

These are things that could work but haven't been tested or aren't
implemented in Ghostling itself:

- Windows support (libghostty-vt supports Windows)

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

Requires CMake 3.19+, a C compiler, and Zig 0.15.x on PATH.
Raylib is fetched automatically via CMake's FetchContent if not already installed.

```sh
cmake -B build -G Ninja
cmake --build build
./build/ghostling
```

> [!WARNING]
>
> Debug builds are VERY SLOW since Ghostty included a lot of extra
> safety and correctness checks. Do not benchmark debug builds.

For a release (optimized) build:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

After the initial configure, you only need to run the build step:

```sh
cmake --build build
```
