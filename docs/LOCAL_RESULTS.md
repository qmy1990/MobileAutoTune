# 本地音频验证

桌面工具 `mat_process` 接受48kHz单声道或双声道 PCM16/float32 WAV。测试应包含人声、纯音、静音、非有限输入、参数变化和输入输出文件校验。音高验证使用变声后的音高选择调式目标。

```sh
./build/mat_process input.wav output.wav --mode offline --tonic 3 --scale major --tuning 1 --pitch-shift 5
```

根据实际歌声试听修音、混响、回声、降噪和极端移调。用户录音与本地生成音频不随源码发布。当前测试范围见 [验证说明](VALIDATION.md)。
