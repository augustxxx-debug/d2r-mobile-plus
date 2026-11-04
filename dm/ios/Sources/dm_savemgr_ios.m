// 存档管理的 iOS 外壳（公共逻辑在 dm/host/dm_savepkg.c、dm_savemgr.c）：系统选文件（UIDocumentPicker，拷一份到临时目录再交给宿主）、系统分享面板（存到文件 / 隔空投送等）、来源身份、启动期换入。
// iOS shell of save management (shared logic in dm/host/dm_savepkg.c, dm_savemgr.c): the system file picker (UIDocumentPicker, a copy in the temporary directory goes to the host), the system share sheet (save to Files, AirDrop and so on), origin identity, apply at boot.
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "front.h"

void dm_savemgr_shell(void (*pick)(void), void (*share)(const char *)); void dm_savemgr_picked(const char *path); void dm_savemgr_picked_from(const char *path, const char *origin); void dm_savemgr_share_done(int saved); int dm_savepkg_is_coop_root(const char *path);
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
// 选文件夹要拷到临时目录：先量总量与文件数，选错大目录不把盘写满、也不拷几万个小文件（数量与总量跟原生 MAX_FILES / zf_open 同一套）。云端占位是点开头的文件，宿主不收 | a picked folder is copied to a temporary directory: measure the total and the file count first so a wrong pick can neither fill the disk nor copy tens of thousands of small files (the same count and total as the native MAX_FILES / zf_open). Cloud placeholders are dot files, which the host does not take
static BOOL pick_too_big(NSURL *u) {
    NSNumber *sz = nil; const long long cap = 320ll << 20; const int max_files = 4096; long long sum = 0; int n = 0; NSNumber *isdir = nil; [u getResourceValue:&isdir forKey:NSURLIsDirectoryKey error:NULL];
    if (!isdir.boolValue) { [u getResourceValue:&sz forKey:NSURLFileSizeKey error:NULL]; return sz.longLongValue > cap; }
    for (NSURL *f in [NSFileManager.defaultManager enumeratorAtURL:u includingPropertiesForKeys:@[NSURLFileSizeKey] options:0 errorHandler:nil]) { if (++n > max_files) return YES; sz = nil; [f getResourceValue:&sz forKey:NSURLFileSizeKey error:NULL]; if ((sum += sz.longLongValue) > cap) return YES; }
    return NO;
}
@interface DmSavePick : NSObject <UIDocumentPickerDelegate> @end
@implementation DmSavePick
// 拷贝排成串行（一个队列），每次选择拷进自己的新目录：两次选择不会共用、互删同一个目录。旧的副本在下一次拷之前清：能开系统面板时宿主没有后台活，旧副本已经没人读（审计 20261104 P1-1 c）| copies run one at a time (one queue) and each pick gets a new directory of its own, so two picks never share or delete each other's directory. Older copies are cleared before the next copy: the panel can only open while the host has no job, so nothing reads them any more
static dispatch_queue_t pick_queue(void) { static dispatch_queue_t q; static dispatch_once_t once; dispatch_once(&once, ^{ q = dispatch_queue_create("dm.save.pick", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0)); }); return q; }
- (void)documentPicker:(UIDocumentPickerViewController *)c didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls { (void)c; NSURL *u = urls.firstObject;
    dispatch_async(pick_queue(), ^{   // 文件夹不能 asCopy：持安全作用域拷到临时目录再交给宿主；选了 App 的文件夹（上游 Documents）就只拷 save | a folder cannot be asCopy: copy to a temporary directory under security scope for the host; if an App's folder (upstream Documents) was picked, only save is copied
        BOOL scoped = [u startAccessingSecurityScopedResource]; NSFileManager *fm = NSFileManager.defaultManager; NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:@"dm_save_pick"]; [fm removeItemAtPath:root error:NULL];
        NSString *base = [root stringByAppendingPathComponent:NSUUID.UUID.UUIDString]; [fm createDirectoryAtPath:base withIntermediateDirectories:YES attributes:nil error:NULL];
        // 原路径一起交给宿主：拷进临时目录后 save.coop 就不在路径里了，单独一个联机角色的 .d2s 会被当成新角色（审计 20261104 P1-3）| the original path goes to the host too: in the temporary copy save.coop is gone from the path, and a lone co-op .d2s would pass as a new character
        NSString *origin = u.path;
        __block NSString *out = nil; NSError *ce = nil; [[[NSFileCoordinator alloc] initWithFilePresenter:nil] coordinateReadingItemAtURL:u options:NSFileCoordinatorReadingWithoutChanges error:&ce byAccessor:^(NSURL *r) {
            if ((origin && dm_savepkg_is_coop_root(origin.fileSystemRepresentation)) || (r.path && dm_savepkg_is_coop_root(r.path.fileSystemRepresentation))) { out = @"!coop"; return; }   // 选的和协调后的地址都按宿主同一个判定先看一遍，不拷 | both the picked and the coordinated URL go through the host's own check first; nothing is copied
            NSURL *from = r; NSURL *sv = [r URLByAppendingPathComponent:@"save" isDirectory:YES]; BOOL dir = NO; if ([fm fileExistsAtPath:sv.path isDirectory:&dir] && dir) from = sv;
            if (pick_too_big(from)) { out = @"!limit"; return; } NSString *to = [base stringByAppendingPathComponent:from.lastPathComponent]; NSError *e = nil; if ([fm copyItemAtURL:from toURL:[NSURL fileURLWithPath:to] error:&e]) out = to; else NSLog(@"save pick copy: %@", e); }];
        if (scoped) [u stopAccessingSecurityScopedResource]; dm_savemgr_picked_from(out.fileSystemRepresentation, out ? origin.fileSystemRepresentation : NULL); });
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)c { (void)c; dm_savemgr_picked(NULL); }
@end
static DmSavePick *g_pick_delegate;
static void on_pick(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *vc = top_vc(); if (!vc) { dm_savemgr_picked(NULL); return; }
        UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeZIP, UTTypeFolder, UTTypeData] asCopy:NO];   // ZIP、上游文件夹、单个 .d2s / .d2i（没有注册类型，算 data），选了什么由宿主校验 | a ZIP, an upstream folder, a single .d2s / .d2i (no registered type, so data); the host validates the pick
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
        [vc presentViewController:a animated:YES completion:nil];
