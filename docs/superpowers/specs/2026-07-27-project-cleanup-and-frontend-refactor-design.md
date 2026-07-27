# OrbitalBoy Cleanup and Frontend Refactor Design

**Date:** 2026-07-27

## Objective

Reduce legacy code, duplicated backend paths, build complexity, and frontend coupling without changing the visible emulator behavior or persisted data formats.

The supported GBA runtime backend will be the native mGBA integration. The in-house GBA core will remain available only as an experimental build option and will not be linked into the main executable by default.

## Scope

The work is divided into three independently verifiable deliveries:

1. Backend consolidation and safe legacy removal.
2. Behavior-preserving frontend decomposition.
3. Build, test, CI, and documentation improvements.

The following behavior must remain compatible:

- Game Boy and Game Boy Color emulation.
- Native mGBA execution for Game Boy Advance.
- ROM selector, menus, keyboard and controller bindings.
- Audio, fullscreen, filters, palettes, captures, and save slots.
- Battery saves, RTC data, save states, controls, cheats, and network settings.
- Debug panels, RunLab integration, link cable, and netplay.

No save-state, replay-independent persistence, or configuration format will be intentionally changed.

## Delivery 1: Backend Consolidation and Legacy Removal

### Native mGBA

`MgbaCore` becomes the only supported GBA runtime backend. Application and frontend APIs will accept `MgbaCore` directly rather than exposing libretro-specific overloads.

CMake configuration must fail with an actionable message when GBA support is requested but native mGBA is unavailable. A headless Game Boy-only build may still be produced without mGBA when GBA support is explicitly disabled.

The build options will be:

- `GBEMU_ENABLE_GBA=ON`: enables native mGBA runtime support.
- `GBEMU_BUILD_EXPERIMENTAL_GBA=OFF`: builds the in-house experimental GBA core and its tests.

### Experimental In-House GBA Core

The in-house `System`, `CpuArm7tdmi`, `Memory`, `Ppu`, and `Apu` implementation will move into a dedicated `gbgba_experimental` target. It will not be part of `gbcore` and will not be linked to `gbemu` unless a future runtime integration is designed separately.

Tests that directly exercise the experimental core will only be registered when `GBEMU_BUILD_EXPERIMENTAL_GBA=ON`. Shared GBA types needed by `MgbaCore`, such as `InputState` and debug snapshots, will live in backend-neutral headers so the native backend does not depend on the experimental system header.

### Libretro Removal

The following will be removed:

- `LibretroCore` implementation and public header.
- Dynamic library loading and libretro environment variables.
- libretro frontend overloads.
- duplicated libretro headless, capture, audio, state, and save paths.

Native mGBA will retain equivalent supported behavior.

### Safe Legacy Removal

The unused standalone `cpu_smoke_tests.cpp` will be removed because its cases are already represented in the main test framework. The unused `toolText` helper will be removed.

The incomplete replay integration will be removed from the session:

- ignored `replayPath` parameter;
- unreachable recording/playback state;
- unused replay persistence module;
- reserved replay path plumbing.

Replay can return later as a separately designed feature with user-facing controls and end-to-end tests.

The tracked `.DS_Store` file will be removed and the pattern added to `.gitignore`. Generated build directories remain untracked; documentation will include a safe cleanup command rather than deleting user artifacts automatically.

## Delivery 2: Frontend Decomposition

### Compatibility Strategy

The frontend will be refactored through extractions that preserve behavior. Event bindings, menu labels, timing behavior, persistence locations, threading rules, and UI layout will not be intentionally redesigned.

Each extraction must leave the existing test suite passing before the next extraction begins.

### Session Configuration

The current long argument list will be replaced by focused values:

- `SessionPaths`: state, battery, controls, cheats, palette, RTC, filters, captures, RunLab state, and RunLab queue paths.
- `NetworkOptions`: link and netplay host/client configuration and delay.
- `RealtimeOptions`: scale, audio buffer, RunLab enablement, paths, and network options.

The public entry point becomes:

```cpp
int runRealtime(GameBoy& gameBoy, const RealtimeOptions& options);
```

### Components

`RealtimeSession` owns the lifetime and coordination of frontend components.

`EmulationWorker` owns the emulation thread, frame production, audio production, pause/step state, and synchronized access to `GameBoy`.

`SessionPersistence` owns save-state metadata, battery/RTC writes, palette/filter preferences, controls, and network configuration persistence.

`NetplaySession` owns link transport, delayed inputs, authoritative inputs, rollback history, checksums, and netplay counters.

