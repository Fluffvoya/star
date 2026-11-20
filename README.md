# Star Engine

Star Engine is a C++23 game engine currently under implementation. The repository has completed the Linux x64
implementation and local validation for the M0 infrastructure milestone. Windows x64 and macOS arm64 host
validation remain for the M0 release gate.

See the [M0 infrastructure progress report](docs/progress/M0-infrastructure.md) for completed scope and remaining
work.

M1 reflection work has started with the [reflection foundation progress report](docs/progress/M1/M1-01-reflection-foundation.md).

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

Shipping and Profile dependency versions are pinned by the repository vcpkg manifest. Configure those builds with
the vcpkg toolchain on the target host.

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
