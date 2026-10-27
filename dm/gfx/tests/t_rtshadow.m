// 光追阴影核心的 Mac 单测（Metal 4，不启动游戏、不读游戏资源）：编译 nvn_rtshadow_msl.h 的全部核与合成管线；用一块地面 + 一块悬空方板的合成场景
// 走一遍生产路径——位置解码（SNORM16x4·exp2 与 FLOAT3）→ BLAS / TLAS → 太阳遮挡射线 → 蒙皮导出的裁剪坐标反投影 → 以绑定姿势为源 refit → 点光射线 → 最近命中 + 两趟模糊，
// 逐像素核对遮罩；纯函数（4×4 求逆、三角带展开、点光挑选）另测。不支持光追的机器返回 77（跳过）。
// Mac unit test of the ray-traced shadow core (Metal 4; the game is not launched, no game assets): compiles every kernel and the compositing pipeline of nvn_rtshadow_msl.h; a synthetic scene of a ground
// plane plus a floating square goes through the production path — position decode (SNORM16x4·exp2 and FLOAT3) → BLAS / TLAS → sun occlusion rays → unprojection of skinned clip positions →
// refit from the bind pose → point-light rays → nearest hit + two blur passes — and the masks are checked per pixel; the pure helpers (4×4 inverse, strip expansion, light picking) are tested separately.
// Returns 77 (skip) on machines without ray tracing.
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../nvn/nvn_rtshadow_util.h"
#include "../nvn/nvn_rtshadow_msl.h"
#define W 64
#define H 64
#define FAIL(...) do { printf("FAIL " __VA_ARGS__); printf("\n"); return 1; } while (0)
static id<MTLDevice> dev; static id<MTL4CommandQueue> queue; static id<MTL4CommandAllocator> alloc; static id<MTLResidencySet> resident;
// 一个命令缓冲、一个计算编码器、提交并等完 | one command buffer, one compute encoder, commit and wait
static int run(void (^body)(id<MTL4ComputeCommandEncoder> ce)) {
    id<MTL4CommandBuffer> cb = [dev newCommandBuffer]; [cb beginCommandBufferWithAllocator:alloc];
    id<MTL4ComputeCommandEncoder> ce = [cb computeCommandEncoder]; body(ce); [ce endEncoding]; [cb endCommandBuffer];
    dispatch_semaphore_t done = dispatch_semaphore_create(0); __block NSError *err = nil;
    MTL4CommitOptions *o = [MTL4CommitOptions new]; [o addFeedbackHandler:^(id<MTL4CommitFeedback> f) { err = f.error; dispatch_semaphore_signal(done); }];
    id<MTL4CommandBuffer> bufs[] = {cb}; [queue commit:bufs count:1 options:o];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC))) { puts("GPU 没完成 | GPU did not complete"); return 1; }
    if (err) { printf("GPU 报错 | GPU error: %s\n", err.localizedDescription.UTF8String); return 1; }
    return 0;
}
static id<MTLBuffer> buf(const void *p, size_t n) { id<MTLBuffer> b = [dev newBufferWithLength:n options:MTLResourceStorageModeShared]; if (p) memcpy(b.contents, p, n); [resident addAllocation:b]; return b; }
static id<MTLTexture> tex(MTLPixelFormat f, int w, int h, MTLStorageMode sm) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO]; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = sm;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d]; [resident addAllocation:t]; return t;
}
static MTL4PrimitiveAccelerationStructureDescriptor *blas_desc(id<MTLBuffer> pos, id<MTLBuffer> ib, uint32_t tris, int refit) {
    MTL4PrimitiveAccelerationStructureDescriptor *pd = [MTL4PrimitiveAccelerationStructureDescriptor new]; MTL4AccelerationStructureTriangleGeometryDescriptor *gd = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
    gd.vertexBuffer = (MTL4BufferRange){pos.gpuAddress, pos.length}; gd.vertexFormat = MTLAttributeFormatFloat3; gd.vertexStride = 12; gd.triangleCount = tris; gd.opaque = YES;
    gd.indexBuffer = (MTL4BufferRange){ib.gpuAddress, ib.length}; gd.indexType = MTLIndexTypeUInt16;
    pd.geometryDescriptors = @[gd]; pd.usage = refit ? MTLAccelerationStructureUsageRefit : MTLAccelerationStructureUsageNone; return pd;
}
static float px(id<MTLTexture> t, int x, int y, int ch) { uint8_t v[4] = {0, 0, 0, 0}; [t getBytes:v bytesPerRow:t.width * (t.pixelFormat == MTLPixelFormatRGBA8Unorm ? 4 : t.pixelFormat == MTLPixelFormatRG8Unorm ? 2 : 1) fromRegion:MTLRegionMake2D(x, y, 1, 1) mipmapLevel:0]; return v[ch] / 255.f; }
// 像素 ↔ 世界：正交俯视，x = ndc.x·50、z = ndc.y·50、深度 = (50 − y)/100 | pixel ↔ world: orthographic top-down, x = ndc.x·50, z = ndc.y·50, depth = (50 − y)/100
static int gx_of(double x) { return (int)((x / 50 + 1) / 2 * W); }
static int gy_of(double z) { return (int)((1 - z / 50) / 2 * H); }
int main(void) { @autoreleasepool {
    // ---- 纯函数 | pure helpers ----
    { double m[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 2, 3, 1}, inv[16], id[16]; rts_inv4(m, inv);
      for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { double s = 0; for (int k = 0; k < 4; k++) s += m[k * 4 + r] * inv[c * 4 + k]; id[c * 4 + r] = s; }
      for (int i = 0; i < 16; i++) if (fabs(id[i] - (i % 5 == 0 ? 1 : 0)) > 1e-9) FAIL("rts_inv4 [%d] = %g", i, id[i]); }
    { uint16_t strip[] = {0, 1, 2, 2, 3, 4}; uint32_t out[12]; uint32_t t = rts_strip_expand(strip, 2, 6, out);   // 0,1,2 | 1,2,2 退化 | 2,2,3 退化 | 2,3,4 → 2 个 | two triangles, two degenerate ones dropped
      if (t != 2 || out[0] != 0 || out[1] != 1 || out[2] != 2 || out[3] != 2 || out[4] != 3 || out[5] != 4) FAIL("rts_strip_expand indexed: %u tris", t);
      t = rts_strip_expand(NULL, 0, 5, out); if (t != 3 || out[6] != 2 || out[7] != 3 || out[8] != 4) FAIL("rts_strip_expand non-indexed: %u tris", t); }
    { rts_player_track t = {.rec = -1}; float torch[3] = {40, 0, 0}, player[3] = {5, 0, 5}; int sure = 0;   // 相机沿 x 走：火把偏移在变、随身光偏移不变 | the camera walks along x: the torch's offset changes, the player light's does not
      for (int f = 0; f < 80; f++) { double p0[3] = {f * 0.5, 0, 0}; float pp[3] = {player[0] + f * 0.5f, player[1], player[2]}; sure = rts_player_feed(&t, 7, pp, p0); if (f < 59 && sure) FAIL("player light recognized too early (frame %d)", f); }
      if (!sure) FAIL("player light not recognized after 80 moving frames");
      for (int f = 0; f < 80; f++) { double p0[3] = {f * 0.5, 0, 0}; if (rts_player_feed(&t, 3, torch, p0)) FAIL("a static torch was taken for the player light (frame %d)", f); }
      for (int f = 0; f < 80; f++) { double p0[3] = {10, 0, 0}; if (rts_player_feed(&t, 9, player, p0)) FAIL("recognized while the camera stands still"); } }
    // 相机：列主序 VP（与 MSL float4x4 同） | camera: column-major VP (same as the MSL float4x4)
    double VP[16] = {0}; VP[0] = 1.0 / 50; VP[2 * 4 + 1] = 1.0 / 50; VP[1 * 4 + 2] = -0.01; VP[3 * 4 + 2] = 0.5; VP[15] = 1; double IA[16]; rts_inv4(VP, IA);
    { float L[16 * 4] = {0};   // 4 盏光：#1 在视线上且亮、#2 远、#3 亮度 0（忽略）、#0 偏离视线 | 4 lights: #1 on the view line and bright, #2 far, #3 zero brightness (ignored), #0 off the line
      float pos[4][3] = {{30, 0, 0}, {0, 20, 0}, {200, 0, 0}, {0, 0, 0}}, lum[4] = {1, 1, 1, 0};   // 得分 = 亮度 / (距视线² + 1e4)：#1 > #0 > #2 | score = brightness / (distance² + 1e4): #1 > #0 > #2
      for (int i = 0; i < 4; i++) { L[i * 16 + 0] = pos[i][0]; L[i * 16 + 1] = pos[i][1]; L[i * 16 + 2] = pos[i][2]; L[i * 16 + 4] = lum[i]; }
      int rec[4]; double best[4], p0[3]; int n = rts_pick_lights_cpu(L, 4, IA, 4, rec, best, p0);
      if (n != 3 || rec[0] != 1 || rec[1] != 0 || rec[2] != 2 || best[0] <= best[1] || best[1] <= best[2]) FAIL("rts_pick_lights_cpu: n=%d order %d %d %d", n, rec[0], rec[1], rec[2]);
      if (fabs(p0[0]) > 1e-6 || fabs(p0[2]) > 1e-6) FAIL("view line origin (%g, %g, %g)", p0[0], p0[1], p0[2]); }
    // ---- Metal ----
    dev = MTLCreateSystemDefaultDevice();
    if (!dev || ![dev supportsRaytracing] || ![dev supportsFamily:MTLGPUFamilyApple9]) { puts("SKIP t_rtshadow: no hardware ray tracing"); return 77; }
    NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e]; if (!lib) FAIL("MSL compile: %s", e.localizedDescription.UTF8String);
    id<MTLComputePipelineState> ps[10]; const char *names[10] = {"rts_decode", "rts_unproj", "rts_trace", "rts_trace_soft", "rts_blur", "rts_ptrace", "rts_pick", "rts_lcopy", "rts_lclear", "rts_lgather"};
    for (int i = 0; i < 10; i++) { ps[i] = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@(names[i])] error:&e]; if (!ps[i]) FAIL("pipeline %s: %s", names[i], e.localizedDescription.UTF8String); }
    id<MTLComputePipelineState> ps_decode = ps[0], ps_unproj = ps[1], ps_trace = ps[2], ps_soft = ps[3], ps_blur = ps[4], ps_ptrace = ps[5], ps_pick = ps[6], ps_lcopy = ps[7], ps_lclear = ps[8], ps_lgather = ps[9];   // 块里不能引用数组 | arrays cannot be referenced inside blocks
    { MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"rts_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"rts_fs"]; pd.colorAttachments[0].pixelFormat = MTLPixelFormatRG11B10Float;
      if (![dev newRenderPipelineStateWithDescriptor:pd error:&e]) FAIL("overlay pipeline: %s", e.localizedDescription.UTF8String); }
    queue = [dev newMTL4CommandQueueWithDescriptor:[MTL4CommandQueueDescriptor new] error:&e]; if (!queue) FAIL("MTL4 queue");
    resident = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&e]; [queue addResidencySet:resident];
    alloc = [dev newCommandAllocatorWithDescriptor:[MTL4CommandAllocatorDescriptor new] error:&e];
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 5; ad.maxTextureBindCount = 3;
    id<MTL4ArgumentTable> at = [dev newArgumentTableWithDescriptor:ad error:&e];
    // 场景：地面 y=0 [−100,100]²（FLOAT3 原样）、方板 y=10 [−10,10]²（SNORM16x4·exp2(w·13)，w = 8/13 → 比例 256） | scene: ground y=0 (raw FLOAT3), square y=10 (SNORM16x4·exp2(w·13), w = 8/13 → scale 256)
    float ground[4][3] = {{-100, 0, -100}, {100, 0, -100}, {100, 0, 100}, {-100, 0, 100}}; uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
    int16_t sq[4][4]; float sqw[4][3] = {{-10, 10, -10}, {10, 10, -10}, {10, 10, 10}, {-10, 10, 10}};
    for (int i = 0; i < 4; i++) { for (int c = 0; c < 3; c++) sq[i][c] = (int16_t)lrintf(sqw[i][c] / 256.f * 32767.f); sq[i][3] = (int16_t)lrintf(8.f / 13.f * 32767.f); }
    id<MTLBuffer> vb_g = buf(ground, sizeof ground), vb_s = buf(sq, sizeof sq), ib = buf(quad, sizeof quad);
    id<MTLBuffer> pos_g = buf(NULL, 4 * 12), pos_s = buf(NULL, 4 * 12), pos_slot = buf(NULL, 4 * 12), clip = buf(NULL, 4 * 16);
    uint32_t kg[4] = {4, 12, 0x22, 0}, ks[4] = {4, 8, 0x2b, 0}; id<MTLBuffer> kb_g = buf(kg, 16), kb_s = buf(ks, 16);
    MTLAccelerationStructureSizes szg = [dev accelerationStructureSizesWithDescriptor:blas_desc(pos_g, ib, 2, 0)], szs = [dev accelerationStructureSizesWithDescriptor:blas_desc(pos_s, ib, 2, 1)];
    id<MTLAccelerationStructure> as_g = [dev newAccelerationStructureWithSize:szg.accelerationStructureSize], as_s = [dev newAccelerationStructureWithSize:szs.accelerationStructureSize], as_slot = [dev newAccelerationStructureWithSize:szs.accelerationStructureSize];
    id<MTLBuffer> inst = buf(NULL, 2 * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor));
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new]; td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = 2;
    td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor); td.instanceDescriptorBuffer = (MTL4BufferRange){inst.gpuAddress, inst.length};
    MTLAccelerationStructureSizes szt = [dev accelerationStructureSizesWithDescriptor:td]; id<MTLAccelerationStructure> tlas = [dev newAccelerationStructureWithSize:szt.accelerationStructureSize];
    NSUInteger scr = 4 << 20; id<MTLBuffer> scratch = [dev newBufferWithLength:scr options:MTLResourceStorageModePrivate];
    for (id r in @[as_g, as_s, as_slot, tlas, scratch]) [resident addAllocation:r];
    MTLIndirectAccelerationStructureInstanceDescriptor *di = inst.contents; memset(di, 0, 2 * sizeof *di);
    for (int i = 0; i < 2; i++) { di[i].transformationMatrix.columns[0].x = di[i].transformationMatrix.columns[1].y = di[i].transformationMatrix.columns[2].z = 1; di[i].options = MTLAccelerationStructureInstanceOptionOpaque; di[i].mask = 0xff; }
    di[0].accelerationStructureID = as_g.gpuResourceID; di[1].accelerationStructureID = as_s.gpuResourceID;
    // 深度：方板范围 0.4（y=10）、其余 0.5（y=0）；通过缓冲拷进 Depth32Float | depth: 0.4 (y=10) over the square, 0.5 (y=0) elsewhere; copied into a Depth32Float through a buffer
    float *dz = malloc(W * H * 4); for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { double wx = ((x + 0.5) / W * 2 - 1) * 50, wz = (1 - (y + 0.5) / H * 2) * 50; dz[y * W + x] = fabs(wx) < 10 && fabs(wz) < 10 ? 0.4f : 0.5f; }
    id<MTLBuffer> dzb = buf(dz, W * H * 4); free(dz);
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:W height:H mipmapped:NO]; dd.usage = MTLTextureUsageShaderRead; dd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [dev newTextureWithDescriptor:dd]; [resident addAllocation:depth];
    id<MTLTexture> mask = tex(MTLPixelFormatR8Unorm, W, H, MTLStorageModeShared), raw = tex(MTLPixelFormatRG8Unorm, W, H, MTLStorageModeShared), tmp = tex(MTLPixelFormatRG8Unorm, W, H, MTLStorageModeShared), pm = tex(MTLPixelFormatRGBA8Unorm, W / 2, H / 2, MTLStorageModeShared);
    // 常量：K（硬）/ KS（软）/ P（点光）/ B（模糊）/ U（反投影） | constants
    float K[64]; for (int i = 0; i < 16; i++) K[i] = (float)IA[i]; float sun[3] = {(float)M_SQRT1_2, (float)M_SQRT1_2, 0}; K[16] = sun[0]; K[17] = sun[1]; K[18] = sun[2]; K[19] = 0; ((uint32_t *)K)[20] = W; ((uint32_t *)K)[21] = H; K[22] = 0.6f; K[23] = 20.f;   // 软阴影的距离归一取 20：方板离地 14 → 半径 ≈ 0.7·12 = 8 像素（生产缺省 200 下这么近的遮挡体是硬的） | soft-shadow distance scale 20: the square 14 above the ground → radius ≈ 0.7·12 = 8 px (with the production default 200 such a near blocker stays hard)
    id<MTLBuffer> kb = buf(K, sizeof K);
    // 点光：记录按 texel 下标寻址、不按 4 对齐（游戏实测）。A 在 texel 4：(30,40,0) 颜色 2 半径 60；B 在 texel 9：(−200,5,0) 颜色 0.5 半径 10；texel 0 处颜色 0（不在分簇表里）。
    // 分簇表：簇 0 头 = 2 盏点光、起点 0，槽 0/1 = texel 4 / 9；簇 1 头 = 前三类结束于 3、1 盏点光，槽 256+3 = texel 4（重复，要去重）。期望列表 2 盏、A 排第一；玩家光按偏移认出 A，副本里 A 的颜色 texel 5 × 0.5
    // point lights: records are addressed by texel index, not 4-aligned (measured in the game). A at texel 4: (30,40,0) color 2 radius 60; B at texel 9: (−200,5,0) color 0.5 radius 10; color 0 at texel 0 (not in the cluster tables).
    // cluster tables: cluster 0 header = 2 point lights from slot 0, slots 0/1 = texel 4 / 9; cluster 1 header = first three kinds end at 3, 1 point light, slot 256+3 = texel 4 (a duplicate to dedupe). Expect 2 listed with A first; the player light matched to A by offset, A's color texel 5 halved in the copy
    float LR[16 * 4] = {0}; float *A = LR + 4 * 4, *Bl = LR + 9 * 4;
    A[0] = 30; A[1] = 40; A[2] = 0; A[4] = A[5] = A[6] = 2.0f; A[7] = 60;   // texel 4 位置、texel 5 颜色 + 半径 | texel 4 position, texel 5 color + radius
    Bl[0] = -200; Bl[1] = 5; Bl[2] = 0; Bl[4] = Bl[5] = Bl[6] = 0.5f; Bl[7] = 10;
    uint32_t HDR[2] = {2u << 24, 3u | (1u << 24)}; uint32_t *SL = calloc(512, 4); SL[0] = 4; SL[1] = 9; SL[256 + 3] = 4;
    id<MTLBuffer> lrec = buf(LR, sizeof LR), picks = buf(NULL, 128), prev = buf(NULL, 64), lcopyb = buf(NULL, sizeof LR), hdrb = buf(HDR, sizeof HDR), slotb = buf(SL, 512 * 4), seen = buf(NULL, 2048 * 4), list = buf(NULL, 257 * 4); free(SL);
    uint32_t LG[4] = {2, 512, 16, 256}; id<MTLBuffer> lgb = buf(LG, sizeof LG);
    float PK[40] = {0}; for (int i = 0; i < 16; i++) PK[i] = (float)IA[i]; PK[24] = PK[29] = PK[34] = PK[39] = 1;   /* 逆视矩阵 = 单位阵：记录已是世界坐标 | inverse view = identity: records already in world space */ uint32_t *pku = (uint32_t *)(PK + 16); pku[0] = 2; pku[1] = 1; ((float *)pku)[2] = 1.0f; pku[3] = 256;
    { double a0[4] = {0, 0, 0.5, 1}, o0[4]; for (int r = 0; r < 4; r++) { o0[r] = 0; for (int c = 0; c < 4; c++) o0[r] += IA[c * 4 + r] * a0[c]; } PK[20] = 30 - (float)(o0[0] / o0[3]); PK[21] = 40 - (float)(o0[1] / o0[3]); PK[22] = 0 - (float)(o0[2] / o0[3]); }
    id<MTLBuffer> pkb = buf(PK, sizeof PK);
    uint32_t LC[4] = {16, 0, 0, 0}; ((float *)LC)[1] = 0.5f; id<MTLBuffer> lcb = buf(LC, sizeof LC);
    float P[32] = {0}; for (int i = 0; i < 16; i++) P[i] = (float)IA[i]; ((uint32_t *)P)[16] = W / 2; ((uint32_t *)P)[17] = H / 2; ((uint32_t *)P)[18] = W; ((uint32_t *)P)[19] = H; P[20] = 0.6f;
    id<MTLBuffer> pb = buf(P, sizeof P);
    uint32_t B0[8] = {W, H, 1, 0, 0, 0, 0, 0}, B1[8] = {W, H, 0, 1, 0, 0, 0, 0}; ((float *)B0)[4] = ((float *)B1)[4] = 12.f; id<MTLBuffer> bb0 = buf(B0, 32), bb1 = buf(B1, 32);
    float U[20]; for (int i = 0; i < 16; i++) U[i] = (float)IA[i]; ((uint32_t *)U)[16] = 4; id<MTLBuffer> ub = buf(U, sizeof U);
    // 蒙皮导出的裁剪坐标：方板右移 30 后经 VP | skinned clip positions: the square shifted by +30 in x, through VP
    { float *c = clip.contents; for (int i = 0; i < 4; i++) { double w4[4] = {sqw[i][0] + 30, sqw[i][1], sqw[i][2], 1}; for (int r = 0; r < 4; r++) { double s = 0; for (int k = 0; k < 4; k++) s += VP[k * 4 + r] * w4[k]; c[i * 4 + r] = (float)s; } } }
    [resident commit];
    MTLSize tg = MTLSizeMake(W / 8, H / 8, 1), tt = MTLSizeMake(8, 8, 1);
    // 1. 深度拷贝、位置解码、BLAS、TLAS、太阳追踪（硬） | 1. depth copy, decode, BLAS, TLAS, sun trace (hard)
    if (run(^(id<MTL4ComputeCommandEncoder> ce) {
        [ce copyFromBuffer:dzb sourceOffset:0 sourceBytesPerRow:W * 4 sourceBytesPerImage:W * H * 4 sourceSize:MTLSizeMake(W, H, 1) toTexture:depth destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [ce setArgumentTable:at]; [ce setComputePipelineState:ps_decode];
        [at setAddress:vb_g.gpuAddress atIndex:0]; [at setAddress:pos_g.gpuAddress atIndex:1]; [at setAddress:kb_g.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [at setAddress:vb_s.gpuAddress atIndex:0]; [at setAddress:pos_s.gpuAddress atIndex:1]; [at setAddress:kb_s.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:as_g descriptor:blas_desc(pos_g, ib, 2, 0) scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:as_s descriptor:blas_desc(pos_s, ib, 2, 1) scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:tlas descriptor:td scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_trace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:mask.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:kb.gpuAddress atIndex:1];
        [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
    })) return 1;
    { const float *p = pos_s.contents; for (int i = 0; i < 4; i++) for (int c = 0; c < 3; c++) if (fabsf(p[i * 3 + c] - sqw[i][c]) > 0.05f) FAIL("decode SNORM16x4 v%d.%d = %g (want %g)", i, c, p[i * 3 + c], sqw[i][c]);
      p = pos_g.contents; if (fabsf(p[0] + 100) > 1e-6f || fabsf(p[11] - 100) > 1e-6f) FAIL("decode FLOAT3"); }
    // 太阳 (1,1,0)/√2：地面 x∈(−20,0) 被方板挡；方板顶面与远处地面亮 | sun (1,1,0)/√2: ground x∈(−20,0) is blocked by the square; the square's top and far ground are lit
    if (px(mask, gx_of(-10), gy_of(0), 0) > 0.01f) FAIL("sun: ground under the square should be shadowed");
    if (px(mask, gx_of(35), gy_of(0), 0) < 0.99f) FAIL("sun: far ground should be lit");
    if (px(mask, gx_of(0.8), gy_of(0.8), 0) < 0.99f) FAIL("sun: square top should be lit");
    if (px(mask, gx_of(-10), gy_of(30), 0) < 0.99f) FAIL("sun: ground beside the square (z=30) should be lit");
    if (px(mask, gx_of(-30), gy_of(0), 0) < 0.99f) FAIL("sun: ground x=−30 should be lit (ray passes the square's edge)");
    // 2. 反投影 + refit：导出的裁剪坐标 → 世界坐标 → 以绑定姿势 BLAS 为源 refit 到槽；TLAS 换成槽；阴影应移到 x∈(10,30) | 2. unproject + refit: shadow moves to x∈(10,30)
    di[1].accelerationStructureID = as_slot.gpuResourceID;
    if (run(^(id<MTL4ComputeCommandEncoder> ce) {
        [ce setArgumentTable:at]; [ce setComputePipelineState:ps_unproj];
        [at setAddress:clip.gpuAddress atIndex:0]; [at setAddress:pos_slot.gpuAddress atIndex:1]; [at setAddress:ub.gpuAddress atIndex:2]; [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce refitAccelerationStructure:as_s descriptor:blas_desc(pos_slot, ib, 2, 1) destination:as_slot scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr} options:MTLAccelerationStructureRefitOptionVertexData];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:tlas descriptor:td scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_trace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:mask.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:kb.gpuAddress atIndex:1];
        [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
        // 点光 (30,40,0)：地面 x≈19.5 被移位后的方板挡，x=−35 不挡 | point light (30,40,0): ground x≈19.5 is blocked by the shifted square, x=−35 is not
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_lclear]; [at setAddress:seen.gpuAddress atIndex:0]; [at setAddress:list.gpuAddress atIndex:1]; [ce dispatchThreadgroups:MTLSizeMake(32, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_lgather]; [at setAddress:hdrb.gpuAddress atIndex:0]; [at setAddress:slotb.gpuAddress atIndex:1]; [at setAddress:seen.gpuAddress atIndex:2]; [at setAddress:list.gpuAddress atIndex:3]; [at setAddress:lgb.gpuAddress atIndex:4];
        [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_pick]; [at setAddress:lrec.gpuAddress atIndex:0]; [at setAddress:picks.gpuAddress atIndex:1]; [at setAddress:pkb.gpuAddress atIndex:2]; [at setAddress:prev.gpuAddress atIndex:3]; [at setAddress:list.gpuAddress atIndex:4];
        [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_lcopy]; [at setAddress:lrec.gpuAddress atIndex:0]; [at setAddress:lcopyb.gpuAddress atIndex:1]; [at setAddress:picks.gpuAddress atIndex:2]; [at setAddress:lcb.gpuAddress atIndex:3];
        [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce setComputePipelineState:ps_ptrace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:pm.gpuResourceID atIndex:1]; [at setResource:tlas.gpuResourceID atBufferIndex:0]; [at setAddress:pb.gpuAddress atIndex:1]; [at setAddress:picks.gpuAddress atIndex:2];
        [ce dispatchThreadgroups:MTLSizeMake(W / 16, H / 16, 1) threadsPerThreadgroup:tt];
        // 软阴影：最近命中 + 两趟模糊 → raw / tmp / mask | soft shadows: nearest hit + two blur passes
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_soft]; [at setTexture:raw.gpuResourceID atIndex:1]; [at setAddress:kb.gpuAddress atIndex:1]; [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_blur]; [at setTexture:raw.gpuResourceID atIndex:0]; [at setTexture:tmp.gpuResourceID atIndex:1]; [at setTexture:depth.gpuResourceID atIndex:2]; [at setAddress:bb0.gpuAddress atIndex:0]; [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [at setTexture:tmp.gpuResourceID atIndex:0]; [at setTexture:mask.gpuResourceID atIndex:1]; [at setAddress:bb1.gpuAddress atIndex:0]; [ce dispatchThreadgroups:tg threadsPerThreadgroup:tt];
    })) return 1;
    { const float *p = pos_slot.contents; for (int i = 0; i < 4; i++) { float want[3] = {sqw[i][0] + 30, sqw[i][1], sqw[i][2]}; for (int c = 0; c < 3; c++) if (fabsf(p[i * 3 + c] - want[c]) > 1e-2f) FAIL("unproject v%d.%d = %g (want %g)", i, c, p[i * 3 + c], want[c]); } }
    // 太阳遮罩此时是软阴影结果：移位后 x∈(10,30) 暗、x=−10 亮；硬边两侧应出现中间值 | the sun mask is now the soft result: x∈(10,30) dark, x=−10 lit; intermediate values must appear at the edges
    if (px(mask, gx_of(20), gy_of(0), 0) > 0.6f) FAIL("refit: ground x=20 should be (softly) shadowed by the shifted square (%g)", px(mask, gx_of(20), gy_of(0), 0));   // 硬判定在下面的 raw 上 | the hard check is on raw below
    if (px(mask, gx_of(-10), gy_of(0), 0) < 0.7f) FAIL("refit: ground x=−10 should be lit now (%g)", px(mask, gx_of(-10), gy_of(0), 0));
    if (px(raw, gx_of(20), gy_of(0), 0) > 0.01f || px(raw, gx_of(20), gy_of(0), 1) < 0.02f) FAIL("soft trace: hit + distance expected at x=20 (%g, %g)", px(raw, gx_of(20), gy_of(0), 0), px(raw, gx_of(20), gy_of(0), 1));
    { int mid = 0, hard_mid = 0; for (int x = 0; x < W; x++) { float v = px(mask, x, gy_of(0), 0), r = px(raw, x, gy_of(0), 0); if (v > 0.1f && v < 0.9f) mid++; if (r > 0.1f && r < 0.9f) hard_mid++; }
      if (hard_mid != 0) FAIL("hard mask has %d intermediate values", hard_mid); if (mid < 2) FAIL("soft mask has only %d intermediate values along the shadow row", mid); }
    { const int32_t *o = picks.contents; const float *of = picks.contents;
      if (o[20] != 2 || o[0] != 4 || o[1] != 9 || o[2] != -1 || fabsf(of[4] - 30) > 1e-3f || fabsf(of[5] - 40) > 1e-3f) FAIL("rts_lgather + rts_pick: n=%d rec %d %d %d first (%g, %g)", o[20], o[0], o[1], o[2], of[4], of[5]);
      if (o[21] != 4) FAIL("rts_pick: player light record %d (want texel 4)", o[21]);
      const float *lc = lcopyb.contents; if (fabsf(lc[5 * 4 + 0] - 1.0f) > 1e-5f || fabsf(lc[5 * 4 + 3] - 60.f) > 1e-5f || fabsf(lc[10 * 4 + 0] - 0.5f) > 1e-5f || fabsf(lc[4 * 4 + 0] - 30) > 1e-5f)
          FAIL("rts_lcopy: player color %g (want 1.0 = 2.0 x 0.5), radius %g, other %g", lc[20], lc[23], lc[40]);
      if (fabsf(((const float *)lrec.contents)[5 * 4] - 2.0f) > 1e-6f) FAIL("rts_lcopy wrote the source records"); }
    if (px(pm, gx_of(19.5) / 2, gy_of(0) / 2, 0) > 0.01f) FAIL("point light: ground x≈19.5 should be shadowed (%g)", px(pm, gx_of(19.5) / 2, gy_of(0) / 2, 0));
    if (px(pm, gx_of(-35) / 2, gy_of(0) / 2, 0) < 0.99f) FAIL("point light: ground x=−35 should be lit");
    if (px(pm, gx_of(-35) / 2, gy_of(0) / 2, 2) < 0.99f) FAIL("point light: unused channels stay lit");
    // 镂空遮挡体（第四阶段）：判定库公共头 + 一个合成的「游戏片元着色器」（贴图 alpha < 阈值丢弃）+ 带 RTS_ALPHA 的追踪核一起编；方板 y=1、x∈[−1,1]、z∈[0,1]，
    // 插值 locn 2 = (u, v) = ((x+1)/2, z)，贴图 4×1 左半 alpha 0、右半 1；竖直向上的射线：左半穿过、右半挡住（距离 1）；判定序号 ~0 的记录按不透明处理（全挡）
    // Alpha-tested occluders (phase 4): the library's common head + a synthetic "game fragment shader" (discard when texture alpha < threshold) + the trace kernels with RTS_ALPHA compiled together; a square at y=1, x∈[−1,1], z∈[0,1],
    // interpolant locn 2 = (u, v) = ((x+1)/2, z), a 4×1 texture with alpha 0 on the left half and 1 on the right; vertical rays upward: the left half passes, the right half blocks (distance 1); a record with index ~0 counts as opaque (all block)
    {
        const char *asrc =
            "namespace rtsa_t { struct spvDescriptorSetBuffer0 { texture2d<float> tx [[id(0)]]; sampler sm [[id(1)]]; constant float4 *thr [[id(2)]]; }; struct main0_in { float4 m_2; };\n"
            "static inline bool main0(thread const main0_in& in, constant spvDescriptorSetBuffer0& d, float4 gl_FragCoord) { float a = d.tx.sample(d.sm, in.m_2.xy, level(0.0)).w; if (a < d.thr->x) { return false; } if (!(gl_FragCoord.w > 0.0)) { return false; } return true; } }\n"
            "static inline bool rts_alpha_eval(constant RtsAI &r, uint3 ix, float2 b, float4 fc) { switch (r.fs) { case 0: { rtsa_t::main0_in in; in.m_2 = rts_vl(r, ix, b, 2); return rtsa_t::main0(in, *(constant rtsa_t::spvDescriptorSetBuffer0 *)r.args, fc); } default: return true; } }\n";
        const char *tk = "kernel void t_alpha(instance_acceleration_structure as [[buffer(0)]], device float2 *o [[buffer(1)]], constant RtsAI *ai [[buffer(3)]], uint i [[thread_position_in_grid]]) {\n"
            "  if (i >= 64u) return; ray r; r.origin = float3(-1.0 + (float(i) + 0.5) / 32.0, 0.0, 0.5); r.direction = float3(0, 1, 0); r.min_distance = 0.0; r.max_distance = 10.0;\n"
            "  float d1, d2; bool h1 = rts_hit(r, as, ai, true, d1), h2 = rts_hit(r, as, ai, false, d2); o[i] = float2(h1 ? 1.0 : 0.0, h2 ? d2 : -1.0); }\n";
        NSString *src = [NSString stringWithFormat:@"#define RTS_ALPHA 1\n%s%s%s%s", k_rts_alpha_head, asrc, k_rts_msl, tk];
        id<MTLLibrary> alib = [dev newLibraryWithSource:src options:nil error:&e]; if (!alib) FAIL("RTS_ALPHA library compile: %s", e.localizedDescription.UTF8String);
        for (NSString *fn in @[@"rts_trace", @"rts_trace_soft", @"rts_ptrace", @"t_alpha"]) if (![dev newComputePipelineStateWithFunction:[alib newFunctionWithName:fn] error:&e]) FAIL("RTS_ALPHA pipeline %s: %s", fn.UTF8String, e.localizedDescription.UTF8String);
        id<MTLComputePipelineState> ps_ta = [dev newComputePipelineStateWithFunction:[alib newFunctionWithName:@"t_alpha"] error:&e];
        float qp[4][3] = {{-1, 1, 0}, {1, 1, 0}, {1, 1, 1}, {-1, 1, 1}}; float var[4][2][4];
        for (int v = 0; v < 4; v++) { var[v][0][0] = qp[v][0]; var[v][0][1] = qp[v][1]; var[v][0][2] = qp[v][2]; var[v][0][3] = 1; var[v][1][0] = (qp[v][0] + 1) / 2; var[v][1][1] = qp[v][2]; var[v][1][2] = var[v][1][3] = 0; }
        id<MTLBuffer> qpos = buf(qp, sizeof qp), qvar = buf(var, sizeof var), qout = buf(NULL, 64 * 8); float thr[4] = {0.5f, 0, 0, 0}; id<MTLBuffer> qthr = buf(thr, 16);
        MTLTextureDescriptor *td2 = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:1 mipmapped:NO]; td2.usage = MTLTextureUsageShaderRead; td2.storageMode = MTLStorageModeShared;
        id<MTLTexture> qtx = [dev newTextureWithDescriptor:td2]; uint8_t tp[16] = {255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255}; [qtx replaceRegion:MTLRegionMake2D(0, 0, 4, 1) mipmapLevel:0 withBytes:tp bytesPerRow:16]; [resident addAllocation:qtx];
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.supportArgumentBuffers = YES; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest; id<MTLSamplerState> qs = [dev newSamplerStateWithDescriptor:sd];
        uint64_t args[3] = {qtx.gpuResourceID._impl, qs.gpuResourceID._impl, qthr.gpuAddress}; id<MTLBuffer> qargs = buf(args, sizeof args);
        typedef struct { float lvp[16]; float vp[4]; float dr[2]; uint32_t fs, stride; uint64_t args, var, ib; uint32_t isz, locmask; uint8_t slot[16]; } t_ai;   // 与 nvn_rtshadow.inc rts_ai 同布局 | same layout as rts_ai in nvn_rtshadow.inc
        _Static_assert(sizeof(t_ai) == 144, "t_ai layout");
        t_ai rec = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {0, 0, 64, 64}, {0, 1}, 0, 2, qargs.gpuAddress, qvar.gpuAddress, ib.gpuAddress, 2, 1u << 2, {0}}; rec.slot[2] = 1;
        id<MTLBuffer> qai = buf(&rec, sizeof rec);
        MTLAccelerationStructureSizes qsz = [dev accelerationStructureSizesWithDescriptor:blas_desc(qpos, ib, 2, 1)]; id<MTLAccelerationStructure> qas = [dev newAccelerationStructureWithSize:qsz.accelerationStructureSize];
        id<MTLBuffer> qinst = buf(NULL, sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)); MTLIndirectAccelerationStructureInstanceDescriptor *qd = qinst.contents; memset(qd, 0, sizeof *qd);
        qd->options = MTLAccelerationStructureInstanceOptionNonOpaque; qd->mask = 0xff; qd->userID = 0; qd->accelerationStructureID = qas.gpuResourceID; qd->transformationMatrix.columns[0].x = qd->transformationMatrix.columns[1].y = qd->transformationMatrix.columns[2].z = 1;
        MTL4InstanceAccelerationStructureDescriptor *qtd = [MTL4InstanceAccelerationStructureDescriptor new]; qtd.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; qtd.instanceCount = 1;
        qtd.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor); qtd.instanceDescriptorBuffer = (MTL4BufferRange){qinst.gpuAddress, qinst.length};
        id<MTLAccelerationStructure> qtl = [dev newAccelerationStructureWithSize:[dev accelerationStructureSizesWithDescriptor:qtd].accelerationStructureSize];
        [resident addAllocation:qas]; [resident addAllocation:qtl]; [resident commit];
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) ((t_ai *)qai.contents)->fs = ~0u;   // 抓不到参数块的记录：当不透明 | a record whose argument block was not captured: opaque
            if (run(^(id<MTL4ComputeCommandEncoder> ce) {
                [ce buildAccelerationStructure:qas descriptor:blas_desc(qpos, ib, 2, 1) scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
                [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
                [ce buildAccelerationStructure:qtl descriptor:qtd scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
                [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
                [ce setArgumentTable:at]; [ce setComputePipelineState:ps_ta]; [at setResource:qtl.gpuResourceID atBufferIndex:0]; [at setAddress:qout.gpuAddress atIndex:1]; [at setAddress:qai.gpuAddress atIndex:3];
                [ce dispatchThreads:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            })) return 1;
            const float *o = qout.contents;
            for (int i = 0; i < 64; i++) { float x = -1.f + (i + 0.5f) / 32.f; int want = pass == 1 || x > 0;
                if ((o[i * 2] > 0.5f) != want) FAIL("alpha %s: ray x=%.3f any-hit %g (want %d)", pass ? "opaque fallback" : "judge", x, o[i * 2], want);
                if (want ? fabsf(o[i * 2 + 1] - 1.f) > 1e-3f : o[i * 2 + 1] >= 0) FAIL("alpha %s: ray x=%.3f closest %g", pass ? "opaque fallback" : "judge", x, o[i * 2 + 1]); }
        }
    }
    puts("PASS t_rtshadow: MSL compiles, decode / BLAS / TLAS / sun trace / unproject / refit / GPU light picking / player-light copy / point light / soft blur / alpha-tested judge (RTS_ALPHA) checked on a synthetic scene");
    return 0;
} }
