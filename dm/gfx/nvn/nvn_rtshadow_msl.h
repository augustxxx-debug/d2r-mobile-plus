// 光追阴影的 Metal 计算核与合成着色器源（nvn_rtshadow.inc 运行时编译；dm/gfx/tests/t_rtshadow.m 在 Mac 上编译并跑同一份）
// Metal compute kernels and compositing shaders of the ray-traced shadows (compiled at run time by nvn_rtshadow.inc; dm/gfx/tests/t_rtshadow.m compiles and runs the same source on the Mac)
#pragma once
// 镂空判定库的公共头（翻译层拼在 rts_alpha.metal 前面；rts_variant.py 编译检查时从本文件抽出同一份）：判定记录与按 locn 取重心插值量 | Common head of the alpha judge library (prepended to rts_alpha.metal by the translation layer; rts_variant.py extracts this very text for its compile check): the judge record and the barycentric fetch of one interpolant by locn
static const char *k_rts_alpha_head =
"#include <metal_stdlib>\nusing namespace metal;\n"
"#ifndef RTS_ALPHA_LOD\n#define RTS_ALPHA_LOD 0.0\n#endif\n"   // 判定采样的 mip 级（缺省 0 = 最清晰的镂空；Mac 实测改成 3 并不更快：贵的是穿过透明处要多判的候选，不是采样） | mip level of judge samples (0 by default = the sharpest cut-outs; measured on the Mac, 3 is not faster: the cost is the extra candidates behind transparent texels, not the sampling)
"struct RtsAI { float4x4 lvp; float4 vp; float2 dr; uint fs; uint stride; constant uchar *args; device const float4 *var; device const uchar *ib; uint isz; uint locmask; uchar slot[16]; };\n"
"static inline float4 rts_vl(constant RtsAI &r, uint3 ix, float2 b, uint l) { uint s = r.slot[l]; return (1.0 - b.x - b.y) * r.var[ix.x * r.stride + s] + b.x * r.var[ix.y * r.stride + s] + b.y * r.var[ix.z * r.stride + s]; }\n";
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
// stride：每顶点几个 float4（蒙皮 _pos = 1；镂空 _var = 1 + 插值输出数，裁剪坐标在第 0 个） | stride: float4s per vertex (skinned _pos = 1; alpha-tested _var = 1 + interpolant outputs, the clip position first)
"struct U { float4x4 inv; uint n; uint stride; };\n"
"kernel void rts_unproj(device const float4 *src [[buffer(0)]], device packed_float3 *dst [[buffer(1)]], constant U &u [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i >= u.n) return; float4 w = u.inv * src[i * max(u.stride, 1u)]; float3 p = w.xyz / w.w;\n"
"  dst[i] = packed_float3(all(isfinite(p)) && all(abs(p) < 1e7) ? p : float3(0)); }\n"   // 防坏数据：非有限 / 离谱的点收成原点（退化三角形），不让 refit 出坏包围盒 | guard against bad data: non-finite / absurd points collapse to the origin (degenerate triangles) so the refit never yields broken bounds
// 遮挡判定。RTS_ALPHA（翻译层把 rts_alpha.metal 拼在前面一起编）：镂空实例是非不透明的，查询循环在每个候选三角形处按顶点导出的插值量（重心插值）、
// 阴影图像素坐标（命中点 × 该次绘制的光源视投影 → 视口）复现游戏原片元着色器（rts_alpha_eval），不丢弃才算挡住；不透明实例直接提交。不带 RTS_ALPHA 时全按不透明（原路径）。
// Occlusion test. RTS_ALPHA (the translation layer prepends rts_alpha.metal and compiles both together): alpha-tested instances are non-opaque; the query loop replays the game's own fragment shader (rts_alpha_eval) at each candidate triangle
// from the exported interpolants (barycentric) and the shadow-map pixel (hit point × that draw's light view-projection → viewport); it blocks only when not discarded. Opaque instances commit by themselves. Without RTS_ALPHA everything is opaque (the original path).
"#ifdef RTS_ALPHA\n"
"#ifndef RTS_ALPHA_DBG\n#define RTS_ALPHA_DBG 0\n#endif\n"
"#ifndef RTS_ALPHA_MAXREJ\n#define RTS_ALPHA_MAXREJ 8\n#endif\n"
"static bool rts_ai_pass(constant RtsAI *ai, uint u, uint prim, float2 b, float3 p) {\n"
"  constant RtsAI &r = ai[u]; uint i0 = prim * 3, i1 = i0 + 1, i2 = i0 + 2;\n"
"#if RTS_ALPHA_DBG == 1\n  return true;\n#endif\n"
"  if (r.isz == 2) { device const ushort *x = (device const ushort *)r.ib; i0 = x[i0]; i1 = x[i1]; i2 = x[i2]; } else if (r.isz == 4) { device const uint *x = (device const uint *)r.ib; i0 = x[i0]; i1 = x[i1]; i2 = x[i2]; }\n"
"  uint3 ix = uint3(i0, i1, i2);\n"
"  float4 c = r.lvp * float4(p, 1.0); float3 n = c.xyz / c.w;\n"
"  float4 fc = float4(floor(r.vp.x + (n.x * 0.5 + 0.5) * r.vp.z) + 0.5, floor(r.vp.y + (0.5 - 0.5 * n.y) * r.vp.w) + 0.5, r.dr.x + n.z * (r.dr.y - r.dr.x), 1.0 / c.w);\n"
"#if RTS_ALPHA_DBG == 2\n  return rts_vl(r, ix, b, 2).x > -1e30 || fc.x > 0.0;\n#endif\n"
"#ifdef RTS_ALPHA_ONLY\n  if (r.fs >= 32u || !((RTS_ALPHA_ONLY >> r.fs) & 1u)) return true;\n#endif\n"
"  return rts_alpha_eval(r, ix, b, fc); }\n"
"static bool rts_hit(ray ry, instance_acceleration_structure as, constant RtsAI *ai, bool any, thread float &dist) {\n"
"  intersection_params ip; ip.accept_any_intersection(any); ip.assume_geometry_type(geometry_type::triangle);\n"
"  intersection_query<triangle_data, instancing> q; q.reset(ry, as, 0xffu, ip);\n"
"  int nrej = 0;\n"
"  while (q.next()) if (q.get_candidate_intersection_type() == intersection_type::triangle) {\n"
"    if (rts_ai_pass(ai, q.get_candidate_user_instance_id(), q.get_candidate_primitive_id(), q.get_candidate_triangle_barycentric_coord(), ry.origin + ry.direction * q.get_candidate_triangle_distance())) q.commit_triangle_intersection();\n"
"    else if (++nrej >= RTS_ALPHA_MAXREJ) q.commit_triangle_intersection(); }\n"   // 判定上限：连丢这么多层就当挡住（叠这么多层树叶本来也几乎不透光），封住最坏情况的开销 | judge cap: after this many discarded layers count it as blocked (that many stacked leaves let almost no light through anyway), bounding the worst-case cost
"  bool h = q.get_committed_intersection_type() != intersection_type::none; dist = h ? q.get_committed_distance() : 0.0; return h; }\n"
"#define RTS_AI , constant RtsAI *ai [[buffer(3)]]\n#define RTS_HIT(ry, as, any, d) rts_hit(ry, as, ai, any, d)\n"
"#else\n"
"static bool rts_hit0(ray ry, instance_acceleration_structure as, bool any, thread float &dist) {\n"
"  intersector<instancing> is; is.accept_any_intersection(any); is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"  intersection_result<instancing> h = is.intersect(ry, as, 0xffu); dist = h.distance; return h.type != intersection_type::none; }\n"
"#define RTS_AI\n#define RTS_HIT(ry, as, any, d) rts_hit0(ry, as, any, d)\n"
"#endif\n"
// pad = 1：半分辨率追踪（size = 遮罩尺寸，按全分辨率深度的 2× 位置取样）；pad = 4：1/4 分辨率（自动降档最低档）；之后 rts_pup 按深度上采样回全分辨率 | pad = 1: half-resolution trace (size = mask size, sampled at 2× positions of the full-resolution depth); pad = 4: quarter resolution (lowest automatic level); then upsampled back by depth with rts_pup
"struct K { float4x4 invVP; float4 sun; uint2 size; float bias; float pad; };\n"
"kernel void rts_trace(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> mask [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant K &k [[buffer(1)]] RTS_AI, uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; uint2 full = uint2(depth.get_width(), depth.get_height()); uint sc = k.pad > 3.0 ? 4u : k.pad > 0.5 ? 2u : 1u; uint2 fg = min(g * sc, full - 1); float z = depth.read(fg);\n"
"  if (z >= 1.0) { mask.write(float4(1), g); return; }\n"   // 远平面 = 没画东西（天空） | far plane = nothing drawn (sky)
"  float2 ndc = float2((float(fg.x) + 0.5) / float(full.x) * 2.0 - 1.0, 1.0 - (float(fg.y) + 0.5) / float(full.y) * 2.0);\n"
"  float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"  ray r; r.origin = p + k.sun.xyz * k.bias; r.direction = k.sun.xyz; r.min_distance = 0.0; r.max_distance = 20000.0;\n"
"  float d; bool hit = RTS_HIT(r, as, true, d);\n"
"  mask.write(float4(hit ? 0.0 : 1.0), g); }\n"
// 软阴影：最近命中 + 命中距离（按 maxd 归一）→ RG8；模糊：半径 = 周围 ±2 像素内最远遮挡距离 × 强度（≤ 12 像素），深度差大的邻居权重小 | soft shadows: nearest hit + hit distance (normalized by maxd) → RG8; blur: radius = widest blocker distance within ±2 px × strength (≤ 12 px), neighbours with a large depth difference weigh less
"struct KS { float4x4 invVP; float4 sun; uint2 size; float bias; float maxd; };\n"
"kernel void rts_trace_soft(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> mask [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant KS &k [[buffer(1)]] RTS_AI, uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; float z = depth.read(g);\n"
"  if (z >= 1.0) { mask.write(float4(1, 0, 0, 0), g); return; }\n"
"  float2 ndc = float2((float(g.x) + 0.5) / float(k.size.x) * 2.0 - 1.0, 1.0 - (float(g.y) + 0.5) / float(k.size.y) * 2.0);\n"
"  float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"  ray r; r.origin = p + k.sun.xyz * k.bias; r.direction = k.sun.xyz; r.min_distance = 0.0; r.max_distance = 20000.0;\n"
"  float d; bool hit = RTS_HIT(r, as, false, d);\n"
"  mask.write(float4(hit ? 0.0 : 1.0, hit ? min(d / k.maxd, 1.0) : 0.0, 0, 0), g); }\n"
"struct B { uint2 size; int2 dir; float soft; float pad; };\n"
"kernel void rts_blur(texture2d<float, access::read> src [[texture(0)]], texture2d<float, access::write> dst [[texture(1)]], depth2d<float, access::read> depth [[texture(2)]], constant B &b [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= b.size.x || g.y >= b.size.y) return; float2 c = src.read(g).xy; float z0 = depth.read(g); float dm = c.y;\n"
"  for (int k = -2; k <= 2; k++) { int2 q = clamp(int2(g) + b.dir * k, int2(0), int2(b.size) - 1); dm = max(dm, src.read(uint2(q)).y); }\n"
"  int r = int(clamp(dm * b.soft, 0.0, 12.0)); if (r == 0) { dst.write(float4(c.x, dm, 0, 0), g); return; }\n"
"  float sum = 0.0, wsum = 0.0; for (int k = -r; k <= r; k++) { int2 q = clamp(int2(g) + b.dir * k, int2(0), int2(b.size) - 1); float2 s = src.read(uint2(q)).xy; float z = depth.read(uint2(q)); float wgt = 1.0 / (1.0 + 4000.0 * abs(z - z0)); sum += s.x * wgt; wsum += wgt; }\n"
"  dst.write(float4(sum / wsum, dm, 0, 0), g); }\n"
// 点光（阶段零，Mac 实测后改）：光源记录不是按 4 texel 对齐排的，真正的点光要经分簇表找——簇头 hdr[簇] 的位 0–5 / 6–15 / 16–23 是前三类光在本簇槽位里的结束下标，
// 位 24–31 是点光数；点光 j 的记录起点（texel 下标）= slots[簇 × 256 + 起点 + j]（起点 = 前三类结束下标的最大值），与 _rt 变体里 rts_pl 比对的值同一口径。
// 记录里的位置是视空间的（片元算光照用的位置 = 世界 · 视矩阵，视常量 cb 第 5–8 行；Mac 上读模型顶点着色器得出），追踪前用逆视矩阵换回世界坐标。
// 记录：texel+0 位置、texel+1 颜色 rgb + 半径、texel+2 第二位置 + 阴影标志（w == 0 才采立方体阴影）、texel+3.x 衰减指数。全在 GPU 上（本帧、主通道之前，与片元读的同一份）；CPU 不读游戏内存。
// Point lights (phase 0, revised after the Mac run): light records are not laid out on 4-texel boundaries; the real point lights are found through the cluster tables - bits 0–5 / 6–15 / 16–23 of hdr[cluster] are the end slots of the first three light kinds,
// bits 24–31 the point-light count; point light j's record start (texel index) = slots[cluster × 256 + start + j] (start = the largest of the three end slots), the same value rts_pl compares in the _rt variants.
// Positions in the records are in view space (the position the fragments light with = world · view matrix, rows 5–8 of the view constants; found by reading the model vertex shader on the Mac) and are taken back to world space with the inverse view matrix before tracing.
// Record: texel+0 position, texel+1 color rgb + radius, texel+2 second position + shadow flag (cube shadow only when w == 0), texel+3.x falloff exponent. All on the GPU (this frame, before the main pass, the same copy the fragments read); the CPU never reads game memory.
"struct Picks { int4 rec; float4 pos[4]; int n; int player; int cand_rec; int pad; float4 cand_off; float4 p0; };\n"
// 收集：先清已见位图与计数（rts_lclear），每簇一个线程把本簇点光的记录起点去重后追加进列表（list[0] = 条数，list[1…cap] = texel 下标） | gather: clear the seen bitmap and count first (rts_lclear), then one thread per cluster appends its point lights' record starts, deduplicated, to the list (list[0] = count, list[1…cap] = texel indices)
"struct LG { uint ncl; uint nslot; uint ntex; uint cap; };\n"
"kernel void rts_lclear(device atomic_uint *seen [[buffer(0)]], device atomic_uint *list [[buffer(1)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i < 2048) atomic_store_explicit(&seen[i], 0u, memory_order_relaxed); if (i == 0) atomic_store_explicit(&list[0], 0u, memory_order_relaxed); }\n"
"kernel void rts_lgather(device const uint *hdr [[buffer(0)]], device const uint *slots [[buffer(1)]], device atomic_uint *seen [[buffer(2)]], device atomic_uint *list [[buffer(3)]], constant LG &g [[buffer(4)]], uint c [[thread_position_in_grid]]) {\n"
"  if (c >= g.ncl) return; uint w = hdr[c], d = w >> 24, st = max(w & 63u, max((w >> 6) & 1023u, (w >> 16) & 255u));\n"
"  for (uint q = 0; q < d; q++) { uint sl = c * 256 + st + q; if (sl >= g.nslot) break; uint t = slots[sl]; if (t + 3 >= g.ntex || t >= 65536u) continue;\n"
"    uint bit = 1u << (t & 31u); if (atomic_fetch_or_explicit(&seen[t >> 5], bit, memory_order_relaxed) & bit) continue;\n"
"    uint k = atomic_fetch_add_explicit(&list[0], 1u, memory_order_relaxed); if (k < g.cap) atomic_store_explicit(&list[1 + k], t, memory_order_relaxed); } }\n"
// 选灯（单线程，列表里的点光）：得分 = 亮度 × 半径覆盖画面中心视线的程度（灯到视线的距离 / 半径越小越大）+ 远近细分；上一帧选中的（位置 1 单位内）×1.5（滞后）；
// 另出离视线最近的候选（CPU 认玩家光用）与 CPU 认定的玩家光偏移匹配的记录（派生副本里调弱它）| picking (one thread, over the listed point lights): score = brightness × how much the radius covers the screen-center view line (distance to the line / radius, smaller is better) + a distance tiebreak; last frame's picks (within 1 unit) ×1.5 (hysteresis);
// also outputs the candidate nearest the view line (for CPU player-light recognition) and the record matching the player-light offset the CPU confirmed (weakened in the derived copy)
"struct PK { float4x4 invVP; uint maxn; uint player_on; float ptol; uint cap; float4 poff; float4x4 invV; };\n"
"kernel void rts_pick(device const float4 *L [[buffer(0)]], device Picks &o [[buffer(1)]], constant PK &k [[buffer(2)]], device float4 *prev [[buffer(3)]], device const uint *list [[buffer(4)]], uint t [[thread_position_in_grid]]) {\n"
"  if (t) return; float4 a = k.invVP * float4(0, 0, 0.5, 1), b = k.invVP * float4(0, 0, 0.9, 1); float3 p0 = a.xyz / a.w, dir = normalize(b.xyz / b.w - p0);\n"   // 视线取深度 0.5 / 0.9 两点 | the view line uses depth 0.5 / 0.9
"  float best[4] = {0, 0, 0, 0}; int bi[4] = {-1, -1, -1, -1}; float cd = 1e30; int cr = -1, pl = -1; float pd = 1e30; uint mx = min(k.maxn, 4u), nl = min(list[0], k.cap);\n"
"  for (uint q = 0; q < nl; q++) { uint r = list[1 + q]; float4 pw = k.invV * float4(L[r].xyz, 1.0); float3 pos = pw.xyz / pw.w; float4 cr4 = L[r + 1]; float lum = max(cr4.x, max(cr4.y, cr4.z)), rad = cr4.w;\n"
"    if (!(lum > 0.0) || !(rad > 0.0) || any(isnan(pos)) || any(isinf(pos))) continue;\n"
"    float3 v = pos - p0; float3 qv = v - dot(v, dir) * dir; float d2 = dot(qv, qv); if (d2 < cd) { cd = d2; cr = int(r); }\n"
"    if (k.player_on) { float e = length(v - k.poff.xyz); if (e < k.ptol && e < pd) { pd = e; pl = int(r); } }\n"
"    float s = lum * (max(0.0, 1.0 - sqrt(d2) / (rad * 2.0)) + 1e-3 / (1.0 + d2)); for (uint j = 0; j < 4; j++) if (prev[j].w > 0.0 && distance(prev[j].xyz, pos) < 1.0) { s *= 1.5; break; }\n"
"    if (!(s > 0.0)) continue; int kk = -1; for (uint j = 0; j < mx; j++) if (bi[j] < 0 || s > best[j]) { kk = int(j); break; }\n"
"    if (kk < 0) continue; for (int j = int(mx) - 1; j > kk; j--) { best[j] = best[j - 1]; bi[j] = bi[j - 1]; } best[kk] = s; bi[kk] = int(r); }\n"
"  int n = 0; for (uint j = 0; j < mx; j++) n += bi[j] >= 0;\n"
"  o.n = n; for (int j = 0; j < 4; j++) { bool u = j < n; o.rec[j] = u ? bi[j] : -1; float4 pw = u ? k.invV * float4(L[bi[j]].xyz, 1.0) : float4(0, 0, 0, 1); float4 pp = u ? float4(pw.xyz / pw.w, max(L[bi[j] + 1].w, 1e-3)) : float4(0); o.pos[j] = pp; prev[j] = pp; }\n"   // w = 半径（> 0 兼作「有效」） | w = radius (> 0 doubles as "valid")
"  float4 cw = cr >= 0 ? k.invV * float4(L[cr].xyz, 1.0) : float4(0, 0, 0, 1); o.player = pl; o.cand_rec = cr; o.cand_off = cr >= 0 ? float4(cw.xyz / cw.w - p0, 1) : float4(0); o.p0 = float4(p0, 1); }\n"
// 派生副本：整份光源记录拷到我们自己的缓冲，玩家光（o.player）的颜色乘倍率；_rt 变体绑这份副本，游戏的原记录不动 | derived copy: the whole light record buffer copied into our own buffer with the player light's (o.player) color scaled; the _rt variants bind this copy and the game's records stay untouched
"struct LC { uint ntex; float scale; uint2 pad; };\n"
"kernel void rts_lcopy(device const float4 *L [[buffer(0)]], device float4 *D [[buffer(1)]], device const Picks &o [[buffer(2)]], constant LC &c [[buffer(3)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i >= c.ntex) return; float4 v = L[i]; if (o.player >= 0 && i == uint(o.player) + 1) v.xyz *= c.scale; D[i] = v; }\n"   // 玩家光记录的颜色 texel = 起点 + 1 | the player light record's color texel = start + 1

