// 空间超分与原版屏幕遮罩合成；调用方负责 GPU 驻留和在途对象退役。
// Spatial upscaling plus original screen-mask composition; caller owns residency and in-flight retirement.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
@interface NvMetalFX : NSObject
@property(nonatomic, readonly) NSArray<id<MTLResource>> *resources;
+ (BOOL)supported:(id<MTLDevice>)device;
- (instancetype)initWithDevice:(id<MTLDevice>)device input:(id<MTLTexture>)input output:(id<MTLTexture>)output;
- (BOOL)matchesInput:(id<MTLTexture>)input output:(id<MTLTexture>)output;
- (BOOL)encode:(id)commandBuffer input:(id<MTLTexture>)input contentWidth:(NSUInteger)width contentHeight:(NSUInteger)height mask:(id<MTLTexture>)mask output:(id<MTLTexture>)output;
// 只放大、不乘遮罩：结果留在 scaledTexture（出图尺寸、输入格式），由调用方自己乘遮罩（帧生成的插值帧用，省一张出图尺寸的输出）
// Scale only, no mask: the result stays in scaledTexture (output size, input format) and the caller applies the mask (used by frame generation's interpolated frames, saving one output-size texture)
- (BOOL)encodeScaleOnly:(id)commandBuffer input:(id<MTLTexture>)input contentWidth:(NSUInteger)width contentHeight:(NSUInteger)height;
@property(nonatomic, readonly) id<MTLTexture> scaledTexture;
@end
