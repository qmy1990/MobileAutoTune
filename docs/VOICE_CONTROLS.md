# 变声与自动修音

音高参数 `pitch_shift_semitones` / `pitchShiftSemitones` 为 `-12..12`，负值降低声音，正值升高声音，`0` 保持原音高。修音强度 `tuning` 为 `0..1`，`0` 关闭修音，`1` 为最大强度，快速贴近目标音并产生机械感。默认修音强度为 `0.625`。

处理顺序为：检测人声音高，叠加平滑的变声偏移，选择调式内最近的目标音，合并变声与修音偏移，执行一次变调。目标始终属于选定的调式；变声参数不会改变所选主音和调式。例如，225Hz 人声升高 1 半音后，在 C 大调中以 B3（约246.94Hz）为目标。

移调控制约20ms平滑；修音强度同时控制音高修正幅度、贴音速度和音符边界的机械感。音高修正不越过目标音。无稳定基频的辅音由置信度控制，独立变声在修音关闭时仍然生效。处理只使用一个音高检测器和一个变调器，参数变化不增加处理遍数或缓冲长度。

```java
MobileAudio.Parameters parameters = new MobileAudio.Parameters();
parameters.tonic = 0;
parameters.scale = MobileAudio.MAJOR;
parameters.pitchShiftSemitones = -5;
parameters.tuning = 1.0f;
sdk.setParameters(parameters);
```

实时模式优先低延迟；后处理使用较长频谱窗口和共振峰处理。极端移调、低音、气声和多声部素材的音质需要实际试听。参数强度不代表音频整体延迟；延迟说明见 [架构](ARCHITECTURE.md)。
