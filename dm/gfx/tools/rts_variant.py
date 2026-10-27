#!/usr/bin/env python3
# 光追阴影：给读级联阴影图的片元着色器出 <键>_rt 变体——把 depth2d_array（fp_t_cb196610_105980A8 / …AA，两级级联）的 sample_compare
# 换成 min(屏幕空间遮罩 rts_mask（R8，3D 尺寸，翻译层每帧算）, 原采样)（原阴影图里只剩镂空遮挡体），参数块末尾加一个贴图绑定（arg_id = 原最大 + 1），反射 json 加 kind "rtmask"。
# 点光立方体阴影（depthcube）不动。原 MSL 来自 Mac 着色器缓存（<缓存>/b-<链>/<键>_b.metal），产物用 dm/ios/tools/ios_metal.compile 出 iphoneos metallib。
# 第二阶段再出蒙皮顶点着色器的 <键>_pos 变体（SKIN_KEYS 的 10 个，与 nvn_mtl.m rt_is_skin 同一张表）：参数块末尾加一个 device float4* rts_pos（kind "rtpos"），
# 入口末尾把裁剪坐标写到 rts_pos[vertex_id − base_vertex]；翻译层在阴影通道用它跑一遍不光栅化的绘制，拿到蒙皮后的位置再 refit 加速结构。
# 第三阶段·点光阴影：_rt 变体里点光立方体阴影的采样处（每盏光一次 sample_compare，有阴影标志的光才采，其余 1.0）改成先查光追点光遮罩：翻译层每帧挑至多 4 盏光各追一条射线，
# 写半分辨率 RGBA8 遮罩（rts_pmask，kind "rtpmask"），4 盏光的记录下标放 int4（rts_plidx，kind "rtplidx"）；本盏光在其中就用遮罩通道，不在就照原样（立方体阴影 / 1.0）。
# 已有且版本一致的变体跳过（json notes 里记 VERSION），出包脚本每次都能调。
# 用法：rts_variant.py <b-<链> 缓存目录（含 .metal / .json）> <输出目录（b-<链>，与 DM_MSL_IOS 同）> [键…]（不给 = 全部含级联阴影采样的片元着色器 + 10 个蒙皮顶点着色器）
# Ray-traced shadows: emits a <key>_rt variant of every fragment shader that samples the cascaded shadow maps - the sample_compare on the depth2d_array
# (fp_t_cb196610_105980A8 / ...AA, the two cascades) becomes min(screen-space mask rts_mask (R8, 3D size, computed per frame by the translation layer), original sample) (the original shadow maps keep only the alpha-tested occluders);
# one texture binding is appended to the argument block (arg_id = previous max + 1) and the reflection json gets a binding of kind "rtmask". Point-light cube shadows (depthcube) are untouched.
# Source MSL comes from the Mac shader cache (<cache>/b-<chain>/<key>_b.metal); the output is compiled by dm/ios/tools/ios_metal.compile into an iphoneos metallib.
# Phase 2 also emits <key>_pos variants of the skinned vertex shaders (the 10 SKIN_KEYS, the same table as rt_is_skin in nvn_mtl.m): a device float4* rts_pos (kind "rtpos") is appended to the
# argument block and the entry writes its clip position to rts_pos[vertex_id - base_vertex]; the translation layer runs a non-rasterizing draw with it in the shadow pass to get skinned positions, then refits the BLAS.
# Phase 3 · point-light shadows: at the point-light cube-shadow sample of the _rt variants (one sample_compare per light, only for lights with the shadow flag, 1.0 otherwise) the ray-traced point-light mask is consulted first:
# the translation layer picks up to 4 lights per frame and traces one ray per pixel each into a half-resolution RGBA8 mask (rts_pmask, kind "rtpmask"); the 4 record indices go into an int4 (rts_plidx, kind "rtplidx");
# a light among them uses its mask channel, any other keeps the original (cube shadow / 1.0).
# Variants that exist with the same VERSION (recorded in the json notes) are skipped, so the packaging script can call this every time.
# Usage: rts_variant.py <b-<chain> cache dir (with .metal / .json)> <output dir (b-<chain>, same as DM_MSL_IOS)> [key...] (none = every fragment shader that samples the cascades + the 10 skinned vertex shaders)
import os, re, sys, json, tempfile, subprocess, concurrent.futures as cf
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../ios/tools')); import ios_metal
SRC = OUT = None; WANT = set()   # 命令行在文件末尾赋值；测试直接调各变换函数 | assigned at the end of the file from the command line; tests call the transformation functions directly
CASCADES = ('fp_t_cb196610_105980A8', 'fp_t_cb196610_105980AA')
VERSION = 'rts_variant 5'   # 改了变换规则就升版本：旧产物全部重出 | bump when the transformation changes: every old product is regenerated
SKIN_KEYS = ('0fa4eb7559f1637a', '1d97d33ab66abcba', '3deaf68fd65b5298', '4dffa355f2fbcdaf', '54c93fa22abff9ee', '64dd7d022f3fae39', '67e2ebc28c830f44', 'add86ee8e20f89b9', 'b3ae9be5cc670f35', 'fbf6dc1873a449e9')
def fresh(key, suffix):   # 产物已有且版本一致 | product present with the same version
    try: return VERSION in json.load(open(os.path.join(OUT, key + suffix + '.json'))).get('notes', [])
    except Exception: return False
