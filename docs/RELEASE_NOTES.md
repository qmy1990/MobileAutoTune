MobileAutoTune — Android8.0+ / iOS14.0+ 人声音频 SDK / Vocal audio SDK.

- MIT 开源，免费商用；发布时保留许可证和第三方声明。 / MIT license with free commercial use.
- 自动修音强度 `0..1`，`1` 为最大机械修音；12主音、大调、自然小调、半音阶。
- ±12半音变声；按变声后的音高选择目标调式音符，一次变调完成处理。
- 可调混响、回声和降噪；实时双轨录音与高质量 WAV 后处理。
- Android 四种 ABI：通用与独立 AAR、C/C++ 静态 SDK、Demo APK。
- iOS：真机arm64、模拟器arm64/x86_64 XCFramework、模拟器 Demo、未签名 IPA。
- JitPack：`com.github.qmy1990:MobileAutoTune:v0.4.0`。
- 中英文 README、SDK 与 Demo 源码、第三方许可证、制品清单与 SHA256SUMS。

Nominal live DSP latency:20ms(frame API)/30ms(stream API). Acoustic round-trip≤50ms is not yet verified. iOS device demos require your own signing. See README for integration, format and threading requirements.
