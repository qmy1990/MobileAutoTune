# Android MMAP 与芯片音频能力

MMAP 是内存映射音频缓冲的数据通路。AAudio 独占 MMAP 可以让应用与 ALSA 驱动共享缓冲，绕过常规混音通路；共享 MMAP 则仍有 AAudio 服务混音。它可以减少缓冲和调度延迟，不能消除 RNNoise 分帧或变调算法的历史窗口，也不会自动提高修音、混响、回声的音质。AAudio 并不等于 MMAP，独占请求也不保证最终独占成功。

## 当前手机实测

2026-10-09，vivo V2227A / Snapdragon 8 Gen 2 / Android 16，有线耳机。系统 `aaudio.mmap_policy` 与 `aaudio.mmap_exclusive_policy` 均为 AUTO（2）；Audio Policy 实际配置包含 MMAP_NOIRQ 输入和输出端口，输出配置为48kHz stereo PCM16，支持的设备包含当前有线耳机。故不能把结果描述成整机没有MMAP配置。

另外使用Android36新增的 `AAudio_getPlatformMMapPolicy` / `AAudio_getPlatformMMapExclusivePolicy` 只读查询，有线耳机输入、输出均返回AUTO（2），内置设备及USB类型也返回AUTO。证据为 `outputs/android-performance/mmap-platform-policy.txt`。这比只读旧系统属性更具体，但仍不等于实际能打开。

正常 SDK 打开得到 AAudio shared FAST，输入、输出 `isMMapUsed` 都是0。独立原生探针的 mono/stereo、float/int16、shared/exclusive 组合，以及实际应用进程内强制MMAP的测试，打开返回 -898 / ErrorIllegalArgument。应用内测试使用当前 SDK 的480帧回调与完整打开配置；返回错误发生在成功创建Live之前，因此没有产生MMAP延迟测量。

这说明当前配置无法打开MMAP，尚未定位拒绝参数的具体来源。现有错误码不足以区分客户端检查、厂商框架修改与HAL限制。配置文件声明支持，不保证该路由和运行环境的实际打开成功。没有证据表明是麦克风授权问题。系统日志抓取未获得本次AAudio详细错误；相关证据在 `outputs/android-performance/mmap-in-app-console.txt`、`mmap-audio-policy.txt`、`mmap-aaudio.txt`。

生产SDK沿用系统AUTO策略，请求低延迟和独占，允许系统选择MMAP；失败回退共享AAudio，再必要时回退OpenSL ES。强制测试只存在Debug instrumentation中，结束后恢复本进程AUTO，不修改系统属性，不重启音频服务，不进入release AAR。

## 权限

标准AAudio/Oboe的MMAP没有额外的App运行时权限或用户弹窗；录音继续需要RECORD_AUDIO。驱动、HAL、音频策略和缓冲共享所需SELinux规则由设备厂商集成。Android官方文档中的root/setprop示例用于系统开发和诊断，不能把它当成正常App启用MMAP的步骤。应用绕过Android音频系统直接访问USB设备是另一个方案，其USB授权不属于MMAP授权。

## 芯片厂商能力

高通8 Gen 2提供Aqstic音频编解码器、扬声器放大器与Audio and Voice Communication Suite；部分能力取决于OEM选择和搭配设备。联发科同样提供多麦录音、环境/风噪抑制和低延迟传输等能力，例如Dimensity9400e官方列出的三麦HDR录音与降风噪。

芯片功能清单不能证明第三方SDK可以直接使用这些算法，也不能证明全部手机都具备相同的监听延迟。应用实际拿到的通路由整机厂商的音频策略与HAL决定。通话降噪可能改变歌声音色，需要比较实录后决定是否启用。GPU/NPU模型加速应测量包含提交、同步和数据转换的完整耗时；它不会自动缩短音频硬件的缓冲。蓝牙播放延迟宣传值也不能替代“麦克风→DSP→耳机”的往返测量。

## 官方资料

- [Android新版按设备类型查询MMAP策略接口](https://developer.android.com/ndk/reference/group/audio)
- [Android AAudio / MMAP 架构、HAL配置与AUTO回退](https://source.android.com/docs/core/audio/aaudio)
- [高通8 Gen 2音频能力及OEM选择说明](https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Gen-2-Product-Brief.pdf)
- [联发科Dimensity9400e多麦录音与降噪](https://www.mediatek.com/products/smartphones/mediatek-dimensity-9400e)


历史测试引用的 `outputs/` 属于本地证据目录，不随开源仓库发布；公开测量摘要见 [验证记录](VALIDATION.md)。
