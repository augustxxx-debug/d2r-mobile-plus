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
SRC = OUT = STAGE = None; WANT = set()   # STAGE：本次产物先写进的暂存目录（七审 Q7-03：全部成功才搬进 OUT；None = 直接写 OUT，单测用） | STAGE: the staging dir this run writes into first (seventh review Q7-03: moved into OUT only if everything succeeded; None = straight into OUT, for the unit tests)   # 命令行在文件末尾赋值；测试直接调各变换函数 | assigned at the end of the file from the command line; tests call the transformation functions directly
CASCADES = ('fp_t_cb196610_105980A8', 'fp_t_cb196610_105980AA')
VERSION = 'rts_variant 6'   # 改了变换规则就升版本：旧产物全部重出 | bump when the transformation changes: every old product is regenerated
SKIN_KEYS = ('0fa4eb7559f1637a', '1d97d33ab66abcba', '3deaf68fd65b5298', '4dffa355f2fbcdaf', '54c93fa22abff9ee', '64dd7d022f3fae39', '67e2ebc28c830f44', 'add86ee8e20f89b9', 'b3ae9be5cc670f35', 'fbf6dc1873a449e9')
def fresh(key, suffix, ver=None):   # 产物已有且版本一致 | product present with the same version
    try:
        with open(os.path.join(OUT, key + suffix + '.json')) as f: return (ver or VERSION) in json.load(f).get('notes', [])
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
# 镂空遮挡体进光追（第四阶段）：阴影通道的镂空绘制 = 顶点着色器出插值量 + 片元着色器按贴图 alpha / 抖动淡出丢弃。光追里在候选命中处复现它：
#   顶点侧 <键>_var：入口末尾把裁剪坐标和每个浮点插值输出写到 rts_var[(vertex_id − base_vertex) × 步长 + k]（k = 0 裁剪坐标，k = 1… 按 main0_out 声明顺序），json 记 rts_var_locs（各输出的 locn）；
#   片元侧：rts_alpha.metal 把每个镂空片元着色器原样放进自己的命名空间，入口改成 bool 函数（discard_fragment → false、return → true、采样补 level(RTS_ALPHA_LOD)），
#   再出一个按序号分派的 rts_alpha_eval(序号, 按 locn 排的插值量, 参数块快照, gl_FragCoord)；翻译层运行时把它和追踪核一起编译。
# Alpha-tested occluders in the ray tracing (phase 4): an alpha-tested shadow draw = the vertex shader's interpolants + the fragment shader discarding by texture alpha / dither fade. The ray tracing replays it at each candidate hit:
#   vertex side <key>_var: at the end of the entry the clip position and every float interpolant are written to rts_var[(vertex_id - base_vertex) * stride + k] (k = 0 clip position, k = 1... in main0_out declaration order); the json records rts_var_locs (the locn of each output);
#   fragment side: rts_alpha.metal puts every alpha-tested fragment shader verbatim into its own namespace with the entry turned into a bool function (discard_fragment → false, return → true, samples get level(RTS_ALPHA_LOD)),
#   plus rts_alpha_eval(index, interpolants by locn, argument-block snapshot, gl_FragCoord) dispatching by index; the translation layer compiles it together with the trace kernels at run time.
VAR_VERSION = 'rts_var 2'
VAR_MAX_OUT = 4   # 只给插值输出 ≤ 4 个的顶点着色器出 _var（阴影通道的都是这类；主通道的 10+ 个输出不需要） | _var only for vertex shaders with ≤ 4 interpolant outputs (the shadow-pass ones; main-pass shaders with 10+ outputs are not needed)
FLOATV = {'float': 'float4(%s, 0.0, 0.0, 0.0)', 'float2': 'float4(%s, 0.0, 0.0)', 'float3': 'float4(%s, 0.0)', 'float4': '%s'}
def entry_params(msl, kind):   # 入口 "<kind> <ret> main0(" 的参数表区间 (左括号, 右括号) | the parameter list span (open, close) of the "<kind> <ret> main0(" entry
    k = re.search(r'\n%s [\w:<>, ]+ main0\(' % kind, msl)
    if not k: raise ValueError('没找到 %s 入口' % kind)
    o = k.end() - 1; return k, o, strip_call(msl, o)