def add_binding(msl):   # 返回 (msl, 新 arg_id)；参数块加一行 | returns (msl, new arg_id); one line added to the argument block
    ids = [int(x) for x in re.findall(r'\[\[id\((\d+)\)\]\]', msl)]; aid = max(ids) + 1
    m = re.search(r'struct spvDescriptorSetBuffer0\n\{\n(.*?)\n\};', msl, re.S)
    if not m: raise ValueError('没找到 spvDescriptorSetBuffer0')
    return msl, aid, m
def transform_pos(msl):   # 顶点：写裁剪坐标到 rts_pos | vertex: write the clip position into rts_pos
    msl, aid, m = add_binding(msl)
    msl = msl[:m.end(1)] + '\n    device float4* rts_pos [[id(%d)]];' % aid + msl[m.end(1):]
    sig = 'vertex main0_out main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])'
    if sig not in msl: raise ValueError('入口签名不是预期的')
    # 关光栅化的管线要求顶点函数返回 void（Mac 实测：否则建管线报错）：入口改成 void，末尾写完导出就返回 | a pipeline with rasterization off needs a void vertex function (measured on the Mac: pipeline creation fails otherwise): the entry returns void and returns right after the export
    msl = msl.replace(sig, 'vertex void main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], uint rts_vid [[vertex_id]], uint rts_bv [[base_vertex]])', 1)
    i = msl.rfind('    return out;\n}')
    if i < 0: raise ValueError('没找到入口末尾的 return out')
    msl = msl[:i] + '    spvDescriptorSet0.rts_pos[rts_vid - rts_bv] = out.gl_Position;\n    return;\n}' + msl[i + len('    return out;\n}'):]
    if msl.count('return out;'): raise ValueError('入口中途还有 return out')
    return msl, aid
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
        pos = 0
        while True:
            i = msl.find(pat, pos)
            if i < 0: break
            j = strip_call(msl, i + len(pat) - 1)
            repl = 'fast::min(spvDescriptorSet0.rts_mask.read(uint2(gl_FragCoord.xy)).x, ' + msl[i:j + 1] + ')'   # 原采样保留：阴影图里只剩镂空遮挡体 | the original sample stays: the shadow map keeps only the alpha-tested occluders
            msl = msl[:i] + repl + msl[j + 1:]; pos = i + len(repl); n += 1   # 替换文本里仍含原采样，要从它后面接着找 | the replacement still contains the original sample, so continue after it
    if not n: return None, 0, -1
    # 参数块：在 struct spvDescriptorSetBuffer0 的 }; 前加一行 | Argument block: add one line before the }; of struct spvDescriptorSetBuffer0
    msl, aid, m = add_binding(msl)
    msl = msl[:m.end(1)] + '\n    texture2d<float> rts_mask [[id(%d)]];' % aid + msl[m.end(1):]
    # 入口要有 gl_FragCoord | the entry needs gl_FragCoord
    if 'gl_FragCoord [[position]]' not in msl:   # 入口签名里有 [[buffer(0)]] 这类括号，按括号配对找签名的右括号 | the signature contains parentheses such as [[buffer(0)]]; find its closing one by matching
        k = msl.find('\nfragment '); k = msl.find('main0(', k) if k >= 0 else -1
        if k < 0: raise ValueError('加不上 gl_FragCoord')
        j = strip_call(msl, k + len('main0'))
        msl = msl[:j] + ', float4 gl_FragCoord [[position]]' + msl[j:]
    return msl, n, aid
