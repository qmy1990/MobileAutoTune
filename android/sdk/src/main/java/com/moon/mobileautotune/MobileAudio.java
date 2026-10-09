package com.moon.mobileautotune;

/** 48kHz mono float PCM SDK. One processing owner; serialize lifecycle/control
 * calls on one thread. process() cannot be used while a live stream is running.
 * File processing is independent and belongs on a worker thread. */
public final class MobileAudio implements AutoCloseable {
    static { System.loadLibrary("mobileaudio_jni"); }
    public static final int REALTIME=0, OFFLINE=1, MAJOR=0, MINOR=1, CHROMATIC=2;
    /** AUTO selects API29+ live-performance capture, otherwise Unprocessed.
     * System capture preprocessing may change timbre; UNPROCESSED preserves the
     * previous capture path. Unsupported modern presets fall back to Unprocessed. */
    public enum CapturePreset {
        AUTO(0), UNPROCESSED(9), VOICE_RECOGNITION(6), VOICE_PERFORMANCE(10);
        final int value; CapturePreset(int value){this.value=value;}
    }
    /** LOW_LATENCY requests 480-sample callbacks and uses the frame DSP entry.
     * COMPATIBLE retains the arbitrary-size adapter. Failed fixed callback opens
     * fall back automatically. Effect algorithms are identical. */
    public enum LiveBuffering { LOW_LATENCY, COMPATIBLE }
    public static final class Parameters {
        public int tonic=3, scale=MAJOR;
        /** Tuning strength0..1.2; values above1 increase mechanical retuning. */
        public float tuning=.75f, reverb=.18f, echo=.08f, denoise=.35f, echoMs=280, concertAHz=440;
        /** -12..12 semitones. Zero preserves the original voice pitch. */
        public float pitchShiftSemitones=0;
        float[] values(){ return new float[]{tonic,scale,tuning,reverb,echo,denoise,echoMs,concertAHz,pitchShiftSemitones}; }
    }
    private long handle;
    private boolean live=false;
    public MobileAudio(int mode){handle=nCreate(mode);if(handle==0)throw new IllegalArgumentException("Cannot create 48kHz engine");}
    private void check(){if(handle==0)throw new IllegalStateException("SDK closed");}
    public synchronized void setParameters(Parameters p){check();if(nSet(handle,p.values())!=0)throw new IllegalArgumentException("Invalid audio parameters");}
    public synchronized void reset(){check();if(live)throw new IllegalStateException("Stop live audio first");nReset(handle);}
    /** Reuses caller arrays. JNI copies; native live callbacks bypass Java. */
    public synchronized void process(float[] input,float[] output,int frames){check();if(live)throw new IllegalStateException("Live stream owns engine");if(frames<0||input.length<frames||output.length<frames)throw new IllegalArgumentException("Invalid buffer");if(nProcess(handle,input,output,frames)!=0)throw new IllegalStateException("Process failed");}
    public synchronized int latencySamples(){check();return nLatency(handle);}
    /** Returns pitch Hz, target Hz, shift semitones, confidence, callback max ms,
     * input underruns, recording dropped samples, route disconnected, disk error.
     * Appended metrics: interval callback max (9), cumulative p99 upper bound
     * (10, 0.1ms buckets; overflow uses lifetime max), output xruns (11, -1 unsupported),
     * current/max actual capture FIFO ms (12/13), discarded backlog samples (14),
     * callbacks/deadline misses (15/16), output/input timestamp latency ms
     * (17/18, -1 unavailable), output burst/buffer ms (19/20), output/input
     * MMAP flags (21/22), audio API (23), input burst ms (24), input clock
     * correction ppm (25, limited to +/-200), actual capture preset (26),
     * preset fallback flag (27), performance hint requested (28, not proof active),
     * output sharing/performance mode (29/30, Oboe enum values), input API (31),
     * live nominal algorithm latency ms (32), actual callback period ms (33).
     * Callback times and timestamp estimates are not acoustic measurements.
     * Metrics are retained after stopLive(); new recording starts fresh. */
    public synchronized float[] telemetry(){check();return nTelemetry(handle);}
    /** Requires microphone permission and a 48kHz route. Records raw and monitored
     * WAVs using a disk worker. Use headphones to prevent acoustic feedback. */
    public synchronized void startLive(String rawWav,String monitoredWav,boolean monitor){startLive(rawWav,monitoredWav,monitor,CapturePreset.AUTO);}
    public synchronized void startLive(String rawWav,String monitoredWav,boolean monitor,CapturePreset preset){startLive(rawWav,monitoredWav,monitor,preset,LiveBuffering.LOW_LATENCY);}
    public synchronized void startLive(String rawWav,String monitoredWav,boolean monitor,CapturePreset preset,LiveBuffering buffering){check();if(buffering==null)throw new IllegalArgumentException("Missing buffering mode");if(preset==null)throw new IllegalArgumentException("Missing capture preset");if(live)throw new IllegalStateException("Already started");int code=nStart(handle,rawWav,monitoredWav,monitor,preset.value,buffering==LiveBuffering.LOW_LATENCY);if(code!=0)throw new IllegalStateException("Audio route/start error: "+code);live=true;}
    public synchronized void stopLive(){if(handle!=0&&live){nStop(handle);live=false;}}
    @Override public synchronized void close(){if(handle!=0){stopLive();nDestroy(handle);handle=0;}}
    public static void processWav(String input,String output,Parameters p,int mode,boolean tail){int code=nWav(input,output,p.values(),mode,tail);if(code!=0)throw new IllegalArgumentException("WAV processing error: "+code+" (requires 48kHz PCM16/float32 WAV)");}
    private static native long nCreate(int mode);
    private static native void nDestroy(long handle);
    private static native int nSet(long handle,float[] p);
    private static native void nReset(long handle);
    private static native int nProcess(long handle,float[] input,float[] output,int frames);
    private static native int nLatency(long handle);
    private static native float[] nTelemetry(long handle);
    private static native int nStart(long handle,String raw,String wet,boolean monitor,int preset,boolean compact);
    private static native void nStop(long handle);
    private static native int nWav(String input,String output,float[] p,int mode,boolean tail);
}
