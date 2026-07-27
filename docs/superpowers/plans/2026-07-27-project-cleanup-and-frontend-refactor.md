# OrbitalBoy Cleanup and Frontend Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make native mGBA the only supported GBA runtime, isolate the in-house GBA core as experimental, remove dead paths, decompose the SDL frontend, and strengthen build/test/CI coverage without changing visible behavior or persisted formats.

**Architecture:** Split backend-neutral GBA types from the experimental core, compile runtime and experimental backends as separate targets, and reduce the frontend entry point to an options object coordinated by focused session components. Perform behavior-preserving extractions behind characterization tests, then update CI and documentation to reflect the supported configurations.

**Tech Stack:** C++17, CMake 3.16+, SDL2, native libmGBA, CTest, GitHub Actions, AppleClang/Clang sanitizers, MSVC.

## Global Constraints

- Native mGBA is the only supported GBA runtime backend.
- The in-house GBA core is disabled by default and never linked into the default executable.
- Visible UI behavior, controls, timing rules, saves, RTC, save states, and configuration formats remain compatible.
- Replay is removed rather than redesigned.
- Ignored ROMs, saves, captures, and generated build artifacts are never deleted automatically.
- Existing unrelated workspace changes are preserved and excluded from task commits.

---

### Task 1: Establish Configuration Characterization

**Files:**
- Create: `tests/cmake/configure_matrix.cmake`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: existing CMake configuration and target names.
- Produces: `GBEMU_ENABLE_GBA` and `GBEMU_BUILD_EXPERIMENTAL_GBA` configure-time contracts.

- [ ] **Step 1: Write the failing configuration checks**

Create a CMake script that configures disposable child builds and asserts:

```cmake
run_configure(gb_only
    -DGBEMU_ENABLE_GBA=OFF
    -DGBEMU_BUILD_EXPERIMENTAL_GBA=OFF)
assert_target_exists(gb_only gbemu)
assert_target_missing(gb_only gbgba_experimental)

run_configure(experimental
    -DGBEMU_ENABLE_GBA=OFF
    -DGBEMU_BUILD_EXPERIMENTAL_GBA=ON)
assert_target_exists(experimental gbgba_experimental)
```

- [ ] **Step 2: Run the checks and confirm they fail because the options/targets do not exist**

Run:

```bash
cmake -P tests/cmake/configure_matrix.cmake
```

Expected: failure naming `GBEMU_ENABLE_GBA` or `gbgba_experimental`.

- [ ] **Step 3: Add the two options without changing source ownership yet**

Add:

```cmake
option(GBEMU_ENABLE_GBA "Build native mGBA runtime support" ON)
option(GBEMU_BUILD_EXPERIMENTAL_GBA "Build the in-house experimental GBA core and tests" OFF)
```

When `GBEMU_ENABLE_GBA=ON`, require both the mGBA headers and library with a clear `FATAL_ERROR`. When it is `OFF`, do not define `GBEMU_HAVE_MGBA`.

- [ ] **Step 4: Run the configuration checks and baseline tests**

Run:

```bash
cmake -P tests/cmake/configure_matrix.cmake
cmake -S . -B /tmp/orbitalboy-plan-default -DBUILD_TESTING=ON
cmake --build /tmp/orbitalboy-plan-default -j 8
ctest --test-dir /tmp/orbitalboy-plan-default --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/cmake/configure_matrix.cmake .github/workflows/ci.yml
git commit -m "build: define supported GBA build modes"
```

### Task 2: Separate Backend-Neutral GBA Types

**Files:**
- Create: `include/gb/core/gba/types.hpp`
- Modify: `include/gb/core/gba/system.hpp`
- Modify: `include/gb/core/gba/mgba_core.hpp`
- Modify: `src/core/gba/mgba_core.cpp`
- Test: `tests/state_and_options_tests.cpp`

**Interfaces:**
- Produces: `gb::gba::InputState` in `types.hpp`.
- Consumes: fixed 240x160 framebuffer and 44,100 Hz native frontend contract.

- [ ] **Step 1: Add a compile-time dependency test**