RTS_PL_FN = '''static inline float rts_pl(float o, int L, float4 fc, texture2d<float> pm, int4 idx)   // 光追点光遮罩：本盏光在挑中的 4 盏里就用遮罩（半分辨率），否则原值 | ray-traced point-light mask: this light among the chosen 4 uses the mask (half resolution), otherwise the original
{
    float4 m = pm.read(uint2(fc.xy) >> 1);
    return L == idx.x ? m.x : (L == idx.y ? m.y : (L == idx.z ? m.z : (L == idx.w ? m.w : o)));
}
'''
def transform_point(msl, aid):   # 点光：立方体阴影采样 → 先查光追点光遮罩；返回 (msl, 改了几处) | point lights: cube-shadow sample → ray-traced point mask first; returns (msl, sites)
    cubes = re.findall(r'depthcube<float> (\w+) \[\[id', msl)
    if not cubes: return msl, 0
    lines = msl.split('\n'); n = 0; i = 0
    while i < len(lines):
        m = re.match(r'(\s*)float (_\d+) = spvDescriptorSet0\.(\w+)\.sample_compare\(', lines[i])
        if m and m.group(3) in cubes:
            ind, a = m.group(1), m.group(2); m2 = re.match(r'\s*(_\d+) = %s;' % a, lines[i + 1])
            if not m2: raise ValueError('立方体阴影采样后面不是赋值')
            b = m2.group(1); back = '\n'.join(lines[max(0, i - 80):i]); L = re.findall(r'fp_t_tcb_\w+\.read\(uint\(as_type<int>\((_\d+)\)\)\)', back)
            if not L: raise ValueError('找不到光源记录下标')
            call = 'rts_pl(%%s, as_type<int>(%s), gl_FragCoord, spvDescriptorSet0.rts_pmask, *spvDescriptorSet0.rts_plidx)' % L[-1]
            lines[i + 1] = '%s%s = %s;' % (ind, b, call % a)
            for j in range(i + 2, min(i + 12, len(lines))):
                if re.match(r'\s*%s = 1\.0;' % b, lines[j]): lines[j] = lines[j].replace('= 1.0;', '= %s;' % (call % '1.0')); break
            else: raise ValueError('找不到无阴影分支的 1.0')
            n += 1
        i += 1
    if not n: return msl, 0
    msl = '\n'.join(lines)
    m = re.search(r'struct spvDescriptorSetBuffer0\n\{\n(.*?)\n\};', msl, re.S)
    msl = msl[:m.end(1)] + '\n    texture2d<float> rts_pmask [[id(%d)]];\n    constant int4* rts_plidx [[id(%d)]];' % (aid, aid + 1) + msl[m.end(1):]
    k = msl.find('\nfragment '); msl = msl[:k + 1] + RTS_PL_FN + msl[k + 1:]
    return msl, n
