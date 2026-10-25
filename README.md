# MobileAutoTune

[English](README.en.md) | 简体中文

适用于 **Android 8.0+ / iOS 14.0+** 的人声音频处理 SDK，共享 C++17 算法核心，支持实时录音监听和高质量 WAV 后处理。

## 开源与商用

本项目原创 SDK 与 Demo 采用 [MIT 许可证](LICENSE)，**免费商用**，允许修改、分发及闭源产品集成，无授权费、无联网激活。发布产品时请保留版权、许可证及 [第三方声明](THIRD_PARTY_NOTICES.md)。第三方依赖继续遵循各自的 MIT、BSD-3-Clause、Apache-2.0 许可证。软件按现状提供。

## 功能

| 功能 | 参数与行为 |
|---|---|
| 自动修音 | 12 个主音、大调／自然小调／半音阶；强度 `0..1`，1 为最大强度，具有快速贴音和机械感 |
| 音高变声 | `-12..+12` 半音，降低或升高声音；先计算变声后的音高，再按所选调式修正 |
| 混响 | 强度 `0..1` |
| 回声 | 强度 `0..1`，延时 `40..800ms` |
| 调式分析 | 录音结束或导入文件后，本机双模型分析12个主音及大小调，输出候选、歧义与局部分段 |
| 降噪 | 强度 `0..1`；实时 RNNoise v0.1，后处理使用较大 RNNoise 模型 |

实时模式使用短历史变调；后处理采用 Signalsmith Stretch、较长音高分析窗口及共振峰处理。实时支持原音／效果双 WAV 录制，变声与修音合并为一次变调，无需网络或额外模型下载。离线调式分析使用独立模型资源，见 [调式分析集成说明](docs/KEY_ANALYSIS.md)。

## 下载与 Demo

