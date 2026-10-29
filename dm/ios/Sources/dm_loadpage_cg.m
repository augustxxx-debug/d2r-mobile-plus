// 加载页的苹果端画法（iOS 与 Mac 共用）：Core Graphics 把 dm/host/dm_loadpage.c 算出的布局画出来——背景图按 cover 裁切铺满、底部渐暗底板、进度条、四行字；
// iOS 再包一层 UIView（DmLoadPageView）定时重画 + AVAudioPlayer 放循环音乐；Mac 侧用同一个画函数画进位图（tests/t_loadpage_render.m 出图核对、front_mac.m 的 DM_TEST_LOADPAGE 演示）。
// 坐标约定：画函数假定上下文原点在左上、y 向下（UIKit drawRect 的样子）；Mac 位图上下文原点在左下，调用前先翻转（见 dm_loadpage_cg_flip）。
// 不碰游戏资源：背景图与音乐是包内 loadpage/ 目录的自制文件（dm/assets/loadpage/）。
// Apple-side drawing of the loading page (shared by iOS and Mac): Core Graphics draws the layout computed by dm/host/dm_loadpage.c - the background cover-cropped full screen, a darkening
// plate at the bottom, the progress bar and the four text lines; iOS wraps it in a UIView (DmLoadPageView) that redraws on a timer and plays the loop music with AVAudioPlayer; the Mac
// draws with the same function into a bitmap (tests/t_loadpage_render.m for the screenshot check, front_mac.m's DM_TEST_LOADPAGE demo).
// Coordinates: the draw function assumes a top-left origin with y down (as in UIKit drawRect); a Mac bitmap context has a bottom-left origin and is flipped first (dm_loadpage_cg_flip).
// No game assets are touched: the background and the music are self-made files in the package's loadpage/ directory (dm/assets/loadpage/).
#include "../../host/dm_loadpage.h"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#include <mach/mach_time.h>

