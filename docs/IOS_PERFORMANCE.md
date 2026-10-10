# iOS 性能验证

SDK 使用 RemoteIO、AVAudioSession measurement、48kHz单声道处理。读取激活后的实际 I/O 缓冲、输入/输出延迟和音频工作组状态。

Demo 支持诊断启动参数 `--mat-live-test`、`--mat-tuning`、`--mat-pitch-shift`，满强度混响、回声和降噪用于持续录音测试。监听需耳机；无耳机时仅验证录音、处理和写盘。

检查回调P99、最大耗时、超周期次数、时间戳缺帧、录音丢帧、渲染和磁盘错误。签名与麦克风权限由宿主提供。当前验证范围见 [验证说明](VALIDATION.md)，整体声学往返延迟需单独回环测量。