// 点光追踪：半分辨率（宽高各半），每像素向 GPU 选中的每盏光一条遮挡射线（到光源为止），RGBA 各一盏 | point-light trace: half resolution (half width and height), one occlusion ray per pixel toward each GPU-picked light (up to the light), one per RGBA channel
"struct P { float4x4 invVP; uint2 size; uint2 full; float bias; float pad0; float pad1; float pad2; };\n"
"kernel void rts_ptrace(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> pm [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant P &k [[buffer(1)]], device const Picks &o [[buffer(2)]] RTS_AI, uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; uint2 fg = min(uint2((float2(g) + 0.5) * float2(k.full) / float2(k.size)), k.full - 1); float z = depth.read(fg); float4 r = float4(1);\n"   // 低分辨率像素中心对应的全分辨率像素（÷2 或 ÷4） | the full-resolution pixel at the low-resolution pixel centre (÷2 or ÷4)
"  if (z < 1.0) {\n"
"    float2 ndc = float2((float(fg.x) + 0.5) / float(k.full.x) * 2.0 - 1.0, 1.0 - (float(fg.y) + 0.5) / float(k.full.y) * 2.0);\n"
"    float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"    for (int i = 0; i < min(o.n, 4); i++) { float3 d = o.pos[i].xyz - p; float len = length(d); if (len <= k.bias * 2.0 || len > o.pos[i].w) continue; d /= len;\n"   // 半径外这盏灯照不到：不追（营地实测点光追踪 4 ms 的大头） | beyond the radius the light does not reach: no ray (measured at the camp: the bulk of the 4 ms point trace)
"      ray ry; ry.origin = p + d * k.bias; ry.direction = d; ry.min_distance = 0.0; ry.max_distance = len - k.bias * 2.0;\n"
"      float dd; if (RTS_HIT(ry, as, true, dd)) r[i] = 0.0; } }\n"
"  pm.write(r, g); }\n"
// 几何核对（DM_RT_SHADOW_CHECK，只在调试时用）：ck_light 从光源视角对 TLAS 追一张深度图，与游戏的阴影图逐 texel 对；ck_cam 把主深度重建出的世界点投到光源、查游戏阴影图得「原版应有的阴影」，与光追遮罩对
// Geometry check (DM_RT_SHADOW_CHECK, debug only): ck_light traces a depth map of the TLAS from the light's view to compare texel by texel with the game's shadow map; ck_cam projects world points rebuilt from the main depth into the light and looks up the game's shadow map ("the shadow the original should have"), to compare with the ray-traced mask
"struct CK { float4x4 invL; float4x4 L; float4x4 invVP; uint2 lsize; uint2 csize; float rev; float pad[3]; float4x4 VP; };\n"
"kernel void rts_ck_light(instance_acceleration_structure as [[buffer(0)]], constant CK &k [[buffer(1)]], device float2 *o [[buffer(2)]], depth2d<float, access::read> sm [[texture(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.lsize.x || g.y >= k.lsize.y) return; float2 ndc = float2((float(g.x) + 0.5) / float(k.lsize.x) * 2.0 - 1.0, 1.0 - (float(g.y) + 0.5) / float(k.lsize.y) * 2.0);\n"
"  float4 a = k.invL * float4(ndc, k.rev > 0.5 ? 1.0 : 0.0, 1.0), b = k.invL * float4(ndc, k.rev > 0.5 ? 0.0 : 1.0, 1.0); float3 pa = a.xyz / a.w, pb = b.xyz / b.w; float len = length(pb - pa);\n"
"  ray r; r.origin = pa; r.direction = (pb - pa) / len; r.min_distance = 0.0; r.max_distance = len;\n"
"  intersector<instancing> is; is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque); intersection_result<instancing> h = is.intersect(r, as, 0xffu);\n"
"  float md = sm.read(g); o[g.y * k.lsize.x + g.x] = float2(h.type != intersection_type::none ? h.distance / len : 1.0, k.rev > 0.5 ? 1.0 - md : md); }\n"
"kernel void rts_ck_cam(depth2d<float, access::read> depth [[texture(0)]], depth2d<float, access::read> sm [[texture(1)]], texture2d<float, access::read> mask [[texture(2)]], constant CK &k [[buffer(1)]], device float2 *o [[buffer(2)]], instance_acceleration_structure as [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.csize.x || g.y >= k.csize.y) return; uint2 fg = g * 2; float z = depth.read(fg); float2 res = float2(1.0, mask.read(fg).x);\n"
"  if (z < 1.0) { float2 ndc = float2((float(fg.x) + 0.5) / float(k.csize.x * 2) * 2.0 - 1.0, 1.0 - (float(fg.y) + 0.5) / float(k.csize.y * 2) * 2.0);\n"
"    float4 w = k.invVP * float4(ndc, z, 1.0); float4 c = k.L * float4(w.xyz / w.w, 1.0); float3 n = c.xyz / c.w; float2 uv = float2(n.x * 0.5 + 0.5, 0.5 - 0.5 * n.y);\n"
"    if (all(uv >= 0.0) && all(uv < 1.0)) { float md = sm.read(uint2(uv * float2(k.lsize))); res.x = (k.rev > 0.5 ? n.z < md - 0.002 : n.z > md + 0.002) ? 0.0 : 1.0; } else res.x = 0.5; }\n"
"  o[g.y * k.csize.x + g.x] = res;\n"
// 相机视角追 TLAS：命中点按视投影算回深度，与深度缓冲比（第 2 组：追出的深度、缓冲深度） | trace the TLAS from the camera: the hit's depth through the view-projection versus the depth buffer (second pair: traced depth, buffer depth)
"  float2 ndc2 = float2((float(fg.x) + 0.5) / float(k.csize.x * 2) * 2.0 - 1.0, 1.0 - (float(fg.y) + 0.5) / float(k.csize.y * 2) * 2.0);\n"
"  float4 a = k.invVP * float4(ndc2, 0.0, 1.0), b = k.invVP * float4(ndc2, 1.0, 1.0); float3 pa = a.xyz / a.w, pb = b.xyz / b.w; float len = length(pb - pa);\n"
"  ray r; r.origin = pa; r.direction = (pb - pa) / len; r.min_distance = 0.0; r.max_distance = len; intersector<instancing> is; is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"  intersection_result<instancing> h = is.intersect(r, as, 0xffu); float zt = -1.0; if (h.type != intersection_type::none) { float4 c = k.VP * float4(pa + r.direction * h.distance, 1.0); zt = c.z / c.w; }\n"
"  o[k.csize.x * k.csize.y + g.y * k.csize.x + g.x] = float2(zt, z); }\n"
// 点光遮罩上采样（评审阶段一）：半分辨率 → 全分辨率，2×2 双线性权重再乘深度相近度（与 rts_blur 同一口径），深度差大的邻居几乎不参与——人物边缘不再漏光 / 漏影
// point-light mask upsampling (review phase 1): half → full resolution, bilinear 2×2 weights times depth similarity (the same measure as rts_blur), neighbours across a depth edge barely count - no more light / shadow leaking at silhouettes
"struct PU { uint2 full; uint2 half_; };\n"
"kernel void rts_pup(texture2d<float, access::read> pm [[texture(0)]], texture2d<float, access::write> out [[texture(1)]], depth2d<float, access::read> depth [[texture(2)]], constant PU &u [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= u.full.x || g.y >= u.full.y) return; float z0 = depth.read(g); float2 sc = float2(u.half_) / float2(u.full); float2 hp = (float2(g) + 0.5) * sc - 0.5; int2 q0 = int2(floor(hp)); float2 fr = hp - float2(q0);\n"   // sc = 低分辨率 / 全分辨率（1/2 或 1/4） | sc = low / full resolution (1/2 or 1/4)
"  float4 acc = float4(0); float ws = 0.0;\n"
"  for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) { int2 q = clamp(q0 + int2(i, j), int2(0), int2(u.half_) - 1); float bw = (i ? fr.x : 1.0 - fr.x) * (j ? fr.y : 1.0 - fr.y);\n"
"    float zq = depth.read(min(uint2((float2(q) + 0.5) / sc), u.full - 1)); float w = (bw + 1e-3) / (1.0 + 4000.0 * abs(zq - z0)); acc += pm.read(uint2(q)) * w; ws += w; }\n"
"  out.write(acc / max(ws, 1e-6), g); }\n"
"struct VO { float4 p [[position]]; };\n"
"vertex VO rts_vs(uint id [[vertex_id]]) { float2 uv = float2((id << 1) & 2, id & 2); VO o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
"fragment float4 rts_fs(VO in [[stage_in]], texture2d<float> mask [[texture(0)]], constant float4 &a [[buffer(0)]]) {\n"
"  float m = mask.read(uint2(in.p.xy)).r; float f = a.x + (1.0 - a.x) * m; return float4(f, f, f, 1.0); }\n";   // 乘到 HDR 上（混合：目标 × 源） | multiplied onto the HDR (blend: destination × source)
