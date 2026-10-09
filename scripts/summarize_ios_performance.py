#!/usr/bin/env python3
"""Summarize RemoteIO profiling without treating OS latency as acoustic latency."""
import json, statistics, sys
from pathlib import Path
r=json.loads(Path(sys.argv[1]).read_text())
if not r.get('completed'):
    raise SystemExit('Incomplete recording: '+r.get('failure','no completion marker'))
samples=r['samples'];summary=dict(r['final'])
summary.update({k:r[k] for k in ['requestedSeconds','elapsedSeconds','inputPorts','outputPorts','sampleRate','systemVersion','monitor']})
summary['sampleCount']=len(samples)
summary['recordingAdvancedEverySample']=all(b['processedSamples']>a['processedSamples'] for a,b in zip(samples,samples[1:]))
summary['diskErrorAfterClose']=r.get('diskErrorAfterClose')
summary['inputLatencyMedianMs']=statistics.median(s['inputLatencyMs'] for s in samples)
summary['outputLatencyMedianMs']=statistics.median(s['outputLatencyMs'] for s in samples)
summary['physicalRoundTripVerified']=False
print(json.dumps(summary,ensure_ascii=False,indent=2))
