#!/bin/bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$TASK_ROOT/android"
./gradlew :sdk:assembleRelease :demo:assembleDebug
mkdir -p "$TASK_ROOT/dist/android"
cp sdk/build/outputs/aar/sdk-release.aar "$TASK_ROOT/dist/android/mobileaudio-sdk.aar"
cp demo/build/outputs/apk/debug/demo-debug.apk "$TASK_ROOT/dist/android/mobileaudio-demo.apk"
cp "$TASK_ROOT/THIRD_PARTY_NOTICES.md" "$TASK_ROOT/dist/android/"
