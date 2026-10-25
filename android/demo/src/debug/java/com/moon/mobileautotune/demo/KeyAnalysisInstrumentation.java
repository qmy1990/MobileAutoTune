package com.moon.mobileautotune.demo;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.WindowManager;
import com.moon.mobileautotune.KeyAnalysisResult;
import com.moon.mobileautotune.KeyAnalyzer;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** Exercises published Java/JNI analysis, model assets, URI input and cancellation. */
public final class KeyAnalysisInstrumentation extends Instrumentation {
  private Bundle arguments;
  private Activity activity;

  @Override
  public void onCreate(Bundle args) {
    super.onCreate(args);
    arguments = args;
    start();
  }

  @Override
  public void onStart() {
    Bundle report = new Bundle();
    try {
      getTargetContext()
          .startActivity(
              new Intent(getTargetContext(), MainActivity.class)
                  .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
      long deadline = System.nanoTime() + 10_000_000_000L;
      while (MainActivity.foreground == null && System.nanoTime() < deadline) {
        Thread.sleep(100);
      }
      activity = MainActivity.foreground;
      check(activity != null, "Demo must be unlocked and in foreground");
      runOnMainSync(
          () -> activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON));
      File models = KeyAnalyzer.bundledModels(getTargetContext());
      KeyAnalyzer.Options options = new KeyAnalyzer.Options();
      options.threads = Integer.parseInt(arguments.getString("threads", "0"));
      options.execution =
          "cpu".equals(arguments.getString("backend"))
              ? KeyAnalyzer.Execution.CPU
              : KeyAnalyzer.Execution.AUTO;
      File input = new File(getTargetContext().getFilesDir(), "key-test.wav");
      writeMelody(input);
      try (KeyAnalyzer analyzer = new KeyAnalyzer(models, options)) {
        float[] last = {-1};
        KeyAnalysisResult result =
            analyzer.analyze(
                input,
                value -> {
                  check(value >= last[0] && value <= 1, "Monotonic progress");
                  last[0] = value;
                });
        check(!result.candidates.isEmpty(), "No key candidates");
        check(
            result.candidates.get(0).tonic == 0 && result.candidates.get(0).scale == 0,
            "C-major test melody");
        check(result.voicedSeconds > 2 && result.noteCount > 6, "Usable vocal melody");
        try (FileOutputStream output =
            new FileOutputStream(
                new File(getTargetContext().getFilesDir(), "key-test-result.json"))) {
          output.write(result.toJson().getBytes(StandardCharsets.UTF_8));
        }
        boolean canceled = false;
        try {
          analyzer.analyze(
              input,
              value -> {
                if (value >= .16f) {
                  analyzer.cancel();
                }
              });
        } catch (java.util.concurrent.CancellationException expected) {
          canceled = true;
        }
        check(canceled, "Cancellation");
        Thread[] canceler = {null};
        canceled = false;
        try {
          analyzer.analyze(
              input,
              value -> {
                if (value >= .32f && canceler[0] == null) {
                  canceler[0] =
                      new Thread(
                          () -> {
                            try {
                              Thread.sleep(150);
                            } catch (InterruptedException interrupted) {
                              Thread.currentThread().interrupt();
                            }
                            analyzer.cancel();
                          });
                  canceler[0].start();
                }
              });
        } catch (java.util.concurrent.CancellationException expected) {
          canceled = true;
        } finally {
          if (canceler[0] != null) canceler[0].join();
        }
        check(canceled, "Cancellation during model inference");
        KeyAnalysisResult imported =
            analyzer.analyze(getTargetContext(), Uri.fromFile(input), null);
        check(
            imported.candidates.get(0).tonic == result.candidates.get(0).tonic,
            "URI/native parity");
        report.putString("backend", result.swiftBackend + "/" + result.rmvpeBackend);
        report.putDouble("seconds", result.elapsedSeconds);
        report.putDouble("warmSeconds", imported.elapsedSeconds);
        report.putLong("residentPssKb", android.os.Debug.getPss());
        if (arguments.containsKey("file")) {
          File selected = new File(arguments.getString("file"));
          KeyAnalysisResult fileResult =
              analyzer.analyze(getTargetContext(), Uri.fromFile(selected), null);
          try (FileOutputStream output =
              new FileOutputStream(
                  new File(getTargetContext().getFilesDir(), "key-file-result.json"))) {
            output.write(fileResult.toJson().getBytes(StandardCharsets.UTF_8));
          }
          report.putDouble("fileSeconds", fileResult.elapsedSeconds);
        }
        report.putString(
            "result",
            "PASS: neural models, major key, document URI, progress, cancellation and reuse");
      }
      if (activity != null) {
        runOnMainSync(
            () -> activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON));
      }
      finish(0, report);
    } catch (Throwable failure) {
      if (activity != null) {
        runOnMainSync(
            () -> activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON));
      }
      report.putString("failure", failure.toString());
      finish(1, report);
    }
  }

  private static void check(boolean condition, String description) {
    if (!condition) {
      throw new AssertionError(description);
    }
  }

  private static void write16(DataOutputStream output, int value) throws IOException {
    output.write(value & 255);
    output.write((value >> 8) & 255);
  }

  private static void write32(DataOutputStream output, int value) throws IOException {
    write16(output, value);
    write16(output, value >> 16);
  }

  private static void writeMelody(File target) throws IOException {
    int[] melody = {60, 64, 67, 65, 64, 62, 60, 67, 69, 67, 65, 64, 62, 59, 60};
    int noteFrames = 6400;
    int frames = melody.length * noteFrames;
    try (DataOutputStream output = new DataOutputStream(new FileOutputStream(target))) {
      output.writeBytes("RIFF");
      write32(output, frames * 2 + 36);
      output.writeBytes("WAVEfmt ");
      write32(output, 16);
      write16(output, 1);
      write16(output, 1);
      write32(output, 16000);
      write32(output, 32000);
      write16(output, 2);
      write16(output, 16);
      output.writeBytes("data");
      write32(output, frames * 2);
      for (int midi : melody) {
        double hz = 440 * Math.pow(2, (midi - 69) / 12.0);
        for (int i = 0; i < noteFrames; i++) {
          double phase = 2 * Math.PI * hz * i / 16000;
          double envelope = Math.min(1, i / 320.0) * Math.min(1, (noteFrames - i) / 320.0);
          double sample =
              .13
                  * envelope
                  * (Math.sin(phase) + .5 * Math.sin(2 * phase) + .25 * Math.sin(3 * phase));
          write16(output, (int) Math.round(sample * 32767));
        }
      }
    }
  }
}
