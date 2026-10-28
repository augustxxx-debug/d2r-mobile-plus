"""光追阴影着色器变体生成器（dm/gfx/tools/rts_variant.py）的三种 MSL 变换契约：级联采样 → min(遮罩, 原采样)、点光立方体采样 → rts_pl、蒙皮顶点 → 写裁剪坐标。
用合成的 spirv-cross 形状的 MSL 核，不碰真实着色器缓存、不编译。
| The three MSL transformation contracts of the ray-traced shadow variant generator (dm/gfx/tools/rts_variant.py): cascade sample → min(mask, original), point-light cube sample → rts_pl, skinned vertex → clip position export.
Synthetic spirv-cross-shaped MSL, no real shader cache, no compilation."""
import importlib
import inspect
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'gfx/tools'))
rv = importlib.import_module('rts_variant')

FS = '''#include <metal_stdlib>
using namespace metal;
struct _16 { float4 _m0[1]; };
struct spvDescriptorSetBuffer0
{
    constant _16* fp_c1 [[id(0)]];
    texture_buffer<float> fp_t_tcb_6A [[id(1)]];
    depth2d_array<float> fp_t_cb196610_105980A8 [[id(2)]];
    sampler fp_t_cb196610_105980A8Smplr [[id(3)]];
    depth2d_array<float> fp_t_cb196610_105980AA [[id(4)]];
    sampler fp_t_cb196610_105980AASmplr [[id(5)]];
    depthcube<float> fp_t_cb196610_105A009C [[id(6)]];
    sampler fp_t_cb196610_105A009CSmplr [[id(7)]];
};
struct main0_out { float4 c [[color(0)]]; };
fragment main0_out main0(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])
{
    main0_out out = {};
    float _10 = spvDescriptorSet0.fp_t_cb196610_105980A8.sample_compare(spvDescriptorSet0.fp_t_cb196610_105980A8Smplr, float2(0.5), uint(rint(1.0)), 0.5);
    float _11 = spvDescriptorSet0.fp_t_cb196610_105980AA.sample_compare(spvDescriptorSet0.fp_t_cb196610_105980AASmplr, select(float2(0.5), float2(0.0), isnan(float2(0.5))), uint(0), 0.25);
    float _5862 = as_type<float>(7);
    float3 _5886 = spvDescriptorSet0.fp_t_tcb_6A.read(uint(as_type<int>(_5862))).xyz;
    float _5976;
    if (_5886.x > 0.0)
    {
        float3 _16702 = float3(0.0, 1.0, 0.0);
        float _5974 = spvDescriptorSet0.fp_t_cb196610_105A009C.sample_compare(spvDescriptorSet0.fp_t_cb196610_105A009CSmplr, select(_16702, float3(0.0), isnan(_16702)), 0.5);
        _5976 = _5974;
    }
    else
    {
        _5976 = 1.0;
    }
    out.c = float4(_10 * _11 * _5976);
    return out;
}
'''

VS = '''#include <metal_stdlib>
using namespace metal;
struct _16 { float4 _m0[1]; };
struct spvDescriptorSetBuffer0
{
    constant _16* vp_c5 [[id(0)]];
    constant _16* vp_c3 [[id(1)]];
};
struct main0_out { float4 gl_Position [[position, invariant]]; };
struct main0_in { float4 a0 [[attribute(0)]]; };
vertex main0_out main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])
{
    main0_out out = {};
    out.gl_Position.x = in.a0.x;
    out.gl_Position.w = 1.0;
    return out;
}
'''


def test_cascade_sample_becomes_min_with_mask():
    out, n, aid = rv.transform(FS)
    assert n == 2 and aid == 8
    assert 'texture2d<float> rts_mask [[id(8)]];' in out
    # 两处级联采样都包进 min(遮罩, 原采样)，原采样原样保留 | both cascade samples are wrapped in min(mask, original), the original kept verbatim
    assert out.count('fast::min(spvDescriptorSet0.rts_mask.read(uint2(gl_FragCoord.xy)).x, spvDescriptorSet0.fp_t_cb196610_105980A') == 2
    assert 'fp_t_cb196610_105980A8Smplr, float2(0.5), uint(rint(1.0)), 0.5))' in out
    assert 'float4 gl_FragCoord [[position]])' in out   # 入口补了 gl_FragCoord | the entry gained gl_FragCoord
    assert out.count('[[position]]') == 1


