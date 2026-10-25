package com.moon.mobileautotune.demo;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import com.moon.mobileautotune.KeyAnalysisResult;
import com.moon.mobileautotune.KeyAnalyzer;
import com.moon.mobileautotune.MobileAudio;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
  static volatile MainActivity foreground;
  final MobileAudio.Parameters parameters = new MobileAudio.Parameters();
  MobileAudio audio;
  volatile boolean live = false;
  boolean busy = false;
  File raw;
  File wet;
  File offline;
  TextView status;
  TextView meter;
  Button start;
  Button process;
  Button importButton;
  CheckBox monitor;
  CheckBox compactProcessing;
  Spinner capturePreset;
  Spinner tonicSelector;
  Spinner scaleSelector;
  CheckBox analyzeAfterRecording;
  Button cancelAnalysis;
  volatile KeyAnalyzer keyAnalyzer;
  volatile boolean destroyed;
  final Handler ui = new Handler(Looper.getMainLooper());
  final ExecutorService worker = Executors.newSingleThreadExecutor();
  MediaPlayer player;
  final Runnable poll =
      new Runnable() {
        public void run() {
          if (audio != null && live) {
            float[] t = showMeter();
            if (t[7] > 0) {
              stopRecording();
              status.setText("音频设备断开，已停止。重新连接后可再次开始。");
            }
          }
          ui.postDelayed(this, 300);
        }
      };

  float[] showMeter() {
    float[] t = audio.telemetry();
    meter.setText(
        String.format(
            Locale.US,
            "F0 %.1f → %.1f Hz | %.2f 半音\n"
                + "近期回调 %.2f ms / P99 ≤%.2f ms / 预算 %.1f ms\n"
                + "输入积压 %.1f ms / 输出欠载 %.0f / 追平 %.0f 帧\n"
                + "输入缺帧 %.0f / 录音丢帧 %.0f / 写盘错误 %.0f\n"
                + "DSP %.1f ms · 输出通路估计 %.1f ms（非实测总延迟）\n"
                + "录音通路 %s · MMAP %s · 调度提示 %s",
            t[0],
            t[1],
            t[2],
            t[9],
            t[10],
            t[33] > 0 ? t[33] : t[19],
            t[12],
            t[11],
            t[14],
            t[5],
            t[6],
            t[8],
            t[32] > 0 ? t[32] : audio.latencySamples() / 48.f,
            t[17],
            t[26] == 10 ? "实时演唱" : t[26] == 9 ? "原始录音" : "语音识别",
            t[21] > 0 ? "是" : "否",
            t[28] > 0 ? "已请求" : "未请求"));
    return t;
  }

  @Override
  public void onCreate(Bundle b) {
    super.onCreate(b);
    audio = new MobileAudio(MobileAudio.REALTIME);
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setPadding(32, 32, 32, 32);
    ScrollView scroll = new ScrollView(this);
    scroll.addView(root);
    setContentView(scroll);
    TextView title = new TextView(this);
    title.setText("MobileAutoTune · 人声工作台");
    title.setTextSize(24);
    root.addView(title);
    TextView hint = new TextView(this);
    hint.setText("48 kHz 单声道人声。默认降E大调。监听需有线/USB耳机；蓝牙不用于50ms验收。");
    root.addView(hint);
    Spinner tonic = new Spinner(this);
    tonicSelector = tonic;
    tonic.setAdapter(
        new ArrayAdapter<>(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            new String[] {"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"}));
    tonic.setSelection(3);
    root.addView(tonic);
    tonic.setOnItemSelectedListener(
        new android.widget.AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(android.widget.AdapterView<?> a) {}

          public void onItemSelected(
              android.widget.AdapterView<?> a, View v, int position, long id) {
            parameters.tonic = position;
            update();
          }
        });
    Spinner scale = new Spinner(this);
    scaleSelector = scale;
    scale.setAdapter(
        new ArrayAdapter<>(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            new String[] {"大调", "自然小调", "半音阶"}));
    root.addView(scale);
    scale.setOnItemSelectedListener(
        new android.widget.AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(android.widget.AdapterView<?> a) {}

          public void onItemSelected(
              android.widget.AdapterView<?> a, View v, int position, long id) {
            parameters.scale = position;
            update();
          }
        });
    slider(root, "修音（高强度电音）", 625, 1000, v -> parameters.tuning = v);
    pitchSlider(root);
    slider(root, "混响", 18, v -> parameters.reverb = v);
    slider(root, "回声", 8, v -> parameters.echo = v);
    slider(root, "降噪（唱歌建议适中）", 35, v -> parameters.denoise = v);
    capturePreset = new Spinner(this);
    capturePreset.setAdapter(
        new ArrayAdapter<>(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            new String[] {"自动选择实时演唱通路", "原始录音通路（保留原音色）", "语音识别通路", "实时演唱通路（Android 10+）"}));
    root.addView(capturePreset);
    compactProcessing = new CheckBox(this);
    compactProcessing.setText("低延迟处理（减少监听缓存）");
    compactProcessing.setChecked(true);
    root.addView(compactProcessing);
    monitor = new CheckBox(this);
    monitor.setText("开启实时耳机监听");
    root.addView(monitor);
    analyzeAfterRecording = new CheckBox(this);
    analyzeAfterRecording.setText("录音结束后分析原始人声调式");
    analyzeAfterRecording.setChecked(true);
    root.addView(analyzeAfterRecording);
    start =
        button(
            root,
            "开始录音",
            () -> {
              if (live) {
                stopRecording();
              } else if (checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                  != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[] {Manifest.permission.RECORD_AUDIO}, 1);
              } else {
                begin();
              }
            });
    importButton =
        button(
            root,
            "导入48kHz WAV",
            () -> {
              if (live || busy) {
                return;
              }
              Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
              i.setType("audio/*");
              i.addCategory(Intent.CATEGORY_OPENABLE);
              startActivityForResult(i, 2);
            });
    process = button(root, "高质量后处理", () -> runOffline());
    button(root, "分析当前原音调式", () -> analyzeKey(null));
    button(
        root,
        "导入音频并分析调式",
        () -> {
          if (live || busy) {
            return;
          }
          Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
          intent.setType("audio/*");
          intent.addCategory(Intent.CATEGORY_OPENABLE);
          startActivityForResult(intent, 3);
        });
    cancelAnalysis =
        button(
            root,
            "取消调式分析",
            () -> {
              KeyAnalyzer analyzer = keyAnalyzer;
              if (analyzer != null) {
                analyzer.cancel();
              }
            });
    cancelAnalysis.setEnabled(false);
    button(root, "试听原音", () -> play(raw));
    button(root, "试听实时效果录音", () -> play(wet));
    button(root, "试听后处理效果", () -> play(offline));
    meter = new TextView(this);
    root.addView(meter);
    status = new TextView(this);
    root.addView(status);
    status.setText("停止录音后可后处理；文件保存在应用外部私有目录。导入文件会混合为单声道。");
    update();
    ui.post(poll);
  }

  interface Setter {
    void set(float value);
  }

  void slider(LinearLayout root, String name, int value, Setter setter) {
    slider(root, name, value * 10, 1000, setter);
  }

  void slider(LinearLayout root, String name, int value, int maximum, Setter setter) {
    TextView text = new TextView(this);
    text.setText(String.format(Locale.US, "%s %.1f%%", name, value / 10.f));
    root.addView(text);
    SeekBar s = new SeekBar(this);
    s.setMax(maximum);
    s.setProgress(value);
    root.addView(s);
    s.setOnSeekBarChangeListener(
        new SeekBar.OnSeekBarChangeListener() {
          public void onStartTrackingTouch(SeekBar s) {}

          public void onStopTrackingTouch(SeekBar s) {}

          public void onProgressChanged(SeekBar s, int v, boolean user) {
            text.setText(String.format(Locale.US, "%s %.1f%%", name, v / 10.f));
            setter.set(v / 1000.f);
            update();
          }
        });
  }

  Button button(LinearLayout root, String label, Runnable action) {
    Button b = new Button(this);
    b.setText(label);
    b.setOnClickListener(v -> action.run());
    root.addView(b);
    return b;
  }

  void pitchSlider(LinearLayout root) {
    TextView text = new TextView(this);
    text.setText("音高（低沉 ↔ 高尖） 0.0 半音");
    root.addView(text);
    SeekBar s = new SeekBar(this);
    s.setMax(48);
    s.setProgress(24);
    s.setContentDescription("音高，负值低沉，正值高尖");
    root.addView(s);
    s.setOnSeekBarChangeListener(
        new SeekBar.OnSeekBarChangeListener() {
          public void onStartTrackingTouch(SeekBar s) {}

          public void onStopTrackingTouch(SeekBar s) {}

          public void onProgressChanged(SeekBar s, int v, boolean user) {
            parameters.pitchShiftSemitones = (v - 24) * .5f;
            text.setText(
                String.format(Locale.US, "音高（低沉 ↔ 高尖） %+.1f 半音", parameters.pitchShiftSemitones));
            update();
          }
        });
    button(root, "恢复原音高", () -> s.setProgress(24));
  }

  void update() {
    if (audio != null) {
      audio.setParameters(parameters);
    }
  }

  MobileAudio.Parameters snapshot() {
    MobileAudio.Parameters p = new MobileAudio.Parameters();
    p.tonic = parameters.tonic;
    p.scale = parameters.scale;
    p.tuning = parameters.tuning;
    p.reverb = parameters.reverb;
    p.echo = parameters.echo;
    p.denoise = parameters.denoise;
    p.pitchShiftSemitones = parameters.pitchShiftSemitones;
    p.echoMs = parameters.echoMs;
    p.concertAHz = parameters.concertAHz;
    return p;
  }

  void begin() {
    if (busy) {
      return;
    }
    stopPlayer();
    File dir = getExternalFilesDir(null);
    if (dir == null) {
      dir = getFilesDir();
    }
    String prefix = "take-" + System.currentTimeMillis();
    raw = new File(dir, prefix + "-raw.wav");
    wet = new File(dir, prefix + "-live.wav");
    offline = null;
    try {
      audio.startLive(
          raw.getAbsolutePath(),
          wet.getAbsolutePath(),
          monitor.isChecked(),
          MobileAudio.CapturePreset.values()[capturePreset.getSelectedItemPosition()],
          compactProcessing.isChecked()
              ? MobileAudio.LiveBuffering.LOW_LATENCY
              : MobileAudio.LiveBuffering.COMPATIBLE);
      live = true;
      getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
      compactProcessing.setEnabled(false);
      capturePreset.setEnabled(false);
      start.setText("停止录音");
      status.setText("正在录音：" + raw);
      monitor.setEnabled(false);
    } catch (Exception e) {
      status.setText(e.getMessage());
    }
  }

  void stopRecording() {
    stopRecording(true);
  }

  void stopRecording(boolean analyze) {
    audio.stopLive();
    live = false;
    getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    showMeter();
    start.setText("开始录音");
    monitor.setEnabled(true);
    capturePreset.setEnabled(true);
    compactProcessing.setEnabled(true);
    status.setText("已保存原音及实时效果。可进行后处理：" + raw);
    if (analyze && !destroyed && analyzeAfterRecording.isChecked()) {
      analyzeKey(null);
    }
  }

  void analyzeKey(Uri imported) {
    if (busy || live || (raw == null && imported == null)) {
      status.setText("请先完成录音，或导入音频进行调式分析。");
      return;
    }
    stopPlayer();
    File source = raw;
    busy = true;
    getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    process.setEnabled(false);
    start.setEnabled(false);
    status.setText("准备人声调式分析…");
    worker.execute(
        () -> {
          try {
            if (keyAnalyzer == null) {
              keyAnalyzer =
                  new KeyAnalyzer(KeyAnalyzer.bundledModels(this), new KeyAnalyzer.Options());
            }
            if (destroyed) {
              return;
            }
            ui.post(() -> cancelAnalysis.setEnabled(true));
            int[] last = {-1};
            KeyAnalyzer.ProgressListener progress =
                value -> {
                  int percent = Math.round(value * 100);
                  if (percent != last[0]) {
                    last[0] = percent;
                    ui.post(
                        () -> {
                          if (!destroyed) {
                            status.setText("正在分析原始人声调式 " + percent + "%");
                          }
                        });
                  }
                };
            KeyAnalysisResult result =
                imported == null
                    ? keyAnalyzer.analyze(source, progress)
                    : keyAnalyzer.analyze(this, imported, progress);
            ui.post(
                () -> {
                  if (!destroyed) {
                    showKeyResult(result);
                  }
                });
          } catch (Exception error) {
            ui.post(
                () -> {
                  if (!destroyed) {
                    status.setText("调式分析：" + error.getMessage());
                  }
                });
          } finally {
            ui.post(
                () -> {
                  if (!destroyed) {
                    getWindow()
                        .clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                    finishWork();
                    start.setEnabled(true);
                    cancelAnalysis.setEnabled(false);
                  }
                });
          }
        });
  }

  void showKeyResult(KeyAnalysisResult result) {
    if (result.status == KeyAnalysisResult.INSUFFICIENT_MELODY) {
      status.setText("旋律信息不足：请录制更完整的清唱乐句。单音、说话或静音无法可靠确定调式。");
      return;
    }
    String[] names = {"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"};
    int count = Math.min(3, result.candidates.size());
    String[] choices = new String[count];
    StringBuilder summary =
        new StringBuilder(
            result.status == KeyAnalysisResult.AMBIGUOUS ? "调式存在歧义，请试听并选择候选。\n" : "人声调式候选：\n");
    for (int i = 0; i < count; i++) {
      KeyAnalysisResult.Candidate candidate = result.candidates.get(i);
      choices[i] = names[candidate.tonic] + (candidate.scale == 0 ? " 大调" : " 小调");
      summary.append(
          String.format(Locale.US, "%s · 相对支持 %.1f%%\n", choices[i], candidate.support * 100));
    }
    if (result.possibleModulation) {
      summary.append("片段之间可能转调。\n");
    }
    summary.append(
        String.format(
            Locale.US, "分析耗时 %.2fs，有效人声 %.1fs\n", result.elapsedSeconds, result.voicedSeconds));
    summary.append("候选支持度不代表准确率；选择后才会用于修音。");
    status.setText(summary);
    new AlertDialog.Builder(this)
        .setTitle("选择修音调式")
        .setItems(
            choices,
            (dialog, index) -> {
              KeyAnalysisResult.Candidate candidate = result.candidates.get(index);
              tonicSelector.setSelection(candidate.tonic);
              scaleSelector.setSelection(candidate.scale);
              parameters.tonic = candidate.tonic;
              parameters.scale = candidate.scale;
              update();
              status.setText("修音调式：" + choices[index]);
            })
        .setNegativeButton("保留当前调式", null)
        .show();
  }

  void runOffline() {
    if (live || busy || raw == null) {
      status.setText("先停止录音或导入WAV。");
      return;
    }
    stopPlayer();
    busy = true;
    process.setEnabled(false);
    File source = raw;
    File target = new File(raw.getParentFile(), "offline-" + System.currentTimeMillis() + ".wav");
    MobileAudio.Parameters p = snapshot();
    long before = System.nanoTime();
    status.setText("正在后处理…");
    worker.execute(
        () -> {
          try {
            MobileAudio.processWav(
                source.getAbsolutePath(), target.getAbsolutePath(), p, MobileAudio.OFFLINE, true);
            double seconds = (System.nanoTime() - before) / 1e9;
            ui.post(
                () -> {
                  offline = target;
                  status.setText(
                      String.format(Locale.US, "后处理完成，耗时 %.3fs（含4秒效果尾音）\n%s", seconds, target));
                  finishWork();
                });
          } catch (Exception e) {
            ui.post(
                () -> {
                  status.setText(e.getMessage());
                  finishWork();
                });
          }
        });
  }

  void finishWork() {
    busy = false;
    process.setEnabled(true);
  }

  void stopPlayer() {
    if (player != null) {
      player.release();
      player = null;
    }
  }

  void play(File file) {
    if (live || busy || file == null || !file.exists()) {
      status.setText("对应音频尚未生成，请先停止录音并处理。");
      return;
    }
    stopPlayer();
    try {
      player = new MediaPlayer();
      player.setDataSource(file.getAbsolutePath());
      player.prepare();
      player.start();
      status.setText("试听：" + file.getName());
    } catch (Exception e) {
      status.setText(e.getMessage());
      stopPlayer();
    }
  }

  @Override
  public void onRequestPermissionsResult(int r, String[] p, int[] g) {
    super.onRequestPermissionsResult(r, p, g);
    if (r == 1 && g.length > 0 && g[0] == PackageManager.PERMISSION_GRANTED) {
      begin();
    } else {
      status.setText("需要麦克风权限才能录音。");
    }
  }

  @Override
  public void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (result != RESULT_OK || data == null) {
      return;
    }
    Uri uri = data.getData();
    if (uri == null) {
      return;
    }
    if (request == 3) {
      analyzeKey(uri);
      return;
    }
    if (request != 2) {
      return;
    }
    busy = true;
    status.setText("导入中…");
    worker.execute(
        () -> {
          try {
            File target = new File(getFilesDir(), "import-" + System.currentTimeMillis() + ".wav");
            try (InputStream in = getContentResolver().openInputStream(uri);
                OutputStream out = new FileOutputStream(target)) {
              if (in == null) {
                throw new IOException("不能读取文件");
              }
              byte[] buffer = new byte[65536];
              int n;
              while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
              }
            }
            ui.post(
                () -> {
                  raw = target;
                  wet = offline = null;
                  status.setText("已导入：" + target);
                  busy = false;
                });
          } catch (Exception e) {
            ui.post(
                () -> {
                  status.setText(e.getMessage());
                  busy = false;
                });
          }
        });
  }

  @Override
  public void onResume() {
    super.onResume();
    foreground = this;
  }

  @Override
  public void onPause() {
    if (foreground == this) {
      foreground = null;
    }
    super.onPause();
    if (live) {
      stopRecording(false);
    }
    stopPlayer();
  }

  @Override
  public void onDestroy() {
    destroyed = true;
    if (keyAnalyzer != null) {
      keyAnalyzer.cancel();
    }
    ui.removeCallbacksAndMessages(null);
    worker.execute(
        () -> {
          if (keyAnalyzer != null) {
            keyAnalyzer.close();
          }
        });
    worker.shutdown();
    if (audio != null) {
      audio.close();
      audio = null;
    }
    super.onDestroy();
  }
}
