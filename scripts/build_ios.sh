#!/bin/bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TASK_BUILD="${1:-$TASK_ROOT/build-ios}"
TASK_DIST="$TASK_ROOT/dist/ios"
mkdir -p "$TASK_DIST/headers/mat" "$TASK_DIST/device" "$TASK_DIST/simulator"
for platform in device simulator; do
    sdk=iphoneos
    architectures=arm64
    if [[ "$platform" == simulator ]]; then sdk=iphonesimulator; architectures='arm64;x86_64'; fi
    cmake -G Xcode -S "$TASK_ROOT/ios" -B "$TASK_BUILD-$platform" \
        -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" \
        -DCMAKE_OSX_ARCHITECTURES="$architectures" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
    cmake --build "$TASK_BUILD-$platform" --config Release --target MobileAudioDemo -- -quiet
    libtool -static -o "$TASK_DIST/$platform/libMobileAudioSDK.a" \
        "$TASK_BUILD-$platform/Release-$sdk/libMobileAudioSDK.a" \
        "$TASK_BUILD-$platform/core/Release-$sdk/libmobileautotune.a" \
        "$TASK_BUILD-$platform/core/Release-$sdk/libmat_rnnoise.a" \
        "$TASK_BUILD-$platform/core/Release-$sdk/libmat_rnnoise_legacy.a"
done
cp "$TASK_ROOT/ios/SDK/MATAudioSDK.h" "$TASK_DIST/headers/"
cp "$TASK_ROOT/include/mat/"*.h "$TASK_DIST/headers/mat/"
cat > "$TASK_DIST/headers/module.modulemap" <<'MAP'
module MobileAudioSDK {
    header "MATAudioSDK.h"
    header "mat/mobile_audio.h"
    header "mat/offline.h"
    export *
    link "c++"
    link framework "Foundation"
    link framework "AVFoundation"
    link framework "AudioToolbox"
}
MAP
# Only replace this script's generated package; preserve other distribution files.
if [[ -d "$TASK_DIST/MobileAudioSDK.xcframework" ]]; then rm -rf "$TASK_DIST/MobileAudioSDK.xcframework"; fi
xcodebuild -create-xcframework \
    -library "$TASK_DIST/device/libMobileAudioSDK.a" -headers "$TASK_DIST/headers" \
    -library "$TASK_DIST/simulator/libMobileAudioSDK.a" -headers "$TASK_DIST/headers" \
    -output "$TASK_DIST/MobileAudioSDK.xcframework"
cp "$TASK_ROOT/THIRD_PARTY_NOTICES.md" "$TASK_DIST/"
printf 'SDK: %s\nDemo project: %s\n' "$TASK_DIST/MobileAudioSDK.xcframework" "$TASK_BUILD-device/MobileAudioIOS.xcodeproj"
