// 光追阴影的 Metal 计算核与合成着色器源（nvn_rtshadow.inc 运行时编译；dm/gfx/tests/t_rtshadow.m 在 Mac 上编译并跑同一份）
// Metal compute kernels and compositing shaders of the ray-traced shadows (compiled at run time by nvn_rtshadow.inc; dm/gfx/tests/t_rtshadow.m compiles and runs the same source on the Mac)
#pragma once
static const char *k_rts_msl =
"#include <metal_stdlib>\n#include <metal_raytracing>\nusing namespace metal; using namespace metal::raytracing;\n"
// 位置解码：fmt 0x2b SNORM16x4、0x2e FLOAT4、0x29 HALF4 都是 xyz · exp2(w · 13)；0x22 FLOAT3 原样 | Position decode
"kernel void rts_decode(device const uchar *src [[buffer(0)]], device packed_float3 *dst [[buffer(1)]], constant uint4 &k [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i >= k.x) return; device const uchar *p = src + (ulong)(k.w + i) * k.y; float4 f;\n"
"  if (k.z == 0x2b) { device const short *s = (device const short *)p; f = float4(s[0], s[1], s[2], s[3]) / 32767.0; }\n"
"  else if (k.z == 0x2e) { device const float *s = (device const float *)p; f = float4(s[0], s[1], s[2], s[3]); }\n"
"  else if (k.z == 0x29) { device const half *s = (device const half *)p; f = float4(s[0], s[1], s[2], s[3]); }\n"
"  else { device const float *s = (device const float *)p; dst[i] = packed_float3(s[0], s[1], s[2]); return; }\n"
"  dst[i] = packed_float3(f.xyz * exp2(f.w * 13.0)); }\n"
// 蒙皮导出的裁剪坐标 → 世界坐标（光源投影是正交的，逆矩阵精确） | skinned clip positions → world (the light projection is orthographic, the inverse is exact)
"struct U { float4x4 inv; uint n; };\n"
"kernel void rts_unproj(device const float4 *src [[buffer(0)]], device packed_float3 *dst [[buffer(1)]], constant U &u [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i >= u.n) return; float4 w = u.inv * src[i]; dst[i] = packed_float3(w.xyz / w.w); }\n"
"struct K { float4x4 invVP; float4 sun; uint2 size; float bias; float pad; };\n"
"kernel void rts_trace(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> mask [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant K &k [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; float z = depth.read(g);\n"
"  if (z >= 1.0) { mask.write(float4(1), g); return; }\n"   // 远平面 = 没画东西（天空） | far plane = nothing drawn (sky)
"  float2 ndc = float2((float(g.x) + 0.5) / float(k.size.x) * 2.0 - 1.0, 1.0 - (float(g.y) + 0.5) / float(k.size.y) * 2.0);\n"
"  float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"  ray r; r.origin = p + k.sun.xyz * k.bias; r.direction = k.sun.xyz; r.min_distance = 0.0; r.max_distance = 20000.0;\n"
"  intersector<instancing> is; is.accept_any_intersection(true); is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"  intersection_result<instancing> h = is.intersect(r, as, 0xffu);\n"
"  mask.write(float4(h.type == intersection_type::none ? 1.0 : 0.0), g); }\n"
// 软阴影：最近命中 + 命中距离（按 maxd 归一）→ RG8；模糊：半径 = 周围 ±2 像素内最远遮挡距离 × 强度（≤ 12 像素），深度差大的邻居权重小 | soft shadows: nearest hit + hit distance (normalized by maxd) → RG8; blur: radius = widest blocker distance within ±2 px × strength (≤ 12 px), neighbours with a large depth difference weigh less
"struct KS { float4x4 invVP; float4 sun; uint2 size; float bias; float maxd; };\n"
"kernel void rts_trace_soft(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> mask [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant KS &k [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; float z = depth.read(g);\n"
"  if (z >= 1.0) { mask.write(float4(1, 0, 0, 0), g); return; }\n"
"  float2 ndc = float2((float(g.x) + 0.5) / float(k.size.x) * 2.0 - 1.0, 1.0 - (float(g.y) + 0.5) / float(k.size.y) * 2.0);\n"
"  float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"  ray r; r.origin = p + k.sun.xyz * k.bias; r.direction = k.sun.xyz; r.min_distance = 0.0; r.max_distance = 20000.0;\n"
"  intersector<instancing> is; is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"  intersection_result<instancing> h = is.intersect(r, as, 0xffu); bool hit = h.type != intersection_type::none;\n"
"  mask.write(float4(hit ? 0.0 : 1.0, hit ? min(h.distance / k.maxd, 1.0) : 0.0, 0, 0), g); }\n"
"struct B { uint2 size; int2 dir; float soft; float pad; };\n"
"kernel void rts_blur(texture2d<float, access::read> src [[texture(0)]], texture2d<float, access::write> dst [[texture(1)]], depth2d<float, access::read> depth [[texture(2)]], constant B &b [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= b.size.x || g.y >= b.size.y) return; float2 c = src.read(g).xy; float z0 = depth.read(g); float dm = c.y;\n"
"  for (int k = -2; k <= 2; k++) { int2 q = clamp(int2(g) + b.dir * k, int2(0), int2(b.size) - 1); dm = max(dm, src.read(uint2(q)).y); }\n"
"  int r = int(clamp(dm * b.soft, 0.0, 12.0)); if (r == 0) { dst.write(float4(c.x, dm, 0, 0), g); return; }\n"
"  float sum = 0.0, wsum = 0.0; for (int k = -r; k <= r; k++) { int2 q = clamp(int2(g) + b.dir * k, int2(0), int2(b.size) - 1); float2 s = src.read(uint2(q)).xy; float z = depth.read(uint2(q)); float wgt = 1.0 / (1.0 + 4000.0 * abs(z - z0)); sum += s.x * wgt; wsum += wgt; }\n"
"  dst.write(float4(sum / wsum, dm, 0, 0), g); }\n"
// 点光（阶段零）：光源记录由 GPU 在本帧、主通道之前读（与片元读的是同一份），选灯、追踪、派生副本都在 GPU 上，CPU 不读游戏内存。
// 记录 = 每盏 4 个 float4 texel：[0] 位置、[1] 颜色、[2].w 阴影标志、[3] 打包参数（按 _rt 变体里原 MSL 的读法）。Picks 与 CPU 侧 rts_picks 布局一致（128 字节）
// Point lights (phase 0): the light records are read by the GPU in this frame before the main pass (the same copy the fragments read); picking, tracing and the derived copy all run on the GPU and the CPU never reads game memory.
// Record = 4 float4 texels per light: [0] position, [1] color, [2].w shadow flag, [3] packed parameters (as the original MSL in the _rt variants reads them). Picks matches the CPU-side rts_picks layout (128 bytes)
"struct Picks { int4 rec; float4 pos[4]; int n; int player; int cand_rec; int pad; float4 cand_off; float4 p0; };\n"
// 选灯（单线程）：得分 = 最大颜色分量 / (到画面中心视线的距离² + 1e4)；上一帧选中的（位置 1 单位内）得分 ×1.5（滞后，免得排序抖动换灯）；另出：离视线最近的候选（给 CPU 认玩家光）、
// 与 CPU 认定的玩家光偏移匹配的记录（派生副本里调弱它）| picking (one thread): score = max color component / (distance² to the screen-center view line + 1e4); last frame's picks (within 1 unit) get ×1.5 (hysteresis so the order does not flip lights);
// also outputs the candidate nearest the view line (for CPU player-light recognition) and the record matching the player-light offset the CPU confirmed (weakened in the derived copy)
"struct PK { float4x4 invVP; uint nrec; uint maxn; uint player_on; float ptol; float4 poff; };\n"
"kernel void rts_pick(device const float4 *L [[buffer(0)]], device Picks &o [[buffer(1)]], constant PK &k [[buffer(2)]], device float4 *prev [[buffer(3)]], uint t [[thread_position_in_grid]]) {\n"
"  if (t) return; float4 a = k.invVP * float4(0, 0, 0.5, 1), b = k.invVP * float4(0, 0, 0.9, 1); float3 p0 = a.xyz / a.w, dir = normalize(b.xyz / b.w - p0);\n"   // 视线取深度 0.5 / 0.9 两点（深度 0 / 1 在本游戏的投影下会落到无穷远） | the view line uses depth 0.5 / 0.9 (depth 0 / 1 lands at infinity with this game's projection)