`RunLabSession` owns command queues, prompt and feedback files, exported state, heartbeat tracking, and visual annotations.

`DebugSession` owns breakpoints, watchpoints, memory writes, memory search, cheats, and debugger presentation state.

`SdlSessionView` owns SDL resources, the event loop, rendering, overlays, menus, window state, and controller lifecycle.

Components communicate through explicit state snapshots and commands. SDL operations remain on the main thread. `GameBoy` mutation remains synchronized through a single owner or its existing mutex until the worker extraction makes single ownership possible.

### GBA Frontend

The GBA realtime frontend will target `MgbaCore` directly. Common rendering and debugger helpers that no longer require templates will use concrete mGBA types. Existing GBA window behavior, controls, captures, save states, battery saves, and debugger features remain available.

## Delivery 3: Build, Tests, CI, and Documentation

### CMake Targets

The project will use focused targets:

- `gbcore`: Game Boy and Game Boy Color core.
- `gbgba_mgba`: native mGBA adapter when `GBEMU_ENABLE_GBA=ON`.
- `gbgba_experimental`: in-house GBA core when `GBEMU_BUILD_EXPERIMENTAL_GBA=ON`.
- `gbfrontend_support`: reusable non-SDL frontend services used by the executable and tests.
- `gbemu`: application and SDL frontend.
- `gbemu_tests`: supported runtime and frontend-support tests.
- `gbgba_experimental_tests`: tests for the experimental GBA implementation.

Production sources shared with tests will be compiled once through libraries rather than separately in the executable and test target.

### Test Registration

CTest will avoid running the same test cases once through `gbemu_tests` and again through every suite. The default CI path runs each case once. Suite-specific invocations remain available through labels or explicit developer commands.

Experimental GBA tests will use accurate suites such as `gba_cpu`, `gba_memory`, `gba_ppu`, `gba_apu`, and `gba_system`.

### Continuous Integration

CI will include:

- Linux headless build and tests.
- Linux SDL compile coverage.
- MCP adapter build and tests.
- AddressSanitizer and UndefinedBehaviorSanitizer job on Linux.
- macOS native build coverage.
- Windows MSVC build coverage.
- Experimental GBA build and tests in a separate job.

Jobs that cannot obtain mGBA from the platform package manager will explicitly disable runtime GBA support rather than silently changing backend behavior.

### Documentation

README build instructions will explain:

- native mGBA as the only supported GBA runtime backend;
- how to build a Game Boy-only binary;
- how to enable the experimental GBA target and tests;
- how to build the MCP adapter;
- how to clean generated build artifacts safely.

The long maintenance history in `docs/guia.md` will be separated from stable architecture guidance. Historical correction notes will move to `CHANGELOG.md` or a dedicated history document, while user-facing instructions remain in maintained guides.

## Error Handling

Missing native mGBA with `GBEMU_ENABLE_GBA=ON` is a configuration error, not a silent fallback.

Failure to initialize SDL, audio, controllers, RunLab files, network transport, saves, or captures will preserve existing user-visible messages unless the current message is ambiguous. Resource ownership extractions will use deterministic cleanup so partial initialization does not leak SDL or thread resources.

Experimental core failures do not affect the default build because the target is disabled by default.

## Testing Strategy

Behavioral changes and bug fixes use test-driven development: a failing test must demonstrate the required behavior before production code is changed.

Pure source removal is verified through reference searches, clean configuration, full compilation, and the existing tests. Structural frontend extractions are protected by characterization tests for configuration mapping, persistence, RunLab commands, netplay state transitions, and lifecycle helpers before code moves.

Every delivery ends with:

- clean default configuration and build;
- supported test suite;
- native mGBA-enabled build where available;
- experimental GBA build and tests;
- sanitizer test run;
- warning review;
- confirmation that generated files and unrelated user changes were not committed.

## Non-Goals

- Redesigning the user interface.
- Changing emulator timing or instruction behavior.
- Changing save-state or battery-save formats.
- Reintroducing replay.
- Adding a new GBA backend.
- Rewriting the frontend in another framework or language.
- Automatically deleting ignored ROMs, saves, captures, or build artifacts.

## Delivery Acceptance

The work is accepted when the default executable contains no libretro or in-house experimental GBA runtime path, native mGBA remains functional, the experimental core is independently buildable, the frontend behavior remains compatible, dead replay and smoke-test code is gone, CI covers the supported configurations, and all relevant builds and tests pass without compiler warnings introduced by the work.
