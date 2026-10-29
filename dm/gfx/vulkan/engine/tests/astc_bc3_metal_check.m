// BC3 → ASTC 转码器（xbg_astc.c）的硬件核对：随机 BC3 块编成 ASTC 4x4，交给 Mac GPU（Metal）的 ASTC 硬件解码，
// 和 ① 我们自己的规范解码 xbg_astc_decode_own ② 引擎 BC3 软解比。用法：astc_bc3_metal_check [块数，缺省 65536]
// clang -fobjc-arc -O2 -I.. -include macos_pthread_name.h -o <仓外产物> astc_bc3_metal_check.m ../xbg_astc.c ../xbg_texture.c ../../../tex/nvn_deswizzle.c ../../../nvn/nvn_tex.c -framework Metal -framework Foundation
// 2026-10-29 Apple M4 Pro：GPU 与规范解码最多差 1（取整），透明度恒定块与 BC3 软解 RGB ≤ 2、透明度 0 | GPU vs spec decoder within 1 (rounding)
// Hardware check of the BC3 -> ASTC transcoder: random BC3 blocks encoded to ASTC 4x4, decoded by the Mac GPU's ASTC hardware (Metal),
// compared with (1) our spec decoder xbg_astc_decode_own and (2) the engine's BC3 software decode.
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include "xbg_astc.h"
#include "xbg_texture.h"
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }
static uint32_t rng = 777; static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int main(int argc, char **argv) { @autoreleasepool {
    const int BX = 256, BY = argc > 1 ? atoi(argv[1]) / 256 : 256, N = BX * BY;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); if (!dev) { fprintf(stderr, "no Metal device\n"); return 2; }
    printf("GPU %s, %d blocks\n", dev.name.UTF8String, N);
    uint8_t *bc3 = malloc((size_t)N * 16), *astc = malloc((size_t)N * 16); int kinds[3] = {0};
    for (int i = 0; i < N; i++) { uint8_t *b = bc3 + i * 16; for (int k = 0; k < 16; k++) b[k] = (uint8_t)rnd();
        if (i % 3 == 0) { b[0] = b[1] = (uint8_t)(i & 1 ? 255 : 200); for (int k = 2; k < 8; k++) b[k] = 0; }   // 透明度一样（索引全 0） | uniform alpha (all indices 0)
        if (i % 3 == 1) { b[0] = 255; b[1] = 0; }
        kinds[xbg_bc3_to_astc(b, astc + i * 16)]++; }
    printf("encoded: uniform alpha %d, varying alpha %d\n", kinds[1], kinds[2]);
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatASTC_4x4_LDR width:BX * 4 height:BY * 4 mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead; td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [dev newTextureWithDescriptor:td]; if (!tex) { fprintf(stderr, "no ASTC texture support\n"); return 2; }
    [tex replaceRegion:MTLRegionMake2D(0, 0, BX * 4, BY * 4) mipmapLevel:0 withBytes:astc bytesPerRow:BX * 16];
    NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void k(texture2d<float, access::read> t [[texture(0)]], device uchar4 *o [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {"
        "  float4 v = t.read(p); o[p.y * t.get_width() + p.x] = uchar4(rint(clamp(v, 0.0, 1.0) * 255.0)); }";
    NSError *err = nil; id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err]; if (!lib) { fprintf(stderr, "%s\n", err.description.UTF8String); return 2; }
    id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
    id<MTLBuffer> out = [dev newBufferWithLength:(size_t)N * 64 options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> q = [dev newCommandQueue]; id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:ps]; [ce setTexture:tex atIndex:0]; [ce setBuffer:out offset:0 atIndex:0];
    [ce dispatchThreads:MTLSizeMake(BX * 4, BY * 4, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
    const uint8_t *hw = out.contents; int own_max = 0, bc_max_rgb[3] = {0}, bc_max_a[3] = {0}; double own_sum = 0; long own_diff = 0;
    for (int i = 0; i < N; i++) { int bx = i % BX, by = i / BX; uint8_t mine[64], ref[64]; xbg_astc_decode_own(astc + i * 16, 0, mine); xbg_decode_block(0x44, bc3 + i * 16, 16, ref, 64);
        int kind = ((astc[i * 16 + 1] >> 2) & 1) ? 2 : 1;   // 块模式第 10 位 = 双平面 | block-mode bit 10 = dual plane
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) for (int c = 0; c < 4; c++) {
            int g = hw[((by * 4 + y) * BX * 4 + bx * 4 + x) * 4 + c], m = mine[(y * 4 + x) * 4 + c], r = ref[(y * 4 + x) * 4 + c];
            int d = abs(g - m); if (d > own_max) own_max = d; own_sum += d; if (d) own_diff++;
            int e = abs(g - r); if (c < 3) { if (e > bc_max_rgb[kind]) bc_max_rgb[kind] = e; } else if (e > bc_max_a[kind]) bc_max_a[kind] = e; } }
    printf("GPU vs own decoder: max %d, mean %.4f, differing channels %ld of %ld\n", own_max, own_sum / ((double)N * 64), own_diff, (long)N * 64);
    printf("GPU vs BC3 decode: uniform alpha max RGB %d alpha %d; varying alpha max RGB %d alpha %d\n", bc_max_rgb[1], bc_max_a[1], bc_max_rgb[2], bc_max_a[2]);
    int ok = own_max <= 1 && bc_max_rgb[1] <= 3 && bc_max_a[1] <= 1;
    printf("%s\n", ok ? "PASS" : "FAIL"); return ok ? 0 : 1;
} }
