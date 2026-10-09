# 独立音高与120%修音（0.2.0）

新增 `pitch_shift_semitones`（C）/ `pitchShiftSemitones`（Java、Objective-C、Swift），范围-12到+12半音，默认0。负值降低声音，正值升高声音，±12对应一个八度。Android、iOS Demo提供0.5半音档位及恢复原音高按钮，录音中可以调整；后处理使用处理开始时的参数快照。

修音tuning范围扩展为0–1.2，Demo显示0–120%。原0–100%响应保持；100–120%在不越过调式目标的前提下增加贴音速度，实时模式还提高音符切换硬度。原满强度贴音时间常数2ms，120%时为2/1.2≈1.667ms。20%指新增控制范围与最大贴音速度比例，不代表主观电音听感可线性量化。

音高偏移与调式修正共同交给原有变调器处理，无新增音频缓冲。先以输入音高加上平滑后的变声偏移，再在用户所选调式内找最近目标音。修音量与偏移合并后只执行一次变调；不会把所选调式整体移位。例如225Hz加1半音后按C大调修正，目标为B3≈246.94Hz，而非先修到A3再升半音得到调式外的A#3。低强度修音按比例靠近目标，100–120%完整靠近调式目标。关闭修音时仍可独立变声；有声及无声辅音均可移调，不依赖可靠F0检测才能出声。偏移变化有20ms控制平滑，平滑时间是参数过渡，不是额外音频延迟。

实时双读头本身会随音高改变声色；后处理共振峰随独立移调改变，同时对自动修音部分保留原共振峰补偿。大幅移调、低音和无声辅音可能出现变调颗粒感，极端±12档位需试听。默认0保留原声色。

```java
MobileAudio.Parameters p=new MobileAudio.Parameters();
p.pitchShiftSemitones=-5; // 更低沉
p.tuning=1.2f;
sdk.setParameters(p);
```

```swift
let p = MATAudioParameters()
p.pitchShiftSemitones = 5 // 更高尖
p.tuning = 1.2
try sdk.setParameters(p)
```

```cpp
auto p = mat_default_parameters();
p.pitch_shift_semitones = -5;
p.tuning = 1.2f;
mat_set_parameters(engine, &p);
```

CLI提供 `--pitch-shift -12..12` 与 `--tuning 0..1.2`。遥测correction_semitones为包含独立偏移的总变调量，target_pitch_hz为按变声后音高选出的原所选调式目标；修音关闭时目标使用检测音高移调后的频率。未知F0仍报告0目标。

## 接口兼容

0.2.0的C参数结构追加一个float字段。旧C二进制不能直接与新头文件布局混用，应更新头文件并重新编译宿主；使用默认参数构造或聚合初始化时，新字段为0。Java新增字段默认0；JNI允许原8值数组，以0补齐独立音高，但新Java库不能与旧native库混用。iOS请配套更新整个XCFramework；NSCopying及后处理快照包含新字段。最低支持版本继续为Android8.0、iOS14.0。

## 验证

核心测试覆盖两种模式的正负移调及±12端点、120%修音不超调、变声后按原调式修音、参数越界与NaN/Inf拒绝、并发调参、零回调heap分配和原有延迟不变。固定帧与流入口仍逐样本相同，仅相差原适配器一帧延迟。hook3使用原100%参数、音高0的实时/后处理WAV与修改前逐字节对照一致。

试听页面为 `outputs/voice-controls/listen.html`；不会覆盖用户原音或既有试听结果。新版本的真机数据追加到本目录性能记录，旧0.1.1长录结果不自动算作0.2.0验收。整体50ms声学指标仍待回环测量。

实测单音225Hz，C大调、120%修音：偏移-6/-1/+1/+6半音时，实时输出约164.800/219.986/246.937/329.668Hz，对应调式目标164.814/220.000/246.942/329.628Hz。后处理输出与同一目标误差均小于1Hz。证据：`outputs/voice-controls/pitch-checks.txt`。这些偏移专门覆盖旧顺序会落在调式外的情况。

0.2.0两台Demo均更新安装并完成120%修音、+1半音、其他效果满强度的90秒测试：Android回调P99上界3.6ms/10ms预算、输出XRun0；iOS回调P99上界0.5ms/2.667ms预算、渲染及写盘错误0（未接耳机，监听关闭）。详情分别见 [Android性能](ANDROID_PERFORMANCE.md) 和 [iOS性能](IOS_PERFORMANCE.md)。


历史测试引用的 `outputs/` 属于本地证据目录，不随开源仓库发布；公开测量摘要见 [验证记录](VALIDATION.md)。
