// 存档管理的 iOS 外壳（公共逻辑在 dm/host/dm_savepkg.c、dm_savemgr.c）：系统选文件（UIDocumentPicker，拷一份到临时目录再交给宿主）、系统分享面板（存到文件 / 隔空投送等）、来源身份、启动期换入。
// iOS shell of save management (shared logic in dm/host/dm_savepkg.c, dm_savemgr.c): the system file picker (UIDocumentPicker, a copy in the temporary directory goes to the host), the system share sheet (save to Files, AirDrop and so on), origin identity, apply at boot.
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "front.h"

void dm_savemgr_shell(void (*pick)(void), void (*share)(const char *)); void dm_savemgr_picked(const char *path); void dm_savemgr_share_done(int saved);
void dm_savemgr_set_source(const char *platform, const char *commit, const char *build, const char *version); void dm_savemgr_boot(void);

static UIViewController *top_vc(void) {   // 同 dm_sharelog.m | as dm_sharelog.m
    UIViewController *vc = nil, *alt = nil, *any = nil;
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class]) for (UIWindow *w in ((UIWindowScene *)s).windows) {
            if (!w.rootViewController) continue;
            if (w.isKeyWindow) vc = w.rootViewController;
            else if (s.activationState == UISceneActivationStateForegroundActive && !alt) alt = w.rootViewController;
            else if (!any) any = w.rootViewController; }
    vc = vc ?: alt ?: any; while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}
@interface DmSavePick : NSObject <UIDocumentPickerDelegate> @end
@implementation DmSavePick
- (void)documentPicker:(UIDocumentPickerViewController *)c didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls { (void)c; dm_savemgr_picked(urls.firstObject.fileSystemRepresentation); }
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)c { (void)c; dm_savemgr_picked(NULL); }
@end
static DmSavePick *g_pick_delegate;
static void on_pick(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *vc = top_vc(); if (!vc) { dm_savemgr_picked(NULL); return; }
        UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeZIP] asCopy:YES];   // asCopy：系统拷到我们的临时目录，不用管安全域访问 | asCopy: the system copies into our temporary directory, no security-scoped access to manage
        if (!g_pick_delegate) g_pick_delegate = [DmSavePick new]; p.delegate = g_pick_delegate; p.allowsMultipleSelection = NO; p.view.accessibilityIdentifier = @"dmSavePicker"; [vc presentViewController:p animated:YES completion:nil]; });
}
static void on_share(const char *path) {
    NSURL *u = [NSURL fileURLWithPath:@(path)];
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *vc = top_vc(); if (!vc) { dm_savemgr_share_done(0); return; }
        UIActivityViewController *a = [[UIActivityViewController alloc] initWithActivityItems:@[u] applicationActivities:nil];
        a.completionWithItemsHandler = ^(UIActivityType t, BOOL done, NSArray *items, NSError *e) { (void)t; (void)items; (void)e; dm_savemgr_share_done(done); };
        a.view.accessibilityIdentifier = @"dmSaveShare"; UIPopoverPresentationController *pp = a.popoverPresentationController;   // iPad 上是弹出框：锚在画面正中 | a popover on iPad: anchored at the centre
        if (pp) { pp.sourceView = vc.view; pp.sourceRect = CGRectMake(CGRectGetMidX(vc.view.bounds), CGRectGetMidY(vc.view.bounds), 1, 1); pp.permittedArrowDirections = 0; }
        [vc presentViewController:a animated:YES completion:nil]; });
}
// boot_thread 调：挂存档与 iCloud 同步之前 | called on boot_thread, before mount and iCloud sync
void dm_savemgr_boot_ios(void) {
    NSDictionary *d = NSBundle.mainBundle.infoDictionary; __block int pad = 0; void (^q)(void) = ^{ pad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad; }; if (NSThread.isMainThread) q(); else dispatch_sync(dispatch_get_main_queue(), q);
    dm_savemgr_set_source(pad ? "ipados" : "ios", [d[@"DMSourceCommit"] description].UTF8String, [d[@"CFBundleVersion"] description].UTF8String, [d[@"DMVersionLabel"] description].UTF8String);
    dm_savemgr_shell(on_pick, on_share); dm_savemgr_boot();
}
