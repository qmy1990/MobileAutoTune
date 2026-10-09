#!/bin/bash
set -euo pipefail
# JitPack may expose sdkmanager only under the command-line tools installation.
TASK_SDKMANAGER="$(command -v sdkmanager || true)"
if [[ -z "$TASK_SDKMANAGER" ]]; then
  TASK_SDKMANAGER="${ANDROID_HOME:?Android SDK missing}/cmdline-tools/latest/bin/sdkmanager"
fi
"$TASK_SDKMANAGER" "platforms;android-34" "ndk;27.0.12077973" "cmake;3.22.1"
