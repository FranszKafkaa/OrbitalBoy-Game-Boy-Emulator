# OrbitalBoy RunLab MCP C++ Adapter

Read-only MCP stdio server for OrbitalBoy RunLab.

Build from the repository root:

```bash
cmake -S . -B build -DGBEMU_BUILD_MCP=ON
cmake --build build --target orbitalboy-mcp
```

Run inspection:

```bash
./build/tools/orbitalboy-mcp-cpp/orbitalboy-mcp --state .runlab/current-state.json
```

Optional input queue control:

```bash
./build/gbemu --rom jogo.gb --runlab-control --runlab-command-queue .runlab/commands.jsonl
./build/tools/orbitalboy-mcp-cpp/orbitalboy-mcp --state .runlab/current-state.json --control-queue .runlab/commands.jsonl --prompt-queue .runlab/prompts.jsonl
```

The adapter reads RunLab state/profile JSON and exposes tools, resources, prompts, and optional joypad input queue commands. It never writes emulator memory, opens network sockets, or reads unconfigured arbitrary files.

Control tools include tap, hold, release_all, advance_frames, pause, resume, and step_frame. Queue status is available through `runlab_get_control_queue_status` and `runlab://control/status`.
For AI control loops, use `runlab_get_control_context`, `runlab_control_macro`, and the `control_game_with_runlab` prompt.

Visual context is available through `.runlab/current-screen.ppm`, `runlab://screenshot/current`, and `runlab_get_visual_context`. A vision-capable client can call `runlab_visual_annotate` to draw temporary player/enemy/scenario boxes over the emulator screen.

Prompt-box requests from emulator `Ctrl+T` are stored in `.runlab/prompts.jsonl`. Clients can use `handle_prompt_box`, `runlab_get_pending_prompt`, and `runlab_ack_prompt` to read, act on, and close those requests.

See `docs/runlab-mcp-cpp.md` for the full workflow and limitations.
