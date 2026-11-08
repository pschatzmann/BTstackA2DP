"""Gap detector for a recorded sine tone (WAV, 16 bit).
A pure sine obeys x[n] = 2 cos(w) x[n-1] - x[n-2]. Dropped / repeated / silenced samples break this;
the residual then spikes. Usage: gaps.py file.wav [min_seconds_to_skip_at_start]"""
import sys, wave
import numpy as np

w = wave.open(sys.argv[1]); r = w.getframerate(); ch = w.getnchannels()
x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64).reshape(-1, ch)[:, 0]
skip = int(float(sys.argv[2]) * r) if len(sys.argv) > 2 else int(0.5 * r)
x = x[skip:]
if len(x) < r:
    print("too short:", len(x) / r, "s"); sys.exit(1)

# estimate the frequency from the robust (median) of local cos(w) estimates
a, b, c = x[1:-1], x[:-2], x[2:]
num = a * (b + c); den = 2 * a * a
blk = 2048
est = [num[i:i+blk].sum() / den[i:i+blk].sum() for i in range(0, len(a) - blk, blk) if den[i:i+blk].sum() > 1e6]
cosw = float(np.median(est)); freq = np.arccos(np.clip(cosw, -1, 1)) * r / (2 * np.pi)
amp = float(np.sqrt(2 * np.mean(x ** 2)))
res = np.abs(c - (2 * cosw * a - b))
noise = float(np.median(res))                      # lossy codec noise floor
thr = max(0.05 * amp, 8 * noise)                   # anomaly: far above the noise floor
bad = np.where(res > thr)[0]
# group neighbouring anomalies into events
events = []
for i in bad:
    if events and i - events[-1][1] <= 64: events[-1][1] = i
    else: events.append([i, i])
# digital silence (underrun filled with zeros)
zero = (np.abs(x) < 1).astype(np.int8)
runs = []; i = 0
while i < len(zero):
    if zero[i]:
        j = i
        while j < len(zero) and zero[j]: j += 1
        if j - i >= 32: runs.append((i, j - i))
        i = j
    else: i += 1
print(f"{sys.argv[1].split('/')[-1]}: {r} Hz, {ch} ch, {len(x)/r:.1f} s analysed, tone {freq:.1f} Hz, "
      f"amplitude {amp:.0f}, noise floor {noise:.0f}")
print(f"  discontinuities: {len(events)}   silent runs (>=32 samples): {len(runs)}")
for s, e in events[:10]:
    print(f"   discontinuity at {(skip+s)/r:8.3f} s (residual {res[s:e+1].max():.0f}, threshold {thr:.0f})")
for s, n in runs[:10]:
    print(f"   silence at       {(skip+s)/r:8.3f} s for {n/r*1000:.1f} ms")
sys.exit(0 if not events and not runs else 1)
