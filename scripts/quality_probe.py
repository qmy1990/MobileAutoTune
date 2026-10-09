#!/usr/bin/env python3
"""Deterministic stationary-noise attenuation probe (not a perceptual MOS score).
Requires numpy/scipy. Keeps inputs and resulting WAVs for listening review.
"""
from pathlib import Path
import json, subprocess, wave
import numpy as np
from scipy.signal import lfilter
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'outputs';OUT.mkdir(exist_ok=True)
rng=np.random.default_rng(42);rate=48000;seconds=6
noise=rng.normal(0,.025,rate*seconds)+lfilter([.02],[1,-.96],rng.normal(0,1,rate*seconds))
def save(path,x):
    with wave.open(str(path),'wb') as f:f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes((np.clip(x,-.999,.999)*32768).astype('<i2').tobytes())
def load(path):
    with wave.open(str(path),'rb') as f:return np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').astype(float)/32768
source=OUT/'probe_noise.wav';save(source,noise)
results=[]
for mode in ['realtime','offline']:
    target=OUT/f'probe_noise__{mode}.wav'
    subprocess.run([str(ROOT/'build/mat_process'),str(source),str(target),'--mode',mode,'--tuning','0','--echo','0','--reverb','0','--denoise','1'],check=True,capture_output=True)
    original=load(source)[-rate*2:];filtered=load(target)[-rate*2:]
    attenuation=20*np.log10(np.sqrt(np.mean(original**2))/max(1e-9,np.sqrt(np.mean(filtered**2))))
    results.append({'mode':mode,'stationary_noise_attenuation_db':float(attenuation)})
    if attenuation<3:raise RuntimeError('Stationary noise probe did not attenuate noise')
# Inject stationary noise into a supplied vocal for manual listening. No quality
# score is assigned because the supplied recording is not a known clean reference.
original_path=ROOT/'audios/hook3.wav'
vocal=load(original_path);noisy=np.clip(vocal+rng.normal(0,.015,len(vocal)),-.999,.999)
source=OUT/'hook3__added_noise.wav';save(source,noisy)
for mode in ['realtime','offline']:
    subprocess.run([str(ROOT/'build/mat_process'),str(source),str(OUT/f'hook3__added_noise__{mode}__denoised.wav'),'--mode',mode,'--tuning','0','--echo','0','--reverb','0','--denoise','.65'],check=True,capture_output=True)
report={'fixture':'6s seeded white + low-frequency stationary noise; RMS measured on final 2s after adaptation','not_a_voice_quality_score':True,'results':results}
(OUT/'noise_probe.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
