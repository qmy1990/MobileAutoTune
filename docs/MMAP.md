# Android MMAP

MMAP 是 AAudio 使用共享内存传递音频的通路，可减少音频服务的数据复制和缓冲开销。它依赖音频 HAL、驱动与当前路由的支持。AAudio、独占模式与 MMAP 不是同一个概念；成功使用 AAudio 不代表实际启用了 MMAP。

SDK 通过 Oboe 请求低延迟与独占模式，并读取实际 MMAP 状态。若设备无法提供相应通路，使用共享 FAST 或 OpenSL ES。强制要求 MMAP 会让部分设备无法打开音频，所以生产接口使用能力协商。Debug 测试支持进程内策略诊断，不修改系统属性。

录音需要 `RECORD_AUDIO` 麦克风权限。MMAP 不需要另申请应用权限；应用权限无法开启厂商未提供的音频 HAL 能力。

高通、联发科等平台可能提供音频 DSP、降噪、回声消除与音频卸载能力，但第三方应用能否使用由设备厂商的系统、HAL 和录音路由决定。CPU、GPU 或 NPU 型号不能保证 MMAP 支持或整体低延迟。VoicePerformance 等 Android 预设可能调用厂商处理，也可能改变歌声音色；SDK 的可调效果仍由公共算法核心实现。

读取 Demo 遥测中的输入/输出 MMAP、后端、共享模式、缓冲和欠载计数，结合耳机路由与声学回环验证设备表现。

- [Android AAudio](https://developer.android.com/ndk/guides/audio/aaudio/aaudio)
- [Oboe 低延迟音频](https://developer.android.com/games/sdk/oboe/low-latency-audio)