def var_outputs(msl):   # main0_out 里的浮点插值输出 [(成员名, 类型, locn)]；非浮点 / flat 的不导出 | float interpolant outputs of main0_out [(member, type, locn)]; non-float / flat ones are not exported
    m = re.search(r'struct main0_out\n\{\n(.*?)\n\};', msl, re.S)
    if not m: raise ValueError('没找到 main0_out')
    outs = []
    for t, nm, loc, rest in re.findall(r'^\s*(\w+) (\w+) \[\[user\(locn(\d+)\)([^\]]*)\]\];', m.group(1), re.M):
        if t in FLOATV and 'flat' not in rest: outs.append((nm, t, int(loc)))
    return outs
def transform_var(msl):   # 顶点：写裁剪坐标 + 浮点插值输出到 rts_var；返回 (msl, arg_id, locs) | vertex: write the clip position + float interpolants into rts_var; returns (msl, arg_id, locs)
    outs = var_outputs(msl); stride = 1 + len(outs)
    msl, aid, m = add_binding(msl)
    msl = msl[:m.end(1)] + '\n    device float4* rts_var [[id(%d)]];' % aid + msl[m.end(1):]
    k, o, c = entry_params(msl, 'vertex'); params = msl[o + 1:c]
    vid = re.search(r'uint (\w+) \[\[vertex_id\]\]', params); bv = re.search(r'uint (\w+) \[\[base_vertex\]\]', params)
    extra = ([] if vid else ['uint rts_vid [[vertex_id]]']) + ([] if bv else ['uint rts_bv [[base_vertex]]'])
    vid = vid.group(1) if vid else 'rts_vid'; bv = bv.group(1) if bv else 'rts_bv'
    msl = msl[:k.start()] + '\nvertex void main0(' + params + ''.join(', ' + e for e in extra) + msl[c:]   # 关光栅化要求 void 返回（同 _pos） | rasterization off needs a void return (as _pos)
    i = msl.rfind('    return out;\n}')
    if i < 0: raise ValueError('没找到入口末尾的 return out')
    w = '    uint rts_i = (%s - %s) * %du;\n    spvDescriptorSet0.rts_var[rts_i] = out.gl_Position;\n' % (vid, bv, stride)
    for n, (nm, t, loc) in enumerate(outs): w += '    spvDescriptorSet0.rts_var[rts_i + %d] = %s;\n' % (n + 1, FLOATV[t] % ('out.' + nm))
    msl = msl[:i] + w + '    return;\n}' + msl[i + len('    return out;\n}'):]
    if msl.count('return out;'): raise ValueError('入口中途还有 return out')
    return msl, aid, [loc for _, _, loc in outs]
ALPHA_BAN = ('dfdx', 'dfdy', 'fwidth', '[[front_facing]]', '[[sample_id]]', '[[sample_mask]]', '[[depth(', '[[color(', 'simd_', 'quad_', 'threadgroup', '.sample_compare(')
TEX_COORD_ARGS = {'texture2d': 2, 'texture3d': 2, 'texturecube': 2, 'texture2d_array': 3, 'texturecube_array': 3, 'depth2d': 2, 'depth2d_array': 3}   # sample(采样器, 坐标[, 层]) 之后插 level | level goes after sample(sampler, coord[, layer])
def split_args(s):   # 顶层逗号切参数 | split arguments at top-level commas
    out, d, cur = [], 0, ''
    for ch in s:
        if ch in '([': d += 1
        elif ch in ')]': d -= 1
        if ch == ',' and d == 0: out.append(cur); cur = ''
        else: cur += ch
    return out + [cur]
