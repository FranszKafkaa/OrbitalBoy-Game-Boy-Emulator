# RunLab MCP C++ Adapter

`orbitalboy-mcp` is a small MCP server for RunLab. It speaks JSON-RPC over stdio and reads the current RunLab snapshot/profile from files.

By default the useful inspection flow is read-only. A separate optional control bridge can append joypad input commands to `.runlab/commands.jsonl`; the emulator only consumes those commands when started with `--runlab-control`. The bridge does not write memory, cheat, solve routes, or open sockets.

## Build

The adapter is optional and off by default:

```bash
cmake -S . -B build -DGBEMU_BUILD_MCP=ON
cmake --build build --target orbitalboy-mcp
```

Tests:

```bash
cmake --build build --target orbitalboy_mcp_tests
./build/tools/orbitalboy-mcp-cpp/orbitalboy_mcp_tests
```

## State Files

By default the server reads:

```text
.runlab/current-state.json
```

You can override paths with environment variables:

```bash
ORBITALBOY_RUNLAB_STATE_PATH=/path/to/current-state.json \
ORBITALBOY_RUNLAB_PROFILE_PATH=/path/to/profile.runlab.json \
./build/tools/orbitalboy-mcp-cpp/orbitalboy-mcp
```

or CLI flags:

```bash
./build/tools/orbitalboy-mcp-cpp/orbitalboy-mcp --state .runlab/current-state.json --profile profiles/game.runlab.json --control-queue .runlab/commands.jsonl --prompt-queue .runlab/prompts.jsonl --feedback-queue .runlab/feedback.jsonl
```

The server rejects path traversal such as `..`. Screenshot resources are limited to the configured state/profile directories.

## Control Queue

Start the emulator with explicit control enabled:

```bash
./build/gbemu --rom caminho/para/jogo.gb --runlab-control --runlab-command-queue .runlab/commands.jsonl
```

Then run the MCP adapter with the same queue path:

```bash
./build/tools/orbitalboy-mcp-cpp/orbitalboy-mcp --state .runlab/current-state.json --control-queue .runlab/commands.jsonl
```

From the repository root, `runlab.sh` wraps both sides with the same default queue/state paths:

```bash
./runlab.sh --emulator --rom caminho/para/jogo.gb
./runlab.sh --mcp
```

`--emulator` writes `.runlab/current-state.json` and `.runlab/current-screen.ppm` periodically while the realtime frontend is running. `Ctrl+T` in the emulator writes user prompts to `.runlab/prompts.jsonl`. The MCP writes prompt feedback to `.runlab/feedback.jsonl`, which the emulator displays as short on-screen status messages. `--mcp` writes `.runlab/commands.jsonl` only when a client calls a control or annotation tool.
The MCP adapter also writes lightweight `heartbeat` entries on startup/request handling so the emulator debug panel can show `MCP CLIENT` even before a control command is sent.
The emulator bridge can also be toggled from the top menu: `DEBUG > RUNLAB MCP ON/OFF`.

## Prompt Box

With the emulator debug panel open, press `Ctrl+T` to open a RunLab MCP prompt box. Type a request and press `Enter`. The emulator appends it to:

```text
.runlab/prompts.jsonl
```

The MCP server does not contain an LLM and cannot act by itself. A connected AI/MCP client should use:

- `handle_prompt_box` prompt template, or
- `runlab_get_pending_prompt` tool directly.

After acting through tools such as `runlab_get_control_context`, `runlab_get_visual_context`, `runlab_control_macro`, and `runlab_visual_annotate`, the client should call `runlab_ack_prompt` with the prompt id.

Feedback appears in the emulator when the MCP reads a prompt and when the client acknowledges it. The raw feedback queue is:

```text
.runlab/feedback.jsonl
```

There is also a small deterministic auto-runner inside `orbitalboy-mcp` for simple commands. It can immediately queue input and mark prompts handled for requests like `ande pela direita`, `pule`, or `passe de fase`. Unknown/free-form prompts still require an AI client.

The MCP writes JSONL commands such as:

```json
{"command":"hold","buttons":["right","a"],"frames":30}
```

Supported commands:

- `tap`: press buttons for a short number of frames, default 6.
- `hold`: hold buttons for `frames`.
- `release_all`: release buttons currently controlled by the RunLab bridge.
- `advance_frames`: run `frames` with no MCP buttons while the emulator is unpaused.
- `pause`: pause the emulator.
- `resume`: resume the emulator.
- `step_frame`: advance `frames` while paused.
- `annotation`: show a temporary visual box on the emulator screen.

Supported button names: `right`, `left`, `up`, `down`, `a`, `b`, `select`, `start`.

Limits:

