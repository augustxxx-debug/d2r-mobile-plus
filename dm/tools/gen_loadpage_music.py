#!/usr/bin/env python3
"""加载页循环音乐（自制，程序化合成；不含任何原版音频）：低音持续音 + 缓慢小调和声垫 + 稀疏钟声 + 低频风声，约 48 秒，首尾交叉淡化成无缝循环。
输出 WAV（44.1 kHz 立体声 16 位）再用 macOS 自带 afconvert 编成 AAC（.m4a，96 kbps，约 0.6 MB）；iOS / Mac（AVAudioPlayer）与安卓（MediaPlayer）都能解。
固定随机种子，重跑得到同一段。用法：python3 dm/tools/gen_loadpage_music.py [输出目录=dm/assets/loadpage]（要 numpy，项目 venv 里有）
Loading page loop music (self-made, procedurally synthesised; no original game audio): low drone + slow minor pad + sparse bells + low wind, about 48 s, cross-faded into a seamless loop.
WAV (44.1 kHz stereo 16-bit) is encoded to AAC (.m4a, 96 kbps, about 0.6 MB) with macOS's own afconvert; decodable by iOS / Mac (AVAudioPlayer) and Android (MediaPlayer). Fixed seed, reproducible.
"""
import os, sys, struct, subprocess, wave
import numpy as np

SR = 44100
DUR = 48.0

def lowpass(x, cutoff):   # 一阶低通 | one-pole low-pass
    a = np.exp(-2 * np.pi * cutoff / SR); y = np.empty_like(x); acc = 0.0
    for i in range(len(x)):   # 纯 Python 循环太慢：用 lfilter 等价写法 | a pure Python loop is too slow: equivalent recursive filter below
        pass
    return _lp(x, a)

def _lp(x, a):
    # y[n] = a*y[n-1] + (1-a)*x[n]，用 numpy 的累积技巧按块算 | computed in blocks with numpy
    y = np.zeros_like(x); b = 1 - a; prev = 0.0
    n = len(x); block = 4096
    for s in range(0, n, block):
        seg = x[s:s + block]; k = np.arange(len(seg))
        # 块内解析解：y[k] = a^(k+1) prev + b * sum_{j<=k} a^(k-j) x[j] | closed form inside the block
        pw = a ** (k + 1)
        conv = np.convolve(seg, a ** np.arange(len(seg)))[:len(seg)] * b
        y[s:s + block] = pw * prev + conv; prev = y[s + len(seg) - 1]
    return y

def env_ad(n, attack, release):
    e = np.ones(n); a = int(attack * SR); r = int(release * SR)
    e[:a] = np.linspace(0, 1, a); e[-r:] *= np.linspace(1, 0, r) ** 2
    return e

def tone(freq, n, rng, detune=0.0, harmonics=((1, 1.0), (2, 0.35), (3, 0.18), (4, 0.08))):
    t = np.arange(n) / SR; out = np.zeros(n)
    for h, amp in harmonics:
        out += amp * np.sin(2 * np.pi * freq * h * (1 + detune) * t + rng.random() * 6.283)
    return out