def test_point_light_cube_sample_goes_through_rts_pl():
    out, n, aid = rv.transform(FS)
    out2, npt = rv.transform_point(out, aid + 1)
    assert npt == 1
    assert 'texture2d<float> rts_pmask [[id(9)]];' in out2 and 'constant int4* rts_plidx [[id(10)]];' in out2
    call = 'rts_pl(%s, as_type<int>(_5862), gl_FragCoord, spvDescriptorSet0.rts_pmask, *spvDescriptorSet0.rts_plidx)'
    assert '_5976 = ' + call % '_5974' + ';' in out2   # 有阴影标志的光：遮罩优先、否则立方体采样 | shadow-flagged light: mask first, else the cube sample
    assert '_5976 = ' + call % '1.0' + ';' in out2     # 无阴影的光：遮罩优先、否则 1.0 | unshadowed light: mask first, else 1.0
    assert out2.index('static inline float rts_pl(') < out2.index('\nfragment ')
    assert 'spvDescriptorSet0.fp_t_cb196610_105A009C.sample_compare(' in out2   # 原立方体采样仍在 | the original cube sample remains


def test_point_light_transform_is_noop_without_cube():
    no_cube = FS.replace('depthcube<float> fp_t_cb196610_105A009C [[id(6)]];', 'texture2d<float> fp_t_other [[id(6)]];').replace('fp_t_cb196610_105A009C.sample_compare(spvDescriptorSet0.fp_t_cb196610_105A009CSmplr, select(_16702, float3(0.0), isnan(_16702)), 0.5)', '1.0')
    out, n, aid = rv.transform(no_cube)
    out2, npt = rv.transform_point(out, aid + 1)
    assert npt == 0 and out2 == out


def test_skinned_vertex_exports_clip_position():
    out, aid = rv.transform_pos(VS)
    assert aid == 2 and 'device float4* rts_pos [[id(2)]];' in out
    assert 'vertex void main0(' in out and 'uint rts_vid [[vertex_id]], uint rts_bv [[base_vertex]])' in out   # 关光栅化要求 void 返回 | rasterization off needs a void return
    body = out[out.index('vertex void main0('):]
    assert body.rstrip().endswith('spvDescriptorSet0.rts_pos[rts_vid - rts_bv] = out.gl_Position;\n    return;\n}')
    assert 'return out;' not in body


def test_skin_key_table_matches_translation_layer():
    # 与 nvn_mtl.m rt_is_skin 的 10 个键同一张表 | the same 10 keys as rt_is_skin in nvn_mtl.m
    src = (root / 'gfx/nvn/nvn_mtl.m').read_text(encoding='utf-8')
    i = src.index('static int rt_is_skin(void)')
    keys = re.findall(r'"([0-9a-f]{16})"', src[i:i + 1200])
    assert sorted(keys) == sorted(rv.SKIN_KEYS) and len(rv.SKIN_KEYS) == 10


def test_fresh_checks_version_note(tmp_path):
    rv.OUT = str(tmp_path)
    (tmp_path / 'k_rt.json').write_text(json.dumps({'notes': [rv.VERSION, 'x']}), encoding='utf-8')
    (tmp_path / 'o_rt.json').write_text(json.dumps({'notes': ['rts_variant 1']}), encoding='utf-8')
    for f in ('k_rt.metallib', 'k_rt.metal', 'o_rt.metallib', 'o_rt.metal'): (tmp_path / f).write_bytes(b'x')
    assert rv.fresh('k', '_rt') and not rv.fresh('o', '_rt') and not rv.fresh('missing', '_rt')


VS_VAR = VS.replace('struct main0_out { float4 gl_Position [[position, invariant]]; };',
                    'struct main0_out\n{\n    float4 m_39 [[user(locn1)]];\n    float2 m_37 [[user(locn2)]];\n    uint4 m_9 [[user(locn5), flat]];\n    float4 gl_Position [[position, invariant]];\n};')

