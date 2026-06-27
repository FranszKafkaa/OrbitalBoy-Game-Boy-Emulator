# RunLab MCP C++ Adapter

`orbitalboy-mcp` is a small read-only MCP server for RunLab. It speaks JSON-RPC over stdio and reads the current RunLab snapshot/profile from files. It does not open sockets, control the emulator, write memory, press inputs, solve routes, or read arbitrary files.

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
./build/tools/orbitalboy-mcp-cpp/orbitalboy-mcp --state .runlab/current-state.json --profile profiles/game.runlab.json
```

The server rejects path traversal such as `..`. Screenshot resources are limited to the configured state/profile directories.

## Tools

- `runlab_get_status`
- `runlab_list_entities`
- `runlab_list_memory_labels`
- `runlab_get_recent_events`
- `runlab_get_active_goal`
- `runlab_get_splits`
- `runlab_analyze_current_state`
- `runlab_export_profile`

All tools are read-only. They report evidence already present in RunLab labels/events and avoid memory writes or emulator control.

## Resources

- `runlab://status`
- `runlab://profile/current`
- `runlab://entities`
- `runlab://memory-labels`
- `runlab://events/recent`
- `runlab://goals/active`
- `runlab://splits`
- `runlab://screenshot/current`

## Prompts

- `explain_recent_death`
- `suggest_splits_from_events`
- `suggest_goal_from_profile`
- `analyze_speedrun_segment`
- `suggest_missing_memory_labels`

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