Add a small translation-unit target that includes only:

```cpp
#include "gb/core/gba/mgba_core.hpp"

static_assert(gb::gba::MgbaCore::ScreenWidth == 240);
static_assert(gb::gba::MgbaCore::ScreenHeight == 160);
static_assert(gb::gba::MgbaCore::SampleRate == 44100);
```

The dependency output must not contain `system.hpp`.

- [ ] **Step 2: Verify the dependency test fails**

Run the target with compiler dependency generation and confirm `system.hpp` is present.

- [ ] **Step 3: Move `InputState` to `types.hpp`**

Define:

```cpp
namespace gb::gba {
struct InputState {
    bool a = false;
    bool b = false;
    bool select = false;
    bool start = false;
    bool right = false;
    bool left = false;
    bool up = false;
    bool down = false;
    bool r = false;
    bool l = false;
};
}
```

Include this header from both `system.hpp` and `mgba_core.hpp`; remove the `system.hpp` include from `mgba_core.hpp`.

- [ ] **Step 4: Run the dependency test and full tests**

- [ ] **Step 5: Commit**

```bash
git add include/gb/core/gba/types.hpp include/gb/core/gba/system.hpp include/gb/core/gba/mgba_core.hpp src/core/gba/mgba_core.cpp tests
git commit -m "refactor: separate shared GBA input types"
```

### Task 3: Isolate the Experimental GBA Core

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/test_main.cpp`
- Modify: `tests/gba_phase2_tests.cpp`
- Modify: `tests/state_and_options_tests.cpp`

**Interfaces:**
- Produces: `gbgba_experimental` and `gbgba_experimental_tests`.
- Consumes: backend-neutral GBA types from Task 2.

- [ ] **Step 1: Extend configuration checks for target linkage**

Assert that the default `gbcore` source list excludes:

```text
src/core/gba/system.cpp
src/core/gba/memory.cpp
src/core/gba/cpu.cpp
src/core/gba/ppu.cpp
src/core/gba/apu.cpp
```

Assert those sources belong to `gbgba_experimental` only when the option is enabled.

- [ ] **Step 2: Verify the check fails**

- [ ] **Step 3: Create focused CMake source lists and targets**

`gbcore` retains GB/GBC sources. `gbgba_mgba` owns `mgba_core.cpp`. `gbgba_experimental` owns the in-house core. Split experimental tests from supported runtime tests and link only the required targets.

- [ ] **Step 4: Reclassify experimental test suites**

Use `gba_cpu`, `gba_memory`, `gba_ppu`, `gba_apu`, and `gba_system` suite names instead of reporting all experimental behavior as `cpu`.

- [ ] **Step 5: Verify both build modes**

Run default, GB-only, and experimental configurations followed by their registered tests.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests
git commit -m "build: isolate the experimental GBA core"
```

### Task 4: Remove Libretro Runtime Support

**Files:**
- Delete: `include/gb/core/gba/libretro_core.hpp`
- Delete: `src/core/gba/libretro_core.cpp`
- Modify: `include/gb/app/sdl_frontend.hpp`
- Modify: `include/gb/app/frontend/gba_realtime.hpp`
- Modify: `src/app/sdl_frontend.cpp`
- Modify: `src/app/frontend/gba_realtime.cpp`
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/state_and_options_tests.cpp`

**Interfaces:**
- Produces: one GBA application path using `MgbaCore`.

- [ ] **Step 1: Add a source/reference guard test**

The guard must fail if tracked sources contain:

```text
LibretroCore
GBEMU_GBA_LIBRETRO_CORE
GBEMU_GBA_CORE
runGbaLibretroRealtime
libretro_core.cpp
```

- [ ] **Step 2: Verify the guard fails against the current tree**

- [ ] **Step 3: Collapse the GBA frontend API**

Expose:

```cpp
int runGbaRealtime(
    gba::MgbaCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir);
