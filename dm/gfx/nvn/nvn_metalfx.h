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
@end
