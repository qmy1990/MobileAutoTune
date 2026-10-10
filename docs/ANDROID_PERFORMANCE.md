# Android 性能验证

实时验证运行完整麦克风输入、四项满强度效果、双 WAV 写盘与参数遥测，使用 LOW_LATENCY 和 AUTO 录音预设。监听验证使用有线或 USB 耳机。

```sh
ANDROID_SERIAL=<device> ./scripts/profile_android.sh 90 false 0 true 1 5
```

测试工具默认关闭扬声器监听，可使用 instrumentation 的 `monitor=true` 参数在耳机路由下测试监听。需要同时查看回调P99、超周期次数、输出欠载、输入积压、追平帧数、录音丢帧和磁盘错误。短测只反映对应设备与时段，发布前应在目标设备上执行更长的热态测试。

当前实测结果见 [验证说明](VALIDATION.md)。回调耗时不等于声学往返延迟。
