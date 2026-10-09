# 验证记录 / Validation

## v0.3.0 发布检查（2026-10-10）

- 包名迁移为 `com.moon.mobileautotune`，JNI符号、Demo标识、混淆规则与Java路径同步更新。
- 核心及WAV测试通过：两种模式移调、变声后按原调式修音、120%不越过目标、NaN/Inf与越界拒绝、并发调参、原地处理、零回调heap分配、延迟对齐、WAV尾帧与尾音、原文件保护。
- `verify_vendor.py`核对1336个锁定依赖文件。
- 用户提供的hook3（降E大调）在两种模式、修音100%/变声0时，与0.2.0发布前对照输出逐字节一致；用户录音和试听文件不公开。
- v0.3.0新包名Demo已安装到Android16真机，Java/JNI桥接测试通过：实时/后处理、移调后调式映射、120%参数及WAV处理。
- iOS发布XCFramework完成独立Swift import/静态链接检查。
- Android构建四种ABI；iOS构建真机arm64和模拟器arm64/x86_64。各ABI编译通过不代表均已覆盖实机运行。
- GitHub Actions执行独立源码构建、核心测试、Maven本地发布与制品打包；实际执行结果以仓库Actions记录为准。

## 已有真机测量（0.2.0，非v0.3.0长录验收）

修音120%、移调+1半音、混响/回声/降噪均满强度，双WAV写盘：

| 设备 | 时长 | 回调P99上界 | 结果 |
|---|---:|---:|---|
| 骁龙8Gen2 / Android16 | 90秒 | 3.6ms / 10ms预算 | 输出XRun0；输入缺帧回调1次；写盘丢样本/错误0 |
| iPhone8Plus / iOS16.7.10 | 90秒 | 0.5ms / 2.667ms预算 | 渲染/时间戳间断/写盘错误0；未接耳机，监听关闭 |

iPhone同参数处理13.125秒hook3，耗时2.250秒（含初始化和WAV读写，不附加尾音），RTF约0.171。原0.1.1十分钟数据单独记录，不能作为新版长录验收。详见 [Android](ANDROID_PERFORMANCE.md)、[iOS](IOS_PERFORMANCE.md)。历史日志路径供本地复查；含设备日志与用户声音的原始证据不随开源仓库发布。

## 测量边界

- 帧入口名义算法延迟20ms，流入口30ms，最大历史年龄30/40ms；设备延迟另计，整体声学往返≤50ms未验证。
- 最低系统运行兼容性仍需Android8.0/iOS14实机验证；最低部署配置已覆盖构建。
- 不将白噪声衰减、单音修音精度等同于唱歌MOS/PESQ或最佳音质。没有NPU/GPU后端。
- 多机型、热态十分钟录音、耳机回环、权限拒绝、中断、路由变化以及不同长度后处理仍需分别验收。

## 复现

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 scripts/verify_vendor.py
# 已授权麦克风、Demo在前台的Android调试设备：
adb shell am instrument -w com.moon.mobileautotune.demo/.SDKSmokeInstrumentation
./scripts/profile_android.sh 90 false 0 true 1.2 1
```

本地用户音频验证：将自己有权使用的48kHzPCM16 WAV放入忽略的`audios/`目录，再运行`python3 scripts/validate_audio.py`；脚本默认按降E大调处理。
