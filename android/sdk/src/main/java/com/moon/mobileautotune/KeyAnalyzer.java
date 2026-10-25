package com.moon.mobileautotune;

import android.content.Context;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.concurrent.CancellationException;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONException;

/**
 * Local, full-precision SwiftF0/RMVPE key analysis. Construct and analyze on a worker thread.
 * Supply the verified model directory, or bundle models/key as assets and call bundledModels(). One
 * active analysis per instance. cancel() may be called on any thread; close after completion.
 */
public final class KeyAnalyzer implements AutoCloseable {
  static {
    System.loadLibrary("mobileaudio_jni");
  }

  public enum Execution {
    AUTO,
    CPU,
    ACCELERATED
  }

  public static final class Options {
    public int threads = 0;
    public Execution execution = Execution.AUTO;
    public boolean detectModulations = true;

    /** Writable directory for decoded audio. Defaults to the model directory. */
    public File temporaryDirectory;
  }

  public interface ProgressListener {
    /** Runs on the analysis thread. Value is monotonic and lies in 0..1. */
    void onProgress(float progress);
  }

  private long handle;
  private boolean analyzing;
  private final AtomicBoolean canceled = new AtomicBoolean();
  private ProgressListener listener;
  private final File temporaryDirectory;

  public KeyAnalyzer(File modelDirectory, Options options) {
    if (modelDirectory == null || options == null || options.execution == null) {
      throw new IllegalArgumentException("Missing models or options");
    }
    temporaryDirectory =
        options.temporaryDirectory != null ? options.temporaryDirectory : modelDirectory;
    handle =
        nCreate(
            modelDirectory.getAbsolutePath(),
            options.threads,
            options.execution.ordinal(),
            options.detectModulations);
    if (handle == 0) {
      throw new IllegalArgumentException("Cannot open verified FP32 key models");
    }
  }

  /** Copies bundled model assets to private storage, once per model hash. Worker-thread API. */
  public static File bundledModels(Context context) throws IOException {
    File directory = new File(context.getFilesDir(), "mat-key-models-6385e8c2ebc3-5370e71ac80a");
    if (!directory.isDirectory() && !directory.mkdirs()) {
      throw new IOException("Cannot create model directory");
    }
    synchronized (KeyAnalyzer.class) {
      String[] names = {"swift-f0.onnx", "rmvpe.onnx"};
      long[] lengths = {135090, 361688443};
      String[] hashes = {
        "6385e8c2ebc3872e82c9ff5946228de44cd3be77a750ea53698b7dbfe94b0a22",
        "5370e71ac80af8b4b7c793d27efd51fd8bf962de3a7ede0766dac0befa3660fd"
      };
      for (int index = 0; index < names.length; index++) {
        String name = names[index];
        File target = new File(directory, name);
        if (target.isFile() && target.length() == lengths[index]) {
          continue;
        }
        File temporary = File.createTempFile("model-", ".tmp", directory);
        try {
          try (InputStream input = context.getAssets().open("key/" + name);
              FileOutputStream output = new FileOutputStream(temporary)) {
            copy(input, output, null);
          }
          if (temporary.length() != lengths[index] || !sha256(temporary).equals(hashes[index])) {
            throw new IOException("Bundled key model checksum mismatch");
          }
          if (!temporary.renameTo(target)) {
            throw new IOException("Cannot install model asset");
          }
        } finally {
          temporary.delete();
        }
      }
    }
    return directory;
  }

  /** WAV inputs use the shared native decoder and high-quality resampler. */
  public KeyAnalysisResult analyze(File audio, ProgressListener progress) throws IOException {
    return analyzeInternal(null, null, audio, progress);
  }

  /** Imports a system document URI locally. No audio is uploaded to a server. */
  public KeyAnalysisResult analyze(Context context, Uri audio, ProgressListener progress)
      throws IOException {
    if (context == null || audio == null) {
      throw new IllegalArgumentException("Missing document URI");
    }
    return analyzeInternal(context.getApplicationContext(), audio, null, progress);
  }

