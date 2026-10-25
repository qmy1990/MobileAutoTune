# 人声调式分析

[English](KEY_ANALYSIS.en.md)

离线分析单个人声的 **12 个主音 × 大调／小调**，可在录音结束后分析原音，也可导入已有音频。分析在设备本机执行，没有音频上传或服务器依赖。它用于提出修音候选；Demo 只有在用户选择候选后才更新修音参数。

## 分析方法

共享 C++ 流程：高质量重采样到 16kHz → SwiftF0 与 RMVPE FP32 音高轨迹 → 按置信度及音高一致性融合 → 全局调律偏移估计、短时中值平滑、音符分段 → 音级分布、调内覆盖、乐句落音与终止进行联合评分。模型一致率按折叠八度后的音级计算，不等同于频率完全相同。结果包含 24 个排序候选、相对支持度、音级覆盖率、模型一致率、有效人声时长、局部分段与可能转调提示。

`support` 是候选间归一化的排序权重，**不是准确率或校准后的概率**。`estimated` 也不是正确性保证；`ambiguous` 表示证据不足以稳定区分候选，`insufficientMelody` 表示旋律信息不足。当前推断器是可解释的统计评分，没有宣称在干声调式基准上达到最高准确率。

清唱短句、五声音阶、关系大小调、跑调、说话以及转调都可能产生歧义。仅凭旋律不能总能还原整首歌的和声调性。建议使用完整、多音级的清唱乐段，并保留候选选择。分析原始人声，不分析已经按目标调式修正的效果音频。这里的“小调”用于小调调性候选；修音端现有小调音阶为自然小调。

## 模型与部署

```bash
python3 scripts/prepare_key_analysis.py --platform models
```

准备好的文件在 `models/key/`。下载地址、固定版本、许可证和 SHA256 在 [模型清单](../models/key/manifest.json)。运行时校验固定模型内容，不接受未经验证的替换模型。SwiftF0 约135KB，RMVPE 约362MB（十进制）；FP32 模型不经过有损量化。

- Android AAR 包含推理运行时，**不包含模型权重**。将 `models/key/` 作为 `assets/key/` 打包，或由宿主下载、验证后传入本地目录。`bundledModels()` 在后台线程把资源复制到应用私有目录。Demo 内置权重，因此安装包较大；首次使用需要复制模型的时间和额外磁盘空间。
- iOS XCFramework 包含推理运行时。把 `models/key/` 作为名为 `KeyModels.bundle` 的文件夹资源加入宿主，或传入自己的模型目录。Demo 已包含该资源。
- 需要为模型、运行时、激活张量和解码临时文件预留存储及内存。长音频采用分块推理，音高轨迹随时长增长；音频 PCM 不会整段载入内存。低内存设备可能无法装载完整模型，宿主应处理错误并保留录音。
- 推理库和模型各自的完整许可证随 SDK／模型包分发，商用时继续保留，见 [第三方声明](../THIRD_PARTY_NOTICES.md)。

## Android

在工作线程构建、调用和关闭分析器。停止录音并关闭 WAV 写入器后，再分析原音。

```java
import com.moon.mobileautotune.KeyAnalyzer;
import com.moon.mobileautotune.KeyAnalysisResult;

KeyAnalyzer.Options options = new KeyAnalyzer.Options();
options.temporaryDirectory = context.getCacheDir();
try (KeyAnalyzer analyzer = new KeyAnalyzer(KeyAnalyzer.bundledModels(context), options)) {
    KeyAnalysisResult result = analyzer.analyze(rawWavFile, progress -> {
        // 当前是工作线程；通过主线程更新进度。
    });
    // 或：analyzer.analyze(context, selectedDocumentUri, progressListener);
    // 检查 result.status 和 result.candidates，由用户选择修音目标。
}
```

