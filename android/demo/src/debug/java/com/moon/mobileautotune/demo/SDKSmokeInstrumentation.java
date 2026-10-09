package com.moon.mobileautotune.demo;
import android.app.Instrumentation;
import android.os.Bundle;
import java.io.*;
import com.moon.mobileautotune.MobileAudio;

/** Debug-only native bridge smoke test, invoked with adb shell am instrument -w. */
public class SDKSmokeInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    static void check(boolean condition,String message){if(!condition)throw new AssertionError(message);}
    @Override public void onStart(){Bundle result=new Bundle();try{
        for(int mode=0;mode<2;mode++){
            try(MobileAudio engine=new MobileAudio(mode)){
                MobileAudio.Parameters p=new MobileAudio.Parameters();p.tuning=p.reverb=p.echo=p.denoise=0;engine.setParameters(p);engine.reset();
                float[] in=new float[48000],out=new float[48000];for(int i=0;i<in.length;i++)in[i]=(float)(.1*Math.sin(i*.03));
                engine.process(in,out,in.length);int latency=engine.latencySamples();
                for(int i=latency;i<out.length;i++)check(Math.abs(out[i]-in[i-latency])<1e-6,"Dry alignment");
                p.tuning=1;p.tonic=0;engine.setParameters(p);engine.reset();
                for(int i=0;i<in.length;i++)in[i]=(float)(.1*Math.sin(2*Math.PI*225*i/48000));
                engine.process(in,out,in.length);float[] telemetry=engine.telemetry();check(Math.abs(telemetry[0]-225)<2,"Pitch tracker");check(Math.abs(telemetry[1]-220)<.1,"Scale mapping");
                result.putFloat(mode==0?"realtime_nominal_ms":"offline_nominal_ms",latency/48.f);
                p.tuning=1.2f;p.pitchShiftSemitones=12;engine.setParameters(p);engine.reset();engine.process(in,out,in.length);
                telemetry=engine.telemetry();check(Math.abs(telemetry[1]-440)<.1,"Enhanced tuning + octave transpose");check(telemetry[2]>11 && telemetry[2]<12,"Total pitch correction");
                p.pitchShiftSemitones=-12;engine.setParameters(p);engine.reset();engine.process(in,out,in.length);check(Math.abs(engine.telemetry()[1]-110)<.1,"Lower octave transpose");
                p.pitchShiftSemitones=1;engine.setParameters(p);engine.reset();engine.process(in,out,in.length);check(Math.abs(engine.telemetry()[1]-246.94165)<.1,"Transpose first then Cmajor: B, not A sharp");
                p.pitchShiftSemitones=Float.NaN;boolean pitchRejected=false;try{engine.setParameters(p);}catch(IllegalArgumentException e){pitchRejected=true;}check(pitchRejected,"Invalid pitch parameter");p.pitchShiftSemitones=0;
                p.denoise=Float.NaN;boolean rejected=false;try{engine.setParameters(p);}catch(IllegalArgumentException e){rejected=true;}check(rejected,"NaN parameter validation");
            }
        }
        File input=new File(getTargetContext().getFilesDir(),"smoke-in.wav"),output=new File(getTargetContext().getFilesDir(),"smoke-out.wav");
        try(DataOutputStream stream=new DataOutputStream(new FileOutputStream(input))){stream.writeBytes("RIFF");p32(stream,36+2674);stream.writeBytes("WAVEfmt ");p32(stream,16);p16(stream,1);p16(stream,1);p32(stream,48000);p32(stream,96000);p16(stream,2);p16(stream,16);stream.writeBytes("data");p32(stream,2674);for(int i=0;i<1337;i++)p16(stream,(int)(4000*Math.sin(i*.03)));}
        MobileAudio.Parameters p=new MobileAudio.Parameters();p.tuning=p.reverb=p.echo=p.denoise=0;
        MobileAudio.processWav(input.getAbsolutePath(),output.getAbsolutePath(),p,MobileAudio.OFFLINE,false);check(output.length()==input.length(),"Offline length");
        try(FileInputStream a=new FileInputStream(input);FileInputStream b=new FileInputStream(output)){int value;while((value=a.read())!=-1)check(value==b.read(),"Offline dry identity");}
        input.delete();output.delete();result.putString("result","PASS: JNI processing, pitch mapping, parameters, offline WAV");finish(0,result);
    }catch(Throwable e){result.putString("failure",e.toString());finish(1,result);}}
    static void p16(DataOutputStream stream,int value)throws IOException{stream.write(value&255);stream.write((value>>8)&255);}
    static void p32(DataOutputStream stream,int value)throws IOException{p16(stream,value);p16(stream,value>>16);}
}