FS_ALPHA = '''#include <metal_stdlib>
using namespace metal;
struct _16 { float4 _m0[4]; };
struct spvDescriptorSetBuffer0
{
    constant _16* fp_c7 [[id(0)]];
    texture2d<float> fp_t_cb196610_1056806E [[id(1)]];
    sampler fp_t_cb196610_1056806ESmplr [[id(2)]];
};
struct main0_in
{
    float4 m_29 [[user(locn2)]];
};
fragment void main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], float4 gl_FragCoord [[position]])
{
    float2 _94 = float2(in.m_29.x * gl_FragCoord.w, in.m_29.y);
    float _56 = spvDescriptorSet0.fp_t_cb196610_1056806E.sample(spvDescriptorSet0.fp_t_cb196610_1056806ESmplr, select(_94, float2(0.0), isnan(_94))).w;
    bool _60 = _56 < (*spvDescriptorSet0.fp_c7)._m0[0].x;
    if (!_60)
    {
        return;
    }
    discard_fragment();
}
'''


def test_vertex_var_exports_clip_position_and_float_interpolants():
    out, aid, locs = rv.transform_var(VS_VAR)
    assert aid == 2 and 'device float4* rts_var [[id(2)]];' in out
    assert locs == [1, 2]   # flat / 非浮点的 locn5 不导出 | the flat / non-float locn5 is not exported
    body = out[out.index('vertex void main0('):]
    assert 'uint rts_i = (rts_vid - rts_bv) * 3u;' in body   # 步长 = 1 + 2 个插值 | stride = 1 + 2 interpolants
    assert 'spvDescriptorSet0.rts_var[rts_i] = out.gl_Position;' in body
    assert 'spvDescriptorSet0.rts_var[rts_i + 1] = out.m_39;' in body and 'spvDescriptorSet0.rts_var[rts_i + 2] = float4(out.m_37, 0.0, 0.0);' in body
    assert body.rstrip().endswith('return;\n}') and 'return out;' not in body


def test_alpha_fragment_becomes_bool_judge():
    src, locs, fill = rv.alpha_fn('k1', FS_ALPHA)
    assert locs == [2] and fill == ['in.m_29 = rts_vl(r, ix, b, 2);']
    assert src.startswith('namespace rtsa_k1 {') and 'static inline bool main0(thread const main0_in& in, constant spvDescriptorSetBuffer0& spvDescriptorSet0, float4 gl_FragCoord)' in src
    assert 'discard_fragment' not in src and 'return false;' in src and 'return true;' in src and 'return;' not in src
    assert ', level(RTS_ALPHA_LOD)).w' in src   # 计算核没有隐式 LOD；mip 级由公共头的宏给 | no implicit LOD in a compute kernel; the mip level comes from the common head's macro
    assert '[[user(' not in src and '#include' not in src   # 输入结构去掉属性；头只在库开头一次 | input attributes stripped; the include only once at the top of the library


def test_alpha_fragment_unsupported_shapes_raise():
    for bad, why in ((FS_ALPHA.replace('discard_fragment();', 'float q = dfdx(_56);'), 'no discard'), (FS_ALPHA.replace('in.m_29.y);', 'dfdx(in.m_29.y));'), 'derivative'),
                     (FS_ALPHA.replace('float4 m_29 [[user(locn2)]];', 'uint4 m_29 [[user(locn2), flat]];'), 'flat input')):
        try: rv.alpha_fn('k2', bad)
        except ValueError: continue
        raise AssertionError('alpha_fn accepted a shader with ' + why)


def test_alpha_library_compiles_and_dispatches(tmp_path):
    item = ('k1',) + rv.alpha_fn('k1', FS_ALPHA)
    lib = rv.alpha_lib([item])
    assert 'case 0: { rtsa_k1::main0_in in; in.m_29 = rts_vl(r, ix, b, 2); return rtsa_k1::main0(in, *(constant rtsa_k1::spvDescriptorSetBuffer0 *)r.args, fc); }' in lib
    assert 'struct RtsAI' in rv.ALPHA_HEAD and 'rts_vl' in rv.ALPHA_HEAD   # 公共头从 nvn_rtshadow_msl.h 抽出 | the common head is extracted from nvn_rtshadow_msl.h
    if os.path.exists('/usr/bin/xcrun'):
        rv.MAC = True
        try: assert rv.compile_check(lib) is None
        finally: rv.MAC = os.environ.get('RTS_TARGET') == 'mac'


