# 新旧系统音频 API 策略（0.2.0）

SDK 根据系统版本选择适用能力，根据打开结果回退。请求低延迟、独占或性能提示不代表设备一定提供相应通路；Demo 展示实际 MMAP 和录音预设，遥测保留后端、共享模式、缓冲和时间戳估计。最低支持版本仍为 Android 8.0 / iOS 14.0。

| 系统 | 默认音频通路 | 新能力与兼容处理 |
|---|---|---|
| Android 8.0 / API26 | Oboe / OpenSL ES | Unprocessed 录音；维持原有 DSP |
| Android 8.1–9 / API27–28 | Oboe / AAudio | 低延迟、独占请求；打开失败重试共享及 OpenSL ES |
| Android 10–11 / API29–30 | AAudio + VoicePerformance | 自动模式优先实时演唱录音预设；打开失败回退 Unprocessed |
| Android 12+ / API31+ | 上述通路 + Performance Hint | 请求 Oboe 的 ADPF 调度提示；设备不支持时正常继续，不提高最低 API |
| iOS 14+ | RemoteIO + measurement | 系统自动把渲染线程加入 Audio Workgroup；控制线程检测可用性，检测失败继续原通路 |

## Android 接入

原三参数 `startLive` 使用 AUTO。四参数重载可明确保留之前的录音配置：

```java
sdk.startLive(rawPath, wetPath, true, MobileAudio.CapturePreset.AUTO);
// 下一次停止后重新开始，保留原有设备收音通路：
// sdk.startLive(rawPath, wetPath, true, MobileAudio.CapturePreset.UNPROCESSED);
```

另有 VOICE_RECOGNITION 和 VOICE_PERFORMANCE 可供设备对照测试。预设在开始录音时确定，运行中不切换硬件通路。显式 VoicePerformance 在 API29 以下回退 Unprocessed，OpenSL ES 也请求原始录音预设；若底层拒绝，Oboe可能进一步退回VoiceRecognition，遥测记录实际结果。Android Demo 新增对应选择器，录音中禁用。

输出先请求独占，失败后重试共享，再失败尝试 OpenSL ES。输入先使用实际输出后端，现代预设失败回退原始预设，继续失败重试共享及 OpenSL ES。仍要求 48kHz mono float；不隐式转换无法满足要求的采样率。

`telemetry()` 的前26项保持兼容，追加：26 实际录音预设（6/9/10），27 预设回退标记，28 性能提示已请求（不等于已生效），29 输出共享模式，30 输出性能模式，31 输入后端；Oboe 枚举值参照 vendored Definitions.h。输入和输出可能分别退回不同后端，独立记录。

VoicePerformance 的目标是实时处理及回放（如卡拉OK），但厂商可能附加录音处理，改变增益、噪声或音色。录音预设选择不替换 SDK 内的 DSP；0.2.0 的独立移调与120%修音控制在共同算法内实现。不能把录音预设当成跨设备一致、更高质量的降噪模型。原始通路用于对照和保留此前音色，仍须实录盲听验证。

## Android 低延迟块处理

默认请求480样本/10ms固定回调，直接使用 `mat_process_frame`，移除任意长度适配器的480样本/10ms附加延迟。四项效果使用同一个DSP实现，逐样本对照测试确认与旧流接口输出只差一个帧的时间偏移。需要保持原通路时可选择 `LiveBuffering.COMPATIBLE`，Demo提供低延迟处理开关，录音中不可切换。固定回调无法打开时重试原硬件回调；输出实际回调配置决定使用哪一个DSP入口，同一个运行中的引擎不混用两种入口。

```java
sdk.startLive(rawPath, wetPath, true, MobileAudio.CapturePreset.AUTO,
              MobileAudio.LiveBuffering.LOW_LATENCY);
```

遥测追加32：当前Live算法名义延迟ms；33：实际回调周期ms。`latencySamples()` 在Live期间返回实际入口的名义延迟，手动process时仍按任意长度接口计算。固定回调下本机名义算法延迟20ms，回调预算10ms；硬件burst仍是4ms，并不能把burst当作此时回调预算。Frame入口最大历史年龄30ms，仍须加硬件延迟。Android16的固定回调短测输出时间戳估计约32ms，不能声称整体延迟只有20ms。

## iOS 接入

当前所有 DSP 都在 RemoteIO 提供的同一个实时回调执行，系统负责把该线程加入设备音频工作组，因此无需手工再加入。新增 `audioWorkgroupAvailable` 遥测用于能力诊断；从 AudioUnit 返回的工作组引用由 ARC 管理。写盘线程不参与实时工作组。若以后拆分并行 DSP 线程，才需要显式加入工作组并验证它们的截止时间。

继续请求 48kHz、128帧 I/O 缓冲，遥测读取会话激活后的实际 I/O 缓冲及 inputLatency/outputLatency。换成 AVAudioEngine 并不保证更低延迟；通话用 VoiceProcessingIO 的系统 AEC/AGC 也不直接替代可调强度、保留歌声的降噪。当前不启用此类会改变唱歌音色的处理。

## 验证边界

Android AAR / Demo 与 iOS arm64、模拟器 arm64/x86_64 SDK / Demo 均按最低部署版本构建。已连接Android16的V2227A和iOS16.7.10的iPhone8 Plus进行实测；最低版本设备运行兼容、新预设音色和声学延迟仍分别验证。已有长录对照见 [Android 性能记录](ANDROID_PERFORMANCE.md)。

这台 Snapdragon8 Gen2 的已测通路为 AAudio shared FAST，MMAP 实际未开启，输出时间戳估计约30ms；新的固定块处理将算法名义延迟降到20ms，但硬件延迟仍会累加。现代 API 无法保证突破厂商的硬件缓冲。需要继续做录音预设对照、缩短流适配缓冲及声学回环测量；当前不宣称满足50ms整体要求。

CPU 的平均处理时间远小于10ms帧长，GPU/NPU不会消除 RNNoise 帧长、变调历史和音频硬件缓冲。后续更大降噪模型可以评估 各平台模型运行时及硬件加速，但必须测包含模型调度与数据转换的完整耗时和歌声保真，不能只按推理算力选择。

MMAP含义、权限、当前设备强制打开结果及芯片能力边界见 [MMAP说明](MMAP.md)。

## 官方依据

- [Android VoicePerformance：API29，面向实时演唱的录音通路](https://developer.android.com/reference/android/media/MediaRecorder.AudioSource#VOICE_PERFORMANCE)
- [Android Oboe 低延迟实践](https://developer.android.com/games/sdk/oboe/low-latency-audio)
- [Android Performance Hint](https://developer.android.com/ndk/reference/group/a-performance-hint)
- [Apple Audio Workgroups：系统管理渲染线程工作组](https://developer.apple.com/documentation/audiotoolbox/understanding-audio-workgroups)
- [Apple 首选缓冲是请求，需要读取实际值](https://developer.apple.com/documentation/avfaudio/avaudiosession/setpreferrediobufferduration(_:))


历史测试引用的 `outputs/` 属于本地证据目录，不随开源仓库发布；公开测量摘要见 [验证记录](VALIDATION.md)。