```

Remove libretro overloads and replace templated helpers that have only one remaining caller with concrete `MgbaCore` functions.

- [ ] **Step 4: Simplify application startup and headless execution**

Remove dynamic core selection, libretro loading, duplicated BMP functions, and duplicated headless loops. Keep the native mGBA load, audio dump, frame dump, saves, and realtime path.

- [ ] **Step 5: Delete libretro sources and run the guard**

- [ ] **Step 6: Run native mGBA build and tests**

- [ ] **Step 7: Commit**

```bash
git add -A include/gb/core/gba src/core/gba include/gb/app src/app CMakeLists.txt tests
git commit -m "refactor: use native mGBA as the sole runtime backend"
```

### Task 5: Remove Incomplete Replay and Safe Legacy Files

**Files:**
- Delete: `include/gb/app/frontend/realtime/replay_io.hpp`
- Delete: `src/app/frontend/realtime/replay_io.cpp`
- Delete: `tests/cpu_smoke_tests.cpp`
- Delete: `.DS_Store`
- Modify: `.gitignore`
- Modify: `include/gb/app/sdl_frontend.hpp`
- Modify: `include/gb/app/frontend/realtime.hpp`
- Modify: `src/app/sdl_frontend.cpp`
- Modify: `src/app/frontend/realtime.cpp`
- Modify: `src/app/main.cpp`
- Modify: `tests/frontend_feature_tests.cpp`
- Modify: `tools/orbitalboy-mcp-cpp/tests/test_tools.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: no replay path or unreachable replay state in the runtime API.

- [ ] **Step 1: Add a source/reference guard for replay**

Fail on `replayPath`, `ReplayData`, `replayRecording`, `replayPlaying`, `saveReplayFile`, and `loadReplayFile`.

- [ ] **Step 2: Verify the guard fails**

- [ ] **Step 3: Remove replay plumbing and tests**

Remove the ignored parameter, state variables, branches in the emulation loop, persistence module, and CMake source entry.

- [ ] **Step 4: Remove legacy test and warning**

Delete the standalone smoke test and remove unused `toolText`.

- [ ] **Step 5: Ignore macOS metadata**

Add:

```gitignore
.DS_Store
**/.DS_Store
```

Remove only the tracked root `.DS_Store`.