#ifdef DM_QA_BUILD
        const char *qa = getenv("DM_TEST_SHARE_CLOSE"); if (qa) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(atoi(qa) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [a dismissViewControllerAnimated:YES completion:^{ dm_savemgr_share_done(0); }]; });   // 系统面板脚本点不到：QA 构建到点收起（CLAUDE.md §2.9）；代码收起不走 completionWithItemsHandler | the system panel cannot be tapped by a script: QA builds close it on time; closing from code skips the completion handler
#endif
    });
}
// boot_thread 调：挂存档与 iCloud 同步之前 | called on boot_thread, before mount and iCloud sync
void dm_savemgr_boot_ios(void) {
    NSDictionary *d = NSBundle.mainBundle.infoDictionary; __block int pad = 0; void (^q)(void) = ^{ pad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad; }; if (NSThread.isMainThread) q(); else dispatch_sync(dispatch_get_main_queue(), q);
    dm_savemgr_set_source(pad ? "ipados" : "ios", [d[@"DMSourceCommit"] description].UTF8String, [d[@"CFBundleVersion"] description].UTF8String, [d[@"DMVersionLabel"] description].UTF8String);
    [NSFileManager.defaultManager removeItemAtPath:[NSTemporaryDirectory() stringByAppendingPathComponent:@"dm_save_pick"] error:NULL];   // 上次选的副本 | the copy from the last pick
    dm_savemgr_shell(on_pick, on_share); dm_savemgr_boot();
}