- max 600 frames per command;
- max 256 pending commands inside the emulator;
- commands already present when the emulator starts are ignored, so stale input is not replayed by surprise;
- the current state JSON includes `control.pending_count`, `control.current_command`, `control.remaining_frames`, `control.step_frames_pending`, and `control.last_message`.

## Visual Screen Annotation

The emulator exports a current RGB screenshot at:

```text
.runlab/current-screen.ppm
```

The current state advertises that file in `status.screenshot_path`, and MCP exposes it through `runlab://screenshot/current`. A vision-capable MCP client can inspect that image, combine it with RunLab entities/OAM/memory labels, and call `runlab_visual_annotate` to draw boxes directly over the emulator screen.

Visual tools:

- `runlab_get_visual_context`: returns screenshot metadata, entities, memory labels, recent events, and coordinate hints.
- `runlab_visual_annotate`: queues a temporary overlay rectangle with `label`, `type`, `x`, `y`, `w`, `h`, and `frames`.

Coordinates are Game Boy screen pixels: `x` 0-159 and `y` 0-143. Suggested `type` values are `player`, `enemy`, `scenario`, `item`, and `object`.

Example annotation:

```json
{
  "label": "PLAYER",
  "type": "player",
  "x": 32,
  "y": 72,
  "w": 16,
  "h": 16,
  "frames": 240
}
```

This only draws an overlay. It does not classify sprites inside the emulator, write memory, or control the game by itself.

## Tools

- `runlab_get_status`
- `runlab_list_entities`
- `runlab_list_memory_labels`
- `runlab_get_recent_events`
- `runlab_get_active_goal`
- `runlab_get_splits`
- `runlab_analyze_current_state`
- `runlab_export_profile`
- `runlab_control_tap`
- `runlab_control_hold`
- `runlab_control_release_all`
- `runlab_control_advance_frames`
- `runlab_control_pause`
- `runlab_control_resume`
- `runlab_control_step_frame`
- `runlab_get_control_queue_status`
- `runlab_get_control_context`
- `runlab_get_visual_context`
- `runlab_get_pending_prompt`
- `runlab_ack_prompt`
- `runlab_visual_annotate`
- `runlab_control_macro`

Inspection tools are read-only. Control tools only append input commands to the configured queue; the emulator must be explicitly started with `--runlab-control` to consume them.

For AI control, prefer this loop:

1. Call `runlab_get_control_context`.
2. If `control.enabled` or `control.client_recently_seen` is false, ask the user to enable `DEBUG > RUNLAB MCP ON/OFF` and run the MCP client.
3. Queue a short action batch with `runlab_control_macro`.
4. Observe again with `runlab_get_control_context`.
5. Stop if recent events show damage, death, goal completion, or level transition.

For visual identification, ask a vision-capable client to call `runlab_get_visual_context`, inspect `.runlab/current-screen.ppm`, then call `runlab_visual_annotate` for likely player/enemy/scenario boxes.

Example macro:

```json
{
  "commands": [
    { "command": "pause" },
    { "command": "tap", "buttons": ["a"], "frames": 4 },
    { "command": "step_frame", "frames": 2 },
    { "command": "resume" }
  ]
}
```

## Resources

- `runlab://status`
- `runlab://profile/current`
- `runlab://entities`
- `runlab://memory-labels`
- `runlab://events/recent`
- `runlab://goals/active`
- `runlab://splits`
- `runlab://control/status`
- `runlab://screenshot/current`
- `runlab://prompt/latest`

## Prompts

- `explain_recent_death`
- `suggest_splits_from_events`
- `suggest_goal_from_profile`
- `analyze_speedrun_segment`
- `suggest_missing_memory_labels`
- `control_game_with_runlab`
- `identify_screen_objects`
- `handle_prompt_box`

These prompts are templates for analysis over existing RunLab evidence. They are not instructions to automate gameplay.

## Recommended Workflow

1. Enable RunLab in the emulator.
2. Select and label the likely player entity.
3. Use correlation scan to promote useful labels such as `player.x`, `player.y`, `lives`, `level_id`, `player.state`, or `goal_flag`.
4. Export or periodically write `.runlab/current-state.json`.
5. Connect an MCP client to `orbitalboy-mcp`.
6. Ask the client to inspect events/labels, for example: `lives diminuiu -> dano/morte candidate`, `level_id mudou -> split`, `player.x aumentou -> progresso`.

## Limitations

- OAM position is not always the logical player position.
- Camera scroll can make screen position misleading.
- Multi-sprite entities can confuse detection.
- Some games store coordinates split across multiple bytes.
- Event and correlation confidence is heuristic evidence, not proof.
- The adapter is file-based; stale files mean stale MCP output.
- MCP control is input-only and frame-limited; it is not a TAS solver.
- `advance_frames` is for unpaused running; use `step_frame` for paused stepping.
