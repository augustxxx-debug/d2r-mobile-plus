#!/usr/bin/env python3
# 抓帧 → scene.bin：把 DM_NVN_RTCAP 抓到的一帧（draws.tsv + 各绘制的索引 / 顶点流 / cb0 / cb3 片段）转成世界空间三角形，
# 加上主通道相机的逆视投影与阴影通道推出的太阳方向，交给 rt_proto 建加速结构。
# 用法：cap2scene.py <f<N> 目录> <scene.bin> [--pass shadow|main|all] [--size WxH] [--skin keep|drop]
# 矩阵约定（来自 vs_models_v 的 MSL）：cb0 的 float4[0..3] 是物体→世界 3x4（行向量约定：world = obj · M），cb3 的 float4[21..24] 是视投影（clip = world · VP）。
# 属性 0 = 位置：xyz · exp2(w · 13)（w 为规格化分量），SNORM16x4 / FLOAT4 / FLOAT3 / HALF4 都认。
# Capture → scene.bin: turns one DM_NVN_RTCAP frame (draws.tsv + per-draw index / vertex-stream / cb0 / cb3 slices) into world-space triangles,
# plus the inverse view-projection of the main-pass camera and the sun direction derived from the shadow pass, for rt_proto to build an acceleration structure.
# Usage: cap2scene.py <f<N> dir> <scene.bin> [--pass shadow|main|all] [--size WxH] [--skin keep|drop]
# Matrix convention (from the vs_models_v MSL): cb0 float4[0..3] is the object→world 3x4 (row-vector convention: world = obj · M), cb3 float4[21..24] is the view-projection (clip = world · VP).
# Attribute 0 = position: xyz · exp2(w · 13) (w a normalized component); SNORM16x4 / FLOAT4 / FLOAT3 / HALF4 are recognized.
import sys, os, struct, math, collections
import numpy as np

D = sys.argv[1]; OUT = sys.argv[2]; args = sys.argv[3:]
def opt(k, d): return args[args.index(k) + 1] if k in args else d
PASS = opt('--pass', 'shadow'); SIZE = opt('--size', '1711x1204'); SKIN = opt('--skin', 'keep')
W, H = (int(x) for x in SIZE.split('x'))
rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(D, 'draws.tsv'))]
hdr = rows[0]; rows = [dict(zip(hdr, r)) for r in rows[1:] if len(r) == len(hdr)]
def rd(name):
    p = os.path.join(D, name); return open(p, 'rb').read() if os.path.exists(p) else None