系统文件选择器返回的 `content://` URI 可直接传给第二种入口。WAV 由共享解码器读取，压缩音频由 Android MediaExtractor / MediaCodec 解码后分析；具体支持格式取决于系统解码器。只读外部文件无需写入权限；临时文件使用应用缓存目录。`cancel()` 可从其他线程调用；等待分析结束后再 `close()`。取消抛出 `CancellationException`，文件或模型失败抛出 `IOException`。

## iOS

```swift
import MobileAudioSDK

// 工作队列内执行；也可传入自己的模型目录。
let models = MATKeyAnalyzer.bundledModelDirectory()!
let analyzer = try MATKeyAnalyzer(modelDirectory: models, options: MATKeyAnalysisOptions())
let result = try analyzer.analyze(audio: rawURL, progress: { fraction in
    // 返回 false 可取消；不要在音频回调或主线程执行分析。
    return true
})
// result.candidates / result.status / result.json
```

文件导入使用系统文档选择器；SDK 通过 AVAudioFile 解码系统支持的格式，管理安全作用域访问及临时文件。`cancel()` 支持其他线程调用，取消错误码为 `-5`。录音时需要麦克风权限；分析已有文件无需麦克风权限。链接 Foundation、AVFoundation、AudioToolbox、CoreML、Accelerate 与 libc++。

## C/C++ 与桌面验证

```bash
python3 scripts/prepare_key_analysis.py --platform desktop
cmake -S . -B build-key -DMAT_BUILD_KEY_ANALYSIS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-key -j4
./build-key/mat_key_analyze models/key input.wav --cpu --threads 4
ctest --test-dir build-key --output-on-failure
```

桌面自动准备支持 macOS；其他桌面平台可自行安装 ONNX Runtime1.20及以上版本 并设置 `MAT_ORT_LIBRARY`。CMake 的 `mobileautotune_key` 目标提供完整模型分析，公共接口在 [key_analysis.h](../include/mat/key_analysis.h)。仅需对已有音高轨迹做调式判断时，链接核心 `mobileautotune`，调用 `mat_key_analyze_tracks()`，不依赖推理运行时。

原生 WAV 支持 PCM16/24/32 和 float32，8..192kHz、1..8声道，混合为单声道人声。PCM 文件入口接受16kHz单声道 little-endian float32。单文件上限24小时；结果最多保留256个局部分段。分块 RMVPE 使用7.68秒主体和两侧各1.28秒上下文，分块双向网络与整段推理可能有边界差异。

## 平台性能

默认线程池按可用硬件选择并限制为最多6线程，关闭忙等，复用推理会话及内存。可通过 `threads` 指定线程数。准确性优先保留固定 FP32 权重，并复用重采样相位系数。

`AUTO`／`automatic` 与 `ACCELERATED`／`accelerated` 均采用安全的加速尝试与 CPU 回退：Android8使用 CPU／XNNPACK，Android8.1及以上额外尝试 NNAPI；Apple 使用多核 CPU；当前动态模型图采用经过真机验证的 CPU 路径，避免 CoreML 模型准备时间不可控。首次分析会增加构建和对比耗时。候选后端在同一片段与 CPU 对比：音高偏差不超过2音分、置信度偏差不超过0.005且发声状态一致，预热推理至少快10%才保留。后续执行失败会退回 CPU。可显式选择 `CPU` 跳过加速探测。

`swiftBackend`／`rmvpeBackend` 报告所选执行配置。`NNAPI+CPU (validated)` 表示该配置通过片段对比，**不表示所有算子都在 NPU/GPU 上执行，也不保证所有音频都零偏差**。实际分区、设备和系统可能使 CPU 更快；不会只因芯片品牌或系统较新就强制加速。首次选型、模型加载、导入解码都应计入用户体验；端到端耗时与长录音的音频效果处理耗时是独立指标。

回调在工作线程运行，进度单调为0..1。一个实例同时只能分析一个文件。不要在实时音频线程中执行模型加载或分析；Demo 在录音停止后分析，避免与实时监听竞争算力。
