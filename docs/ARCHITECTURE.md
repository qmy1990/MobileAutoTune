# 架构与算法说明

```
48kHz 单声道 PCM
  → 非有限值/输入幅度保护
  → RNNoise → 与等延迟原音混合（降噪强度）
  → 因果音高检测 + 平滑的变声音高偏移（±12半音）
  → 按变声后音高选调式最近音 → 置信度/滞回/跟随速度
  → 合并变声偏移与调式修正量
  → 实时双读头时域变调 / 后处理频谱变调
  → 与同延迟原音交叉淡化（修音开关过渡）
  → 8路混响 + 分数延迟回声
  → 高电平软限幅
```

## 选型

音高检测实现 [YIN（de Cheveigné / Kawahara, 2002）](https://pubmed.ncbi.nlm.nih.gov/12002874/) 的归一化差分、首个周期极小值及抛物线细化。48kHz 输入先低通再降采样到 12kHz；实时窗口 384 点（32ms），后处理窗口 1024 点（85.3ms），每 10ms 更新。音高范围约 65–1000Hz。窗口为历史分析窗口，不直接增加音频输出缓冲，但影响新音符被可靠检测的响应时间。后处理再用 5 帧中值稳定（经过无声段后清空）。不是神经网络音高识别，重噪/多声部时可能误判八度。

实时变调是受音高周期控制的双读头变长延迟线与 Hann 交叉淡化。它借鉴[音高同步波形处理](https://www.isca-archive.org/eurospeech_1989/charpentier89_eurospeech.pdf)的周期连续思想，但**不是完整的标记音高历元的 TD-PSOLA 实现**，不能据此声称具有完整 PSOLA 的共振峰保持能力。稳定周期输入下，交叉淡化头之间相差一个周期，重置跨度为两个周期；短历史允许低延迟，低于 100Hz 时窗口受限。最大读取历史为 20ms、名义中心 10ms。对高质量低音使用后处理模式。

后处理变调使用 [Signalsmith Stretch](https://signalsmith-audio.com/code/stretch/)（MIT），3840 样本/80ms窗口、480样本/10ms步长，共振峰补偿，8kHz tonality limit。按照库的 `inputLatency()+outputLatency()` 报告时序；WAV导出补零冲刷、裁去预滚动，确保末段数据不会被丢弃。

降噪使用 [Xiph RNNoise](https://github.com/xiph/rnnoise)（BSD-3-Clause）。实时用官方 v0.1 小模型（低成本、约10ms分析/合成延迟）；后处理用当前较大官方模型，延迟频谱与分析/合成合计按20ms对齐原音。所有模型在本地二进制内，归一化 PCM 转为 RNNoise 要求的16位幅度范围再还原。输出 VAD 不用于硬门控，避免切断唱歌尾音。当前较大模型的上游 ARM NEON 路径可用内置量化权重，并不需要 TFLite/CoreML/ONNX Runtime。

## 时序

| 模式/入口 | 降噪延迟 | 变调名义中心 | 适配缓存 | 名义延迟 | 变调最大历史 | 最大历史年龄 |
|---|---:|---:|---:|---:|---:|---:|
| 实时，480帧入口 | 10ms | 10ms | 0 | 20ms | 20ms | 30ms |
| 实时，任意帧入口 | 10ms | 10ms | 10ms | 30ms | 20ms | 40ms |
| 后处理，480帧入口 | 20ms | 80ms | 0 | 100ms | 固定库延迟 | 100ms |

实际修音过程中变调读头相位会移动，所以最大历史年龄与名义中心不同。设备收音/播放还有缓冲，不能将这里的40ms直接等同整体声学延迟。

## 移动平台

Android 的 Oboe 自动选择 AAudio / OpenSL ES；Android 8.0 API26 走兼容路径，Android 8.1+ 使用适用的 AAudio。按 [Android 官方低延迟指导](https://developer.android.com/games/sdk/oboe/low-latency-audio) 请求 LowLatency、Exclusive、原生48kHz、单声道float，输出保留两帧burst调度余量。Android12/API31+请求 Oboe/Android Performance Hint，能力不可用时继续普通回调。Android10/API29+的自动录音预设优先VoicePerformance，旧系统保留Unprocessed；开放显式预设选择。打开失败会重试原始录音、共享或OpenSL ES通路。独占和 MMAP 可能退回共享 FAST 通路，必须记录实际配置。

Android默认请求480样本的固定回调，实际获得480样本回调时直接调用帧DSP入口，省去10ms流适配缓存；无法打开固定回调时退回任意帧入口。回调模式在开始时确定，不在运行中混用DSP入口。兼容模式可显式选择。

实时输出回调每次只进行一次非阻塞输入读取，把实际取得的全部样本放入预分配的 FIFO。启动时排空旧输入，进入稳定状态后保持约一帧burst输入余量；输入输出时钟差用连续线性插值补偿，比例限制±200ppm（不到0.004半音）。积压超过两帧burst时丢弃超额旧数据并计数，以恢复低延迟；这是故障恢复，正常时钟差不靠丢块处理。Legacy AAudio 的 `getAvailableFrames()` 是 written/read计数差，丢失的输入帧可能使它持续增长，因此正式通路不以该值判断实际队列。缺帧填零并统计，错误回调标记设备断开，由宿主停止/重新启动。

遥测包含实际 FIFO 当前/峰值、输出XRun、近期/累计P99回调耗时、超周期次数、时钟补偿、MMAP标记、输入/输出时间戳延迟估计；时间戳查询在控制线程执行。音频回调只操作固定缓冲和原子计数，不写日志或文件。调度 trace 可看到 `MAT audio callback` 和 `MAT DSP`。

iOS 使用 RemoteIO 单个输出渲染回调，在同一回调拉取输入。AVAudioSession 使用 playAndRecord + measurement，iOS14+系统自动将RemoteIO渲染线程加入Audio Workgroup，SDK在控制线程读取工作组可用状态，缺少能力仍继续RemoteIO；不会把写盘线程加入实时工作组。申请48kHz、128帧缓冲，参考 [Apple 首选缓冲说明](https://developer.apple.com/documentation/avfaudio/avaudiosession/setpreferrediobufferduration(_:))。拒绝实际非48kHz路由。录音权限由宿主请求；中断、媒体服务重置、设备变化时停止，由宿主明确重启。

两端录音都使用预分配的 SPSC 环形缓冲，音频线程只复制浮点 PCM。独立线程写两条 PCM16 WAV。队列满时统计丢帧，磁盘错误通过遥测返回；已丢帧的录音不能作为无丢帧验收结果。监控开关只静音硬件输出，效果文件仍保留处理声音。

## 内存与并发

C++引擎拥有降噪状态、音高环形缓存、变调缓存、混响延迟线与回声缓存。构造阶段分配，处理阶段复用。实时RNNoise静态FFT首次初始化用 `call_once` 在构造阶段预热，避免音频回调分配和多实例初始化竞争。

参数邮箱采用原子载荷加代数版本号，音频回调最多读取一次；控制更新未完成时沿用上一快照。没有等待控制线程的循环。一个控制写者和一个音频处理者。重置/销毁仅在音频流停止后执行。

WAV读取/处理/写入每次480帧，内存与文件时长无关。解析校验RIFF边界、fmt编码、块对齐、采样率和截断文件。临时文件创建在输出目录，处理成功才rename，取消/失败移除临时文件，保留旧目标。写出的长度为原音长度（可选再追加192000帧尾音）。


构建和测试范围见 [验证说明](VALIDATION.md)。