# 七审 Q7-03：生成器必需目标编不过要非零退出、什么都不换；出包脚本照退出码停下（命令行真跑，RTS_TARGET=mac 用本机 Metal 编译器）
# Seventh review Q7-03: a required target failing to compile exits non-zero and replaces nothing; the packaging script stops on that exit code (real command-line runs, RTS_TARGET=mac with the local Metal compiler)
GEN = root / 'gfx/tools/rts_variant.py'
KEY = '0000000000000001'


def _cli(src, out):
    if not shutil.which('xcrun'): raise unittest.SkipTest('没有 xcrun（Metal 编译器）')
    return subprocess.run([sys.executable, str(GEN), str(src), str(out)], capture_output=True, text=True, env=dict(os.environ, RTS_TARGET='mac'), timeout=900)


def _src(d, metal):
    d.mkdir(); (d / (KEY + '_b.json')).write_text('{"stage": "fs", "bindings": []}', encoding='utf-8'); (d / (KEY + '_b.metal')).write_text(metal, encoding='utf-8')


def test_cli_compile_failure_exits_nonzero_and_replaces_nothing(tmp_path):
    src, out = tmp_path / 'src', tmp_path / 'out'; _src(src, '#error QA_INTENTIONAL_COMPILE_FAILURE\n' + FS); out.mkdir()
    old = out / (KEY + '_rt.json'); old.write_text(json.dumps({'notes': ['rts_variant 1']}), encoding='utf-8')   # 上一次留下的旧产物 | an old product from a previous run
    r = _cli(src, out)
    assert r.returncode != 0, r.stdout + r.stderr
    assert '【失败】' in r.stdout and '产物没有替换' in r.stdout.strip().splitlines()[-1]
    assert not (out / (KEY + '_rt.metallib')).exists() and json.loads(old.read_text(encoding='utf-8'))['notes'] == ['rts_variant 1']
    assert not [f for f in os.listdir(out) if f.startswith('.rts_stage-')]


def test_cli_success_exits_zero_with_products(tmp_path):
    src, out = tmp_path / 'src', tmp_path / 'out'; _src(src, FS)
    r = _cli(src, out)
    assert r.returncode == 0, r.stdout + r.stderr
    assert (out / (KEY + '_rt.metallib')).exists() and rv.VERSION in json.loads((out / (KEY + '_rt.json')).read_text(encoding='utf-8'))['notes']
    assert r.stdout.strip().splitlines()[-1].startswith('汇总') and not [f for f in os.listdir(out) if f.startswith('.rts_stage-')]


def test_run_sh_stops_when_generator_fails():
    run = (root / 'ios/run.sh').read_text(encoding='utf-8')
    line = next(l for l in run.splitlines() if 'rts_variant.py' in l and 'python3' in l)
    assert '|| {' in line and 'exit 1' in line   # 生成器非零退出 → 出包停下 | generator non-zero exit → packaging stops


# 八审 Q8-02：json 在、版本对不等于产物齐全——缺了 metallib 的旧产物要重出（或非零退出），不能报 fresh、退出 0 放过；
# 最终核对覆盖全部应有产物（含 fresh 的），搬运时 json 最后落盘（它是「产物有效」的标记），出包日志按 metallib 计数，遗留暂存目录开头就清掉（观察 1）
# Eighth review Q8-02: a json with the right version does not mean the product is complete - an old product missing its metallib is regenerated (or the run exits non-zero), never reported fresh with exit 0;
# the final check covers every expected product (fresh ones included), the json lands last when moving (it marks the product valid), the packaging log counts metallibs, and leftover staging dirs are removed at the start (observation 1)
def test_cli_rebuilds_product_whose_metallib_is_missing(tmp_path):
    src, out = tmp_path / 'src', tmp_path / 'out'; _src(src, FS)
    assert _cli(src, out).returncode == 0
    lib = out / (KEY + '_rt.metallib'); lib.unlink(); assert (out / (KEY + '_rt.json')).exists()
    r = _cli(src, out)
    assert r.returncode != 0 or lib.exists(), '缺 metallib 却退出 0、没补：' + r.stdout + r.stderr
    assert lib.exists() and "'ok': 1" in r.stdout.strip().splitlines()[-1], r.stdout + r.stderr