- [ ] **Step 6: Run reference guards, warning-enabled build, and tests**

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "chore: remove incomplete replay and legacy files"
```

### Task 6: Replace the Realtime Argument List

**Files:**
- Create: `include/gb/app/frontend/realtime_options.hpp`
- Modify: `include/gb/app/frontend/realtime.hpp`
- Modify: `include/gb/app/sdl_frontend.hpp`
- Modify: `src/app/sdl_frontend.cpp`
- Modify: `src/app/frontend/realtime.cpp`
- Modify: `src/app/main.cpp`
- Test: `tests/frontend_feature_tests.cpp`

**Interfaces:**
- Produces:

```cpp
struct SessionPaths;
struct NetworkOptions;
struct RealtimeOptions;
int runRealtime(GameBoy&, const RealtimeOptions&);
```

- [ ] **Step 1: Write a failing options-mapping test**

Construct `RealtimeOptions` from a known ROM and assert every path and network field reaches the public frontend boundary unchanged.

- [ ] **Step 2: Verify it fails because the types do not exist**

- [ ] **Step 3: Add the option structures and mapping helper**

Use value types with defaults matching current behavior. Build them in `main.cpp` and replace the long call.

- [ ] **Step 4: Run focused and full tests**

- [ ] **Step 5: Commit**

```bash
git add include/gb/app src/app tests/frontend_feature_tests.cpp
git commit -m "refactor: encapsulate realtime session options"
```

### Task 7: Extract Persistence and RunLab Sessions

**Files:**
- Create: `include/gb/app/frontend/realtime/session_persistence.hpp`
- Create: `src/app/frontend/realtime/session_persistence.cpp`
- Create: `include/gb/app/frontend/realtime/runlab_session.hpp`
- Create: `src/app/frontend/realtime/runlab_session.cpp`
- Modify: `src/app/frontend/realtime.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/frontend_feature_tests.cpp`
- Test: `tests/state_and_options_tests.cpp`

**Interfaces:**
- Produces: `SessionPersistence` for load/save preferences and `RunLabSession` for file/queue state.

- [ ] **Step 1: Write failing characterization tests**

Cover preference round trips, RunLab prompt escaping, queue paths, feedback offsets, and state export throttling using temporary directories.

- [ ] **Step 2: Verify failures identify missing session classes**

- [ ] **Step 3: Implement the focused classes by moving existing behavior**

Keep file formats and messages unchanged. `realtime.cpp` delegates rather than duplicating file operations.

- [ ] **Step 4: Run focused tests and full tests**

- [ ] **Step 5: Commit**

```bash
git add include/gb/app/frontend/realtime src/app/frontend/realtime tests CMakeLists.txt
git commit -m "refactor: extract persistence and RunLab sessions"
```

### Task 8: Extract Netplay and Debug State

**Files:**
- Create: `include/gb/app/frontend/realtime/netplay_session.hpp`
- Create: `src/app/frontend/realtime/netplay_session.cpp`
- Create: `include/gb/app/frontend/realtime/debug_session.hpp`
- Create: `src/app/frontend/realtime/debug_session.cpp`
- Modify: `src/app/frontend/realtime.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/frontend_feature_tests.cpp`

**Interfaces:**
- Produces: deterministic netplay input/history transitions and debugger state/commands independent of SDL rendering.

- [ ] **Step 1: Write failing netplay transition tests**

Cover delayed local input, predicted remote input replacement, bounded history, rollback counters, and checksum mismatch counters.

- [ ] **Step 2: Write failing debug-state tests**

Cover breakpoint toggling, writable-address validation, queued writes, memory-search refinement, and watch freeze state.

- [ ] **Step 3: Verify both test groups fail for missing classes**

- [ ] **Step 4: Move existing state and algorithms into the new classes**

Do not change packet formats, history limits, address rules, or user-visible behavior.

- [ ] **Step 5: Run focused tests, full tests, and sanitizer tests**

- [ ] **Step 6: Commit**

```bash
git add include/gb/app/frontend/realtime src/app/frontend/realtime tests CMakeLists.txt
git commit -m "refactor: extract netplay and debug session state"
```

### Task 9: Extract SDL Resource and Worker Lifecycles

**Files:**
- Create: `include/gb/app/frontend/realtime/emulation_worker.hpp`
- Create: `src/app/frontend/realtime/emulation_worker.cpp`
- Create: `include/gb/app/frontend/realtime/sdl_session_view.hpp`
- Create: `src/app/frontend/realtime/sdl_session_view.cpp`
- Create: `include/gb/app/frontend/realtime/realtime_session.hpp`
- Create: `src/app/frontend/realtime/realtime_session.cpp`
- Modify: `src/app/frontend/realtime.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/frontend_feature_tests.cpp`

**Interfaces:**
- Produces: deterministic worker start/stop/join and RAII-owned SDL resources coordinated by `RealtimeSession`.

- [ ] **Step 1: Write failing lifecycle tests**

Use non-SDL fakes for queues and worker callbacks. Assert partial initialization cleanup, idempotent stop, queue closure, and joining all started workers.

- [ ] **Step 2: Verify the lifecycle tests fail**

- [ ] **Step 3: Implement `EmulationWorker`**

Move emulation/render/audio worker ownership and synchronization without changing timing or queue depths.

- [ ] **Step 4: Implement SDL RAII ownership**

`SdlSessionView` destroys textures, controllers, renderer, window, audio device, and SDL in reverse initialization order. SDL calls and event polling remain on the main thread.

- [ ] **Step 5: Reduce `runRealtime` to session construction and execution**

The entry point creates `RealtimeSession`, calls `run()`, and returns its result.

- [ ] **Step 6: Run tests and manually smoke-test GB/GBC frontend**

Run a known local ROM only when available; automated verification must not depend on ROM files.

- [ ] **Step 7: Commit**

```bash
git add include/gb/app/frontend/realtime src/app/frontend/realtime CMakeLists.txt tests
git commit -m "refactor: extract realtime session lifecycles"
```

### Task 10: Reorganize Shared Build Targets and CTest

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tools/orbitalboy-mcp-cpp/CMakeLists.txt`
- Modify: `tests/test_main.cpp`
- Modify: `tests/test_framework.hpp`