def alpha_fn(key, msl):   # 镂空片元着色器 → (命名空间源码, 输入 locs)；不支持的抛 ValueError | alpha-tested fragment shader → (namespaced source, input locs); unsupported ones raise ValueError
    if 'discard_fragment()' not in msl: raise ValueError('不丢弃')
    for b in ALPHA_BAN:
        if b in msl: raise ValueError('含 %s' % b.strip())
    k, o, c = entry_params(msl, 'fragment')
    if not msl[k.start():o].startswith('\nfragment void main0'): raise ValueError('入口不是 void')
    params = msl[o + 1:c]
    for p in split_args(params):
        p = p.strip()
        if not (p.startswith('main0_in in [[stage_in]]') or p.startswith('constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]]') or p == 'float4 gl_FragCoord [[position]]'): raise ValueError('入口参数 %s' % p)
    u = msl.rfind('using namespace metal;'); pre = msl[u + len('using namespace metal;'):k.start()] if u >= 0 else ''
    pre = '\n'.join(l for l in pre.split('\n') if not l.startswith(('#include', '#pragma')))
    locs = []
    mi = re.search(r'struct main0_in\n\{\n(.*?)\n\};', pre, re.S)
    fill = []
    if mi:
        mem = []
        for line in mi.group(1).split('\n'):
            g = re.match(r'\s*(\w+) (\w+) \[\[user\(locn(\d+)\)([^\]]*)\]\];', line)
            if not g or g.group(1) not in FLOATV or 'flat' in g.group(4): raise ValueError('输入 %s' % line.strip())
            t, nm, loc = g.group(1), g.group(2), int(g.group(3)); locs.append(loc); mem.append('    %s %s;' % (t, nm))
            fill.append('in.%s = rts_vl(r, ix, b, %d)%s;' % (nm, loc, {'float': '.x', 'float2': '.xy', 'float3': '.xyz', 'float4': ''}[t]))
        pre = pre[:mi.start()] + 'struct main0_in\n{\n' + '\n'.join(mem) + '\n};' + pre[mi.end():]
    else: pre += '\nstruct main0_in { int rts_unused; };\n'
    texs = dict((nm, t) for t, nm in re.findall(r'(\w+)<\w+> (\w+) \[\[id\(\d+\)\]\];', pre))
    if 'struct spvDescriptorSetBuffer0' not in pre: pre += '\nstruct spvDescriptorSetBuffer0 { int rts_unused; };\n'
    body = msl[c + 1:]; bo = body.index('{'); bc = strip_braces(body, bo); body = body[bo + 1:bc]
    # 采样补 level(RTS_ALPHA_LOD)：计算核里没有隐式 LOD | samples get level(0): there is no implicit LOD in a compute kernel
    pos = 0
    while True:
        g = re.compile(r'spvDescriptorSet0\.(\w+)\.sample\(').search(body, pos)
        if not g: break
        o2 = g.end() - 1; c2 = strip_call(body, o2); args = split_args(body[o2 + 1:c2]); t = texs.get(g.group(1))
        if t not in TEX_COORD_ARGS: raise ValueError('采样 %s 的贴图类型 %s' % (g.group(1), t))
        if any(a.strip().startswith(('level(', 'gradient')) for a in args): pos = c2; continue
        args = [a for a in args if not a.strip().startswith('bias(')]
        n = TEX_COORD_ARGS[t]; args = args[:n] + [' level(RTS_ALPHA_LOD)'] + args[n:]   # 宏在判定库公共头里（缺省 3，翻译层可按 DM_RT_ALPHA_LOD 改） | the macro lives in the library's common head (3 by default; the translation layer may override it from DM_RT_ALPHA_LOD)
        rep = ','.join(args); body = body[:o2 + 1] + rep + body[c2:]; pos = o2 + 1 + len(rep)
    body = body.replace('discard_fragment();', 'return false;').replace('return;', 'return true;')
    ns = 'rtsa_' + key
    src = 'namespace %s {\n%s\nstatic inline bool main0(thread const main0_in& in, constant spvDescriptorSetBuffer0& spvDescriptorSet0, float4 gl_FragCoord)\n{%s    return true;\n}\n}\n' % (ns, pre.strip('\n'), body)
    return src, locs, fill
def strip_braces(s, start):   # start 指向 "{"；返回配对的 "}" 下标 | start points at "{"; returns the index of the matching "}"
    d = 0
    for i in range(start, len(s)):
        if s[i] == '{': d += 1
        elif s[i] == '}':
            d -= 1
            if d == 0: return i
    raise ValueError('花括号不配对')
