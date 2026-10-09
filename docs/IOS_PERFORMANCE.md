# iOS真机性能记录（2026-10-09）

iPhone8 Plus / iOS16.7.10，48kHz mono，RemoteIO + AVAudioSession measurement，最低部署目标iOS14。手机没有耳机；为避免扬声器回授，测试监听关闭。测试含实时麦克风、四项效果满强度与原音/效果双WAV写盘。

## 已完成90秒测试

实际时长90.013秒；记录持续推进。回调周期2.667ms、P99上界0.5ms、最大1.242ms、超周期0次。renderError、录音丢样本、磁盘错误、采样时间戳间断均为0。Audio Workgroup可用，实时回调由系统加入工作组。

会话报告inputLatency约1.438ms、outputLatency2ms、ioBufferDuration2.667ms。它们不是声学往返测量，且本次没有耳机监听，不能据此确认整链路50ms或听感达标。SDK算法仍使用不规则流适配入口，名义算法延迟30ms。

单段hook3（13.125秒）四项满强度后处理，含初始化与WAV读写、不增加效果尾音，耗时2.206秒，RTF约0.168。此项验证一段音频，不能代替六段及1/5/10分钟、冷热态完整验收。

证据：`outputs/ios-performance/first-90/Documents/performance-ios.json`、`performance-offline.json`及`summary.json`。第一次600秒测试在约127秒时提前停止（报告completed=false），不计为十分钟完成。测试诊断会在提前停止时写失败报告，不会误报通过。

## 构建和安装

SDK设备arm64、模拟器arm64/x86_64与两个Demo构建通过。XCFramework包含静态算法依赖。真机Demo使用现有免费开发签名更新安装，保留既有录音。拒绝权限、切后台、路由变化和耳机监听需分别验收；本机没有覆盖最低iOS14运行兼容性。

[平台API和兼容处理](PLATFORM_APIS.md)

## 最终十分钟结果

`outputs/ios-performance/final-600/Documents/performance-ios.json`：completed=true、requestedSeconds600、实际600.028秒、600次采样，记录持续推进。回调P99上界0.5ms、最大1.071ms、超周期0次；renderError、timestampGapSamples、录音丢样本、磁盘错误及关闭后磁盘错误均0。225008次回调，DSP处理28800480样本。

Audio Workgroup可用；实际缓冲和输入/输出会话延迟与90秒测试一致。监听仍关闭、没有耳机，不作为耳机监听或声学往返验收。摘要见同目录 `summary.json`。

## 0.2.0：变声后修音与120%上限

新Demo更新安装后，修音1.2、独立音高+1半音、其他三效果均1，双WAV写盘，90.012秒完成、90次采样记录持续推进。回调P99上界0.5ms、最大1.578ms、超2.667ms周期0次；renderError、录音丢样本、磁盘错误、关闭后磁盘错误和timestampGapSamples均0。33755次回调、处理4320480样本。Audio Workgroup可用。

仍使用内置麦克风，手机没有耳机，监听关闭；会话inputLatency约1.438ms、outputLatency2ms、I/O缓冲2.667ms，算法流入口名义延迟仍30ms。新参数不增加音频缓冲，不能据本次结果确认耳机监听听感或50ms声学延迟。前述十分钟测试属于0.1.1。

随后使用同一参数快照自动后处理hook3（13.125秒），包含初始化、WAV读写、不增加尾音，耗时2.250秒、RTF约0.171。证据：`outputs/voice-controls/ios-90/Documents/performance-ios.json`、`performance-offline.json`及`outputs/voice-controls/ios-90/summary.json`。
