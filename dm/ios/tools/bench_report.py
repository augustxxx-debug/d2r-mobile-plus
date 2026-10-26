#!/usr/bin/env python3
"""功耗基准报告：读 power_bench.sh 拉回的 heat.tsv / boot.txt / params.txt，按渲染分辨率档位分段，给每段的电量下降速率、温控、帧率、GPU、掉帧。
电量只有 1% 分辨率：每段的 %/小时 带 ±(1% ÷ 段时长) 的量化误差，报告里一并给出。
Power benchmark report: reads heat.tsv / boot.txt / params.txt pulled by power_bench.sh, splits by render-resolution level, and gives each segment's battery drain rate,
thermal state, fps, GPU and dropped frames. The battery level has 1% resolution: each segment's %/hour carries a quantization error of ±(1% ÷ segment length), stated in the report."""
import os
import re
import sys


def load_params(d):
    p = {}
    for line in open(os.path.join(d, 'params.txt'), encoding='utf-8'):
        k, _, v = line.rstrip('\n').partition('=')
        p[k] = v
    return p


def load_heat(d):
    rows = []
    for i, line in enumerate(open(os.path.join(d, 'heat.tsv'), encoding='utf-8')):
        if i == 0:
            continue
        c = line.rstrip('\n').split('\t')
        if len(c) < 9:
            continue
        r = {'sec': float(c[0]), 'thermal': int(c[1]), 'cpu': float(c[2]), 'fps': float(c[3]), 'gpu': float(c[4]), 'gpu95': float(c[5]), 'n': int(c[6]), 'bright': float(c[7])}
        if len(c) >= 12:
            r['battery'] = float(c[9]); r['charging'] = c[10]; r['lowpower'] = c[11]
        if len(c) >= 14:
            r['level'] = int(c[12]); r['fx'] = int(c[13])
        rows.append(r)
    return rows


def segments(params):
    seg = int(params['seg_s'])
    levels = [int(x) for x in params['levels'].split(',')]
    return [(lv, i * seg, (i + 1) * seg) for i, lv in enumerate(levels)]


NAMES = {0: '自动 Auto', 1: '省电 Eco', 2: '均衡 Balanced', 3: '原生 Native'}


def dropped_by_segment(d):
    """boot.txt 里「测试 DM_TEST_RENDER_RES：第 N 秒 → 档 lv」行把日志切成段，段内「上屏 至帧」行累计掉帧。 | Split boot.txt by the schedule log lines and sum dropped frames from the present rows in each part."""
    out = []
    cur = None
    try:
        lines = open(os.path.join(d, 'boot.txt'), encoding='utf-8', errors='replace').read().splitlines()
    except FileNotFoundError:
        return out
    for l in lines:
        m = re.search(r'DM_TEST_RENDER_RES：第 (\d+) 秒 → 渲染分辨率档位 (\d)', l)
        if m:
            cur = {'level': int(m.group(2)), 'frames': 0, 'dropped': 0, 'discarded': 0, 'warps': 0, 'spawned': 0}
            out.append(cur)
            continue
        if cur is None:
            continue
        m = re.search(r'上屏 至帧 \d+：(\d+) 帧，掉帧 (\d+)（丢弃 (\d+)）', l)
        if m:
            cur['frames'] += int(m.group(1)); cur['dropped'] += int(m.group(2)); cur['discarded'] += int(m.group(3))
        if 'DM_TEST_WARP_LV：拍' in l:
            cur['warps'] += 1
        m = re.search(r'DM_TEST_SPAWN：.*→ 成 (\d+)', l)
        if m:
            cur['spawned'] += int(m.group(1))
    return out


