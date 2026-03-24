# Ghostling - Minimal libghostty Terminal

Ghostling is a demo project meant to highlight a minimum
functional terminal built on the libghostty C API in a
[single C file](https://github.com/ghostty-org/ghostling/blob/main/main.c).

The example uses Raylib for windowing and rendering. It uses a 2D graphics renderer
instead of a direct GPU renderer like the primary [Ghostty](https://ghostty.org) GUI. This is to
showcase the flexibility of libghostty and how it can be used in a variety of
contexts.

> [!WARNING]
>
> The Ghostling terminal isn't meant to be a full featured, daily use
> terminal. It is a minimal viable terminal based on libghostty. Also, since
> this is basically a demo, I didn't carefully audit every single place for
> correctness, and this is C, so you've been warned!

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
highly optimized memory usage, and a robust, fuzzed, and tested codebase, all
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

### Limitations Due to Upstreams

There are some known issues with this demo:

- Kitty keyboard protocol support is broken with some inputs. This is
  due to limitations of the underlying Raylib input system; it doesn't
  support rich enough input events to fully and correctly implement the Kitty
  keyboard protocol. This is a [known issue](https://github.com/glfw/glfw/issues/1502).
  The libghostty-vt API supports Kitty keyboard protocol correctly, but
  requires correct input events to do so.

## Building

Requirements:

- [CMake](https://cmake.org/) 3.19+
- [Ninja](https://ninja-build.org/)
- A C compiler
- [Zig](https://ziglang.org/) 0.15.x on PATH
- macOS: [Command Line Tools or Xcode](https://developer.apple.com/xcode/)

```sh
cmake -B build -G Ninja
cmake --build build
./build/ghostling          # Linux / macOS
.\build\ghostling.exe      # Windows
```

On Windows, ghostling uses ConPTY and auto-detects the best available
shell (pwsh > powershell > cmd). Pass a shell path as the first argument
to override (e.g. `ghostling.exe C:\Windows\System32\cmd.exe`).

> [!WARNING]
>
> Debug builds are VERY SLOW since Ghostty included a lot of extra
> safety and correctness checks. Do not benchmark debug builds.

For a release (optimized) build:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

After the initial configuration, you only need to run the build step:

```sh
cmake --build build
```

## FAQ

### Why Not Zig?

libghostty-vt has a fully capable and proven Zig API. Ghostty GUI itself
uses this and is a good -- although complex -- example of how to use it.
However, this demo is meant to showcase the minimal C API since C is so
much more broadly used and accessible to a wide variety of developers and
language ecosystems.

### What about Rust or any other language?

libghostty-vt has a C API and can have zero dependencies, so it can be used
with minimally thin bindings in basically any language. I'm not sure yet if
the Ghostty project will maintain official bindings for languages other than C
and Zig, but I hope the community will create and maintain bindings for many
languages!

### Does libghostty require Raylib?

**No no no!** libghostty has no opinion about the renderer or GUI framework
used; it's even standalone WASM-compatible for browsers and other environments.

libghostty provides a [high-performance render state API](https://libghostty.tip.ghostty.org/group__render.html)
which only keeps track of the _state_ required to build a renderer. This is the
same API used by Ghostty GUI for Metal and OpenGL rendering and in this repository
for the Raylib 2D graphics API. You can layer any renderer on top of this!

### Why CMake, Raylib, etc.?

I needed to pick _something_. Really, any build system and any library
could be used. CMake is widely used and supported, and Raylib is a simple
and elegant library for windowing and 2D rendering that is easy to set up.
Don't get bogged down in these details!
