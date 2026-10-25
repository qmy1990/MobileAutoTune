# MobileAutoTune

[简体中文](README.md) | English

A vocal audio SDK for **Android 8.0+ and iOS 14.0+**, built on a shared C++17 core, with real-time recording/monitoring and high-quality offline WAV processing.

## Open source and commercial use

Original SDK and demo code is licensed under [MIT](LICENSE): **free commercial use**, modification, redistribution, and integration into closed-source products, without license fees or online activation. Retain copyright, license notices, and [third-party notices](THIRD_PARTY_NOTICES.md) when distributing your product. Bundled dependencies retain their MIT, BSD-3-Clause, and Apache-2.0 licenses. Provided as is.

## Features

| Feature | Controls and behavior |
|---|---|
| Automatic pitch correction | 12 tonics; major, natural minor, chromatic; strength `0..1`, 1 gives maximum mechanical retuning |
| Voice pitch shifting | `-12..+12` semitones; correction targets the selected scale **after** the voice pitch shift |
| Reverb | Strength `0..1` |
| Echo | Strength `0..1`, delay `40..800ms` |
| Key analysis | Offline dual-model analysis after recording or audio import; tonic/mode candidates, ambiguity and local segments |
| Noise reduction | Strength `0..1`; RNNoise v0.1 in real time and a larger RNNoise model offline |

Real-time mode uses short-history pitch shifting. Offline mode uses Signalsmith Stretch, longer pitch analysis, and formant processing. Record both raw and processed WAVs. Voice shifting and tuning share one pitch-shift operation; no network or separate model downloads are required for the effects. Offline key analysis uses separate model resources; see the [key-analysis guide](docs/KEY_ANALYSIS.en.md).

## Downloads and demos

