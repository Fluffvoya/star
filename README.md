# Star Engine

Star Engine is a C++23 game engine currently under implementation. The repository is in the M0 infrastructure
milestone; only the facilities documented below are available.

See the [M0 infrastructure progress report](docs/progress/M0-infrastructure.md) for completed scope and remaining
work.

The current runtime modules provide Core utilities, an SDL3-backed Platform layer, and a Jobs scheduler with
asynchronous runs, priority queues, parallel-for helpers, and nested-safe job waiting. See the
[M0 Jobs progress report](docs/progress/M0-jobs.md) for its current API constraints and validation status.

## Prerequisites

- CMake 3.28 or newer
- Ninja
- A C++23 compiler
- SDL3 3.2 or newer

Initialize the pinned YAML parser and its dependencies after cloning the repository.

```sh
git submodule update --init --recursive
```

On Linux, configure and validate the current scaffold with:

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

This preset uses `build/linux-dev/` for its CMake cache, with executables in `build/linux-dev/bin/` and
libraries in `build/linux-dev/lib/`. Each preset uses its own build directory.

Runtime modules keep public headers under `include/<module>/` and implementation files under `src/`.

The Windows x64 and macOS arm64 presets are declared but have not yet been validated by this change. Run
`cmake --list-presets` on the target host to list the presets available on that platform.

`star-hello-window` opens an SDL3 window and prints frame timings. For local automated validation, set
`SDL_VIDEODRIVER=dummy` and run it with `--frames=3`.
