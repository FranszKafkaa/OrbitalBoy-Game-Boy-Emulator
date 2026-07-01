#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GBEMU_BIN="$BUILD_DIR/gbemu"
MCP_BIN="$BUILD_DIR/tools/orbitalboy-mcp-cpp/orbitalboy-mcp"
STATE_PATH="$ROOT_DIR/.runlab/current-state.json"
QUEUE_PATH="$ROOT_DIR/.runlab/commands.jsonl"
PROMPT_QUEUE_PATH="$ROOT_DIR/.runlab/prompts.jsonl"
FEEDBACK_QUEUE_PATH="$ROOT_DIR/.runlab/feedback.jsonl"
MODE=""
ROM_PATH=""

usage() {
  cat <<'EOF'
Usage:
  ./runlab.sh --emulator --rom caminho/para/jogo.gb [opcoes do gbemu...]
  ./runlab.sh --mcp [opcoes do orbitalboy-mcp...]

Modes:
  --emulator, --emu   Run gbemu with RunLab MCP control queue enabled.
  --mcp              Run the RunLab MCP stdio adapter.

Options:
  --rom PATH         ROM path for --emulator.
  --build-dir PATH   Build directory. Default: ./build
  --state PATH       RunLab state JSON. Default: ./.runlab/current-state.json
  --queue PATH       RunLab command queue. Default: ./.runlab/commands.jsonl
  --prompt-queue PATH RunLab prompt queue. Default: ./.runlab/prompts.jsonl
  --feedback-queue PATH RunLab feedback queue. Default: ./.runlab/feedback.jsonl
  --help             Show this help.
  --                 Pass the remaining arguments to the selected program.

Examples:
  ./runlab.sh --emulator --rom roms/Mario/mario.gb
  ./runlab.sh --mcp
  ./runlab.sh --emulator --rom jogo.gb -- --scale 5 --hardware dmg

Notes:
  --emulator writes the RunLab state JSON and consumes the command queue.
  --mcp writes the command queue only when a client calls a control tool.
  Ctrl+T in the emulator writes prompts to the prompt queue for the MCP client.
  orbitalboy-mcp auto-runs simple prompts like "ande pela direita", "pule", and "passe de fase".
EOF
}

fail_missing_binary() {
  local bin="$1"
  local target="$2"
  if [[ ! -x "$bin" ]]; then
    echo "Missing executable: $bin" >&2
    echo "Build it with:" >&2
    echo "  cmake -S \"$ROOT_DIR\" -B \"$BUILD_DIR\" -DGBEMU_BUILD_MCP=ON" >&2
    echo "  cmake --build \"$BUILD_DIR\" --target $target" >&2
    exit 1
  fi
}

extra_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --emulator|--emu)
      MODE="emulator"
      shift
      ;;
    --mcp)
      MODE="mcp"
      shift
      ;;
    --rom)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --rom" >&2
        exit 2
      fi
      ROM_PATH="$2"
      shift 2
      ;;
    --build-dir)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --build-dir" >&2
        exit 2
      fi
      BUILD_DIR="$2"
      GBEMU_BIN="$BUILD_DIR/gbemu"
      MCP_BIN="$BUILD_DIR/tools/orbitalboy-mcp-cpp/orbitalboy-mcp"
      shift 2
      ;;
    --state)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --state" >&2
        exit 2
      fi
      STATE_PATH="$2"
      shift 2
      ;;
    --queue)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --queue" >&2
        exit 2
      fi
      QUEUE_PATH="$2"
      shift 2
      ;;
    --prompt-queue)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --prompt-queue" >&2
        exit 2
      fi
      PROMPT_QUEUE_PATH="$2"
      shift 2
      ;;
    --feedback-queue)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --feedback-queue" >&2
        exit 2
      fi
      FEEDBACK_QUEUE_PATH="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      extra_args=("$@")
      break
      ;;
    *)
      extra_args+=("$1")
      shift
      ;;
  esac
done

if [[ -z "$MODE" ]]; then
  usage >&2
  exit 2
fi

mkdir -p "$(dirname "$STATE_PATH")" "$(dirname "$QUEUE_PATH")" "$(dirname "$PROMPT_QUEUE_PATH")" "$(dirname "$FEEDBACK_QUEUE_PATH")"

case "$MODE" in
  emulator)
    fail_missing_binary "$GBEMU_BIN" "gbemu"
    if [[ -z "$ROM_PATH" ]]; then
      echo "--emulator requires --rom PATH" >&2
      exit 2
    fi
    exec "$GBEMU_BIN" \
      --rom "$ROM_PATH" \
      --runlab-control \
      --runlab-state "$STATE_PATH" \
      --runlab-command-queue "$QUEUE_PATH" \
      ${extra_args[@]+"${extra_args[@]}"}
    ;;
  mcp)
    fail_missing_binary "$MCP_BIN" "orbitalboy-mcp"
    if [[ -t 0 ]]; then
      cat >&2 <<EOF
RunLab MCP is running on stdio and waiting for JSON-RPC input.
It is normal for it to print nothing until an MCP client connects.

State: $STATE_PATH
Queue: $QUEUE_PATH
Prompts: $PROMPT_QUEUE_PATH
Feedback: $FEEDBACK_QUEUE_PATH

Quick manual test:
  printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./runlab.sh --mcp
EOF
    fi
    exec "$MCP_BIN" \
      --state "$STATE_PATH" \
      --control-queue "$QUEUE_PATH" \
      --prompt-queue "$PROMPT_QUEUE_PATH" \
      --feedback-queue "$FEEDBACK_QUEUE_PATH" \
      ${extra_args[@]+"${extra_args[@]}"}
    ;;
esac
