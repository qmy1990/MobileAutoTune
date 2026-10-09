MobileAutoTune 首次公开发布 / First public release.

- MIT 开源，免费商用；保留许可证和第三方署名。 / MIT-licensed; free commercial use with retained license and dependency notices.
- Java 包名 `com.moon.mobileautotune`，Demo `com.moon.mobileautotune.demo`。
- 自动修音、±12半音变声、混响、回声、可调降噪；先变声后按原所选调式修音，强度最高120%。
- Android8.0+：四种ABI通用及独立AAR、C/C++原生包、可安装Debug Demo APK。
- iOS14+：真机arm64、模拟器arm64/x86_64 XCFramework；模拟器Demo及未签名IPA（真机需自行签名）。
- JitPack: `com.github.qmy1990:MobileAutoTune:v0.3.0`。
- SDK与Demo源码、中英文README、SHA256SUMS及制品manifest。

Nominal live DSP latency is20ms(frame)/30ms(stream); acoustic round-trip ≤50ms is not yet verified. See README and validation documentation for format, threading and monitoring requirements.
