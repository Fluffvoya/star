# Star Engine

Star Engine is a C++23 game engine under development. Local validation covers Linux x64 and Windows x64
(MinGW-w64) for the M0 infrastructure, M1 reflection and serialization, and M2 RHI and Vulkan milestones.
See the [milestone reports](docs/progress/README.md) for stage-by-stage results and platform coverage.

Architecture decisions are recorded under [docs/adr/](docs/adr/README.md), and the implementation baseline that
they summarize lives in [docs/architecture/implementation-plan.md](docs/architecture/implementation-plan.md).

The current runtime modules provide Core utilities, an SDL3-backed Platform layer, and a Jobs scheduler with
asynchronous runs, priority queues, parallel-for helpers, and nested-safe job waiting. See the
[M0 Jobs progress report](docs/progress/m0/02-jobs.md) for its API constraints and validation status.

## Prerequisites

- CMake 3.28 or newer
- Ninja
- A C++23 compiler and a C compiler
- SDL3 3.2 or newer
- xxHash 0.8 or newer
- fastgltf for source asset tools (from the vcpkg manifest)

Reflection generation uses libclang 18 or newer and its matching Clang driver. CMake skips
StarHeaderTool when libclang is unavailable. The `generated-reflection` and
`generated-reflection-incremental` tests verify the generator when it is built.

Initialize the pinned EnTT 3.14.0 (MIT) and rapidyaml dependencies after cloning the repository.

```sh
git submodule update --init --recursive
```

Linux Debug and Development presets discover installed CMake packages, including packages installed
through pacman. On native Linux x64 without a toolchain file, CMake also searches
`$VCPKG_ROOT/installed/x64-linux` for fastgltf if the normal search fails. For other installation
prefixes, set `CMAKE_PREFIX_PATH`. To install dependencies from the project manifest, set
`VCPKG_ROOT` and configure a separate build
with `cmake --preset linux-dev -B build/linux-dev-vcpkg -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"`.

Configure Shipping and Profile builds with the vcpkg toolchain on the target host.
Windows presets select that toolchain from `VCPKG_ROOT`.

On Linux, configure and build with the Development preset:

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

Set `-DSTAR_BUILD_ASSET_PIPELINE=OFF` to configure a runtime-only build without the source asset
importers or the forward rendering import sample. The Renderer module consumes derived model and
texture assets. With the vcpkg toolchain, CMake selects the `asset-pipeline` manifest feature when
the source asset tools are enabled.

The preset uses `build/linux-dev/`. For a custom build, pass the same directory to configure and build.

Runtime modules keep public headers under `include/<module>/` and implementation files under `src/`.

On Windows, follow the [MinGW build and test guide](docs/windows-build.md) for vcpkg setup, CTest filtering,
reflection generation, and the Vulkan acceptance tests. macOS arm64 presets are available for host validation.

`star-hello-window` opens an SDL3 window and prints frame timings. For local automated validation, set
`SDL_VIDEODRIVER=dummy` and run it with `--frames=3`.
