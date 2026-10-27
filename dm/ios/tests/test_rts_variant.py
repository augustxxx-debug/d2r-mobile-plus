"""光追阴影着色器变体生成器（dm/gfx/tools/rts_variant.py）的三种 MSL 变换契约：级联采样 → min(遮罩, 原采样)、点光立方体采样 → rts_pl、蒙皮顶点 → 写裁剪坐标。
用合成的 spirv-cross 形状的 MSL 核，不碰真实着色器缓存、不编译。
| The three MSL transformation contracts of the ray-traced shadow variant generator (dm/gfx/tools/rts_variant.py): cascade sample → min(mask, original), point-light cube sample → rts_pl, skinned vertex → clip position export.
Synthetic spirv-cross-shaped MSL, no real shader cache, no compilation."""
import importlib
import json
import os
import re
import sys
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
