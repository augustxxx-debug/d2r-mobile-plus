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
// 右上角静音钮：暗色圆底 + 金色细边 + 喇叭；有声音画两道声波，静音画一个叉 | the mute button at the top right: dark round base + thin gold edge + speaker; two sound waves when on, a cross when muted
static void mute_icon(CGContextRef c, dm_lp_rect r, int muted) {
    double cx = r.x + r.w / 2, cy = r.y + r.h / 2, R = r.w / 2;
    CGContextSaveGState(c);
    CGContextSetRGBFillColor(c, 0, 0, 0, 0.45); CGContextFillEllipseInRect(c, CGRectMake(r.x, r.y, r.w, r.h));
    set_rgb(c, DM_LP_COL_BAR_EDGE, 1, 1); CGContextSetLineWidth(c, 1.2); CGContextStrokeEllipseInRect(c, CGRectMake(r.x + 0.6, r.y + 0.6, r.w - 1.2, r.h - 1.2));
    set_rgb(c, DM_LP_COL_TITLE, 1, 0); set_rgb(c, DM_LP_COL_TITLE, 1, 1);
    double s = R / 20.0, x0 = cx - 9 * s;   // 图标按 40 点钮设计，s 缩放 | icon designed for a 40 pt button, scaled by s
    CGContextMoveToPoint(c, x0, cy - 4 * s); CGContextAddLineToPoint(c, x0 + 5 * s, cy - 4 * s); CGContextAddLineToPoint(c, x0 + 11 * s, cy - 9.5 * s);
    CGContextAddLineToPoint(c, x0 + 11 * s, cy + 9.5 * s); CGContextAddLineToPoint(c, x0 + 5 * s, cy + 4 * s); CGContextAddLineToPoint(c, x0, cy + 4 * s); CGContextClosePath(c); CGContextFillPath(c);
    CGContextSetLineWidth(c, 2 * s); CGContextSetLineCap(c, kCGLineCapRound);
    if (muted) { double a = x0 + 15 * s, b = 7 * s; CGContextMoveToPoint(c, a, cy - b / 2 * 1.2); CGContextAddLineToPoint(c, a + b, cy + b / 2 * 1.2); CGContextMoveToPoint(c, a + b, cy - b / 2 * 1.2); CGContextAddLineToPoint(c, a, cy + b / 2 * 1.2); CGContextStrokePath(c); }
    else for (int k = 0; k < 2; k++) { double rr = (5.5 + k * 4.5) * s; CGContextAddArc(c, x0 + 11 * s, cy, rr, -0.85, 0.85, 0); CGContextStrokePath(c); }
    CGContextRestoreGState(c);
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
    mute_icon(c, L.mute, dm_loadpage_muted());
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
@interface DmLoadPageView : UIView @property (assign) CGImageRef bg; @property (assign) CGImageRef logo; @property (strong) NSTimer *timer; @property (strong) AVAudioPlayer *music; @property (assign) int musicOn; @property (strong) NSString *dir; @property (strong) UIButton *muteBtn; @end
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
    self.dir = dir;
    // 静音开关：玩家上次的选择（偏好 DMLoadPageMuted，缺省有声音）；透明按钮压在右上角的图标上，位置取公共布局 | mute toggle: the player's last choice (preference DMLoadPageMuted, sound on by default); a transparent button over the top-right icon, placed by the shared layout
    dm_loadpage_set_muted([NSUserDefaults.standardUserDefaults boolForKey:@"DMLoadPageMuted"]);
    self.muteBtn = [UIButton buttonWithType:UIButtonTypeCustom]; [self.muteBtn addTarget:self action:@selector(muteTap) forControlEvents:UIControlEventTouchUpInside]; [self addSubview:self.muteBtn]; [self muteLabel];
    __weak DmLoadPageView *w = self;
    self.timer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *t) { DmLoadPageView *v = w; if (!v) { [t invalidate]; return; } [v tick]; }];
    [self tick];
    return self;
}
- (dm_lp_layout)lay { UIEdgeInsets si = self.safeAreaInsets; dm_lp_screen s = { self.bounds.size.width, self.bounds.size.height, si.left, si.top, si.right, si.bottom }; dm_lp_layout L; dm_loadpage_layout(&s, &L); return L; }
- (void)layoutSubviews { [super layoutSubviews]; dm_lp_layout L = [self lay]; self.muteBtn.frame = CGRectInset(CGRectMake(L.mute.x, L.mute.y, L.mute.w, L.mute.h), -12, -12); }
- (void)muteLabel { self.muteBtn.accessibilityLabel = @(dm_loadpage_ui(dm_loadpage_muted() ? "dmLoadMusicOn" : "dmLoadMusicOff")); }
- (void)muteTap { int m = dm_loadpage_toggle_muted(); [NSUserDefaults.standardUserDefaults setBool:m forKey:@"DMLoadPageMuted"]; [self muteLabel]; [self tick]; }
- (AVAudioPlayer *)player {   // 第一次要放时才建 | created the first time music is wanted
    if (self.music || getenv("DM_TEST_LOADPAGE_NOMUSIC")) return self.music;
    NSURL *u = [NSURL fileURLWithPath:[self.dir stringByAppendingPathComponent:@(dm_loadpage_music_name())]]; NSError *e = nil;
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryAmbient error:nil];   // 跟静音开关、不打断别的 App 的声音；进游戏后 front_ios.m 再改成游戏用的类别 | follows the mute switch and does not interrupt other apps' audio; front_ios.m switches to the game's category later
    self.music = [[AVAudioPlayer alloc] initWithContentsOfURL:u error:&e];
    if (!self.music) { fprintf(stderr, "[dm] 加载页：音乐 %s 打不开（%s），不放\n", dm_loadpage_music_name(), e.localizedDescription.UTF8String ?: "?"); return nil; }
    self.music.numberOfLoops = -1; self.music.volume = 0; [self.music prepareToPlay]; return self.music;
}
// 音乐跟着「该不该放」走（进行中且没静音）：要放就淡入 0.6 秒，不要就淡出 0.6 秒后暂停（再打开从原处接着放） | the music follows "should it play" (running and not muted): fade in over 0.6 s when wanted, fade out over 0.6 s then pause when not (turning it on again resumes where it was)
- (void)tick {
    int want = dm_loadpage_music_wanted();
    if (want && !self.musicOn) { AVAudioPlayer *m = [self player]; if (m) { self.musicOn = 1; [m play]; [m setVolume:0.55 fadeDuration:0.6]; } }
    else if (!want && self.musicOn) { self.musicOn = 0; AVAudioPlayer *m = self.music; [m setVolume:0 fadeDuration:0.6];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.7 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ if (!self.musicOn) [m pause]; }); }
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
