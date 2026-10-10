# 平台音频 API

最低支持 Android8.0 与 iOS14.0。SDK 请求设备支持的低延迟通路，能力不可用时使用兼容路径。实际后端、MMAP、录音预设、缓冲与回调周期通过遥测返回。

| 系统 | 音频通路 | 行为 |
|---|---|---|
| Android API26 | Oboe / OpenSL ES | 原始录音预设 |
| Android API27–28 | Oboe / AAudio | 低延迟、独占请求；共享及 OpenSL ES 回退 |
| Android API29–30 | AAudio + VoicePerformance | AUTO 优先实时演唱预设，失败回退 Unprocessed |
| Android API31+ | 上述通路 + Performance Hint | 请求实时调度提示，设备不支持时正常继续 |
| iOS14+ | RemoteIO + measurement | 系统管理渲染线程 Audio Workgroup |

## Android

```java
sdk.startLive(rawPath, wetPath, true, MobileAudio.CapturePreset.AUTO,
    MobileAudio.LiveBuffering.LOW_LATENCY);
```

监听需耳机。录音预设 AUTO、UNPROCESSED、VOICE_RECOGNITION、VOICE_PERFORMANCE 在开始录音时确定。厂商预设可能改变增益或音色；需要保留原始人声时可选 UNPROCESSED。API29 以下的 VoicePerformance 请求回退为 Unprocessed。

LOW_LATENCY 请求480样本/10ms回调，成功时使用帧 DSP 入口，名义算法延迟20ms。固定回调不可用时使用任意长度适配器，名义算法延迟30ms；可通过 COMPATIBLE 显式选择任意长度入口。同一运行中的引擎只使用一种入口。

输出保留两帧硬件 burst 缓冲；输入使用预分配 FIFO 和有界时钟补偿。时间戳查询仅在控制线程运行。Java 遥测共34项，其中26为实际录音预设，27为预设回退标记，28为已请求调度提示，29/30为输出共享/性能模式，31为输入后端，32为当前算法名义延迟毫秒，33为实际回调周期毫秒。

独占、MMAP 和调度提示均属于请求，实际是否可用取决于设备。详见 [MMAP](MMAP.md)。

## iOS

RemoteIO 在单个渲染回调中拉取输入并完成 DSP；系统将渲染线程加入音频工作组。SDK 查询 `audioWorkgroupAvailable`，录音写盘线程不加入实时工作组。

AVAudioSession 使用 playAndRecord + measurement，请求48kHz和128帧 I/O 缓冲。以激活后的实际采样率、缓冲和 inputLatency/outputLatency 为准。非48kHz路由返回错误。中断、路由变化和媒体服务重置会停止录音，宿主应按业务需要恢复。

## 延迟

回调执行耗时、算法历史和硬件输入/输出延迟是不同指标。整体声学往返延迟需要回环实测，不能从算法延迟或时间戳估计直接推导。当前未完成整体往返≤50ms验收。

## 官方资料

- [Android Oboe 低延迟音频](https://developer.android.com/games/sdk/oboe/low-latency-audio)
- [Android VoicePerformance](https://developer.android.com/reference/android/media/MediaRecorder.AudioSource#VOICE_PERFORMANCE)
- [Android Performance Hint](https://developer.android.com/ndk/reference/group/a-performance-hint)
- [Apple Audio Workgroups](https://developer.apple.com/documentation/audiotoolbox/understanding-audio-workgroups)
- [Apple 首选 I/O 缓冲](https://developer.apple.com/documentation/avfaudio/avaudiosession/setpreferrediobufferduration(_:))