def alpha_head():   # 判定库公共头：从 nvn_rtshadow_msl.h 的 k_rts_alpha_head 原样抽出（翻译层运行时拼的是同一份） | the library's common head: extracted verbatim from k_rts_alpha_head in nvn_rtshadow_msl.h (the translation layer prepends the very same text at run time)
    import ast
    h = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../nvn/nvn_rtshadow_msl.h'), encoding='utf-8').read()
    i = h.index('static const char *k_rts_alpha_head ='); j = h.index(';\n', i)
    return ''.join(ast.literal_eval(q) for q in re.findall(r'^"(?:[^"\\]|\\.)*"', h[i:j], re.M))
ALPHA_HEAD = alpha_head()
def alpha_lib(items):   # items = [(键, 命名空间源码, locs, 填充语句)] → rts_alpha.metal 源码 | items = [(key, namespaced source, locs, fill statements)] → rts_alpha.metal source
    out = ['// 光追镂空遮挡体判定库（%s 生成，勿手改）：序号 = rts_alpha.json 的 fs 下标；RtsAI / rts_vl 由翻译层拼在前面（nvn_rtshadow_msl.h k_rts_alpha_head） | ray-traced alpha-tested occluder library (generated by %s, do not edit): index = position in rts_alpha.json fs; RtsAI / rts_vl are prepended by the translation layer (k_rts_alpha_head in nvn_rtshadow_msl.h)\n' % (VAR_VERSION, VAR_VERSION), '#include <metal_stdlib>\nusing namespace metal;\n']
    out += [s for _, s, _, _ in items]
    out.append('static inline bool rts_alpha_eval(constant RtsAI &r, uint3 ix, float2 b, float4 fc)\n{\n    switch (r.fs)\n    {\n')
    for i, (key, _, _, fill) in enumerate(items):
        out.append('    case %d: { rtsa_%s::main0_in in; %s return rtsa_%s::main0(in, *(constant rtsa_%s::spvDescriptorSetBuffer0 *)r.args, fc); }\n' % (i, key, ' '.join(fill), key, key))
    out.append('    default: return true;\n    }\n}\n')
    return ''.join(out)
ALPHA_PROBE = '\nkernel void rts_alpha_probe(constant RtsAI *r [[buffer(0)]], device uint *o [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = rts_alpha_eval(r[i], uint3(i), float2(0.25), float4(i)); }\n'   # 编译检查用：让每个分支都真正生成代码 | for the compile check: makes every branch actually generate code
def compile_check(src):   # 编得过返回 None，否则返回错误文本 | None when it compiles, else the error text
    with tempfile.TemporaryDirectory() as td:
        mp, ap = os.path.join(td, 'a.metal'), os.path.join(td, 'a.air'); open(mp, 'w').write(ALPHA_HEAD + src + ALPHA_PROBE)
        cmd = (['xcrun', '-sdk', 'macosx', 'metal', '-c'] if MAC else list(ios_metal.METAL)) + [mp, '-o', ap]
        r = subprocess.run(cmd, capture_output=True, text=True)
        return None if r.returncode == 0 else (r.stderr or r.stdout)[-600:]