uint64_t dm_loadpage_now_ms(void) {   // 单调毫秒（两端同一口径） | monotonic milliseconds (same on both sides)
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() * tb.numer / tb.denom / 1000000ull;
}
CGImageRef dm_loadpage_cg_image(const char *path) {   // 解码包内的背景 JPEG；失败返回 NULL（页面照画，只是黑底） | decode the packaged background JPEG; NULL on failure (the page still draws, on black)
    if (!path) return NULL;
    CFURLRef u = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false); if (!u) return NULL;
    CGImageSourceRef src = CGImageSourceCreateWithURL(u, NULL); CFRelease(u); if (!src) return NULL;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL); CFRelease(src); return img;
}
static void set_rgb(CGContextRef c, unsigned rgb, double a, int stroke) {
    CGFloat r = ((rgb >> 16) & 255) / 255.0, g = ((rgb >> 8) & 255) / 255.0, b = (rgb & 255) / 255.0;
    if (stroke) CGContextSetRGBStrokeColor(c, r, g, b, a); else CGContextSetRGBFillColor(c, r, g, b, a);
}
static void round_rect(CGContextRef c, dm_lp_rect r, double rad) {
    CGPathRef p = CGPathCreateWithRoundedRect(CGRectMake(r.x, r.y, r.w, r.h), rad, rad, NULL); CGContextAddPath(c, p); CGPathRelease(p);
}
// 一行字：box 内垂直居中，align 0 左 / 1 右；太长就截尾加省略号；带投影。字体：标题与字标用衬线粗体（Georgia，中韩俄文自动回退系统字体），正文 / 副文用系统字体
// One text line: vertically centred in box, align 0 left / 1 right; truncated with an ellipsis when too long; with a drop shadow. Fonts: title and wordmark in bold serif (Georgia; CJK / Cyrillic fall back to the system font), body / sub in the system font
static void text_line(CGContextRef c, const char *utf8, dm_lp_rect box, double pt, unsigned rgb, int align, int serif, int bold) {
    if (!utf8 || !*utf8 || box.w <= 0) return;
    CFStringRef s = CFStringCreateWithCString(NULL, utf8, kCFStringEncodingUTF8); if (!s) return;
    CTFontRef f = serif ? CTFontCreateWithName(bold ? CFSTR("Georgia-Bold") : CFSTR("Georgia"), pt, NULL) : CTFontCreateUIFontForLanguage(bold ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem, pt, NULL);
    if (!f) f = CTFontCreateWithName(CFSTR("Helvetica"), pt, NULL);
    CGFloat col[4] = { ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, 1 };
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB(); CGColorRef cg = CGColorCreate(cs, col);
    CFTypeRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName }; CFTypeRef vals[] = { f, cg };
    CFDictionaryRef attr = CFDictionaryCreate(NULL, (const void **)keys, (const void **)vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, s, attr);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    double width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    if (width > box.w) {   // 截尾 | truncate
        CFAttributedStringRef el = CFAttributedStringCreate(NULL, CFSTR("…"), attr); CTLineRef ell = CTLineCreateWithAttributedString(el);
        CTLineRef t = CTLineCreateTruncatedLine(line, box.w, kCTLineTruncationEnd, ell); if (t) { CFRelease(line); line = t; width = CTLineGetTypographicBounds(line, NULL, NULL, NULL); }
        CFRelease(ell); CFRelease(el);
    }
    CGFloat asc = CTFontGetAscent(f), desc = CTFontGetDescent(f);
    double x = align ? box.x + box.w - width : box.x, baseline = box.y + (box.h - (asc + desc)) / 2 + asc;
    CGContextSaveGState(c);
    CGContextSetTextMatrix(c, CGAffineTransformMakeScale(1, -1));   // 上下文 y 向下：字形矩阵翻回来 | context y goes down: flip the glyph matrix back
    CGContextSetShadowWithColor(c, CGSizeMake(0, 1), 2.5, (CGColorRef)CFAutorelease(CGColorCreate(cs, (CGFloat[]){ 0, 0, 0, 0.85 })));
    CGContextSetTextPosition(c, x, baseline);
    CTLineDraw(line, c);
    CGContextRestoreGState(c);
    CFRelease(line); CFRelease(as); CFRelease(attr); CGColorRelease(cg); CGColorSpaceRelease(cs); CFRelease(f); CFRelease(s);
}
void dm_loadpage_cg_flip(CGContextRef c, double h) { CGContextTranslateCTM(c, 0, h); CGContextScaleCTM(c, 1, -1); }   // 左下原点的上下文（Mac 位图）→ 左上原点 | bottom-left origin (Mac bitmap) → top-left origin
// 整页：scr 为逻辑点尺寸与安全区；bg 可为 NULL | the whole page: scr in logical points with safe insets; bg may be NULL
void dm_loadpage_cg_draw(CGContextRef c, CGImageRef bg, CGImageRef logo, const dm_lp_screen *scr, uint64_t now_ms) {
    dm_lp_layout L; dm_loadpage_layout(scr, &L); dm_lp_text T; dm_loadpage_text(&T, now_ms);
    CGContextSetRGBFillColor(c, 0.02, 0.015, 0.01, 1); CGContextFillRect(c, CGRectMake(0, 0, scr->w, scr->h));
    if (bg) {   // cover 裁切：从图里取 bg_src 那块，画满 bg_dst（CGContextDrawImage 按左下原点画，局部翻一次） | cover crop: take bg_src from the image, draw it over bg_dst (CGContextDrawImage assumes a bottom-left origin; flip locally)
        double sx = (double)CGImageGetWidth(bg) / L.bg_w, sy = (double)CGImageGetHeight(bg) / L.bg_h;   // 图的实际像素与布局假定的尺寸不同（换过图）也照样裁对 | the real pixel size may differ from the one the layout assumes (replaced image): still cropped right
        CGImageRef part = CGImageCreateWithImageInRect(bg, CGRectMake(L.bg_src.x * sx, L.bg_src.y * sy, L.bg_src.w * sx, L.bg_src.h * sy));
        if (part) { CGContextSaveGState(c); CGContextTranslateCTM(c, L.bg_dst.x, L.bg_dst.y + L.bg_dst.h); CGContextScaleCTM(c, 1, -1);
            CGContextSetInterpolationQuality(c, kCGInterpolationHigh); CGContextDrawImage(c, CGRectMake(0, 0, L.bg_dst.w, L.bg_dst.h), part); CGContextRestoreGState(c); CGImageRelease(part); }
    }
    // 底部渐暗底板：从标题上方 48 点起到屏底，字压在图上也看得清 | bottom darkening plate: from 48 pt above the title to the screen bottom, keeps the text legible over the picture
    { CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB(); CGFloat comps[8] = { 0, 0, 0, 0, 0, 0, 0, 0.62 }; CGFloat locs[2] = { 0, 1 };
      CGGradientRef g = CGGradientCreateWithColorComponents(cs, comps, locs, 2);
      double y0 = L.title.y - 48; CGContextSaveGState(c); CGContextClipToRect(c, CGRectMake(0, y0, scr->w, scr->h - y0));
      CGContextDrawLinearGradient(c, g, CGPointMake(0, y0), CGPointMake(0, scr->h), 0); CGContextRestoreGState(c); CGGradientRelease(g); CGColorSpaceRelease(cs); }
    // 进度条：暗底 + 细边 + 红色已走段（顶部一线高光、下方一圈柔光） | the bar: dark track + thin edge + red filled part (a highlight line on top, a soft glow below)
    set_rgb(c, DM_LP_COL_BAR_TRACK, 0.9, 0); round_rect(c, L.bar, L.bar_radius); CGContextFillPath(c);
    set_rgb(c, DM_LP_COL_BAR_EDGE, 0.9, 1); CGContextSetLineWidth(c, 1); round_rect(c, (dm_lp_rect){ L.bar.x - 0.5, L.bar.y - 0.5, L.bar.w + 1, L.bar.h + 1 }, L.bar_radius + 0.5); CGContextStrokePath(c);
    if (L.bar_fill.w > 1) {
        CGContextSaveGState(c); CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextSetShadowWithColor(c, CGSizeMake(0, 0), 6, (CGColorRef)CFAutorelease(CGColorCreate(cs, (CGFloat[]){ 1.0, 0.45, 0.1, 0.6 })));
        set_rgb(c, DM_LP_COL_BAR_FILL, 1, 0); round_rect(c, L.bar_fill, L.bar_radius); CGContextFillPath(c); CGContextRestoreGState(c);
        CGContextSetRGBFillColor(c, 1, 0.78, 0.45, 0.55); CGContextFillRect(c, CGRectMake(L.bar_fill.x + 2, L.bar_fill.y + 1, L.bar_fill.w - 4 > 0 ? L.bar_fill.w - 4 : 0, 1)); CGColorSpaceRelease(cs);
    }
    if (logo && L.logo.w > 0) { CGContextSaveGState(c); CGContextTranslateCTM(c, L.logo.x, L.logo.y + L.logo.h); CGContextScaleCTM(c, 1, -1);   // 字标图（透明 PNG）：等比居中 | wordmark (transparent PNG): aspect kept, centred
        CGContextSetInterpolationQuality(c, kCGInterpolationHigh); CGContextDrawImage(c, CGRectMake(0, 0, L.logo.w, L.logo.h), logo); CGContextRestoreGState(c); }
    else text_line(c, T.brand, L.brand, L.brand_pt, DM_LP_COL_TITLE, 0, 1, 1);   // 图读不到才退回文字字标 | text wordmark only when the image is unavailable
    text_line(c, T.title, L.title, L.title_pt, DM_LP_COL_TITLE, 0, 1, 1);
    text_line(c, T.percent, L.percent, L.title_pt, DM_LP_COL_PERCENT, 1, 1, 1);
    text_line(c, T.body, L.body, L.body_pt, DM_LP_COL_BODY, 0, 0, 0);
    text_line(c, T.sub, L.sub, L.sub_pt, DM_LP_COL_SUB, 0, 0, 0);
}
// 画进 PNG（Mac 出图核对 / iOS 测试开关）：按 scale 倍像素渲染 | render to a PNG (Mac screenshot check / iOS test switch): rendered at scale× pixels
int dm_loadpage_cg_png(const char *path, CGImageRef bg, CGImageRef logo, const dm_lp_screen *scr, double scale, uint64_t now_ms) {
    size_t pw = (size_t)(scr->w * scale + 0.5), ph = (size_t)(scr->h * scale + 0.5);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate(NULL, pw, ph, 8, 0, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (!c) { CGColorSpaceRelease(cs); return 0; }
    dm_loadpage_cg_flip(c, (double)ph); CGContextScaleCTM(c, scale, scale);
    dm_loadpage_cg_draw(c, bg, logo, scr, now_ms);
    CGImageRef out = CGBitmapContextCreateImage(c); int ok = 0;
    CFURLRef u = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    CGImageDestinationRef d = u ? CGImageDestinationCreateWithURL(u, CFSTR("public.png"), 1, NULL) : NULL;
    if (d) { CGImageDestinationAddImage(d, out, NULL); ok = CGImageDestinationFinalize(d); CFRelease(d); }
    if (u) CFRelease(u); CGImageRelease(out); CGContextRelease(c); CGColorSpaceRelease(cs);
    return ok;
}

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
// iOS 视图：每 100 毫秒重画一次（进度条与「预计还要」才跟着走），进行中放音乐、完成 / 失败时淡出停掉。背景图只解码一次
// iOS view: redraws every 100 ms (so the bar and "about … left" move), plays the music while running and fades it out on done / failure. The background is decoded once
@interface DmLoadPageView : UIView @property (assign) CGImageRef bg; @property (assign) CGImageRef logo; @property (strong) NSTimer *timer; @property (strong) AVAudioPlayer *music; @property (assign) int musicOff; @end
@implementation DmLoadPageView
- (instancetype)initWithFrame:(CGRect)f {
    if (!(self = [super initWithFrame:f])) return nil;
    self.opaque = YES; self.contentMode = UIViewContentModeRedraw; self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    NSString *dir = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"loadpage"];
    CGRect b = UIScreen.mainScreen.bounds; const char *name = dm_loadpage_bg_name(b.size.width, b.size.height, NULL, NULL);
    self.bg = dm_loadpage_cg_image([dir stringByAppendingPathComponent:@(name)].fileSystemRepresentation);
    if (!self.bg) fprintf(stderr, "[dm] 【红】加载页：背景图 %s 读不到（包里没有 loadpage/ 目录？），画黑底\n", name);
    self.logo = dm_loadpage_cg_image([dir stringByAppendingPathComponent:@(dm_loadpage_logo_name(NULL, NULL))].fileSystemRepresentation);
    if (!self.logo) fprintf(stderr, "[dm] 【红】加载页：字标图读不到，退回文字字标\n");
    __weak DmLoadPageView *w = self;
    self.timer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *t) { DmLoadPageView *v = w; if (!v) { [t invalidate]; return; } [v tick]; }];
    [self musicStart:dir];
    return self;
}
- (void)musicStart:(NSString *)dir {
    if (getenv("DM_TEST_LOADPAGE_NOMUSIC")) return;
    NSURL *u = [NSURL fileURLWithPath:[dir stringByAppendingPathComponent:@(dm_loadpage_music_name())]]; NSError *e = nil;
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryAmbient error:nil];   // 跟静音开关、不打断别的 App 的声音；进游戏后 front_ios.m 再改成游戏用的类别 | follows the mute switch and does not interrupt other apps' audio; front_ios.m switches to the game's category later
    self.music = [[AVAudioPlayer alloc] initWithContentsOfURL:u error:&e];
    if (!self.music) { fprintf(stderr, "[dm] 加载页：音乐 %s 打不开（%s），不放\n", dm_loadpage_music_name(), e.localizedDescription.UTF8String ?: "?"); return; }
    self.music.numberOfLoops = -1; self.music.volume = 0.55; [self.music prepareToPlay]; [self.music play];
}
- (void)tick {
    if (self.music && !self.musicOff && !dm_loadpage_music_wanted()) { self.musicOff = 1; [self.music setVolume:0 fadeDuration:1.2]; AVAudioPlayer *m = self.music;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.4 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [m stop]; }); }
    [self setNeedsDisplay];
}
- (void)drawRect:(CGRect)r {
    (void)r; CGContextRef c = UIGraphicsGetCurrentContext(); UIEdgeInsets si = self.safeAreaInsets;
    dm_lp_screen s = { self.bounds.size.width, self.bounds.size.height, si.left, si.top, si.right, si.bottom };
    dm_loadpage_cg_draw(c, self.bg, self.logo, &s, dm_loadpage_now_ms());
}
- (void)safeAreaInsetsDidChange { [super safeAreaInsetsDidChange]; [self setNeedsDisplay]; }
- (void)removeFromSuperview { [self.timer invalidate]; self.timer = nil; [self.music stop]; self.music = nil; [super removeFromSuperview]; }
- (void)dealloc { [self.timer invalidate]; [self.music stop]; if (self.bg) CGImageRelease(self.bg); if (self.logo) CGImageRelease(self.logo); }
@end
UIView *dm_loadpage_view_new(CGRect frame) { return [[DmLoadPageView alloc] initWithFrame:frame]; }
// 测试开关 DM_TEST_LOADPAGE_SHOT=<文件>：把当前页按屏幕倍率渲染成 PNG（不截屏、不经合成器） | test switch DM_TEST_LOADPAGE_SHOT=<file>: render the current page to a PNG at the screen scale (no screen capture, no compositor)
void dm_loadpage_view_shot(UIView *v, const char *path) {
    DmLoadPageView *p = (DmLoadPageView *)v; UIEdgeInsets si = v.safeAreaInsets;
    dm_lp_screen s = { v.bounds.size.width, v.bounds.size.height, si.left, si.top, si.right, si.bottom };
    fprintf(stderr, "[dm] 加载页：渲染截图 %s → %s\n", path, dm_loadpage_cg_png(path, p.bg, p.logo, &s, UIScreen.mainScreen.scale, dm_loadpage_now_ms()) ? "成功" : "失败");
}
#endif
