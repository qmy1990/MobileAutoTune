#!/bin/bash
set -euo pipefail
TASK_SDKMANAGER=""
for TASK_SDK_ROOT in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}"; do
  [[ -n "$TASK_SDK_ROOT" ]] || continue
  for TASK_CANDIDATE in "$TASK_SDK_ROOT"/cmdline-tools/{latest,*,}/bin/sdkmanager; do
    if [[ -x "$TASK_CANDIDATE" ]]; then
      TASK_SDKMANAGER="$TASK_CANDIDATE"
      break 2
    fi
  done
done
if [[ -n "$TASK_SDKMANAGER" ]]; then
  "$TASK_SDKMANAGER" "platforms;android-34" "ndk;27.0.12077973" "cmake;3.22.1"
else
  printf 'Gradle will provision the configured Android SDK packages.\n'
fi
