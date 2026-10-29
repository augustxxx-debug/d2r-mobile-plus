// 低分辨率太阳（第 2 / 3 档）+ rts_pup 上采样不把背光面的 0 漏到相邻受光面（十六审 Q16-02，审计方 harness 收进仓库）：球体（角色 / 岩石近似）站在地面上、正交俯视，
// 生产核 rts_trace（全 / 1/2 / 1/4 分辨率，nlmin = 0 背光写 0）+ rts_pup；四个太阳角，量低分辨率上采样相对全分辨率「受光处被压暗、且在背光面 3 像素内」的像素。
// 块左上取样（老的错位）时 1/4 分辨率这类像素 193–395 个；块中心取样（与 rts_pup 的块中心深度权重一致）45–97 个。门槛：每个太阳角 1/4 ≤ 150、1/2 ≤ 120，1/4 总不同 ≤ 850
// Reduced-resolution sun (levels 2 / 3) + rts_pup upsampling must not leak back-face 0 into adjacent lit surfaces (16th review Q16-02; the auditor's harness, taken into the repository): spheres (characters / rocks) on the ground, orthographic top view,
// production kernels rts_trace (full / 1/2 / 1/4, nlmin = 0, back faces written 0) + rts_pup; for four sun angles, counts pixels lit at full resolution but darkened after upsampling within 3 px of a back face.
// Sampling the block's top-left (the old misalignment) gives 193–395 such pixels at 1/4; sampling the block centre (matching rts_pup's block-centre depth weights) 45–97. Thresholds: per angle 1/4 ≤ 150, 1/2 ≤ 120, 1/4 total differences ≤ 850
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../nvn/nvn_rtshadow_util.h"
#include "../nvn/nvn_rtshadow_msl.h"
#define W 512
#define H 512
static id<MTLDevice> dev; static id<MTL4CommandQueue> queue; static id<MTL4CommandAllocator> alloc; static id<MTLResidencySet> resident;
static int run(void (^body)(id<MTL4ComputeCommandEncoder> ce)) {
    id<MTL4CommandBuffer> cb = [dev newCommandBuffer]; [cb beginCommandBufferWithAllocator:alloc];
    id<MTL4ComputeCommandEncoder> ce = [cb computeCommandEncoder]; body(ce); [ce endEncoding]; [cb endCommandBuffer];
    dispatch_semaphore_t done = dispatch_semaphore_create(0); __block NSError *err = nil;
    MTL4CommitOptions *o = [MTL4CommitOptions new]; [o addFeedbackHandler:^(id<MTL4CommitFeedback> f) { err = f.error; dispatch_semaphore_signal(done); }];
    id<MTL4CommandBuffer> bufs[] = {cb}; [queue commit:bufs count:1 options:o];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC))) { puts("GPU timeout"); return 1; }
    if (err) { printf("GPU error: %s\n", err.localizedDescription.UTF8String); return 1; } return 0; }