  private KeyAnalysisResult analyzeInternal(
      Context context, Uri uri, File source, ProgressListener progress) throws IOException {
    synchronized (this) {
      if (handle == 0) {
        throw new IllegalStateException("Key analyzer closed");
      }
      if (analyzing) {
        throw new IllegalStateException("Analysis already running");
      }
      analyzing = true;
      canceled.set(false);
      listener = progress;
    }
    long analysisStart = System.nanoTime();
    File imported = null;
    File decoded = null;
    try {
      notifyProgress(0);
      if (uri != null) {
        imported = File.createTempFile("mat-key-import-", ".audio", context.getCacheDir());
        try (InputStream input = context.getContentResolver().openInputStream(uri);
            FileOutputStream output = new FileOutputStream(imported)) {
          if (input == null) {
            throw new IOException("Cannot open selected audio");
          }
          copy(input, output, canceled);
        }
        source = imported;
      }
      if (source == null || !source.isFile()) {
        throw new IOException("Audio file does not exist");
      }
      if (!isWav(source)) {
        decoded =
            File.createTempFile(
                "mat-key-decoded-",
                ".wav",
                context != null ? context.getCacheDir() : temporaryDirectory);
        decode(source, decoded);
        source = decoded;
      }
      notifyProgress(.15f);
      String json = nAnalyze(handle, source.getAbsolutePath(), this);
      checkCanceled();
      if (json == null) {
        throw new IOException("Key analysis returned no result");
      }
      try {
        org.json.JSONObject metadata = new org.json.JSONObject(json);
        metadata.put("elapsedSeconds", (System.nanoTime() - analysisStart) / 1e9);
        return new KeyAnalysisResult(metadata.toString());
      } catch (JSONException error) {
        throw new IOException("Invalid key analysis result", error);
      }
    } finally {
      if (decoded != null) {
        decoded.delete();
      }
      if (imported != null) {
        imported.delete();
      }
      synchronized (this) {
        analyzing = false;
        listener = null;
      }
    }
  }

  public synchronized void cancel() {
    canceled.set(true);
    if (handle != 0) {
      nCancel(handle);
    }
  }

  @Override
  public synchronized void close() {
    if (analyzing) {
      throw new IllegalStateException("Cancel and await analysis before closing");
    }
    if (handle != 0) {
      nDestroy(handle);
      handle = 0;
    }
  }

  // JNI calls this on the worker thread, outside the realtime audio engine.
  private boolean nativeProgress(float value) {
    if (canceled.get()) {
      return false;
    }
    notifyProgress(.15f + .85f * value);
    return !canceled.get();
  }

  private void checkCanceled() {
    if (canceled.get()) {
      throw new CancellationException("Key analysis canceled");
    }
  }

  private void notifyProgress(float value) {
    checkCanceled();
    if (listener != null) {
      listener.onProgress(value);
    }
  }

  private static void copy(InputStream input, FileOutputStream output, AtomicBoolean cancellation)
      throws IOException {
    byte[] buffer = new byte[131072];
    int count;
    while ((count = input.read(buffer)) != -1) {
      if (cancellation != null && cancellation.get()) {
        throw new CancellationException("Import canceled");
      }
      output.write(buffer, 0, count);
    }
  }

  private static boolean isWav(File file) throws IOException {
    try (FileInputStream input = new FileInputStream(file)) {
      byte[] header = new byte[12];
      return input.read(header) == 12
          && header[0] == 'R'
          && header[1] == 'I'
          && header[2] == 'F'
          && header[3] == 'F'
          && header[8] == 'W'
          && header[9] == 'A'
          && header[10] == 'V'
          && header[11] == 'E';
    }
  }

  private static String sha256(File file) throws IOException {
    try (FileInputStream input = new FileInputStream(file)) {
      MessageDigest digest = MessageDigest.getInstance("SHA-256");
      byte[] buffer = new byte[131072];
      int count;
      while ((count = input.read(buffer)) != -1) {
        digest.update(buffer, 0, count);
      }
      StringBuilder result = new StringBuilder(64);
      for (byte value : digest.digest()) {
        result.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
      }
      return result.toString();
    } catch (NoSuchAlgorithmException error) {
      throw new IOException("SHA-256 unavailable", error);
    }
  }

