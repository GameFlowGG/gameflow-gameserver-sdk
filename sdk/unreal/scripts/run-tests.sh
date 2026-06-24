#!/usr/bin/env bash
# Build the host project (which compiles the GameFlow plugin) and run its
# automation tests headless against the local engine.
#
# Usage: run-tests.sh [TestPrefix]   (default: GameFlow)
# Requires: UE_ROOT pointing at an Unreal Engine 5.3+ root. Node >= 23.6 on PATH
#           for the conformance-backed tests (they self-skip when absent).
set -euo pipefail
: "${UE_ROOT:?set UE_ROOT to your Unreal Engine root}"
PREFIX="${1:-GameFlow}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
UPROJECT="$HERE/HostProject/GameFlowHost.uproject"

case "$(uname -s)" in
  Linux)  PLAT=Linux;  BUILD="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"; UE_CMD="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" ;;
  Darwin) PLAT=Mac;    BUILD="$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh";   UE_CMD="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" ;;
  *)      PLAT=Win64;  BUILD="$UE_ROOT/Engine/Build/BatchFiles/Build.bat";      UE_CMD="$UE_ROOT/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ;;
esac

echo "[run-tests] building GameFlowHostEditor ($PLAT Development)..."
"$BUILD" GameFlowHostEditor "$PLAT" Development -Project="$UPROJECT" -WaitMutex

echo "[run-tests] running automation tests matching '$PREFIX'..."
"$UE_CMD" "$UPROJECT" \
  -ExecCmds="Automation RunTests $PREFIX; Quit" \
  -unattended -nopause -nosplash -nullrhi -stdout -fullstdoutlogoutput \
  -testexit="Automation Test Queue Empty" \
  -log -abslog="$HERE/HostProject/Saved/Logs/automation.log"
