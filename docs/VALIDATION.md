# 验证说明

## 自动测试

- C++核心：参数边界、零强度原音对齐、±12半音变声、变声后调式量化、最大修音目标、帧与流入口一致性、并发参数更新、处理阶段零堆分配。
- WAV：PCM16/float32、单声道/双声道、RIFF边界、截断和无效输入、取消、输出原子替换、预滚动裁剪与尾音。
- Android：Java/JNI创建、处理、遥测、参数拒绝和WAV导出；四ABI AAR及原生SDK；64位ELF与APK 16KB对齐。
- iOS：arm64真机、arm64/x86_64模拟器、XCFramework模块与Demo构建。
- 依赖：上游源码锁定清单、完整许可证和分发声明。

## Android 真机

Snapdragon8 Gen2 / Android16，满强度修音、混响、回声、降噪和 +5半音变声，90秒双轨录音，硬件监听关闭：回调P99约3.2ms、最大3.86ms，10ms周期无处理超时；录音丢帧和磁盘错误为0。FIFO中位2.09ms，峰值8ms；输出欠载1次，输入缺帧回调2次，积压恢复307样本。该结果描述对应测试时段，不能替代长时间热态及耳机回环验收。

## iOS 真机

iPhone8 Plus / iOS16.7.10，四项满强度效果和 +5半音变声，90秒双轨录音，内置麦克风、监听关闭：回调P99为0.5ms、最大1.10ms，2.67ms周期无处理超时；时间戳缺帧、录音丢帧、渲染和写盘错误均为0。音频工作组可用。该路由测试不包含耳机监听与声学回环。

## 音频与设备范围

最大修音强度为 `1`，关闭修音为 `0`。变声与修音使用一次变调，参数变化不增加算法缓冲。桌面人声与纯音用于输出一致性和目标音高验证，Android真机用于JNI和持续录音性能验证。

构建支持的最低版本为Android8.0和iOS14.0；所有最低系统、CPU架构和耳机路由仍需宿主项目在目标设备上验收。算法名义实时延迟20/30ms，整体声学往返≤50ms尚未完成测量。后处理性能取决于设备、素材和文件I/O，应对目标素材测量完整导出耗时。

发布附件提供 `manifest.json` 和 `SHA256SUMS`，CI见 [GitHub Actions](https://github.com/qmy1990/MobileAutoTune/actions)。用户录音、设备日志和个人签名不包含在开源源码或安装包中。

## Offline vocal key analysis

The FP32 SwiftF0/RMVPE pipeline uses pinned model hashes. Validation covers all24 major/minor keys with synthetic pitch tracks, detuning/vibrato, silence, monotone and invalid observations; resampling at8/16/22.05/44.1/48/96/192kHz, chunk seams, the mel frontend and SHA256 known answers. Core, WAV and key tests pass with address/undefined sanitizers. The independent NumPy mel reference has maximum absolute error below0.0004 on the local validation input.

Device results for a6-second,15-note harmonic C-major test signal:

| Device / execution | First analysis | Reused analysis | Other checks |
|---|---:|---:|---|
| Snapdragon8Gen2, Android16, CPU4 threads | 4.90s | 0.53s | AAC document import0.71s; URI/native key parity; cancellation during inference; reuse |
| Snapdragon8Gen2, Android16, automatic6 threads | 8.62s | 0.51s | Selected CPU for both models;13.13-second dry vocal1.33s |
| iPhone8 Plus, iOS16.7.10, default CPU6 threads | 5.92s | Not separately timed | AVAudioFile decoding; cancellation; reuse |

First-analysis time includes model verification/loading; Android bundled-asset copying occurs before this measurement. Automatic Android selection also includes provider comparison. These are offline measurements, not realtime audio round-trip measurements or a key-accuracy benchmark. Apple uses the validated multicore CPU path for the pinned dynamic models. Android tries NNAPI/XNNPACK and retains a provider only after numerical and timing checks.

Private short dry-vocal samples can disagree with the whole-song key label and are reported as ambiguous. They do not establish top-1 key accuracy and are not distributed. Candidate support and pitch-model agreement are diagnostics, not probabilities of the musical key being correct. The synthesized signal checks numerical/integration behavior; its known key does not measure performance on real singing.
