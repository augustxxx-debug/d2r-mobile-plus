// iPhone Duo 切屏的方向处理的 UIKit 一半（B115，判定见 dm_foldland.c）| The UIKit half of the iPhone Duo screen switch orientation handling (B115, decisions in dm_foldland.c)
#import <UIKit/UIKit.h>
#include "dm_foldland.h"
void plog(const char *fmt, ...);
static int g_short;
// 改方向要等系统回话（实测约 0.2 秒），这期间先藏住游戏视图，外屏不亮出旧方向的画面；系统不回话也只藏 0.5 秒
// Changing orientation waits for the system's reply (about 0.2 s measured); the game view stays hidden until then so the cover screen never shows the old orientation; with no reply it is hidden for 0.5 s at most
static __weak UIView *g_hidden;
static BOOL reveal(void) { UIView *v = g_hidden; if (!v) return NO; g_hidden = nil; v.hidden = NO; return YES; }
void dm_foldland_scene_update(UIWindowScene *ws) {
    if (UIDevice.currentDevice.userInterfaceIdiom != UIUserInterfaceIdiomPhone) return;
    CGSize n = ws.screen.nativeBounds.size; int kind = dm_foldland_screen(&g_short, (int)MIN(n.width, n.height));
    UIInterfaceOrientation io = ws.effectiveGeometry.interfaceOrientation; UIDeviceOrientation dev = UIDevice.currentDevice.orientation;
    if (kind == DM_FL_NONE) { if (io == UIInterfaceOrientationLandscapeRight) { if (reveal()) plog("切屏：画面放出，方向到位"); } return; }
    if (reveal()) plog("切屏：画面放出，又切屏了");
    int t = dm_foldland_target(kind, (int)io, (int)dev);
    plog("切屏：%s，原生短边 %d，界面方向 %ld，设备方向 %ld%s", kind == DM_FL_CLOSING ? "合上到外屏" : "翻开到内屏", g_short, (long)io, (long)dev, t ? "，改落到 3" : "");
    if (!t) return;
    UIView *v = ws.keyWindow.rootViewController.view; v.hidden = YES; g_hidden = v;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), ^{ if (reveal()) plog("切屏：画面放出，等系统超时"); });
    [ws requestGeometryUpdateWithPreferences:[[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeRight] errorHandler:^(NSError *e) { plog("切屏：改方向被拒，画面放出 %s", e.localizedDescription.UTF8String); reveal(); }];
}
