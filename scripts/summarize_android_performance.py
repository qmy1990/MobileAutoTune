#!/usr/bin/env python3
"""Summarize fixed telemetry schema; do not mistake counters for acoustic delay."""
import json
import statistics
import sys
from pathlib import Path

NAMES = ['pitch_hz','target_hz','shift_semitones','confidence','callback_max_ms',
         'input_underrun_callbacks','recording_dropped_samples','disconnected',
         'disk_error','interval_callback_max_ms','callback_p99_upper_ms',
         'output_xruns','capture_fifo_ms','capture_fifo_max_ms',
         'capture_resync_samples','callbacks','callback_over_period',
         'output_timestamp_latency_ms','input_timestamp_latency_ms',
         'output_burst_ms','output_buffer_ms','output_mmap','input_mmap',
         'audio_api','input_burst_ms','capture_clock_ppm',
         'capture_preset','capture_preset_fallback','performance_hint_requested',
         'output_sharing_mode','output_performance_mode','input_audio_api',
         'live_algorithm_latency_ms','callback_period_ms']

def summarize(path):
    r=json.loads(Path(path).read_text())
    final=dict(zip(NAMES,r['final']))
    samples=r['samples']
    # row[0] is elapsed test-second label; subsequent items are telemetry.
    final['measured_test_seconds']=r['seconds']
    final['sample_count']=len(samples)
    final['recording_advanced_every_sample']=all(b[16]>a[16] for a,b in zip(samples,samples[1:]))
    final['output_latency_median_ms']=statistics.median(row[18] for row in samples if row[18]>=0)
    final['fifo_median_ms']=statistics.median(row[13] for row in samples)
    final['fifo_p99_ms']=sorted(row[13] for row in samples)[min(len(samples)-1,int(len(samples)*.99))]
    final['physical_round_trip_verified']=False
    final['legacy_queue_is_counter_estimate']=r['legacy']
    print(json.dumps(final,ensure_ascii=False,indent=2))

if __name__=='__main__':summarize(sys.argv[1])
