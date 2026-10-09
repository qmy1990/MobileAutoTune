package com.moon.mobileautotune.demo;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import java.io.*;
import java.lang.reflect.Field;
import org.json.*;
import com.moon.mobileautotune.MobileAudio;

/** Debug-only endurance test: full live microphone, DSP, monitoring and two WAV
 * writers; preserves existing takes. am instrument -w -e seconds 180 -e legacy
 * false com.moon.mobileautotune.demo/.LivePerformanceInstrumentation */
public class LivePerformanceInstrumentation extends Instrumentation {
    Bundle arguments;
    private static native int nMmap(long handle,int policy);
    private static native void nLegacy(long handle,boolean legacy);
    @Override public void onCreate(Bundle args){super.onCreate(args);arguments=args;start();}
    @Override public void onStart(){Bundle result=new Bundle();MainActivity activity=null;
        try {
            int seconds=Math.min(1800,Math.max(5,Integer.parseInt(arguments.getString("seconds","180"))));
            boolean legacy=Boolean.parseBoolean(arguments.getString("legacy","false"));
            // Some vendor Android16 builds never complete startActivitySync after a
            // shell foreground launch. Observe the real resumed activity instead.
            getTargetContext().startActivity(new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            long readyDeadline=System.nanoTime()+60_000_000_000L;
            while(MainActivity.foreground==null && System.nanoTime()<readyDeadline)Thread.sleep(100);
            activity=MainActivity.foreground;
            if(activity==null)throw new IllegalStateException("Demo must be unlocked and in foreground");
            MainActivity a=activity;Field field=MobileAudio.class.getDeclaredField("handle");field.setAccessible(true);nLegacy(field.getLong(a.audio),legacy);
            boolean forcedMmap=Boolean.parseBoolean(arguments.getString("mmap","false"));
            if(forcedMmap){int policyResult=nMmap(field.getLong(a.audio),3);if(policyResult!=0)throw new IllegalStateException("MMAP test policy error: "+policyResult);}
            // Exercise every effect at full strength, including the neural model.
            runOnMainSync(()->{a.parameters.reverb=a.parameters.echo=a.parameters.denoise=1;a.parameters.tuning=Float.parseFloat(arguments.getString("tuning","1"));a.parameters.pitchShiftSemitones=Float.parseFloat(arguments.getString("pitch","0"));a.update();a.monitor.setChecked(true);a.capturePreset.setSelection(Integer.parseInt(arguments.getString("preset","0")));a.compactProcessing.setChecked(Boolean.parseBoolean(arguments.getString("compact","true")));a.begin();});
            if(!a.live)throw new IllegalStateException("Live microphone could not start: "+a.status.getText());
            JSONObject report=new JSONObject();report.put("forced_mmap",forcedMmap);report.put("legacy",legacy);report.put("seconds",seconds);report.put("all_effects",1);report.put("tuning",a.parameters.tuning);report.put("pitch_shift_semitones",a.parameters.pitchShiftSemitones);report.put("compact",arguments.getString("compact","true"));report.put("capture_preset_selection",arguments.getString("preset","0"));report.put("raw",a.raw.getAbsolutePath());report.put("wet",a.wet.getAbsolutePath());
            JSONArray samples=new JSONArray();report.put("samples",samples);
            long begin=System.nanoTime();
            for(int i=0;i<seconds;i++){
                Thread.sleep(1000);float[] values=a.audio.telemetry();JSONArray row=new JSONArray();row.put(i+1);for(float v:values)row.put(v);samples.put(row);
                if(!a.live)throw new IllegalStateException("Recording stopped before endurance interval finished");
                if(i%15==14){Bundle progress=new Bundle();progress.putString("progress","seconds="+(i+1)+" p99="+values[10]+" xruns="+values[11]+" backlog_ms="+values[12]+" trimmed="+values[14]);sendStatus(0,progress);}
                if(values[7]>0)throw new IllegalStateException("Audio route disconnected");
            }
            runOnMainSync(a::stopRecording);JSONArray last=new JSONArray();for(float v:a.audio.telemetry())last.put(v);report.put("final",last);
            report.put("elapsed_seconds",(System.nanoTime()-begin)/1e9);
            File json=new File(getTargetContext().getFilesDir(),"performance-"+(legacy?"baseline":"optimized")+".json");try(FileWriter w=new FileWriter(json)){w.write(report.toString(2));}
            result.putString("report",json.getAbsolutePath());result.putString("result","Completed: sustained full-effect live recording; inspect report for performance criteria");restoreMmap(a);finish(0,result);
        }catch(Throwable e){if(activity!=null){MainActivity a=activity;runOnMainSync(()->{if(a.live)a.stopRecording();});}restoreMmap(activity);result.putString("failure",e.toString());finish(1,result);}
    }
    private void restoreMmap(MainActivity activity){
        if(!Boolean.parseBoolean(arguments.getString("mmap","false")))return;
        runOnMainSync(()->{try{long handle=0;if(activity!=null && activity.audio!=null){Field f=MobileAudio.class.getDeclaredField("handle");f.setAccessible(true);handle=f.getLong(activity.audio);}nMmap(handle,2);}catch(Exception e){nMmap(0,2);}});
    }
}
