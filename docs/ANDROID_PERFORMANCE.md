# Android 真机性能记录（2026-10-09）

设备：vivo V2227A，SM8550 / 骁龙8 Gen2，Android16 / API36，有线耳机路由。测试包含实时麦克风、四项效果满强度、耳机监听和两条 WAV 写盘。未清除 App 数据；更新安装保留已有录音。不同时间的环境声不是同一人声重放，因此回调耗时不能作严格相同输入的算法 A/B。

## 已完成的长录对照

| 指标 | 原策略，90秒 | 首次优化策略，300秒 |
|---|---:|---:|
| 输出 XRun | 336 | 0 |
| 输入缺帧回调 | 32 | 2 |
| 写盘丢样本 / 错误 | 0 / 0 | 0 / 0 |
| 回调 P99 上界 | 3.5ms | 3.3ms |
| 回调最大 | 4.063ms | 4.118ms |
| 输出 burst / buffer | 4 / 4ms | 4 / 8ms |
| 输出时间戳估计中位数 | 25.65ms | 29.93ms |

首轮优化通过增加一个4ms输出调度余量、按实际读取数据管理固定输入队列、过量积压恢复及性能提示改善稳定性。平均 CPU 负载没有数量级变化。输出时间戳估计增加约4ms，属于稳定性代价；用户反馈“不再卡顿，但延迟明显”，所以整体低延迟目标仍未完成。

原策略 `getAvailableFrames()` 的计数在90秒后增加到1474ms等效样本数，但它是 written/read 差，捕获丢帧会使其虚增。**这不是实测1474ms物理延迟**。原始 AudioFlinger 快照显示有真实捕获积压；新版以非阻塞 read 返回的实际样本管理有界队列，300秒队列最大8ms。

后续版本加入±200ppm平滑时钟补偿，保留超阈值恢复。计划600秒的测试在约255秒后主动停止以优先处理用户的延迟反馈；其控制台记录在停止前输出XRun为0，但没有完整结束报告，不能标记为10分钟验证通过。其后加入现代 API 选择的包属于另一个版本，不能沿用300秒报告当作最终包完整长录结果。

## 算法与路由分析

ARM 原生 DSP 单独运行五分钟，无逐渐变慢趋势。尝试浮点 flush-to-zero 没有有效收益，未加入该改动。移除实时路径不使用的离线变调 setter 和历史数组复制后，hook3 使用相同实时预设的导出 WAV 与修改前逐字节一致。

Perfetto 对照中，实际麦克风回调内 DSP 平均约0.76–0.80ms，回调约0.80–0.85ms；偶发达到约4ms硬件周期。两帧burst提供调度余量。原始日志、trace 和官方 trace processor 保存在 outputs/android-performance/，仅用于本机分析。

该设备实际打开的是 AAudio shared FAST（input/output MMAP均0）。测试 mono/stereo、float/int16、shared/exclusive、Media/Game，以及显式设备/包名，未取得 MMAP。强制MMAP探针返回 -898（ErrorIllegalArgument，参数被拒绝）；仅能说明这些配置未能打开，不能证明整机所有路由都不支持MMAP。VoiceCommunication输出用法在本机反而约80–95ms，因此未采用。

当前算法名义延迟：480帧入口20ms，任意帧流入口30ms；最大历史读取年龄分别30/40ms。设备输出时间戳估计约30ms，输入时间戳在当前通路不可用。回调耗时、FIFO长度与输出时间戳都不能代替麦克风到耳机的声学往返测试。最大50ms及“人耳无明显滞后”尚未达标确认。

## 复现与证据

- 原策略：`outputs/android-performance/baseline/endurance.json`、`summary.json`、`instrumented.perfetto-trace`
- 首次300秒优化：`outputs/android-performance/optimized/endurance-first.json`、`summary-first.json`、`trace.perfetto-trace`
- 后续中止测试：`outputs/android-performance/optimized/endurance-final-console.txt`、`clocked-early.perfetto-trace`
- 新 API 选择、回退与限制：[平台 API 说明](PLATFORM_APIS.md)

```sh
./scripts/build_android.sh
adb install -r dist/android/mobileaudio-demo.apk
adb shell am instrument -w com.moon.mobileautotune.demo/.SDKSmokeInstrumentation
./scripts/profile_android.sh 600 false
```

