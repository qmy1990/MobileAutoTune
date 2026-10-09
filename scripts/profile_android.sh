#!/bin/bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TASK_DURATION="${1:-600}"
TASK_LEGACY="${2:-false}"
TASK_PRESET="${3:-0}" # 0 AUTO, 1 raw, 2 recognition, 3 performance
TASK_COMPACT="${4:-true}"
TASK_TUNING="${5:-1}"
TASK_PITCH="${6:-0}"
TASK_ADB="${ANDROID_HOME:-$HOME/Library/Android/sdk}/platform-tools/adb"
TASK_LABEL="$(date +%Y%m%d-%H%M%S)-legacy-${TASK_LEGACY}-preset-${TASK_PRESET}-compact-${TASK_COMPACT}"
TASK_OUT="$TASK_ROOT/outputs/android-performance/$TASK_LABEL"
mkdir -p "$TASK_OUT"
# Assumes the latest Debug demo is installed and microphone is allowed.
# Vendor systems may block the instrumentation's background activity start;
# the explicit shell launch below brings only this demo to the foreground.
"$TASK_ADB" shell am instrument -w -e seconds "$TASK_DURATION" -e legacy "$TASK_LEGACY" -e preset "$TASK_PRESET" -e compact "$TASK_COMPACT" \
  -e tuning "$TASK_TUNING" -e pitch "$TASK_PITCH" com.moon.mobileautotune.demo/.LivePerformanceInstrumentation > "$TASK_OUT/console.txt" 2>&1 &
TASK_PID=$!
sleep 2
"$TASK_ADB" shell am start -n com.moon.mobileautotune.demo/.MainActivity
wait "$TASK_PID"
if ! rg -q 'INSTRUMENTATION_RESULT: report=' "$TASK_OUT/console.txt"; then
  cat "$TASK_OUT/console.txt"
  exit 1
fi
if [[ "$TASK_LEGACY" == true ]]; then TASK_REPORT=baseline; else TASK_REPORT=optimized; fi
"$TASK_ADB" shell run-as com.moon.mobileautotune.demo cat "files/performance-$TASK_REPORT.json" > "$TASK_OUT/endurance.json"
python3 "$TASK_ROOT/scripts/summarize_android_performance.py" "$TASK_OUT/endurance.json"
