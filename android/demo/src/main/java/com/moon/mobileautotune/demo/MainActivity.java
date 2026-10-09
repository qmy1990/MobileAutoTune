package com.moon.mobileautotune.demo;
import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.*;
import android.view.View;
import java.io.*;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import com.moon.mobileautotune.MobileAudio;

public class MainActivity extends Activity {
    static volatile MainActivity foreground;
    final MobileAudio.Parameters parameters=new MobileAudio.Parameters();
    MobileAudio audio; volatile boolean live=false; boolean busy=false; File raw,wet,offline;
    TextView status,meter; Button start,process,importButton; CheckBox monitor,compactProcessing; Spinner capturePreset;
    final Handler ui=new Handler(Looper.getMainLooper());
    final ExecutorService worker=Executors.newSingleThreadExecutor(); MediaPlayer player;
    final Runnable poll=new Runnable(){public void run(){
        if(audio!=null&&live){float[] t=showMeter();if(t[7]>0){stopRecording();status.setText("音频设备断开，已停止。重新连接后可再次开始。");}}
        ui.postDelayed(this,300);
    }};
    float[] showMeter(){float[] t=audio.telemetry();meter.setText(String.format(Locale.US,"F0 %.1f → %.1f Hz | %.2f 半音\n近期回调 %.2f ms / P99 ≤%.2f ms / 预算 %.1f ms\n输入积压 %.1f ms / 输出欠载 %.0f / 追平 %.0f 帧\n输入缺帧 %.0f / 录音丢帧 %.0f / 写盘错误 %.0f\nDSP %.1f ms · 输出通路估计 %.1f ms（非实测总延迟）\n录音通路 %s · MMAP %s · 调度提示 %s",t[0],t[1],t[2],t[9],t[10],t[33]>0?t[33]:t[19],t[12],t[11],t[14],t[5],t[6],t[8],t[32]>0?t[32]:audio.latencySamples()/48.f,t[17],t[26]==10?"实时演唱":t[26]==9?"原始录音":"语音识别",t[21]>0?"是":"否",t[28]>0?"已请求":"未请求"));return t;}
    @Override public void onCreate(Bundle b){super.onCreate(b);audio=new MobileAudio(MobileAudio.REALTIME);
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(32,32,32,32);
        ScrollView scroll=new ScrollView(this);scroll.addView(root);setContentView(scroll);
        TextView title=new TextView(this);title.setText("MobileAutoTune · 人声工作台");title.setTextSize(24);root.addView(title);
        TextView hint=new TextView(this);hint.setText("48 kHz 单声道人声。默认降E大调。监听需有线/USB耳机；蓝牙不用于50ms验收。");root.addView(hint);
        Spinner tonic=new Spinner(this);tonic.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"C","C♯","D","E♭","E","F","F♯","G","A♭","A","B♭","B"}));tonic.setSelection(3);root.addView(tonic);
        tonic.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener(){public void onNothingSelected(android.widget.AdapterView<?> a){}public void onItemSelected(android.widget.AdapterView<?> a,View v,int position,long id){parameters.tonic=position;update();}});
        Spinner scale=new Spinner(this);scale.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"大调","自然小调","半音阶"}));root.addView(scale);
        scale.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener(){public void onNothingSelected(android.widget.AdapterView<?> a){}public void onItemSelected(android.widget.AdapterView<?> a,View v,int position,long id){parameters.scale=position;update();}});
        slider(root,"修音（高强度电音）",75,120,v->parameters.tuning=v);pitchSlider(root);slider(root,"混响",18,v->parameters.reverb=v);slider(root,"回声",8,v->parameters.echo=v);slider(root,"降噪（唱歌建议适中）",35,v->parameters.denoise=v);
        capturePreset=new Spinner(this);capturePreset.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"自动选择实时演唱通路", "原始录音通路（保留原音色）", "语音识别通路", "实时演唱通路（Android 10+）"}));root.addView(capturePreset);
        compactProcessing=new CheckBox(this);compactProcessing.setText("低延迟处理（减少监听缓存）");compactProcessing.setChecked(true);root.addView(compactProcessing);
        monitor=new CheckBox(this);monitor.setText("开启实时耳机监听");root.addView(monitor);
        start=button(root,"开始录音",()->{if(live)stopRecording();else if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},1);else begin();});
        importButton=button(root,"导入48kHz WAV",()->{if(live||busy)return;Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);i.setType("audio/*");i.addCategory(Intent.CATEGORY_OPENABLE);startActivityForResult(i,2);});
        process=button(root,"高质量后处理",()->runOffline());
        button(root,"试听原音",()->play(raw));button(root,"试听实时效果录音",()->play(wet));button(root,"试听后处理效果",()->play(offline));
        meter=new TextView(this);root.addView(meter);status=new TextView(this);root.addView(status);status.setText("停止录音后可后处理；文件保存在应用外部私有目录。导入文件会混合为单声道。");update();ui.post(poll);
    }
    interface Setter{void set(float value);}
    void slider(LinearLayout root,String name,int value,Setter setter){slider(root,name,value,100,setter);}
    void slider(LinearLayout root,String name,int value,int maximum,Setter setter){TextView text=new TextView(this);text.setText(name+" "+value+"%");root.addView(text);SeekBar s=new SeekBar(this);s.setMax(maximum);s.setProgress(value);root.addView(s);s.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}public void onProgressChanged(SeekBar s,int v,boolean user){text.setText(name+" "+v+"%");setter.set(v/100.f);update();}});}
    Button button(LinearLayout root,String label,Runnable action){Button b=new Button(this);b.setText(label);b.setOnClickListener(v->action.run());root.addView(b);return b;}
    void pitchSlider(LinearLayout root){
        TextView text=new TextView(this);text.setText("音高（低沉 ↔ 高尖） 0.0 半音");root.addView(text);
        SeekBar s=new SeekBar(this);s.setMax(48);s.setProgress(24);s.setContentDescription("音高，负值低沉，正值高尖");root.addView(s);
        s.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}public void onProgressChanged(SeekBar s,int v,boolean user){parameters.pitchShiftSemitones=(v-24)*.5f;text.setText(String.format(Locale.US,"音高（低沉 ↔ 高尖） %+.1f 半音",parameters.pitchShiftSemitones));update();}});
        button(root,"恢复原音高",()->s.setProgress(24));
    }
    void update(){if(audio!=null)audio.setParameters(parameters);}
    MobileAudio.Parameters snapshot(){MobileAudio.Parameters p=new MobileAudio.Parameters();p.tonic=parameters.tonic;p.scale=parameters.scale;p.tuning=parameters.tuning;p.reverb=parameters.reverb;p.echo=parameters.echo;p.denoise=parameters.denoise;p.pitchShiftSemitones=parameters.pitchShiftSemitones;p.echoMs=parameters.echoMs;p.concertAHz=parameters.concertAHz;return p;}
    void begin(){if(busy)return;stopPlayer();File dir=getExternalFilesDir(null);if(dir==null)dir=getFilesDir();String prefix="take-"+System.currentTimeMillis();raw=new File(dir,prefix+"-raw.wav");wet=new File(dir,prefix+"-live.wav");offline=null;try{audio.startLive(raw.getAbsolutePath(),wet.getAbsolutePath(),monitor.isChecked(),MobileAudio.CapturePreset.values()[capturePreset.getSelectedItemPosition()],compactProcessing.isChecked()?MobileAudio.LiveBuffering.LOW_LATENCY:MobileAudio.LiveBuffering.COMPATIBLE);live=true;getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);compactProcessing.setEnabled(false);capturePreset.setEnabled(false);start.setText("停止录音");status.setText("正在录音："+raw);monitor.setEnabled(false);}catch(Exception e){status.setText(e.getMessage());}}
    void stopRecording(){audio.stopLive();live=false;getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);showMeter();start.setText("开始录音");monitor.setEnabled(true);capturePreset.setEnabled(true);compactProcessing.setEnabled(true);status.setText("已保存原音及实时效果。可进行后处理："+raw);}
    void runOffline(){if(live||busy||raw==null){status.setText("先停止录音或导入WAV。");return;}stopPlayer();busy=true;process.setEnabled(false);File source=raw,target=new File(raw.getParentFile(),"offline-"+System.currentTimeMillis()+".wav");MobileAudio.Parameters p=snapshot();long before=System.nanoTime();status.setText("正在后处理…");worker.execute(()->{try{MobileAudio.processWav(source.getAbsolutePath(),target.getAbsolutePath(),p,MobileAudio.OFFLINE,true);double seconds=(System.nanoTime()-before)/1e9;ui.post(()->{offline=target;status.setText(String.format(Locale.US,"后处理完成，耗时 %.3fs（含4秒效果尾音）\n%s",seconds,target));finishWork();});}catch(Exception e){ui.post(()->{status.setText(e.getMessage());finishWork();});}});}
    void finishWork(){busy=false;process.setEnabled(true);}
    void stopPlayer(){if(player!=null){player.release();player=null;}}
    void play(File file){if(live||busy||file==null||!file.exists()){status.setText("对应音频尚未生成，请先停止录音并处理。");return;}stopPlayer();try{player=new MediaPlayer();player.setDataSource(file.getAbsolutePath());player.prepare();player.start();status.setText("试听："+file.getName());}catch(Exception e){status.setText(e.getMessage());stopPlayer();}}
    @Override public void onRequestPermissionsResult(int r,String[] p,int[] g){super.onRequestPermissionsResult(r,p,g);if(r==1&&g.length>0&&g[0]==PackageManager.PERMISSION_GRANTED)begin();else status.setText("需要麦克风权限才能录音。");}
    @Override public void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request!=2||result!=RESULT_OK||data==null)return;Uri uri=data.getData();if(uri==null)return;busy=true;status.setText("导入中…");worker.execute(()->{try{File target=new File(getFilesDir(),"import-"+System.currentTimeMillis()+".wav");try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(target)){if(in==null)throw new IOException("不能读取文件");byte[] buffer=new byte[65536];int n;while((n=in.read(buffer))>0)out.write(buffer,0,n);}ui.post(()->{raw=target;wet=offline=null;status.setText("已导入："+target);busy=false;});}catch(Exception e){ui.post(()->{status.setText(e.getMessage());busy=false;});}});}
    @Override public void onResume(){super.onResume();foreground=this;}
    @Override public void onPause(){if(foreground==this)foreground=null;super.onPause();if(live)stopRecording();stopPlayer();}
    @Override public void onDestroy(){ui.removeCallbacksAndMessages(null);worker.shutdown();if(audio!=null){audio.close();audio=null;}super.onDestroy();}
}