**Interfaces:**
- Produces: `gbfrontend_support` and one default execution per test case.

- [ ] **Step 1: Add configuration assertions for source ownership**

Assert production support sources compile through `gbfrontend_support` rather than separately in `gbemu` and `gbemu_tests`.

- [ ] **Step 2: Verify the assertion fails**

- [ ] **Step 3: Create the support library and simplify targets**

Link the executable and tests to the support library. Preserve platform-specific SDL linkage and compile definitions.

- [ ] **Step 4: Remove duplicated default CTest execution**

Register the full supported suite once. Retain suite filtering as an explicit developer command and use labels for separately built experimental/MCP tests.

- [ ] **Step 5: Run clean default, headless, SDL, MCP, and experimental builds**

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tools/orbitalboy-mcp-cpp/CMakeLists.txt tests
git commit -m "build: reuse frontend support and streamline tests"
```

### Task 11: Expand CI and Update Documentation

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Modify: `docs/guia.md`
- Create: `CHANGELOG.md`

**Interfaces:**
- Produces: documented supported build matrix and stable architecture guide.

- [ ] **Step 1: Validate workflow syntax locally where tooling is available**

Check that every job uses explicit GBA options and that Linux covers headless, SDL compile, MCP, sanitizers, and experimental core.

- [ ] **Step 2: Add macOS and Windows build jobs**

macOS installs mGBA and SDL2. Windows either installs supported dependencies or explicitly builds GB-only; no job silently falls back to another GBA backend.

- [ ] **Step 3: Update README build commands**

Document:

```bash
cmake -S . -B build -DGBEMU_ENABLE_GBA=ON
cmake -S . -B build-gb -DGBEMU_ENABLE_GBA=OFF
cmake -S . -B build-experimental \
  -DGBEMU_ENABLE_GBA=OFF \
  -DGBEMU_BUILD_EXPERIMENTAL_GBA=ON \
  -DBUILD_TESTING=ON
```

- [ ] **Step 4: Separate stable guide content from maintenance history**

Move dated correction notes into `CHANGELOG.md`. Keep architecture, controls, and supported behavior in `docs/guia.md`.

- [ ] **Step 5: Run documentation/reference guards**

Confirm no documentation mentions libretro as a runtime backend or presents the experimental core as default.

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/ci.yml README.md docs/guia.md CHANGELOG.md
git commit -m "ci: cover supported builds and document backend policy"
```

### Task 12: Final Verification and Cleanup Report

**Files:**
- Modify only if verification exposes a defect.

**Interfaces:**
- Consumes: all previous tasks.
- Produces: evidence that the refactor meets the design acceptance criteria.

- [ ] **Step 1: Run formatting and reference guards**

Confirm no libretro/replay/dead-smoke references and no whitespace errors.

- [ ] **Step 2: Run clean build matrix**

Build and test:

- default native mGBA + SDL + MCP;
- GB-only headless;
- experimental GBA tests;
- sanitizer build.

- [ ] **Step 3: Review compiler warnings**

The project must introduce no warnings under `-Wall -Wextra -Wpedantic`; fix any warning before continuing.

- [ ] **Step 4: Inspect executable linkage**

Confirm the default executable links native mGBA and contains no libretro loader or experimental core symbols.

- [ ] **Step 5: Inspect Git scope**

Confirm unrelated user changes and ignored ROM/save/build artifacts are not staged or committed.

- [ ] **Step 6: Request code review**

Use the requesting-code-review workflow, resolve actionable findings, and repeat the relevant verification commands.

- [ ] **Step 7: Commit verification-only fixes if required**

List the files changed by verification with `git diff --name-only`, stage only those paths individually, review `git diff --cached`, and commit them with:

```bash
git commit -m "fix: address final refactor verification findings"
```