[Latest release and all assets](https://github.com/qmy1990/MobileAutoTune/releases/latest) · [v0.5.0](https://github.com/qmy1990/MobileAutoTune/releases/tag/v0.5.0)

| Download | Contents |
|---|---|
| [Android demo APK](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-demo-v0.5.0.apk) | Installable Debug demo: recording, controls, import, playback, offline processing |
| [Universal Android AAR](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/mobileautotune-v0.5.0.aar) | `armeabi-v7a`, `arm64-v8a`, `x86`, `x86_64`; per-ABI AARs are also available |
| [Android C/C++ SDK](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-android-native-v0.5.0.zip) | Static core libraries for all four ABIs, headers, and CMake integration |
| [iOS XCFramework](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAudioSDK-v0.5.0.xcframework.zip) | Device arm64; simulator arm64/x86_64 |
| [Unsigned iOS demo IPA](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-demo-v0.5.0-unsigned.ipa) | Requires your own development/distribution signing; cannot be installed directly on arbitrary iPhones |
| [iOS simulator demo](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-demo-v0.5.0-simulator.zip) | Unzip, then run `xcrun simctl install booted MobileAudioDemo.app` |
| [Key model pack](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-key-models-v0.5.0.zip) | Pinned FP32 models, manifest and licenses for SDK hosts |

Demo sources: [Android](android/demo) / [iOS](ios/Demo). Release assets include `SHA256SUMS` and `manifest.json`. iOS14+ device builds are arm64; armv7 is not included.

## Android integration

### JitPack / Maven

Add the repository in `settings.gradle`:

```groovy
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        maven { url 'https://jitpack.io' }
    }
}
```

Add the dependency to your app module:

```groovy
dependencies {
    implementation 'com.github.qmy1990:MobileAutoTune:v0.5.0'
}
```

Alternatively, put the downloaded AAR in `app/libs/` and use `implementation files('libs/mobileautotune-v0.5.0.aar')`. Choose one method. The Java package is **`com.moon.mobileautotune`**; the demo application ID is `com.moon.mobileautotune.demo`. 64-bit shared libraries use16KB alignment; the host APK still needs16KB-capable packaging (AGP8.5.1+). The AAR includes JNI libraries, the shared C++ runtime, and consumer ProGuard rules. If your host bundles a different `libc++_shared.so`, align NDK/STL versions instead of blindly overriding it.

### Usage

Declare `android.permission.RECORD_AUDIO` and obtain runtime microphone permission first. Use two distinct files in an app-writable directory.

```java
import com.moon.mobileautotune.MobileAudio;

MobileAudio sdk = new MobileAudio(MobileAudio.REALTIME);
MobileAudio.Parameters p = new MobileAudio.Parameters();
p.tonic = 0; // C=0, Eb=3, A=9
p.scale = MobileAudio.MAJOR;
p.pitchShiftSemitones = -5;
p.tuning = 1.0f;
p.reverb = 0.18f;
p.echo = 0.08f;
p.denoise = 0.35f;
sdk.setParameters(p);
// Enable monitor only with headphones.
sdk.startLive(rawPath, wetPath, false);
sdk.stopLive();
sdk.close();
// Run on a worker thread with a stable parameter snapshot.
MobileAudio.processWav(inputPath, outputPath, p, MobileAudio.OFFLINE, true);
```

Oboe requests AAudio/low-latency I/O with fallback for older systems and unsupported routes. Low-latency block processing is the default. Capture presets include AUTO, UNPROCESSED, VOICE_RECOGNITION, and VOICE_PERFORMANCE. See [platform APIs](docs/PLATFORM_APIS.md).

## iOS integration

Add the XCFramework to your Xcode app and select **Do Not Embed** for this static library. Link `Foundation`, `AVFoundation`, `AudioToolbox`, `CoreML`, `Accelerate`, and `libc++` (declared by the module map). Add `NSMicrophoneUsageDescription` and obtain microphone permission before recording.

```swift
import MobileAudioSDK

let sdk = MATAudioSDK(offline: false)!
let p = MATAudioParameters()
p.tonic = 9
p.scale = 1 // A natural minor
p.pitchShiftSemitones = 5
p.tuning = 1.0
try sdk.setParameters(p)
try sdk.startLive(raw: rawURL, processed: wetURL, monitor: false)
sdk.stopLive()
// Run on a worker thread with a stable snapshot.
try MATAudioSDK.processWav(input: inputURL, output: outputURL,
    parameters: p.copy() as! MATAudioParameters, offline: true, appendTail: true)
```

The SDK uses RemoteIO and AVAudioSession measurement, owns the session while running, and stops on interruptions or route changes. Restart explicitly when appropriate.

## C/C++ integration

Public APIs: [mobile_audio.h](include/mat/mobile_audio.h) / [offline.h](include/mat/offline.h). The prebuilt Android native archive includes CMake imports. Source builds expose the `mobileautotune` target. The core requires host-provided audio I/O.

```cpp
auto *engine = mat_create(48000, MAT_REALTIME);
auto p = mat_default_parameters();
p.pitch_shift_semitones = -5;
p.tuning = 1.0f;
mat_set_parameters(engine, &p);
mat_process_frame(engine, input480, output480);
mat_destroy(engine);
```

## Vocal key analysis

The demos support automatic analysis after recording stops and local audio-document import. Use Android `KeyAnalyzer`, iOS `MATKeyAnalyzer` or the C API. Fixed FP32 SwiftF0 and RMVPE models use a validated multicore CPU path on Apple, with optional Android providers validated against CPU on a calibration chunk.

Analysis runs on a local worker thread. About362MB of model weights are distributed separately from AAR/XCFramework libraries and bundled in the demos. Short phrases and relative major/minor can remain ambiguous; candidate support is not accuracy. See the [integration guide](docs/KEY_ANALYSIS.en.md) for model setup, API examples and limitations.

## Notes

- Processing uses **48kHz mono float32 PCM**. WAV input supports48kHz PCM16/float32, mono or stereo; stereo is mixed to mono. Convert other formats in your host.
- Designed for a single voice, without backing-track separation or melody inference. Strong denoising can reduce breathiness and sustained tails; start around `0.2..0.5` for singing. Extreme shifts and low voices may sound grainy.
- Use wired/USB/Lightning headphones for monitoring to avoid speaker feedback. Bluetooth commonly adds substantial delay.
- Nominal algorithm latency is **20ms** for480-sample frames and **30ms** for arbitrary-size streaming; maximum history ages are30/40ms. Hardware/system latency is additional. **Acoustic round-trip ≤50ms has not been verified**; callback time is not end-to-end latency.
- One processing thread per instance; never run lifecycle operations concurrently with processing. One control thread may update parameters. Java `process()` copies through JNI; use the C API for strict real-time callbacks. Offline processing belongs on a worker. Do not mix live and manual processing on an instance.
- Exports trim pipeline preroll; `appendTail=true` adds4seconds of effect tail. Input and output must be different files.

## Build from source

Android: JDK17, SDK34, NDK27.0.12077973, CMake3.22.1; set `ANDROID_HOME` or local `android/local.properties`. iOS: macOS, Xcode15+, CMake3.22+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 scripts/verify_vendor.py
./scripts/build_android.sh
./scripts/build_ios.sh
python3 scripts/package_release.py
```

Artifacts are under `dist/`. Open `android/` in Android Studio. The generated iOS project is `build-ios-device/MobileAudioIOS.xcodeproj`; set your own development team, unique bundle ID, and enable signing for a device demo. User recordings and personal signing files are excluded. GitHub Actions builds, tests, and publishes release assets for version tags; JitPack builds Android Maven artifacts from tags.

[Architecture](docs/ARCHITECTURE.md) · [Voice controls](docs/VOICE_CONTROLS.md) · [Validation](docs/VALIDATION.md) · [Dependencies](THIRD_PARTY_NOTICES.md)
