# Vocal key analysis

[简体中文](KEY_ANALYSIS.md)

Offline analysis of **12 tonics × major/minor** for one unaccompanied voice. Analyze the original recording after stopping its WAV writer, or import an existing audio document. All processing runs locally. The demo applies a proposed key only after the user selects it.

## Method and interpretation

The shared C++ pipeline uses high-quality 16kHz resampling, FP32 SwiftF0 and RMVPE pitch tracks, confidence/agreement fusion, tuning-offset estimation, short median smoothing and note segmentation. Key ranking combines pitch-class profiles, scale coverage, phrase endings and cadential transitions. Model agreement is measured on octave-aligned pitch classes. Results include 24 candidates, relative support, scale coverage, model agreement, voiced duration, local segments and possible modulation.

`support` is a normalized ranking weight, **not calibrated accuracy or a probability of correctness**. `estimated` is still an estimate; `ambiguous` preserves alternatives; `insufficientMelody` means there is too little usable melodic evidence. The transparent statistical ranker does not claim benchmark-leading dry-vocal key accuracy.

Short phrases, pentatonic melodies, relative major/minor, out-of-tune singing, speech and modulation can be ambiguous. Melody alone cannot always recover the harmonic key of a whole song. Prefer a complete phrase containing several scale degrees, retain alternatives and analyze the dry signal before pitch correction. Minor candidates represent minor tonality; the correction engine's existing minor scale is natural minor.

## Models and deployment

```bash
python3 scripts/prepare_key_analysis.py --platform models
```

Files are prepared in `models/key/`. [manifest.json](../models/key/manifest.json) pins upstream revisions, URLs, licenses and SHA256 hashes. The runtime verifies the fixed model contents. SwiftF0 is about135KB and RMVPE about362MB, in decimal units; model weights remain FP32.

- The Android AAR includes inference libraries and excludes model weights. Bundle the files as `assets/key/`, or let the host download and verify them before passing a local directory. `bundledModels()` copies packaged assets into private storage on a worker thread. The demo bundles weights, so its APK is large and first use needs copying time and additional disk space.
- The iOS XCFramework includes inference libraries. Add `models/key/` as a folder resource named `KeyModels.bundle`, or supply another local model directory. The demo includes this resource.
- Reserve storage and memory for model weights, runtime activations and decoded temporary files. PCM is processed in bounded chunks; pitch-track storage grows with duration. Low-memory devices can fail to load the full model. Handle errors and preserve recordings.
- Redistribute the included model/runtime licenses and [third-party notices](../THIRD_PARTY_NOTICES.md).

## Android

Construct, analyze and close on a worker thread. Stop recording and finalize its WAV file first.

```java
import com.moon.mobileautotune.KeyAnalyzer;
import com.moon.mobileautotune.KeyAnalysisResult;

KeyAnalyzer.Options options = new KeyAnalyzer.Options();
options.temporaryDirectory = context.getCacheDir();
try (KeyAnalyzer analyzer = new KeyAnalyzer(KeyAnalyzer.bundledModels(context), options)) {
    KeyAnalysisResult result = analyzer.analyze(rawWavFile, progress -> {
        // Worker thread: dispatch UI updates to the main thread.
    });
    // Or: analyzer.analyze(context, selectedDocumentUri, progressListener);
    // Inspect result.status and let the user choose from result.candidates.
}
```

The URI entry point accepts system document-picker `content://` URIs and copies audio locally. WAV uses the shared native decoder. MediaExtractor/MediaCodec decode other formats supported by the device. Imported files need no write permission; temporary files use app cache storage. Call `cancel()` from another thread, await completion, then close. Cancellation throws `CancellationException`; analysis failures throw `IOException`.

## iOS

```swift
import MobileAudioSDK

// Run on a worker queue. A custom local model directory is also supported.
let models = MATKeyAnalyzer.bundledModelDirectory()!
let analyzer = try MATKeyAnalyzer(modelDirectory: models, options: MATKeyAnalysisOptions())
let result = try analyzer.analyze(audio: rawURL, progress: { fraction in
    return true // Return false to cancel.
})
// result.candidates / result.status / result.json
```

AVAudioFile decodes system-supported formats; the wrapper balances security-scoped access and removes temporary files. `cancel()` is thread-safe; cancellation returns error code `-5`. Existing-file analysis requires no microphone permission. Link Foundation, AVFoundation, AudioToolbox, CoreML, Accelerate and libc++.

## C/C++ and desktop validation

```bash
python3 scripts/prepare_key_analysis.py --platform desktop
cmake -S . -B build-key -DMAT_BUILD_KEY_ANALYSIS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-key -j4
./build-key/mat_key_analyze models/key input.wav --cpu --threads 4
ctest --test-dir build-key --output-on-failure
```

Automatic desktop runtime preparation supports macOS. On other desktop platforms, install ONNX Runtime1.20 or newer and set `MAT_ORT_LIBRARY`. Link `mobileautotune_key` for model inference; public APIs are in [key_analysis.h](../include/mat/key_analysis.h). To analyze externally supplied pitch tracks without an ML runtime, link the core `mobileautotune` and call `mat_key_analyze_tracks()`.

Native WAV supports PCM16/24/32 and float32, 8..192kHz, 1..8 channels, downmixed to mono. Raw PCM accepts little-endian mono float32 at16kHz. Files are limited to24hours, with at most256 local segments. RMVPE uses7.68-second chunks with1.28-second context on each side; chunked bidirectional inference can differ from whole-file inference near boundaries.

## Hardware behavior

The default thread pool is bounded at6 threads with spinning disabled; sessions and memory are reused. `threads` can override the default. FP32 weights and cached exact resampling kernels preserve the accuracy-oriented path.

`AUTO`/`automatic` and `ACCELERATED`/`accelerated` both attempt acceleration with safe CPU fallback. Android8 tries CPU/XNNPACK; Android8.1+ also tries NNAPI; Apple uses the device-tested multicore CPU path for these dynamic graphs; CoreML model preparation is not bounded on the supported deployment range. Initial selection increases first-analysis time. A provider must match the CPU on the same chunk within2cents of pitch,0.005 confidence and identical voiced status, and run at least10% faster after warmup. Runtime failures fall back to CPU. Select `CPU` to skip provider probing.

`swiftBackend` and `rmvpeBackend` describe the selected configuration. `NNAPI+CPU (validated)` does **not** mean all operators ran on a GPU/NPU, or guarantee zero differences on all audio. Actual graph partitions, hardware and OS may favor CPU. Chip branding or a newer OS alone does not justify forcing acceleration. Include setup, loading, import and decode in user-facing latency; this offline feature has its own throughput measurements.

Progress callbacks run on the analysis thread and increase monotonically from0to1. One active analysis per instance. Never load models or analyze inside a realtime audio callback. The demo analyzes after recording stops to avoid competing with monitoring.
