# 2026-10-09 本地验收结果

主机：Intel Core i5-7360U 2.30GHz，8GB RAM，Release O3。

- C++ core / WAV自动测试：通过。
- AddressSanitizer + UndefinedBehaviorSanitizer：通过。
- 第三方1336个文件校验：通过。
- Android API26配置，arm64-v8a/x86_64 AAR及APK：构建通过。
- Android API34 x86_64模拟器JNI测试：通过（outputs/android-smoke.txt）。Demo已启动显示，但模拟器System UI出现ANR，因此尚未完成完整界面交互与录音测试。
- iOS最低14.0，真机arm64和模拟器arm64/x86_64：构建通过。
- XCFramework独立Swift import与静态链接：通过。
- iOS模拟器界面验证：未完成，CoreSimulator卡在首次LaunchServices数据迁移；未连接真机。

| 模式 | 输出数量 | 文件耗时RTF最小 | 文件耗时RTF最大 |
|---|---:|---:|---:|
| realtime | 24 | 0.044 | 0.091 |
| offline | 24 | 0.156 | 0.531 |

全部48个输出已验证时长与输入一致，未追加尾音。后处理全部RTF<2；这仅是本机数据。

| 模式 | 平均计算ms/10ms帧 | p99计算ms | 最大计算ms | 超过10ms的帧数 / 3000 |
|---|---:|---:|---:|---:|
| realtime | 0.381 | 1.415 | 10.558 | 1 |
| offline | 1.863 | 6.789 | 28.658 | 8 |

普通桌面线程存在调度尖峰，实时测试有1帧略超过10ms。不能将平均/p99或30ms名义算法延迟当成50ms手机声学上限的证明。手机端≤50ms及≤2×后处理预算均需按docs/VALIDATION.md在真机测量。

试听：outputs/listen.html，包含全部原音/自然/电音/降噪/空间效果。加入额外噪声的hook3试听与固定种子噪声验证结果也在outputs/。