def test_fresh_requires_metallib_and_metal(tmp_path):
    rv.OUT = str(tmp_path)
    (tmp_path / 'k_rt.json').write_text(json.dumps({'notes': [rv.VERSION]}), encoding='utf-8')
    assert not rv.fresh('k', '_rt')   # 只有 json | json only
    (tmp_path / 'k_rt.metallib').write_bytes(b'x'); assert not rv.fresh('k', '_rt')   # 缺 .metal | .metal missing
    (tmp_path / 'k_rt.metal').write_text('x', encoding='utf-8'); assert rv.fresh('k', '_rt')


def test_final_check_covers_fresh_products(tmp_path):
    rv.OUT = str(tmp_path)
    for f in ('a_rt.json', 'a_rt.metal', 'a_rt.metallib'): (tmp_path / f).write_text(json.dumps({'notes': [rv.VERSION]}), encoding='utf-8')
    (tmp_path / 'b_rt.json').write_text(json.dumps({'notes': [rv.VERSION]}), encoding='utf-8')   # 上一次就留下的半个产物 | half a product left over from an earlier run
    (tmp_path / 'rts_alpha.metal').write_text('x', encoding='utf-8'); (tmp_path / 'rts_alpha.json').write_text(json.dumps({'version': rv.VAR_VERSION}), encoding='utf-8')
    assert rv.missing_products([('a', '_rt'), ('b', '_rt')]) == ['b_rt']
    (tmp_path / 'rts_alpha.metal').unlink(); assert rv.missing_products([('a', '_rt')]) == ['rts_alpha']


def test_move_order_puts_json_last():
    files = ['k_rt.json', 'k_rt.metal', 'k_rt.metallib', 'rts_alpha.json', 'rts_alpha.metal', 'a_pos.json', 'a_pos.metallib', 'a_pos.metal']
    order = rv.move_order(files)
    assert sorted(order) == sorted(files)
    for f in files:
        if f.endswith('.json'): assert all(order.index(f) > order.index(g) for g in files if g.rsplit('.', 1)[0] == f.rsplit('.', 1)[0] and g != f), order


def test_cli_removes_leftover_staging_dirs(tmp_path):
    src, out = tmp_path / 'src', tmp_path / 'out'; _src(src, FS); left = out / '.rts_stage-killed'; left.mkdir(parents=True); (left / 'x_rt.json').write_text('{}', encoding='utf-8')
    r = _cli(src, out)
    assert r.returncode == 0 and not [f for f in os.listdir(out) if f.startswith('.rts_stage-')], r.stdout + r.stderr


def test_run_sh_counts_metallibs():
    run = (root / 'ios/run.sh').read_text(encoding='utf-8')
    line = next(l for l in run.splitlines() if '光追变体：' in l)
    assert "_rt.metallib$'" in line and "_pos.metallib$'" in line and '_rt.json$' not in line, line


# 原先是 pytest 风格的函数、没有入口，直接跑不执行断言（七审测试接线缺口）：收进 unittest，收集到 0 个也算失败
# These used to be pytest-style functions without an entry point, so running the file executed no assertion (seventh review test-wiring gap): collected into unittest, and collecting 0 counts as a failure
class RtsVariant(unittest.TestCase):
    pass


for _name, _fn in sorted(dict(globals()).items()):
    if _name.startswith('test_') and inspect.isfunction(_fn):
        def _make(fn=_fn):
            def t(self):
                if 'tmp_path' in inspect.signature(fn).parameters:
                    with tempfile.TemporaryDirectory() as d: fn(Path(d))
                else: fn()
            return t
        setattr(RtsVariant, _name, _make())


if __name__ == '__main__':
    result = unittest.main(exit=False, verbosity=2).result
    if result.testsRun == 0: print('一个测试都没收集到'); sys.exit(1)
    sys.exit(0 if result.wasSuccessful() else 1)
