#!/usr/bin/env python3
"""Process all supplied WAVs and emit reproducible timings/listening comparisons.
No third-party Python dependencies. Run after a Release CMake build.
"""
import argparse, html, json, pathlib, subprocess, time, wave, struct, platform, statistics
ROOT = pathlib.Path(__file__).resolve().parents[1]
PRESETS = {
 'natural': {'tuning': .65, 'reverb': .18, 'echo': .08, 'denoise': .30},
 'hard_tune': {'tuning': 1, 'reverb': .10, 'echo': .05, 'denoise': .25},
 'denoise_only': {'tuning': 0, 'reverb': 0, 'echo': 0, 'denoise': 1},
 'space_only': {'tuning': 0, 'reverb': .65, 'echo': .60, 'denoise': 0},
}
def wav_info(path):
    with wave.open(str(path), 'rb') as f:
        duration = f.getnframes() / f.getframerate()
        if f.getsampwidth() != 2: raise ValueError('Validation samples must be PCM16')
        peak, square, count = 0, 0, 0
        while data := f.readframes(8192):
            values = struct.unpack('<' + 'h' * (len(data)//2), data)
            peak = max(peak, max(map(abs, values), default=0))
            square += sum(v*v for v in values); count += len(values)
        return {'duration_s':duration,'sample_rate':f.getframerate(),'channels':f.getnchannels(), 'peak':peak/32768, 'rms':(square/max(count,1))**.5/32768}
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',default=str(ROOT/'build/mat_process'));parser.add_argument('--audios',default=str(ROOT/'audios'));parser.add_argument('--output',default=str(ROOT/'outputs'));args=parser.parse_args()
    output=pathlib.Path(args.output);output.mkdir(parents=True,exist_ok=True)
    records=[]
    for source in sorted(pathlib.Path(args.audios).glob('*.wav')):
        source_info=wav_info(source)
        for mode in ['realtime','offline']:
            for name,preset in PRESETS.items():
                target=output/(source.stem+'__'+mode+'__'+name+'.wav')
                cmd=[args.binary,str(source),str(target),'--mode',mode,'--tonic','3','--scale','major']
                for option,value in preset.items():cmd += ['--'+option,str(value)]
                result=subprocess.run(cmd,capture_output=True,text=True,check=True);timing=json.loads(result.stdout)
                info=wav_info(target)
                if abs(info['duration_s']-source_info['duration_s'])>1/48000: raise RuntimeError('Length compensation failed')
                rtf=timing['seconds']/source_info['duration_s']
                records.append({'source':str(source),'output':str(target),'mode':mode,'preset':name,'parameters':preset,'input':source_info,'output_info':info,**timing,'rtf':rtf})
                print(f'{source.name} {mode} {name}: {timing["seconds"]:.3f}s, RTF {rtf:.3f}',flush=True)
    report={'host':platform.platform(),'machine':platform.machine(),'processor':platform.processor(),'date':time.strftime('%Y-%m-%d'),'tonic':'Eb major','notes':['Desktop wall times include WAV I/O; no mobile device claim.', 'Outputs contain no appended tails to compare exact source durations.', 'RTF < 2 is the requested offline budget; acoustic latency needs device loopback measurement.'],'results':records}
    (output/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    rows=[]
    for source in sorted(pathlib.Path(args.audios).glob('*.wav')):
        rows.append(f'<h2>{html.escape(source.name)}</h2><p>原音</p><audio controls src="{html.escape(source.as_uri())}"></audio>')
        for record in [r for r in records if r['source']==str(source)]:
            rows.append(f'<p>{record["mode"]} · {record["preset"]} · RTF {record["rtf"]:.3f}</p><audio controls src="{html.escape(pathlib.Path(record["output"]).name)}"></audio>')
    (output/'listen.html').write_text('<!doctype html><meta charset="utf-8"><title>降E大调处理对比</title><style>body{font:16px system-ui;max-width:900px;margin:40px auto;background:#fafafa;color:#162030}audio{width:100%}h2{margin-top:40px}</style><h1>MobileAutoTune · 降E大调试听</h1><p>每个音频提供原音、自然修音、电音、单独降噪、混响回声；分别比较实时算法与后处理算法。</p>'+''.join(rows))
    table=['# 本地音频验证', '', f'主机：{report["host"]}，{report["machine"]}。调式：降E大调。', '', '时长与原音一致，未追加混响尾音。耗时含文件 I/O；手机指标未测量。', '', '| 原音 | 模式 | 预设 | 音频秒数 | 耗时秒数 | RTF |', '|---|---|---|---:|---:|---:|']
    for r in records:table.append(f'| {pathlib.Path(r["source"]).name} | {r["mode"]} | {r["preset"]} | {r["input"]["duration_s"]:.3f} | {r["seconds"]:.3f} | {r["rtf"]:.3f} |')
    (output/'VALIDATION.md').write_text('\n'.join(table)+'\n')
    print('Report:',output/'VALIDATION.md')
if __name__=='__main__':main()