def main():
    d = sys.argv[1]
    params = load_params(d)
    rows = load_heat(d)
    # heat 的秒从进局计，时间表的秒从宿主初始化计：用日志里第一条时间表切档行对齐不可得，按 heat 第一行当 0 处理，误差 = 进局前的几秒
    # heat seconds count from entering the game while the schedule counts from host init; align on the first heat row, error = the few seconds before entering the game
    segs = segments(params)
    if rows and 'level' in rows[0]:   # 按表里的渲染档列切段（连续相同档位为一段） | split by the render-level column (runs of equal level)
        segs = []; start = None; prev = None
        for r in rows:
            if r['level'] != prev:
                if prev is not None: segs.append((prev, start, r['sec']))
                start, prev = r['sec'], r['level']
        if prev is not None: segs.append((prev, start, rows[-1]['sec'] + 5))
        segs = [s for s in segs if s[2] - s[1] >= 60]
    drops = dropped_by_segment(d)
    print(f"# 功耗基准报告 {params.get('run', '')}\n")
    print(f"每段 {int(params['seg_s']) // 60} 分钟，档位序列 {params['levels']}，MetalFX {params.get('metalfx', 'keep')}，关卡轮换 `{params.get('warp', '')}`，刷怪 `{params.get('spawn', '')}`。\n")
    has_batt = rows and 'battery' in rows[0]
    if not has_batt:
        print("**heat.tsv 没有电量列**（旧包）：只能给温控 / 帧率 / GPU。\n")
    print("| 档位 | 时长 | 电量 起→止 | 下降速率 %/h（±量化误差） | 温控档位分布 | 帧率均值 | GPU 均值 / p95 ms | CPU% 均值 | 掉帧 | 传送/刷怪 |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for i, (lv, a, b) in enumerate(segs):
        part = [r for r in rows if a + 15 <= r['sec'] < b and r['n'] > 0]   # 切档后 15 秒不算 | skip 15 s after the switch
        if not part:
            print(f"| {NAMES.get(lv, lv)} | — | 无样本 | | | | | | | |"); continue
        dur_h = (part[-1]['sec'] - part[0]['sec']) / 3600
        th = {}
        for r in part:
            th[r['thermal']] = th.get(r['thermal'], 0) + 1
        thtxt = ' '.join(f"{k}:{v * 100 // len(part)}%" for k, v in sorted(th.items()))
        fps = sum(r['fps'] for r in part) / len(part)
        gpu = sum(r['gpu'] for r in part) / len(part); gpu95 = sum(r['gpu95'] for r in part) / len(part)
        cpu = sum(r['cpu'] for r in part) / len(part)
        if has_batt and dur_h > 0:
            b0, b1 = part[0]['battery'], part[-1]['battery']
            rate = (b0 - b1) / dur_h; err = 1.0 / dur_h
            charging = {r['charging'] for r in part}
            batt = f"{b0:.0f}%→{b1:.0f}%" + ('' if charging == {'未充电'} else f"（{'/'.join(sorted(charging))}）")
            ratetxt = f"{rate:.1f} ±{err:.1f}"
        else:
            batt, ratetxt = '—', '—'
        dr = drops[i] if i < len(drops) else None
        drtxt = f"{dr['dropped']}/{dr['frames']}（{100 * dr['dropped'] / max(dr['frames'], 1):.2f}%）" if dr else '—'
        ev = f"{dr['warps']}/{dr['spawned']}" if dr else '—'
        print(f"| {NAMES.get(lv, lv)} | {dur_h * 60:.1f} min | {batt} | {ratetxt} | {thtxt} | {fps:.1f} | {gpu:.2f} / {gpu95:.2f} | {cpu:.0f} | {drtxt} | {ev} |")
    print("\n说明：电量按 1% 量化，±误差 = 1% ÷ 段时长；温控分布为各档位占比；GPU 毫秒受 GPU 调频影响，只作参考；掉帧按日志中各段的「上屏」行累计。")
    print("Notes: battery is quantized to 1%, ±error = 1% ÷ segment length; thermal shows the share of each state; GPU ms is affected by GPU DVFS and is indicative only; dropped frames are summed from the present rows in each segment of the log.")


if __name__ == '__main__':
    main()