static id<MTLBuffer> buf(const void *p, size_t n) { id<MTLBuffer> b = [dev newBufferWithLength:n options:MTLResourceStorageModeShared]; if (p) memcpy(b.contents, p, n); [resident addAllocation:b]; return b; }
static id<MTLTexture> tex(MTLPixelFormat f, int w, int h) { MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO]; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModeShared; id<MTLTexture> t = [dev newTextureWithDescriptor:d]; [resident addAllocation:t]; return t; }
static void rd(id<MTLTexture> t, uint8_t *o) { [t getBytes:o bytesPerRow:t.width fromRegion:MTLRegionMake2D(0, 0, t.width, t.height) mipmapLevel:0]; }
// 场景：地面 y=0；球 S[i] = (cx, cy, cz, r) | scene
#define NS 3
static const double SPH[NS][4] = {{0, 8, 0, 8}, {25, 12, -20, 6}, {-25, 3, 22, 3}};
static double SUN[3];
static int hit_sph(const double o[3], const double d[3]) { for (int i = 0; i < NS; i++) { double oc[3] = {o[0] - SPH[i][0], o[1] - SPH[i][1], o[2] - SPH[i][2]}; double b = oc[0] * d[0] + oc[1] * d[1] + oc[2] * d[2], c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - SPH[i][3] * SPH[i][3]; double h = b * b - c; if (h < 0) continue; double t = -b - sqrt(h); if (t > 1e-3 || -b + sqrt(h) > 1e-3) return 1; } return 0; }
static double height(double x, double z, int *who) { double best = 0; *who = -1; for (int i = 0; i < NS; i++) { double dx = x - SPH[i][0], dz = z - SPH[i][2], d2 = dx * dx + dz * dz; if (d2 < SPH[i][3] * SPH[i][3]) { double h = SPH[i][1] + sqrt(SPH[i][3] * SPH[i][3] - d2); if (h > best) { best = h; *who = i; } } } return best; }
static int one(double el, double az, int res[3]) { @autoreleasepool {   // res：1/2 邻近背光面被压暗、1/4 同、1/4 总不同 | res: 1/2 near-back-face darkened, 1/4 the same, 1/4 total differences
    el *= M_PI / 180; az *= M_PI / 180;
    SUN[0] = cos(el) * cos(az); SUN[1] = sin(el); SUN[2] = cos(el) * sin(az);
    dev = MTLCreateSystemDefaultDevice(); if (!dev || ![dev supportsRaytracing]) { puts("SKIP"); return 77; }
    NSError *e = nil; id<MTLLibrary> lib = [dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e]; if (!lib) { printf("MSL: %s\n", e.localizedDescription.UTF8String); return 1; }
    id<MTLComputePipelineState> ps_trace = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_trace"] error:&e], ps_pup = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_pup"] error:&e];
    queue = [dev newMTL4CommandQueueWithDescriptor:[MTL4CommandQueueDescriptor new] error:&e]; resident = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&e]; [queue addResidencySet:resident];
    alloc = [dev newCommandAllocatorWithDescriptor:[MTL4CommandAllocatorDescriptor new] error:&e];
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 5; ad.maxTextureBindCount = 3; id<MTL4ArgumentTable> at = [dev newArgumentTableWithDescriptor:ad error:&e];
    // 几何：地面 2 三角 + 每个球 UV 64×32 | geometry
    int SEG = 64, RING = 32; int nv = 4 + NS * (SEG + 1) * (RING + 1); int nt = 2 + NS * SEG * RING * 2;
    float *V = calloc(nv * 3, 4); uint32_t *I = calloc(nt * 3, 4); int v = 0, t = 0;
    float g4[4][3] = {{-100, 0, -100}, {100, 0, -100}, {100, 0, 100}, {-100, 0, 100}}; for (int i = 0; i < 4; i++) memcpy(V + 3 * v++, g4[i], 12);
    I[t * 3] = 0; I[t * 3 + 1] = 1; I[t * 3 + 2] = 2; t++; I[t * 3] = 0; I[t * 3 + 1] = 2; I[t * 3 + 2] = 3; t++;
    for (int s = 0; s < NS; s++) { int base = v; for (int r = 0; r <= RING; r++) for (int q = 0; q <= SEG; q++) { double th = M_PI * r / RING, ph = 2 * M_PI * q / SEG; double R = SPH[s][3] * 1.002;   // 外接一点，免得面片在解析球面下方
            V[3 * v] = SPH[s][0] + R * sin(th) * cos(ph); V[3 * v + 1] = SPH[s][1] + R * cos(th); V[3 * v + 2] = SPH[s][2] + R * sin(th) * sin(ph); v++; }
        for (int r = 0; r < RING; r++) for (int q = 0; q < SEG; q++) { int a = base + r * (SEG + 1) + q, b = a + SEG + 1; I[t * 3] = a; I[t * 3 + 1] = b; I[t * 3 + 2] = a + 1; t++; I[t * 3] = a + 1; I[t * 3 + 1] = b; I[t * 3 + 2] = b + 1; t++; } }
    id<MTLBuffer> vb = buf(V, nv * 12), ib = buf(I, nt * 12); free(V); free(I);
    MTL4PrimitiveAccelerationStructureDescriptor *pd = [MTL4PrimitiveAccelerationStructureDescriptor new]; MTL4AccelerationStructureTriangleGeometryDescriptor *gd = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
    gd.vertexBuffer = (MTL4BufferRange){vb.gpuAddress, vb.length}; gd.vertexFormat = MTLAttributeFormatFloat3; gd.vertexStride = 12; gd.triangleCount = nt; gd.opaque = YES; gd.indexBuffer = (MTL4BufferRange){ib.gpuAddress, ib.length}; gd.indexType = MTLIndexTypeUInt32; pd.geometryDescriptors = @[gd];
    MTLAccelerationStructureSizes sz = [dev accelerationStructureSizesWithDescriptor:pd]; id<MTLAccelerationStructure> blas = [dev newAccelerationStructureWithSize:sz.accelerationStructureSize];
    id<MTLBuffer> inst = buf(NULL, sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)); MTLIndirectAccelerationStructureInstanceDescriptor *di = inst.contents; memset(di, 0, sizeof *di);
    di->transformationMatrix.columns[0].x = di->transformationMatrix.columns[1].y = di->transformationMatrix.columns[2].z = 1; di->options = MTLAccelerationStructureInstanceOptionOpaque; di->mask = 0xff; di->accelerationStructureID = blas.gpuResourceID;
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new]; td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = 1; td.instanceDescriptorStride = sizeof *di; td.instanceDescriptorBuffer = (MTL4BufferRange){inst.gpuAddress, inst.length};
    MTLAccelerationStructureSizes szt = [dev accelerationStructureSizesWithDescriptor:td]; id<MTLAccelerationStructure> tlas = [dev newAccelerationStructureWithSize:szt.accelerationStructureSize];
    NSUInteger scr = MAX(sz.buildScratchBufferSize, szt.buildScratchBufferSize); id<MTLBuffer> scratch = [dev newBufferWithLength:scr options:MTLResourceStorageModePrivate];
    for (id r in @[blas, tlas, scratch]) [resident addAllocation:r];
    // 相机：正交俯视，x = ndc.x·50、z = ndc.y·50、深度 = 0.5 − 0.01·y（正向深度，近平面 0，zn = 0）
    double VP[16] = {0}; VP[0] = 1.0 / 50; VP[2 * 4 + 1] = 1.0 / 50; VP[1 * 4 + 2] = -0.01; VP[3 * 4 + 2] = 0.5; VP[15] = 1; double IA[16]; rts_inv4(VP, IA);
    float *dz = malloc(W * H * 4); int *who = malloc(W * H * sizeof(int)); uint8_t *truth = malloc(W * H), *backf = malloc(W * H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { double wx = ((x + 0.5) / W * 2 - 1) * 50, wz = (1 - (y + 0.5) / H * 2) * 50; int w; double h = height(wx, wz, &w); dz[y * W + x] = (float)(0.5 - 0.01 * h); who[y * W + x] = w;
        double n[3] = {0, 1, 0}; if (w >= 0) { n[0] = (wx - SPH[w][0]) / SPH[w][3]; n[1] = (h - SPH[w][1]) / SPH[w][3]; n[2] = (wz - SPH[w][2]) / SPH[w][3]; }
        double ndl = n[0] * SUN[0] + n[1] * SUN[1] + n[2] * SUN[2]; backf[y * W + x] = ndl < 0;
        double o[3] = {wx + n[0] * 0.05, h + n[1] * 0.05, wz + n[2] * 0.05}; truth[y * W + x] = ndl < 0 ? 0 : !hit_sph(o, SUN); }
    id<MTLBuffer> dzb = buf(dz, W * H * 4); free(dz);
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:W height:H mipmapped:NO]; dd.usage = MTLTextureUsageShaderRead; dd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [dev newTextureWithDescriptor:dd]; [resident addAllocation:depth];
    float K[64] = {0}; for (int i = 0; i < 16; i++) K[i] = (float)IA[i]; K[16] = SUN[0]; K[17] = SUN[1]; K[18] = SUN[2]; ((uint32_t *)K)[20] = W; ((uint32_t *)K)[21] = H; K[22] = 0.6f; K[23] = 0; K[24] = 1; K[25] = 0; K[26] = 0;   // 生产缺省：偏移 0.6、法线偏移 1、背光阈值 0
    float KH[64], KQ[64]; memcpy(KH, K, sizeof K); memcpy(KQ, K, sizeof K); KH[23] = 1; ((uint32_t *)KH)[20] = W / 2; ((uint32_t *)KH)[21] = H / 2; KQ[23] = 4; ((uint32_t *)KQ)[20] = W / 4; ((uint32_t *)KQ)[21] = H / 4;
    id<MTLBuffer> kb = buf(K, 256), kbh = buf(KH, 256), kbq = buf(KQ, 256); uint32_t puh[4] = {W, H, W / 2, H / 2}, puq[4] = {W, H, W / 4, H / 4}; id<MTLBuffer> pubh = buf(puh, 16), pubq = buf(puq, 16);
    id<MTLTexture> mf = tex(MTLPixelFormatR8Unorm, W, H), mh = tex(MTLPixelFormatR8Unorm, W / 2, H / 2), mq = tex(MTLPixelFormatR8Unorm, W / 4, H / 4), uh = tex(MTLPixelFormatR8Unorm, W, H), uq = tex(MTLPixelFormatR8Unorm, W, H);
    [resident commit];
    MTLSize tt = MTLSizeMake(8, 8, 1);
    if (run(^(id<MTL4ComputeCommandEncoder> ce) {
        [ce copyFromBuffer:dzb sourceOffset:0 sourceBytesPerRow:W * 4 sourceBytesPerImage:W * H * 4 sourceSize:MTLSizeMake(W, H, 1) toTexture:depth destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [ce buildAccelerationStructure:blas descriptor:pd scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        [ce buildAccelerationStructure:tlas descriptor:td scratchBuffer:(MTL4BufferRange){scratch.gpuAddress, scr}];
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure | MTLStageBlit beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setArgumentTable:at]; [ce setComputePipelineState:ps_trace]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setResource:tlas.gpuResourceID atBufferIndex:0];
        [at setTexture:mf.gpuResourceID atIndex:1]; [at setAddress:kb.gpuAddress atIndex:1]; [ce dispatchThreadgroups:MTLSizeMake(W / 8, H / 8, 1) threadsPerThreadgroup:tt];
        [at setTexture:mh.gpuResourceID atIndex:1]; [at setAddress:kbh.gpuAddress atIndex:1]; [ce dispatchThreadgroups:MTLSizeMake(W / 16, H / 16, 1) threadsPerThreadgroup:tt];
        [at setTexture:mq.gpuResourceID atIndex:1]; [at setAddress:kbq.gpuAddress atIndex:1]; [ce dispatchThreadgroups:MTLSizeMake(W / 32, H / 32, 1) threadsPerThreadgroup:tt];
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setComputePipelineState:ps_pup]; [at setTexture:depth.gpuResourceID atIndex:2];
        [at setTexture:mh.gpuResourceID atIndex:0]; [at setTexture:uh.gpuResourceID atIndex:1]; [at setAddress:pubh.gpuAddress atIndex:0]; [ce dispatchThreadgroups:MTLSizeMake(W / 8, H / 8, 1) threadsPerThreadgroup:tt];
        [at setTexture:mq.gpuResourceID atIndex:0]; [at setTexture:uq.gpuResourceID atIndex:1]; [at setAddress:pubq.gpuAddress atIndex:0]; [ce dispatchThreadgroups:MTLSizeMake(W / 8, H / 8, 1) threadsPerThreadgroup:tt]; })) return 1;
    uint8_t *F = malloc(W * H), *UH = malloc(W * H), *UQ = malloc(W * H); rd(mf, F); rd(uh, UH); rd(uq, UQ);
    // 邻近背光面：3 像素内（全分辨率）有背光面像素的「非背光面」像素 | near a back face: non-back-face pixels with a back-face pixel within 3 px
    uint8_t *nearb = calloc(W * H, 1); for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { if (backf[y * W + x]) continue; for (int j = -3; j <= 3 && !nearb[y * W + x]; j++) for (int i = -3; i <= 3; i++) { int xx = x + i, yy = y + j; if (xx >= 0 && yy >= 0 && xx < W && yy < H && backf[yy * W + xx]) { nearb[y * W + x] = 1; break; } } }
    int nback = 0, fdiff_back = 0, fdiff_other = 0; for (int i = 0; i < W * H; i++) { nback += backf[i]; int f = F[i] > 127; if (f != truth[i]) { if (backf[i]) fdiff_back++; else fdiff_other++; } }
    printf("太阳 仰角 %.0f° 方位 %.0f°；背光面像素 %d / %d\n", el * 180 / M_PI, az * 180 / M_PI, nback, W * H);
    printf("全分辨率 vs 解析真值（新语义：背光面 = 0）：背光面像素不一致 %d（%.1f%% 的背光面）；其余不一致 %d\n", fdiff_back, nback ? fdiff_back * 100.0 / nback : 0, fdiff_other);
    const char *nm[2] = {"1/2", "1/4"}; uint8_t *U[2] = {UH, UQ};
    for (int k = 0; k < 2; k++) { int dark_lit = 0, dark_lit_nb = 0, dark_lit_gnd_nb = 0, light_sh = 0, light_sh_nb = 0, tot = 0;
        for (int i = 0; i < W * H; i++) { int f = F[i] > 127, u = U[k][i] > 127; if (f != u) tot++;
            if (f && !u) { dark_lit++; if (nearb[i]) { dark_lit_nb++; if (who[i] < 0) dark_lit_gnd_nb++; } }
            if (!f && u) { light_sh++; if (nearb[i] || backf[i]) light_sh_nb++; } }
        printf("%s 分辨率 + rts_pup vs 全分辨率：不同 %d；受光处被压暗（漏影）%d，其中邻近背光面 %d（地面 %d）；影里被提亮（漏光）%d，其中在 / 邻近背光面 %d\n", nm[k], tot, dark_lit, dark_lit_nb, dark_lit_gnd_nb, light_sh, light_sh_nb);
        res[k] = dark_lit_nb; if (k == 1) res[2] = tot; }
    return 0; } }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static const double ang[4][2] = {{45, 20}, {30, 200}, {60, 110}, {40, 300}}; int fail = 0;
    for (int a = 0; a < 4; a++) { int r[3] = {0}; int rc = one(ang[a][0], ang[a][1], r); if (rc) return rc;
        if (r[0] > 120 || r[1] > 150 || r[2] > 850) { printf("FAIL 太阳 %.0f° / %.0f°：邻近背光面被压暗 1/2 %d（≤ 120）、1/4 %d（≤ 150），1/4 总不同 %d（≤ 850）| back-face 0 leaked into lit pixels\n", ang[a][0], ang[a][1], r[0], r[1], r[2]); fail++; } }
    if (fail) return 1;
    puts("PASS t_rts_upsample: reduced-resolution sun + rts_pup keeps back-face 0 off adjacent lit pixels (4 sun angles, 1/2 and 1/4)");
    return 0; }