def main():
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'loadpage')
    os.makedirs(dest, exist_ok=True)
    rng = np.random.default_rng(1029)
    n = int(DUR * SR); t = np.arange(n) / SR
    L = np.zeros(n); R = np.zeros(n)
    # 1. 持续低音 D（73.4 Hz）与五度 A，两声部各略失谐、慢速颤动 | drone: D (73.4 Hz) and the fifth A, slightly detuned pairs with slow wobble
    for f, amp, pan in ((73.42, 0.22, -0.3), (73.42 * 1.004, 0.18, 0.3), (110.0, 0.10, 0.2), (110.0 * 0.997, 0.09, -0.2), (36.71, 0.12, 0.0)):
        d = tone(f, n, rng) * amp * (0.85 + 0.15 * np.sin(2 * np.pi * 0.07 * t + rng.random() * 6))
        L += d * (1 - pan) / 2 * 1.4; R += d * (1 + pan) / 2 * 1.4
    # 2. 和声垫：D 小调 → Bb 大 → G 小 → A（小调五级），每个和弦 12 秒，缓起缓落 | pad: Dm → Bb → Gm → A, 12 s each, slow swells
    chords = [(146.83, 174.61, 220.0), (116.54, 146.83, 174.61), (98.0, 116.54, 146.83), (110.0, 138.59, 164.81)]
    seg = n // 4
    for i, ch in enumerate(chords):
        s0 = i * seg; e = env_ad(seg, 3.5, 4.0)
        for j, f in enumerate(ch):
            pad = tone(f, seg, rng, detune=0.002 * (j - 1), harmonics=((1, 1.0), (2, 0.25), (3, 0.08))) * 0.07 * e
            pan = (j - 1) * 0.35
            L[s0:s0 + seg] += pad * (1 - pan) / 2 * 1.4; R[s0:s0 + seg] += pad * (1 + pan) / 2 * 1.4
    # 3. 稀疏钟声：D 小调音阶的高音，指数衰减，随机落点，整段 7 下 | sparse bells: high notes of D minor, exponential decay, random placement, 7 strikes
    scale = (587.33, 698.46, 783.99, 880.0, 1046.5, 1174.7)
    for k in range(7):
        at = int((2 + k * 6.3 + rng.random() * 2.5) * SR); f = scale[rng.integers(0, len(scale))]
        m = min(n - at, int(5.0 * SR)); tt = np.arange(m) / SR
        bell = (np.sin(2 * np.pi * f * tt) + 0.4 * np.sin(2 * np.pi * f * 2.76 * tt) * np.exp(-tt * 3) + 0.2 * np.sin(2 * np.pi * f * 5.4 * tt) * np.exp(-tt * 6)) * np.exp(-tt * 1.1) * 0.08
        pan = rng.random() * 0.8 - 0.4
        L[at:at + m] += bell * (1 - pan) / 2 * 1.4; R[at:at + m] += bell * (1 + pan) / 2 * 1.4
    # 4. 风：低通白噪声，幅度慢起伏 | wind: low-passed white noise with slow amplitude swell
    wind = _lp(rng.standard_normal(n), np.exp(-2 * np.pi * 220 / SR)) * 0.9
    wind *= 0.05 * (0.6 + 0.4 * np.sin(2 * np.pi * 0.045 * t + 1.0)) * (0.7 + 0.3 * np.sin(2 * np.pi * 0.11 * t))
    L += wind; R += _lp(rng.standard_normal(n), np.exp(-2 * np.pi * 220 / SR)) * 0.9 * 0.05 * (0.6 + 0.4 * np.sin(2 * np.pi * 0.045 * t + 2.5))
    # 5. 无缝循环：末尾 3 秒与开头 3 秒交叉淡化 | seamless loop: cross-fade the last 3 s into the first 3 s
    xf = int(3 * SR); ramp = np.linspace(0, 1, xf)
    for ch in (L, R):
        head = ch[:xf].copy(); ch[-xf:] = ch[-xf:] * (1 - ramp) + head * ramp
    # 6. 软限幅、归一化到 -3 dBFS | soft clip, normalise to -3 dBFS
    st = np.stack([L, R], 1); st = np.tanh(st * 1.6) / np.tanh(1.6); st *= 0.7 / np.max(np.abs(st))
    pcm = (st * 32767).astype('<i2')
    wav = os.path.join(dest, 'loadpage_loop.wav')
    with wave.open(wav, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
    m4a = os.path.join(dest, 'loadpage_loop.m4a')
    if os.path.exists(m4a): os.remove(m4a)
    subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '96000', '-q', '127', '-s', '2', wav, m4a], check=True)
    os.remove(wav)
    print('loadpage_loop.m4a %.1f s %.0f KB' % (DUR, os.path.getsize(m4a) / 1024))

if __name__ == '__main__':
    main()
