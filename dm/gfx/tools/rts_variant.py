#!/usr/bin/env python3
# 光追阴影第二阶段：给读级联阴影图的片元着色器出 <键>_rt 变体——把 depth2d_array（fp_t_cb196610_105980A8 / …AA，两级级联）的 sample_compare
# 换成读屏幕空间遮罩 rts_mask（R8，3D 尺寸，翻译层每帧算），参数块末尾加一个贴图绑定（arg_id = 原最大 + 1），反射 json 加 kind "rtmask"。
# 点光立方体阴影（depthcube）不动。原 MSL 来自 Mac 着色器缓存（<缓存>/b-<链>/<键>_b.metal），产物用 dm/ios/tools/ios_metal.compile 出 iphoneos metallib。
# 用法：rts_variant.py <b-<链> 缓存目录（含 .metal / .json）> <输出目录（b-<链>，与 DM_MSL_IOS 同）> [键…]（不给 = 全部含级联阴影采样的片元着色器）
# Ray-traced shadows phase 2: emits a <key>_rt variant of every fragment shader that samples the cascaded shadow maps - the sample_compare on the depth2d_array
# (fp_t_cb196610_105980A8 / ...AA, the two cascades) is replaced by a read of the screen-space mask rts_mask (R8, 3D size, computed per frame by the translation layer);
# one texture binding is appended to the argument block (arg_id = previous max + 1) and the reflection json gets a binding of kind "rtmask". Point-light cube shadows (depthcube) are untouched.
# Source MSL comes from the Mac shader cache (<cache>/b-<chain>/<key>_b.metal); the output is compiled by dm/ios/tools/ios_metal.compile into an iphoneos metallib.
# Usage: rts_variant.py <b-<chain> cache dir (with .metal / .json)> <output dir (b-<chain>, same as DM_MSL_IOS)> [key...] (none = every fragment shader that samples the cascades)
import os, re, sys, json, tempfile, subprocess, concurrent.futures as cf
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../ios/tools')); import ios_metal
SRC, OUT = sys.argv[1:3]; WANT = set(sys.argv[3:]); os.makedirs(OUT, exist_ok=True)
CASCADES = ('fp_t_cb196610_105980A8', 'fp_t_cb196610_105980AA')
def strip_call(s, start):   # start 指向 "sample_compare(" 的左括号；返回右括号下标 | start points at the "(" of "sample_compare("; returns the index of the matching ")"
    depth = 0
    for i in range(start, len(s)):
        if s[i] == '(': depth += 1
        elif s[i] == ')':
            depth -= 1
            if depth == 0: return i
    raise ValueError('括号不配对')
def transform(msl):
    n = 0
    for name in CASCADES:
        pat = 'spvDescriptorSet0.' + name + '.sample_compare('
        while True:
            i = msl.find(pat)
            if i < 0: break
            j = strip_call(msl, i + len(pat) - 1)
            msl = msl[:i] + 'spvDescriptorSet0.rts_mask.read(uint2(gl_FragCoord.xy)).x' + msl[j + 1:]; n += 1
    if not n: return None, 0, -1
    ids = [int(x) for x in re.findall(r'\[\[id\((\d+)\)\]\]', msl)]; aid = max(ids) + 1
    # 参数块：在 struct spvDescriptorSetBuffer0 的 }; 前加一行 | Argument block: add one line before the }; of struct spvDescriptorSetBuffer0
    m = re.search(r'struct spvDescriptorSetBuffer0\n\{\n(.*?)\n\};', msl, re.S)
    if not m: raise ValueError('没找到 spvDescriptorSetBuffer0')
    msl = msl[:m.end(1)] + '\n    texture2d<float> rts_mask [[id(%d)]];' % aid + msl[m.end(1):]
    # 入口要有 gl_FragCoord | the entry needs gl_FragCoord
    if 'gl_FragCoord [[position]]' not in msl:
        msl = re.sub(r'(fragment \w+ main0\([^)]*?)\)\n\{', lambda mm: mm.group(1) + ', float4 gl_FragCoord [[position]])\n{', msl, count=1)
        if 'gl_FragCoord [[position]]' not in msl: raise ValueError('加不上 gl_FragCoord')
    return msl, n, aid
def one(key):
    msl = open(os.path.join(SRC, key + '_b.metal')).read()
    if not any('spvDescriptorSet0.' + c + '.sample_compare(' in msl for c in CASCADES): return key, 'skip'
    out, n, aid = transform(msl)
    j = json.load(open(os.path.join(SRC, key + '_b.json')))
    if j.get('stage') not in ('ps', 'fs'): return key, 'skip'
    j['key'] = key + '_rt'; j['bindings'].append({'msl_name': 'rts_mask', 'msl_type': 'texture2d<float>', 'arg_id': aid, 'kind': 'rtmask', 'reg': -1})
    j['chain_metal'] = ios_metal.RECORD; j.setdefault('notes', []).append('rt shadow variant: %d cascade sample_compare → rts_mask' % n)
    with tempfile.TemporaryDirectory() as td:
        mp, ap = os.path.join(td, key + '_rt.metal'), os.path.join(td, key + '_rt.air'); open(mp, 'w').write(out)
        try: ios_metal.compile(mp, ap, os.path.join(OUT, key + '_rt.metallib'))
        except subprocess.CalledProcessError as e: return key, 'FAIL %s' % e
    open(os.path.join(OUT, key + '_rt.metal'), 'w').write(out); json.dump(j, open(os.path.join(OUT, key + '_rt.json'), 'w'), indent=1, ensure_ascii=False)
    return key, 'ok %d sites aid %d' % (n, aid)
keys = sorted(WANT) if WANT else sorted(f[:-8] for f in os.listdir(SRC) if f.endswith('_b.metal'))
res = {}
with cf.ThreadPoolExecutor(8) as ex:
    for key, r in ex.map(one, keys): res[r.split()[0]] = res.get(r.split()[0], 0) + 1; (print(key, r) if not r.startswith('skip') else None)
print('汇总', res)