  private void decode(File input, File output) throws IOException {
    MediaExtractor extractor = new MediaExtractor();
    MediaCodec codec = null;
    boolean codecStarted = false;
    try (RandomAccessFile pcm = new RandomAccessFile(output, "rw")) {
      extractor.setDataSource(input.getAbsolutePath());
      MediaFormat format = null;
      for (int i = 0; i < extractor.getTrackCount(); i++) {
        MediaFormat track = extractor.getTrackFormat(i);
        String mime = track.getString(MediaFormat.KEY_MIME);
        if (mime != null && mime.startsWith("audio/")) {
          extractor.selectTrack(i);
          format = track;
          break;
        }
      }
      if (format == null) {
        throw new IOException("Selected file has no audio track");
      }
      codec = MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME));
      codec.configure(format, null, null, 0);
      codec.start();
      codecStarted = true;
      pcm.setLength(0);
      pcm.write(new byte[44]);
      int sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE);
      int channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
      int encoding = AudioFormat.ENCODING_PCM_16BIT;
      long duration =
          format.containsKey(MediaFormat.KEY_DURATION)
              ? format.getLong(MediaFormat.KEY_DURATION)
              : 0;
      boolean inputDone = false;
      boolean outputDone = false;
      int idle = 0;
      byte[] transfer = new byte[131072];
      MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
      while (!outputDone) {
        checkCanceled();
        boolean advanced = false;
        if (!inputDone) {
          int index = codec.dequeueInputBuffer(10000);
          if (index >= 0) {
            ByteBuffer buffer = codec.getInputBuffer(index);
            if (buffer == null) {
              throw new IOException("Decoder input buffer unavailable");
            }
            int size = extractor.readSampleData(buffer, 0);
            long time = size < 0 ? 0 : extractor.getSampleTime();
            codec.queueInputBuffer(
                index,
                0,
                Math.max(0, size),
                time,
                size < 0 ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0);
            inputDone = size < 0;
            if (!inputDone) {
              extractor.advance();
            }
            advanced = true;
          }
        }
        int index = codec.dequeueOutputBuffer(info, 10000);
        if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
          MediaFormat actual = codec.getOutputFormat();
          int nextRate = actual.getInteger(MediaFormat.KEY_SAMPLE_RATE);
          int nextChannels = actual.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
          int nextEncoding =
              actual.containsKey(MediaFormat.KEY_PCM_ENCODING)
                  ? actual.getInteger(MediaFormat.KEY_PCM_ENCODING)
                  : AudioFormat.ENCODING_PCM_16BIT;
          if (pcm.length() > 44
              && (nextRate != sampleRate || nextChannels != channels || nextEncoding != encoding)) {
            throw new IOException("Audio format changes within the file");
          }
          sampleRate = nextRate;
          channels = nextChannels;
          encoding = nextEncoding;
          if (encoding != AudioFormat.ENCODING_PCM_16BIT
              && encoding != AudioFormat.ENCODING_PCM_FLOAT) {
            throw new IOException("Unsupported decoder PCM encoding");
          }
          advanced = true;
        } else if (index >= 0) {
          try {
            ByteBuffer buffer = codec.getOutputBuffer(index);
            if (buffer == null) {
              throw new IOException("Decoder output buffer unavailable");
            }
            buffer.position(info.offset);
            buffer.limit(info.offset + info.size);
            while (buffer.hasRemaining()) {
              int size = Math.min(buffer.remaining(), transfer.length);
              buffer.get(transfer, 0, size);
              pcm.write(transfer, 0, size);
            }
            if (pcm.length() > 0xffffffffL) {
              throw new IOException("Decoded WAV exceeds 4GB");
            }
            if (duration > 0) {
              notifyProgress(
                  .14f * Math.max(0f, Math.min(1f, (float) info.presentationTimeUs / duration)));
            }
            outputDone = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
            advanced = true;
          } finally {
            codec.releaseOutputBuffer(index, false);
          }
        }
        idle = advanced ? 0 : idle + 1;
        if (idle > 1500) {
          throw new IOException("Audio decoder stalled");
        }
      }
      long bytes = pcm.length() - 44;
      if (bytes <= 0) {
        throw new IOException("Decoded audio is empty");
      }
      int bits = encoding == AudioFormat.ENCODING_PCM_FLOAT ? 32 : 16;
      pcm.seek(0);
      pcm.writeBytes("RIFF");
      write32(pcm, bytes + 36);
      pcm.writeBytes("WAVEfmt ");
      write32(pcm, 16);
      write16(pcm, bits == 32 ? 3 : 1);
      write16(pcm, channels);
      write32(pcm, sampleRate);
      write32(pcm, (long) sampleRate * channels * bits / 8);
      write16(pcm, channels * bits / 8);
      write16(pcm, bits);
      pcm.writeBytes("data");
      write32(pcm, bytes);
    } finally {
      if (codec != null) {
        try {
          if (codecStarted) {
            codec.stop();
          }
        } catch (RuntimeException ignored) {
        } finally {
          codec.release();
        }
      }
      extractor.release();
    }
  }

  private static void write16(RandomAccessFile file, long value) throws IOException {
    file.write((int) value & 255);
    file.write((int) (value >> 8) & 255);
  }

  private static void write32(RandomAccessFile file, long value) throws IOException {
    write16(file, value);
    write16(file, value >> 16);
  }

  private static native long nCreate(
      String directory, int threads, int execution, boolean modulations);

  private static native void nDestroy(long handle);

  private static native void nCancel(long handle);

  private static native String nAnalyze(long handle, String path, KeyAnalyzer progress)
      throws IOException;
}