MAC = os.environ.get('RTS_TARGET') == 'mac'   # RTS_TARGET=mac：出 Mac 宿主用的 macosx metallib（输出目录 = Mac 着色器库，DM_MSL_DIR/b-<链>） | RTS_TARGET=mac: emit macosx metallibs for the Mac host (output dir = the Mac shader library, DM_MSL_DIR/b-<chain>)
MAC_RECORD = 'xcrun -sdk macosx metal -c; xcrun -sdk macosx metallib'   # 同 dmsh_build.py | same as dmsh_build.py
def mac_compile(msl, air, lib):
    subprocess.run(['xcrun', '-sdk', 'macosx', 'metal', '-c', msl, '-o', air], check=True)
    subprocess.run(['xcrun', '-sdk', 'macosx', 'metallib', air, '-o', lib], check=True)
def emit(key, suffix, out, j, binding, note):
    j['key'] = key + suffix
    if binding: j['bindings'].append(binding)
    j['chain_metal'] = MAC_RECORD if MAC else ios_metal.RECORD; j['notes'] = [n for n in j.get('notes', []) if not n.startswith('rts_variant')] + [VERSION, note]
    with tempfile.TemporaryDirectory() as td:
        mp, ap = os.path.join(td, key + suffix + '.metal'), os.path.join(td, key + suffix + '.air'); open(mp, 'w').write(out)
        try: (mac_compile if MAC else ios_metal.compile)(mp, ap, os.path.join(OUT, key + suffix + '.metallib'))
        except subprocess.CalledProcessError as e: return 'FAIL %s' % e
    open(os.path.join(OUT, key + suffix + '.metal'), 'w').write(out); json.dump(j, open(os.path.join(OUT, key + suffix + '.json'), 'w'), indent=1, ensure_ascii=False)
    return 'ok'
def one(key):
    j = json.load(open(os.path.join(SRC, key + '_b.json'))); msl = open(os.path.join(SRC, key + '_b.metal')).read()
    if j.get('stage') in ('vs', 'vp', 'vertex') and key in SKIN_KEYS:
        if fresh(key, '_pos'): return key, 'fresh'
        out, aid = transform_pos(msl)
        r = emit(key, '_pos', out, j, {'msl_name': 'rts_pos', 'msl_type': 'device float4*', 'arg_id': aid, 'kind': 'rtpos', 'reg': -1}, 'rt skinned position export: clip position → rts_pos')
        return key, r + (' pos aid %d' % aid if r == 'ok' else '')
    if j.get('stage') not in ('ps', 'fs'): return key, 'skip'
    if not any('spvDescriptorSet0.' + c + '.sample_compare(' in msl for c in CASCADES): return key, 'skip'
    if fresh(key, '_rt'): return key, 'fresh'
    out, n, aid = transform(msl)
    out, np = transform_point(out, aid + 1)
    j['bindings'].append({'msl_name': 'rts_mask', 'msl_type': 'texture2d<float>', 'arg_id': aid, 'kind': 'rtmask', 'reg': -1})
    if np: j['bindings'].append({'msl_name': 'rts_pmask', 'msl_type': 'texture2d<float>', 'arg_id': aid + 1, 'kind': 'rtpmask', 'reg': -1}); j['bindings'].append({'msl_name': 'rts_plidx', 'msl_type': 'constant int4*', 'arg_id': aid + 2, 'kind': 'rtplidx', 'reg': -1})
    r = emit(key, '_rt', out, j, None, 'rt shadow variant: %d cascade sample_compare → rts_mask, %d point-light cube sample → rts_pl' % (n, np))
    return key, r + (' %d sites aid %d point %d' % (n, aid, np) if r == 'ok' else '')
if __name__ == '__main__':
    SRC, OUT = sys.argv[1:3]; WANT = set(sys.argv[3:]); os.makedirs(OUT, exist_ok=True)
    keys = sorted(WANT) if WANT else sorted(f[:-8] for f in os.listdir(SRC) if f.endswith('_b.metal'))
    res = {}
    with cf.ThreadPoolExecutor(8) as ex:
        for key, r in ex.map(one, keys): res[r.split()[0]] = res.get(r.split()[0], 0) + 1; (print(key, r) if not r.startswith(('skip', 'fresh')) else None)
    print('汇总', res)
