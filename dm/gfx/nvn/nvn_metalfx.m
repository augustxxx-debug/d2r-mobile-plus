// MetalFX 空间超分：使用 Apple 公共 API，仅接收 3D 颜色，保留原版遮罩相乘。
// MetalFX spatial scaling using Apple's public API; takes 3D color only and retains the original mask multiplication.
#import "nvn_metalfx.h"
#import <MetalFX/MetalFX.h>
#ifndef DM_NVN_M3
#define DM_NVN_M3 0
#endif

@implementation NvMetalFX {
#if DM_NVN_M3
    id<MTLFXSpatialScaler> _scaler;
#else
    id<MTL4FXSpatialScaler> _scaler;
    id<MTL4ArgumentTable> _arguments;
#endif
    id<MTLTexture> _scaled;
    id<MTLRenderPipelineState> _pipeline;
    id<MTLSamplerState> _sampler;
    NSUInteger _width, _height, _outWidth, _outHeight;
    MTLPixelFormat _inputFormat, _outputFormat;
}
+ (BOOL)supported:(id<MTLDevice>)device {
    if (!device) return NO;
#if DM_NVN_M3
    return [MTLFXSpatialScalerDescriptor supportsDevice:device];
#else
    return [MTLFXSpatialScalerDescriptor supportsMetal4FX:device];
#endif
}
- (instancetype)initWithDevice:(id<MTLDevice>)device input:(id<MTLTexture>)input output:(id<MTLTexture>)output {
    if (!(self = [super init]) || ![NvMetalFX supported:device] || !input || !output) return nil;
    _width = input.width; _height = input.height; _outWidth = output.width; _outHeight = output.height;
    _inputFormat = input.pixelFormat; _outputFormat = output.pixelFormat;
    MTLFXSpatialScalerDescriptor *d = [MTLFXSpatialScalerDescriptor new];
    d.inputWidth = _width; d.inputHeight = _height; d.outputWidth = _outWidth; d.outputHeight = _outHeight;
    d.colorTextureFormat = _inputFormat; d.outputTextureFormat = _inputFormat;
    d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModeLinear;
#if DM_NVN_M3
    _scaler = [d newSpatialScalerWithDevice:device];
#else
    NSError *error = nil;
    id<MTL4Compiler> compiler = [device newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&error];
    _scaler = compiler ? [d newSpatialScalerWithDevice:device compiler:compiler] : nil;
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxTextureBindCount = 2; ad.maxSamplerStateBindCount = 1;
    _arguments = [device newArgumentTableWithDescriptor:ad error:&error];
    if (!_arguments) return nil;
#endif
    if (!_scaler || (input.usage & _scaler.colorTextureUsage) != _scaler.colorTextureUsage) return nil;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:_inputFormat width:_outWidth height:_outHeight mipmapped:NO];
    td.storageMode = MTLStorageModePrivate; td.usage = _scaler.outputTextureUsage | MTLTextureUsageShaderRead;
    _scaled = [device newTextureWithDescriptor:td];
    _scaled.label = @"Plus MetalFX 3D color";
    if (!_scaled) return nil;
    // 放大后保留原版暗角／遮罩；不触及稍后的 UI 合成，也不重复原版锐化。
    // Preserve the original vignette/mask after scaling; leave later UI composition alone and avoid double sharpening.
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
      "struct V { float4 p [[position]]; float2 uv; };\n"
      "vertex V vs(uint i [[vertex_id]]) { float2 uv=float2((i<<1)&2,i&2); V v; v.p=float4(uv*float2(2,-2)+float2(-1,1),0,1); v.uv=uv; return v; }\n"
      "fragment float4 fs(V v [[stage_in]], texture2d<float> c [[texture(0)]], texture2d<float> m [[texture(1)]], sampler s [[sampler(0)]]) { return float4(c.sample(s,v.uv).rgb*m.sample(s,v.uv).r,1); }\n";
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:nil];
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [library newFunctionWithName:@"vs"]; pd.fragmentFunction = [library newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = _outputFormat;
    _pipeline = [device newRenderPipelineStateWithDescriptor:pd error:nil];
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge; sd.supportArgumentBuffers = YES;
    _sampler = [device newSamplerStateWithDescriptor:sd];
    return _pipeline && _sampler ? self : nil;
}
- (NSArray<id<MTLResource>> *)resources { return @[_scaled]; }
- (BOOL)matchesInput:(id<MTLTexture>)input output:(id<MTLTexture>)output {
    return input.width == _width && input.height == _height && input.pixelFormat == _inputFormat && output.width == _outWidth && output.height == _outHeight && output.pixelFormat == _outputFormat;
}
- (BOOL)encode:(id)commandBuffer input:(id<MTLTexture>)input contentWidth:(NSUInteger)width contentHeight:(NSUInteger)height mask:(id<MTLTexture>)mask output:(id<MTLTexture>)output {
    if (![self matchesInput:input output:output] || !mask || !commandBuffer || !width || !height || width > _width || height > _height) return NO;
    _scaler.colorTexture = input; _scaler.inputContentWidth = width; _scaler.inputContentHeight = height; _scaler.outputTexture = _scaled;
    [_scaler encodeToCommandBuffer:commandBuffer];
#if DM_NVN_M3
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new];
#else
    MTL4RenderPassDescriptor *rp = [MTL4RenderPassDescriptor new];
#endif
    rp.colorAttachments[0].texture = output; rp.colorAttachments[0].loadAction = MTLLoadActionDontCare; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
#if DM_NVN_M3
    id<MTLRenderCommandEncoder> e = [commandBuffer renderCommandEncoderWithDescriptor:rp];
    [e setFragmentTexture:_scaled atIndex:0]; [e setFragmentTexture:mask atIndex:1]; [e setFragmentSamplerState:_sampler atIndex:0];
#else
    id<MTL4RenderCommandEncoder> e = [commandBuffer renderCommandEncoderWithDescriptor:rp];
    [e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [_arguments setTexture:_scaled.gpuResourceID atIndex:0]; [_arguments setTexture:mask.gpuResourceID atIndex:1]; [_arguments setSamplerState:_sampler.gpuResourceID atIndex:0];
    [e setArgumentTable:_arguments atStages:MTLRenderStageFragment];
#endif
    if (!e) return NO;
    [e setViewport:(MTLViewport){0,0,_outWidth,_outHeight,0,1}]; [e setRenderPipelineState:_pipeline]; [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
#if !DM_NVN_M3
    [e barrierAfterStages:MTLStageAll beforeQueueStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
#endif
    [e endEncoding]; return YES;
}
@end