# 位置格式（NVN 顶点格式号 → (numpy dtype, 分量数, 规格化除数)）| position format (NVN vertex format → (numpy dtype, components, normalization divisor))
FMT = {0x2b: ('<i2', 4, 32767.0), 0x2e: ('<f4', 4, 0), 0x22: ('<f4', 3, 0), 0x29: ('<f2', 4, 0), 0x1f: ('<i2', 3, 32767.0), 0x26: ('<i1', 4, 127.0), 0x2d: ('<i2', 4, 0)}
tris = []; stats = collections.Counter(); cam = None; sun = None; nshadow_draw = 0
for r in rows:
    shadow = r['shadow'] == '1'; main = r['ncol'] == '1' and r['cfmt0'] != '0' and r['dfmt'] != '0' and int(r['tw']) >= 500 and r['tw'] != r['th']
    cb3 = rd('d%s_cb3.bin' % r['idx'])
    if cb3 and len(cb3) >= 400:
        vp = np.frombuffer(cb3[336:400], dtype='<f4').reshape(4, 4).astype(np.float64)   # 行向量约定 | row-vector convention
        if main and cam is None and abs(vp).sum() > 0: cam = vp
        if shadow and sun is None and abs(vp).sum() > 0:
            d = vp[:3, 2]; n = np.linalg.norm(d)
            if n > 0: sun = d / n if d[1] > 0 else -d / n   # 光源视投影的 z 列就是光线方向；取指向天上（y > 0）的那一侧为太阳方向 | the z column of the light VP is the light axis; take the side pointing up (y > 0) as the sun direction
    if PASS == 'shadow' and not shadow: continue
    if PASS == 'main' and not main: continue
    if r['prim'] not in ('4', '5'): stats['skip_prim%s' % r['prim']] += 1; continue
    fmt = int(r['a0fmt'], 16)
    if fmt not in FMT: stats['skip_fmt%#x' % fmt] += 1; continue
    vb = rd('d%s_vb.bin' % r['idx']); cb0 = rd('d%s_cb0.bin' % r['idx'])
    if vb is None or cb0 is None or len(cb0) < 64: stats['skip_nodata'] += 1; continue
    dt, nc, div = FMT[fmt]; stride = int(r['stride']); off = int(r['a0off']); vmin = int(r['vmin']); vmax = int(r['vmax'])
    nv = vmax - vmin + 1
    if nv <= 0 or len(vb) < nv * stride - (stride - off - nc * np.dtype(dt).itemsize): stats['skip_short'] += 1; continue
    raw = np.frombuffer(vb, dtype=np.uint8)
    idxs = (np.arange(nv) * stride + off)[:, None] + np.arange(nc * np.dtype(dt).itemsize)[None, :]
    if idxs.max() >= len(raw): stats['skip_short'] += 1; continue
    P = raw[idxs].copy().view(dt).reshape(nv, nc).astype(np.float64)
    if div: P /= div
    if nc == 4: P = P[:, :3] * np.exp2(P[:, 3:4] * 13.0)
    M = np.frombuffer(cb0[:64], dtype='<f4').reshape(4, 4).astype(np.float64)   # 物体→世界 3x4（第 4 列未用）| object→world 3x4 (4th column unused)
    Wp = P @ M[:3, :3] + M[3, :3]
    if r['mode'] == 'I':
        ib = rd('d%s_ib.bin' % r['idx']); isz = 2 if r['itype'] == '1' else 4
        if ib is None: stats['skip_noib'] += 1; continue
        I = np.frombuffer(ib, dtype='<u2' if isz == 2 else '<u4').astype(np.int64) + int(r['base_vertex']) - vmin
    else:
        I = np.arange(int(r['count']))
    if r['prim'] == '4': T = I[:len(I) // 3 * 3].reshape(-1, 3)
    else:
        T = np.stack([I[:-2], I[1:-1], I[2:]], 1); T = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])]   # 条带（退化三角形剔除）| strip (degenerate triangles dropped)
    T = T[(T >= 0).all(1) & (T < nv).all(1)]
    if not len(T): stats['skip_empty'] += 1; continue
    tris.append(Wp[T].reshape(-1, 9).astype(np.float32)); stats['draws'] += 1; stats['inst>1'] += int(r['inst']) > 1
    if shadow: nshadow_draw += 1
A = np.concatenate(tris) if tris else np.zeros((0, 9), np.float32)
print('绘制 %d（含实例绘制 %d，只取实例 0）；三角形 %d；跳过 %s' % (stats['draws'], stats['inst>1'], len(A), {k: v for k, v in stats.items() if k.startswith('skip')}))
if cam is None: sys.exit('【红】没找到主通道相机（cb3 +336）')
if sun is None: print('【黄】没找到阴影通道：太阳方向用缺省 (0.3, 1, 0.3)'); sun = np.array([0.3, 1, 0.3]); sun /= np.linalg.norm(sun)
lo, hi = A.reshape(-1, 3).min(0), A.reshape(-1, 3).max(0)
print('世界包围盒 %s ~ %s；相机 VP 第 4 行 %s（w 行全 0,0,0,1 = 正交）；太阳方向 %s' % (lo, hi, cam[:, 3], sun))
# 列主序 4x4（clip = M · world）：M = VP 的转置；再求逆 | column-major 4x4 (clip = M · world): M = transpose of VP; then invert
Mc = cam.T; inv = np.linalg.inv(Mc)
with open(OUT, 'wb') as f:
    f.write(struct.pack('<IIIIII', 0x54524d44, 1, len(A), W, H, stats['draws']))
    f.write(inv.T.astype('<f4').tobytes())   # simd 列主序：按列写出 | simd column-major: written column by column
    f.write(np.asarray(sun, '<f4').tobytes()); f.write(A.tobytes())
print('已写 %s（%d 三角形，%.1f MiB）' % (OUT, len(A), os.path.getsize(OUT) / 1048576))
