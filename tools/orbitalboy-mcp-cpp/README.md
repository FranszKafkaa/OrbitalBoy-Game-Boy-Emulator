# OrbitalBoy RunLab MCP C++ Adapter

Read-only MCP stdio server for OrbitalBoy RunLab.

Build from the repository root:

```bash
cmake -S . -B build -DGBEMU_BUILD_MCP=ON
cmake --build build --target orbitalboy-mcp
```

Run:

```bash
./build/tools/orbitalboy-mcp-cpp/orbitalboy-mcp --state .runlab/current-state.json
```

The adapter reads RunLab state/profile JSON and exposes tools, resources, and prompts for semantic inspection. It never writes emulator memory, controls input, opens network sockets, or reads unconfigured arbitrary files.

See `docs/runlab-mcp-cpp.md` for the full workflow and limitations.
