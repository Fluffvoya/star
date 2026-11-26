# Star Engine

Star Engine is a C++23 game engine under development. Local validation covers Linux x64 and Windows x64
(MinGW-w64) for the M0 infrastructure, M1 reflection and serialization, and M2 RHI and Vulkan milestones.
See the [milestone reports](docs/progress/README.md) for stage-by-stage results and platform coverage.

The current runtime modules provide Core utilities, an SDL3-backed Platform layer, and a Jobs scheduler with
asynchronous runs, priority queues, parallel-for helpers, and nested-safe job waiting. See the
[M0 Jobs progress report](docs/progress/m0/02-jobs.md) for its API constraints and validation status.

## Prerequisites

- CMake 3.28 or newer
- Ninja
- A C++23 compiler
- SDL3 3.2 or newer
- xxHash 0.8 or newer

Reflection generation uses libclang 18 or newer and its matching Clang driver. CMake skips
StarHeaderTool when libclang is unavailable. The `generated-reflection` and
`generated-reflection-incremental` tests verify the generator when it is built.

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

On Windows, follow the [MinGW build and test guide](docs/windows-build.md) for vcpkg setup, CTest filtering,
reflection generation, and the Vulkan acceptance tests. macOS arm64 presets are available for host validation.

`star-hello-window` opens an SDL3 window and prints frame timings. For local automated validation, set
`SDL_VIDEODRIVER=dummy` and run it with `--frames=3`.
