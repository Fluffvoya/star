# Star Engine

Star Engine is a C++23 game engine currently under implementation. The repository is in the M0 infrastructure
milestone; only the facilities documented below are available.

## Prerequisites

- CMake 3.28 or newer
- Ninja
- A C++23 compiler
- SDL3 3.2 or newer

On Linux, configure and validate the current scaffold with:

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

The Windows x64 and macOS arm64 presets are declared but have not yet been validated by this change. Run
`cmake --list-presets` on the target host to list the presets available on that platform.

`StarHelloWindow` opens an SDL3 window and prints frame timings. For local automated validation, set
`SDL_VIDEODRIVER=dummy` and run it with `--frames=3`.