脚本需要手机调试连接和麦克风权限，测试过程中保持 Demo 前台；会新建两条录音，不删除已有录音。`legacy=true`仅用于Debug包对照，release AAR没有该测试入口。每次测试都应检查记录推进、输入缺帧、输出XRun、写盘丢帧和错误。10分钟完成后还需在人声与热态负载下重复，并测声学延迟。

## 现代API与固定帧处理对照

同一Android16手机，四效果满强度、监听及双WAV写盘：

| 90秒测试 | VoicePerformance / 旧流适配 | Unprocessed / 旧流适配 | VoicePerformance / 固定480帧 |
|---|---:|---:|---:|
| 输出XRun | 0 | 0 | 0 |
| 输入缺帧回调 | 4 | 2 | 3 |
| 回调P99上界 | 3.2ms | 3.4ms | 3.7ms |
| 最大回调 | 4.540ms | 4.419ms | 4.393ms |
| 超出回调周期次数 | 100 | 175 | 0 |
| 输出时间戳估计中位数 | 30.066ms | 30.227ms | 32.265ms |
| 算法名义延迟 | 30ms | 30ms | 20ms |

实际VoicePerformance预设为10，无预设回退；它没有显著降低本机输出延迟。固定帧处理消除10ms流适配延迟，输出侧估计增加约2ms，用户反馈“延迟减轻，没有爆音或断续”。三个测试均无写盘丢样本或错误，输入FIFO最大8ms。固定帧的回调预算为10ms，旧模式实际硬件回调为4ms，不能跨列直接比较超周期次数作为算法加速倍数。

证据目录分别为 `outputs/android-performance/20261009-222112-legacy-false-preset-0`、`20261009-222638-legacy-false-preset-1`、`20261009-223512-legacy-false-preset-0-compact-true`。随后十分钟测试在约225秒时因Demo进入后台而提前停止，不计为长录完成。Demo录音时已增加保持屏幕常亮；切到后台仍按原生命周期策略停止并保存录音。

应用内强制MMAP同样返回-898，系统配置确实声明MMAP通路。失败根因尚未确认，详见 [MMAP记录](MMAP.md)。

固定480帧的最终版本另抓取15秒Perfetto，1102次回调：DSP平均1.867ms、最大3.817ms；完整回调平均1.952ms、最大3.928ms。固定帧相对原192帧回调每次处理更多样本，不能只比较单次CPU耗时推断性能倒退。证据为 `outputs/android-performance/final-compact.perfetto-trace` 和 `final-compact-trace-summary.txt`。

## 最终固定帧版本十分钟结果

证据：`outputs/android-performance/20261009-230106-legacy-false-preset-0-compact-true/endurance.json`。600.930秒、600次采样，记录持续推进。回调P99上界3.5ms、最大5.200ms、超10ms回调周期0次。输出XRun共2次，在第7秒与第120秒首次观察到，之后至600秒未增加；不能标记为零XRun通过。输入缺帧回调16次、输入恢复丢弃1465样本；写盘丢样本、磁盘错误和路由断开均0。输入FIFO中位数3.448ms、P99约6.021ms、最大8.021ms，未随录音时长持续增长。

输出时间戳估计中位数32.083ms，实际AAudio共享通路、MMAP输入/输出均0、VoicePerformance10，无预设回退。固定块算法名义延迟20ms。可支持“没有持续积压导致越录越慢”，仍未取得整体50ms或全窗口无XRun验收。原音/效果WAV长度一致600.5秒，同目录 `wav-headers.json` 保存元数据。

## 0.2.0：变声后修音与120%上限

新版更新安装后，SDK Java/JNI测试通过，包含+1半音后仍按C大调选择B3目标。90秒测试开启耳机监听、双WAV写盘，修音1.2、独立音高+1半音、其他三效果均1。90次采样记录持续推进；回调P99上界3.6ms、最大4.018ms、超10ms周期0次，输出XRun0。输入缺帧回调1次；捕获恢复丢弃、录音丢样本、磁盘错误和断开均0。输入FIFO中位数3.406ms、P99约5.521ms、最大7.563ms。

算法名义延迟仍20ms，输入/输出MMAP均0，输出时间戳估计中位数32.874ms。新增控制没有增加变调器、音高检测器或音频缓冲。该测试验证新参数90秒运行；前述0.1.1十分钟结果单独保留，整体50ms声学延迟仍未测定。证据：`outputs/voice-controls/android-90/endurance.json`、`summary.json`（原始目录`outputs/android-performance/20261009-233517-legacy-false-preset-0-compact-true/`）。

复现：`./scripts/profile_android.sh 90 false 0 true 1.2 1`。