[最新发布与全部文件](https://github.com/qmy1990/MobileAutoTune/releases/latest) · [v0.5.0](https://github.com/qmy1990/MobileAutoTune/releases/tag/v0.5.0)

| 下载 | 说明 |
|---|---|
| [Android Demo APK](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-demo-v0.5.0.apk) | 可安装的 Debug Demo，包含录音、参数调整、导入、试听和后处理 |
| [Android 通用 AAR](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/mobileautotune-v0.5.0.aar) | `armeabi-v7a`、`arm64-v8a`、`x86`、`x86_64`；发布页另有各 ABI 单独 AAR |
| [Android C/C++ SDK](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-android-native-v0.5.0.zip) | 四种 ABI 的静态核心库、头文件及 CMake 集成文件 |
| [iOS XCFramework](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAudioSDK-v0.5.0.xcframework.zip) | 真机 arm64；模拟器 arm64/x86_64 |
| [iOS 未签名 Demo IPA](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-demo-v0.5.0-unsigned.ipa) | 需自己的开发／分发签名，不能直接安装到任意 iPhone |
| [iOS 模拟器 Demo](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-demo-v0.5.0-simulator.zip) | 解压后 `xcrun simctl install booted MobileAudioDemo.app` |
| [调式分析模型包](https://github.com/qmy1990/MobileAutoTune/releases/download/v0.5.0/MobileAutoTune-key-models-v0.5.0.zip) | 固定 FP32 模型、清单与许可证；供 AAR／XCFramework 宿主部署 |

Demo 源码：[Android](android/demo) / [iOS](ios/Demo)。发布附件提供 `SHA256SUMS` 与 `manifest.json`。iOS 14 起仅提供 arm64 真机库，无 armv7 真机库。

## Android 集成

### JitPack / Maven

`settings.gradle` 添加仓库：

```groovy
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        maven { url 'https://jitpack.io' }
    }
}
```

应用模块 `build.gradle`：

```groovy
dependencies {
    implementation 'com.github.qmy1990:MobileAutoTune:v0.5.0'
}
```

也可将下载的 AAR 放入 `app/libs/`，使用 `implementation files('libs/mobileautotune-v0.5.0.aar')`，二选一。Java 包名为 **`com.moon.mobileautotune`**，Demo 应用 ID 为 `com.moon.mobileautotune.demo`。64位共享库按16KB页面对齐；宿主APK仍需使用支持16KB打包的AGP（8.5.1+）。AAR 自带 JNI 库、C++ shared runtime 与混淆保留规则；宿主若已打包不同版本 `libc++_shared.so`，应统一 NDK/STL 版本，不盲目覆盖。

### 使用

Manifest 声明 `android.permission.RECORD_AUDIO`，启动录音前申请运行时麦克风权限。路径使用应用可写目录中的两个不同文件。

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
// monitor=true 仅在耳机监听时使用。
sdk.startLive(rawPath, wetPath, false);
sdk.stopLive();
sdk.close();
// 在工作线程执行；参数应使用固定快照。
MobileAudio.processWav(inputPath, outputPath, p, MobileAudio.OFFLINE, true);
```

SDK 通过 Oboe 请求 AAudio / 低延迟通路，旧系统与不支持的设备自动回退。默认使用低延迟块处理；录音预设可选 AUTO、UNPROCESSED、VOICE_RECOGNITION、VOICE_PERFORMANCE。详见 [平台 API](docs/PLATFORM_APIS.md)。

## iOS 集成

将 XCFramework 添加到 Xcode 的 Frameworks，静态库选择 **Do Not Embed**。链接 `Foundation`、`AVFoundation`、`AudioToolbox`、`CoreML`、`Accelerate` 和 `libc++`（module map 已声明），在 Info.plist 添加 `NSMicrophoneUsageDescription`，录音前请求麦克风权限。

```swift
import MobileAudioSDK

let sdk = MATAudioSDK(offline: false)!
let p = MATAudioParameters()
p.tonic = 9
p.scale = 1 // A 自然小调
p.pitchShiftSemitones = 5
p.tuning = 1.0
try sdk.setParameters(p)
try sdk.startLive(raw: rawURL, processed: wetURL, monitor: false)
sdk.stopLive()
// 在工作线程执行，固定参数快照：
try MATAudioSDK.processWav(input: inputURL, output: outputURL,
    parameters: p.copy() as! MATAudioParameters, offline: true, appendTail: true)
```

SDK 使用 RemoteIO 与 AVAudioSession measurement。运行时占用音频会话，路由变化或中断会停止录音，宿主应显式恢复。

## C/C++ 集成

公共 API：[mobile_audio.h](include/mat/mobile_audio.h) / [offline.h](include/mat/offline.h)。预编译 Android 原生包内含 CMake 导入文件；直接构建源码时链接 `mobileautotune` 目标。核心本身不负责设备 I/O。

```cpp
auto *engine = mat_create(48000, MAT_REALTIME);
auto p = mat_default_parameters();
p.pitch_shift_semitones = -5;
p.tuning = 1.0f;
mat_set_parameters(engine, &p);
mat_process_frame(engine, input480, output480);
mat_destroy(engine);
```

## 人声调式分析

Demo 支持“录音结束后分析原始人声调式”和“导入音频并分析调式”。SDK 提供 Android `KeyAnalyzer`、iOS `MATKeyAnalyzer` 和 C API。SwiftF0 与 RMVPE 使用固定 FP32 权重，Apple 使用已验证的多核 CPU 路径；Android 提供经过片段一致性验证的可选平台加速。

分析在本地工作线程执行。模型权重约362MB，独立于 AAR／XCFramework 分发，Demo 内置。短清唱和关系大小调可能无法唯一确定调式，候选支持度不代表准确率。完整 API、模型准备、性能与限制见 [中文说明](docs/KEY_ANALYSIS.md)。

## 注意事项

- 实时音效与 WAV 效果处理格式固定 **48kHz、单声道 float32 PCM**。WAV 接受 48kHz PCM16／float32、单声道或双声道；双声道混合为单声道。其他格式由宿主转换。
- 适用于单个人声；不做伴奏分离，也不能推断歌曲旋律。高强度降噪可能削弱气声与尾音，唱歌建议从 `0.2..0.5` 试听；极端移调和低音可能出现颗粒感。
- 实时监听使用有线／USB／Lightning 耳机，避免扬声器回授。蓝牙延迟通常较高。
- 名义算法延迟：480 样本帧入口 **20ms**，任意长度流入口 **30ms**；最大历史读取年龄分别30／40ms。硬件与系统缓冲还会累加，**尚未通过整体声学往返 ≤50ms 验收**；回调耗时不能当作整体延迟。
- 单个实例只允许一个处理线程，生命周期不能与处理并发；参数由一个控制线程更新。Android Java `process()` 有 JNI 拷贝，严格实时回调建议使用 C API。后处理放到工作线程，同一实例不同时执行 live 与手动 process。
- 默认导出裁去算法预滚动；`appendTail=true` 追加4秒效果尾音。输入和输出不能是同一文件。

## 从源码构建

Android：JDK17、Android SDK34、NDK27.0.12077973、CMake3.22.1；设置 `ANDROID_HOME` 或本地 `android/local.properties`。iOS：macOS、Xcode15+、CMake3.22+。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 scripts/verify_vendor.py
./scripts/build_android.sh
./scripts/build_ios.sh
python3 scripts/package_release.py
```

构建产物在 `dist/`。Android Studio 打开 `android/`。iOS 生成项目在 `build-ios-device/MobileAudioIOS.xcodeproj`；运行真机 Demo 时设置自己的开发团队、唯一 Bundle ID，并启用签名。源码不包含用户录音和个人签名文件。GitHub Actions 在版本 tag 上构建、测试并发布附件；JitPack 从 tag 构建 Android Maven 制品。

[架构](docs/ARCHITECTURE.md) · [变声与修音](docs/VOICE_CONTROLS.md) · [验证说明](docs/VALIDATION.md) · [第三方依赖](THIRD_PARTY_NOTICES.md)