"  float best[4] = {0, 0, 0, 0}; int bi[4] = {-1, -1, -1, -1}; float cd = 1e30; int cr = -1, pl = -1; float pd = 1e30; uint mx = min(k.maxn, 4u);\n"
"  for (uint r = 0; r < k.nrec; r++) { float3 pos = L[r * 4].xyz, col = L[r * 4 + 1].xyz; float lum = max(col.x, max(col.y, col.z));\n"
"    if (!(lum > 0.0) || any(isnan(pos)) || any(isinf(pos))) continue;\n"
"    float3 v = pos - p0; float3 q = v - dot(v, dir) * dir; float d2 = dot(q, q); if (d2 < cd) { cd = d2; cr = int(r); }\n"
"    if (k.player_on) { float e = length(v - k.poff.xyz); if (e < k.ptol && e < pd) { pd = e; pl = int(r); } }\n"
"    float s = lum / (d2 + 1e4); for (uint j = 0; j < 4; j++) if (prev[j].w > 0.0 && distance(prev[j].xyz, pos) < 1.0) { s *= 1.5; break; }\n"
"    int kk = -1; for (uint j = 0; j < mx; j++) if (bi[j] < 0 || s > best[j]) { kk = int(j); break; }\n"
"    if (kk < 0) continue; for (int j = int(mx) - 1; j > kk; j--) { best[j] = best[j - 1]; bi[j] = bi[j - 1]; } best[kk] = s; bi[kk] = int(r); }\n"
"  int n = 0; for (uint j = 0; j < mx; j++) n += bi[j] >= 0;\n"
"  o.n = n; for (int j = 0; j < 4; j++) { bool u = j < n; o.rec[j] = u ? bi[j] * 4 : -1; float4 pp = u ? float4(L[bi[j] * 4].xyz, 1) : float4(0); o.pos[j] = pp; prev[j] = pp; }\n"
"  o.player = pl; o.cand_rec = cr; o.cand_off = cr >= 0 ? float4(L[cr * 4].xyz - p0, 1) : float4(0); o.p0 = float4(p0, 1); }\n"
// 派生副本：整份光源记录拷到我们自己的缓冲，玩家光（o.player）的颜色乘倍率；_rt 变体绑这份副本，游戏的原记录不动 | derived copy: the whole light record buffer copied into our own buffer with the player light's (o.player) color scaled; the _rt variants bind this copy and the game's records stay untouched
"struct LC { uint ntex; float scale; uint2 pad; };\n"
"kernel void rts_lcopy(device const float4 *L [[buffer(0)]], device float4 *D [[buffer(1)]], device const Picks &o [[buffer(2)]], constant LC &c [[buffer(3)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i >= c.ntex) return; float4 v = L[i]; if (o.player >= 0 && i == uint(o.player) * 4 + 1) v.xyz *= c.scale; D[i] = v; }\n"
// 点光追踪：半分辨率（宽高各半），每像素向 GPU 选中的每盏光一条遮挡射线（到光源为止），RGBA 各一盏 | point-light trace: half resolution (half width and height), one occlusion ray per pixel toward each GPU-picked light (up to the light), one per RGBA channel
"struct P { float4x4 invVP; uint2 size; uint2 full; float bias; float pad0; float pad1; float pad2; };\n"
"kernel void rts_ptrace(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> pm [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant P &k [[buffer(1)]], device const Picks &o [[buffer(2)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; uint2 fg = min(g * 2, k.full - 1); float z = depth.read(fg); float4 r = float4(1);\n"
"  if (z < 1.0) {\n"
"    float2 ndc = float2((float(fg.x) + 0.5) / float(k.full.x) * 2.0 - 1.0, 1.0 - (float(fg.y) + 0.5) / float(k.full.y) * 2.0);\n"
"    float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"    intersector<instancing> is; is.accept_any_intersection(true); is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"    for (int i = 0; i < min(o.n, 4); i++) { float3 d = o.pos[i].xyz - p; float len = length(d); if (len <= k.bias * 2.0) continue; d /= len;\n"
"      ray ry; ry.origin = p + d * k.bias; ry.direction = d; ry.min_distance = 0.0; ry.max_distance = len - k.bias * 2.0;\n"
"      intersection_result<instancing> h = is.intersect(ry, as, 0xffu); if (h.type != intersection_type::none) r[i] = 0.0; } }\n"
"  pm.write(r, g); }\n"
"struct VO { float4 p [[position]]; };\n"
"vertex VO rts_vs(uint id [[vertex_id]]) { float2 uv = float2((id << 1) & 2, id & 2); VO o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
"fragment float4 rts_fs(VO in [[stage_in]], texture2d<float> mask [[texture(0)]], constant float4 &a [[buffer(0)]]) {\n"
"  float m = mask.read(uint2(in.p.xy)).r; float f = a.x + (1.0 - a.x) * m; return float4(f, f, f, 1.0); }\n";   // 乘到 HDR 上（混合：目标 × 源） | multiplied onto the HDR (blend: destination × source)
