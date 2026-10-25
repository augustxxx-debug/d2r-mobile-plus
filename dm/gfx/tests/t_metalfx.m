// 用合成纯色验证 MetalFX 输出、原版遮罩相乘和尺寸检查，不读取游戏资源。
// Verify MetalFX output, original-mask multiplication and dimension guards with synthetic colors; no game resources.
#import "../nvn/nvn_metalfx.h"
#include <stdio.h>
#include <stdlib.h>
#ifndef DM_NVN_M3
#define DM_NVN_M3 0
#endif
int main(void) { @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (![NvMetalFX supported:device]) { puts("SKIP MetalFX: GPU/backend not supported"); return 77; }
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:64 height:64 mipmapped:NO];
    d.storageMode = MTLStorageModeShared; d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> input = [device newTextureWithDescriptor:d];
    uint16_t *pixels = calloc(64*64*4,sizeof *pixels);
    for (int i = 0; i < 64*64; i++) { pixels[i*4] = 0x3800; pixels[i*4+1] = 0x3400; pixels[i*4+3] = 0x3c00; }
    [input replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:pixels bytesPerRow:64*8]; free(pixels);
    d.pixelFormat = MTLPixelFormatR8Unorm;
    id<MTLTexture> mask = [device newTextureWithDescriptor:d]; unsigned char maskBytes[64*64]; memset(maskBytes,128,sizeof maskBytes);
    [mask replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:maskBytes bytesPerRow:64];
    d.width = d.height = 128; d.pixelFormat = MTLPixelFormatRGBA8Unorm; d.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    id<MTLTexture> output = [device newTextureWithDescriptor:d];
    NvMetalFX *fx = [[NvMetalFX alloc] initWithDevice:device input:input output:output];
    if (!fx) { puts("FAIL MetalFX initialization"); return 1; }
    for (int cycle=0;cycle<3;cycle++) {
    memset(maskBytes,cycle == 1 ? 64 : 128,sizeof maskBytes);
    [mask replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:maskBytes bytesPerRow:64];
#if DM_NVN_M3
    id<MTLCommandQueue> queue = [device newCommandQueue]; id<MTLCommandBuffer> cb = [queue commandBuffer];
#else
    NSError *error = nil;
    id<MTL4CommandQueue> queue = [device newMTL4CommandQueueWithDescriptor:[MTL4CommandQueueDescriptor new] error:&error];
    id<MTLResidencySet> resident = [device newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&error];
    [resident addAllocation:input]; [resident addAllocation:mask]; [resident addAllocation:output];
    for (id<MTLResource> resource in fx.resources) [resident addAllocation:resource];
    [resident commit]; [queue addResidencySet:resident];
    id<MTL4CommandAllocator> allocator = [device newCommandAllocatorWithDescriptor:[MTL4CommandAllocatorDescriptor new] error:&error];
    id<MTL4CommandBuffer> cb = [device newCommandBuffer]; [cb beginCommandBufferWithAllocator:allocator];
#endif
    if ([fx encode:cb input:input contentWidth:65 contentHeight:64 mask:mask output:output]) { puts("FAIL invalid input extent accepted"); return 1; }
    if (![fx encode:cb input:input contentWidth:64 contentHeight:64 mask:mask output:output]) { puts("FAIL encode"); return 1; }
#if DM_NVN_M3
    [cb commit]; [cb waitUntilCompleted]; if (cb.error) { puts("FAIL GPU command"); return 1; }
#else
    [cb endCommandBuffer]; dispatch_semaphore_t finished = dispatch_semaphore_create(0); __block BOOL gpuError = NO;
    MTL4CommitOptions *options = [MTL4CommitOptions new]; [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) { gpuError = feedback.error != nil; dispatch_semaphore_signal(finished); }];
    id<MTL4CommandBuffer> buffers[] = {cb}; [queue commit:buffers count:1 options:options];
    if (dispatch_semaphore_wait(finished,dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC)) || gpuError) { puts("FAIL GPU completion"); return 1; }
#endif
    unsigned char readback[128*128*4]; [output getBytes:readback bytesPerRow:128*4 fromRegion:MTLRegionMake2D(0,0,128,128) mipmapLevel:0];
    int checks = 0;
    for (int y = 4; y < 124; y+=17) for (int x = 4; x < 124; x+=17) { unsigned char *p = readback+(y*128+x)*4; checks++;
        if (abs((int)p[0]-(cycle == 1 ? 32 : 64))>3 || abs((int)p[1]-(cycle == 1 ? 16 : 32))>3 || p[2]>2 || p[3]!=255) { printf("FAIL color/mask at %d,%d: %u %u %u %u\n",x,y,p[0],p[1],p[2],p[3]); return 1; } }
    printf("PASS MetalFX backend %d: %d color/mask samples; invalid extent rejected\n",DM_NVN_M3?3:4,checks);
    }
    return 0;
} }