def build_alpha(keys):   # 全部镂空片元着色器 → rts_alpha.metal / .json（单个编不过的剔除，记进 json） | every alpha-tested fragment shader → rts_alpha.metal / .json (ones that fail to compile alone are dropped and recorded in the json)
    items, skipped = [], {}
    for key in keys:
        try:
            j = json.load(open(os.path.join(SRC, key + '_b.json')))
            if j.get('stage') not in ('ps', 'fs'): continue
            msl = open(os.path.join(SRC, key + '_b.metal')).read()
            if 'discard_fragment()' not in msl or '[[color(' in msl: continue
            src, locs, fill = alpha_fn(key, msl)
            if len(locs) > VAR_MAX_OUT: skipped[key] = '输入 %d 个' % len(locs); continue
            items.append((key, src, locs, fill))
        except Exception as e: skipped[key] = str(e)[:80]
    def ok(it): return it, compile_check(alpha_lib([it]))
    with cf.ThreadPoolExecutor(8) as ex: res = list(ex.map(ok, items))
    good = [it for it, err in res if not err]; skipped.update({it[0]: '编不过 ' + err.split('\n')[0][:80] for it, err in res if err})
    src = alpha_lib(good); err = compile_check(src)
    if err: raise RuntimeError('rts_alpha.metal 整体编不过：' + err)
    d = STAGE or OUT; open(os.path.join(d, 'rts_alpha.metal'), 'w').write(src)
    json.dump({'version': VAR_VERSION, 'fs': [{'key': k, 'locs': l} for k, _, l, _ in good], 'skipped': skipped}, open(os.path.join(d, 'rts_alpha.json'), 'w'), indent=1, ensure_ascii=False)
    return len(good), len(skipped)
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
RTS_PL_FN = '''static inline float rts_pl(float o, int L, float4 fc, texture2d<float> pm, int4 idx)   // 光追点光遮罩：本盏光在挑中的 4 盏里就用遮罩（翻译层按深度上采样到全分辨率），否则原值 | ray-traced point-light mask: this light among the chosen 4 uses the mask (upsampled to full resolution by depth in the translation layer), otherwise the original
{
    float4 m = pm.read(uint2(fc.xy));
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
def emit(key, suffix, out, j, binding, note, ver=None):
    j['key'] = key + suffix
    if binding: j['bindings'].append(binding)
    j['chain_metal'] = MAC_RECORD if MAC else ios_metal.RECORD; j['notes'] = [n for n in j.get('notes', []) if not n.startswith(('rts_variant', 'rts_var'))] + [ver or VERSION, note]
    with tempfile.TemporaryDirectory() as td:
        mp, ap = os.path.join(td, key + suffix + '.metal'), os.path.join(td, key + suffix + '.air'); open(mp, 'w').write(out)
        try: (mac_compile if MAC else ios_metal.compile)(mp, ap, os.path.join(STAGE or OUT, key + suffix + '.metallib'))
        except subprocess.CalledProcessError as e: return 'FAIL %s' % e
    d = STAGE or OUT; open(os.path.join(d, key + suffix + '.metal'), 'w').write(out); json.dump(j, open(os.path.join(d, key + suffix + '.json'), 'w'), indent=1, ensure_ascii=False)
    return 'ok'
def one(key):
    j = json.load(open(os.path.join(SRC, key + '_b.json'))); msl = open(os.path.join(SRC, key + '_b.metal')).read()
    if j.get('stage') in ('vs', 'vp', 'vertex'):
        rs = []
        if key in SKIN_KEYS and not fresh(key, '_pos'):
            out, aid = transform_pos(msl)
            r = emit(key, '_pos', out, json.loads(json.dumps(j)), {'msl_name': 'rts_pos', 'msl_type': 'device float4*', 'arg_id': aid, 'kind': 'rtpos', 'reg': -1}, 'rt skinned position export: clip position → rts_pos')
            rs.append(r + (' pos aid %d' % aid if r == 'ok' else ''))
        try: nout = len(var_outputs(msl)) if '\nvertex ' in msl else 99
        except ValueError: nout = 99
        if nout <= VAR_MAX_OUT and not fresh(key, '_var', VAR_VERSION):
            try:
                out, aid, locs = transform_var(msl); jv = json.loads(json.dumps(j)); jv['rts_var_locs'] = locs
                r = emit(key, '_var', out, jv, {'msl_name': 'rts_var', 'msl_type': 'device float4*', 'arg_id': aid, 'kind': 'rtvar', 'reg': -1}, 'rt alpha occluder export: clip position + interpolants %s → rts_var' % locs, VAR_VERSION)
                rs.append(r + (' var aid %d locs %s' % (aid, locs) if r == 'ok' else ''))
            except ValueError as e: rs.append('varskip %s' % e)
        return key, '; '.join(rs) if rs else 'fresh'
    if j.get('stage') not in ('ps', 'fs'): return key, 'skip'
    if not any('spvDescriptorSet0.' + c + '.sample_compare(' in msl for c in CASCADES): return key, 'skip'
    if fresh(key, '_rt'): return key, 'fresh'
    out, n, aid = transform(msl)
    out, np = transform_point(out, aid + 1)
    j['bindings'].append({'msl_name': 'rts_mask', 'msl_type': 'texture2d<float>', 'arg_id': aid, 'kind': 'rtmask', 'reg': -1})
    if np: j['bindings'].append({'msl_name': 'rts_pmask', 'msl_type': 'texture2d<float>', 'arg_id': aid + 1, 'kind': 'rtpmask', 'reg': -1}); j['bindings'].append({'msl_name': 'rts_plidx', 'msl_type': 'constant int4*', 'arg_id': aid + 2, 'kind': 'rtplidx', 'reg': -1})
    r = emit(key, '_rt', out, j, None, 'rt shadow variant: %d cascade sample_compare → rts_mask, %d point-light cube sample → rts_pl' % (n, np))
    return key, r + (' %d sites aid %d point %d' % (n, aid, np) if r == 'ok' else '')
# 七审 Q7-03：必需目标（符合条件的 _rt / _pos / _var、判定库）编不过就整体失败——非零退出、OUT 里什么都不换（出包脚本 run.sh 据此停下）；
# 「不适用 / 剔除」（skip、varskip、判定库里单个编不过而剔除的）不算失败。产物先在 OUT 下的暂存目录生成，全部成功才逐个换进 OUT，换完再核每个 ok 的产物都在、版本对
# Seventh review Q7-03: a required target (an eligible _rt / _pos / _var, the judge library) that fails to compile fails the whole run - non-zero exit, nothing replaced in OUT (the packaging script run.sh stops on it);
# "not applicable / excluded" (skip, varskip, single shaders dropped from the judge library) is not a failure. Products are generated in a staging dir under OUT and moved into OUT one by one only if everything succeeded, then every ok product is checked to be present with the right version
def main(argv):
    global SRC, OUT, STAGE, WANT
    SRC, OUT = argv[1:3]; WANT = set(argv[3:]); os.makedirs(OUT, exist_ok=True)
    keys = sorted(WANT) if WANT else sorted(f[:-8] for f in os.listdir(SRC) if f.endswith('_b.metal'))
    STAGE = tempfile.mkdtemp(prefix='.rts_stage-', dir=OUT)
    try:
        res, fails, made = {}, [], []
        with cf.ThreadPoolExecutor(8) as ex:
            for key, r in ex.map(one, keys):
                res[r.split()[0]] = res.get(r.split()[0], 0) + 1
                if 'FAIL' in r: fails.append((key, r))
                if not r.startswith(('skip', 'fresh')): print(key, r)
        try:
            na, ns = build_alpha(sorted(f[:-8] for f in os.listdir(SRC) if f.endswith('_b.metal')))
            print('镂空判定库 %d 个片元着色器（剔除 %d）' % (na, ns))
        except Exception as e: fails.append(('rts_alpha', 'FAIL %s' % e))
        if fails:
            for key, r in fails: print('【失败】', key, r[:300])
            print('汇总', res, '失败 %d 个，产物没有替换' % len(fails)); return 2
        made = sorted(os.listdir(STAGE))
        for f in made: os.replace(os.path.join(STAGE, f), os.path.join(OUT, f))
        missing = []
        for f in made:
            if f.endswith('.json') and f != 'rts_alpha.json':
                k, suf = f[:-5].rsplit('_', 1)
                if not os.path.exists(os.path.join(OUT, f[:-5] + '.metallib')) or not fresh(k, '_' + suf, VAR_VERSION if suf == 'var' else None): missing.append(f[:-5])
        if missing: print('【失败】产物不全 / 版本不对：', ' '.join(missing[:20])); print('汇总', res, '产物不全 %d 个' % len(missing)); return 3
        print('汇总', res); return 0
    finally:
        for f in os.listdir(STAGE): os.remove(os.path.join(STAGE, f))
        os.rmdir(STAGE); STAGE = None

if __name__ == '__main__':
    sys.exit(main(sys.argv))
