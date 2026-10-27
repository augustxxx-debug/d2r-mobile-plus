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
    assert 'uint rts_vid [[vertex_id]], uint rts_bv [[base_vertex]])' in out
    body = out[out.index('vertex main0_out main0('):]
    assert body.rstrip().endswith('spvDescriptorSet0.rts_pos[rts_vid - rts_bv] = out.gl_Position;\n    return out;\n}')
    assert body.count('return out;') == 1


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
