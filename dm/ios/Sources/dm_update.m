// 显示版本 + 新版本提示的 iOS 侧。
// 画面上的字是游戏自己的控件（布局 tools/touch_layout.py，写字与显隐 dm/host/dm_update.c）；这里只管两件（只提示、不提供更新动作，四审 Q4-04）：
//   ① 把自己的构建号（CFBundleVersion = dm/tools/version.sh 的构建号，与日志头「构建」同一个数，比新旧用）和显示版本（DMVersionLabel「1.0.0+28 · b346e40」，dm/tools/version.sh）交给宿主，选角页左下角常显显示版本；
//   ② 每次选角页出现（启动、从局里退回来）都在后台取一次版本文件（这是我们自己加的唯一一处联网，只读一个小文件，单次 10 秒超时、失败隔几秒重试共 3 次，不挡启动与进局；同一时刻只有一个查询在途）；
// 版本文件地址不在仓库里：出包脚本写进 Info.plist 的 SWUpdateURL（tools/adhoc.sh 传 DM_UPDATE_URL）。没有这个键、或构建号不是大于 1 的整数
// （读不到提交时出的包是 1）→ 不联网、不提示。不比自己新：画面上什么都不出；没网 / 超时 / 地址失效 / 格式不对：选角页构建号右边出一行失败提示（dm_update_fail：没有无线数据许可 → 提示去设置里允许；其余 → 提示去浏览器打开版本文件所在的站点），这次启动取到了就撤掉。
// 版本文件 latest.json（UTF-8）三个必填字段：{"build": 1793043416, "note": "一句话 ≤ 24 个汉字", "urgent": false}，缺字段或类型不对 = 格式不对（旧文件里的 "install_url" 不再用、有没有都行）；
// 可选 "version": "1.1.0"（给人看的版本号，≤ 20 个字符，只许数字字母与 .+-）：有就在提示里写它，没有写 build。build 必须与新包的 CFBundleVersion 一致。
// 每次查完按新结果处理（不限一天一次，进首页就查、有新的就提示）：比自己新 → 显示 / 换成这次的；不比自己新 → 撤掉已有提示；
// 取不到 / 格式不对 → 已有的提示留着不动（一次网络抖动不该把确实存在的新版本提示抹掉；这次启动还没提示过的话照旧什么都不出）。
// 测试注入（只影响这两件的输入，不设就是产品行为）：DM_TEST_UPDATE_URL 换版本文件地址（可用 file://），DM_TEST_UPDATE_BUILD 换自己的构建号，
//   DM_TEST_UPDATE_DENY=1 把取不到当成「无线数据许可被拒」。
// iOS side of the display version + new-version notice.
// The text on screen is the game's own widgets (layout in tools/touch_layout.py, text writing and show/hide in dm/host/dm_update.c); only two things are handled here (notice only, no update action, fourth review Q4-04):
//   ① Hand our own build number (CFBundleVersion = the build number from dm/tools/version.sh, the same number as 「构建」(Build) in the log header, used for comparing) and display version (DMVersionLabel "1.0.0+28 · b346e40", dm/tools/version.sh) to the host; the display version is always shown at the bottom-left of the character select page;
//   ② Each time the character select page appears (launch, returning from a run), fetch the version file once in the background (this is the only network access we added ourselves; it reads one small file, 10-second timeout per attempt, retries a few seconds after a failure for 3 attempts in total, does not block launch or entering a run; only one query is in flight at any moment);
// The version file address is not in the repository: the packaging script writes it into SWUpdateURL in Info.plist (tools/adhoc.sh passes DM_UPDATE_URL). Without this key, or when the build number is not an integer greater than 1
// (packages built without a readable commit are 1) → no network access, no notice. Not newer than ours: nothing appears on screen; no network / timeout / dead address / bad format: a failure notice line appears to the right of the build number on the character select page (dm_update_fail: no wireless data permission → tell the player to allow it in Settings; otherwise → tell the player to open in a browser the site that serves the version file), removed once a fetch succeeds during this launch.
// The version file latest.json (UTF-8) has three required fields: {"build": 1793043416, "note": "one sentence, ≤ 24 Chinese characters", "urgent": false}; a missing field or wrong type = bad format (an old file's "install_url" is no longer used and may be present or absent);
// optional "version": "1.1.0" (the human version, ≤ 20 characters, digits, letters and .+- only): shown in the notice when present, otherwise build. build must equal the new package's CFBundleVersion.
// After each query, act on the new result (not limited to once a day; query on entering the home page and show a notice if there is a new one): newer than ours → show / replace with this one; not newer than ours → remove the existing notice;
// fetch failed / bad format → leave the existing notice untouched (one network glitch should not erase a notice for a new version that really exists; if nothing has been shown yet during this launch, still show nothing as before).
// Test injection (affects only the inputs of these two things; unset = product behavior): DM_TEST_UPDATE_URL replaces the version file address (file:// is allowed), DM_TEST_UPDATE_BUILD replaces our own build number,
//   DM_TEST_UPDATE_DENY=1 treats a failed fetch as "wireless data permission denied".
#import <UIKit/UIKit.h>
#import <Network/Network.h>
#include <stdatomic.h>
void plog(const char *fmt, ...);
void dm_update_init(int own_build, const char *label, void (*on_select)(void));   // dm/host/dm_update.c
void dm_update_offer(int new_build, const char *ver, const char *note, int urgent);
void dm_update_safe_left(int units);
void dm_update_fail(int kind);
void dm_update_site(const char *host);
int dm_community_manifest(const char *json, size_t len);   // dm/host/dm_community.c：同一份版本文件里的 community 字段（群号 / 仓库地址） | the community field (group number / repository address) of the same version file
void dm_community_copy_init(int (*cb)(const char *text));

static NSString *g_url; static int g_own; static _Atomic int g_inflight;

// 取不到时补一行系统眼里的网络状态：分清「手机没连网」和「这个 App 没拿到无线数据许可」（两种情况请求报的都是「没有网络」）。只读，不弹框
// When the fetch fails, add one line with the network state as the system sees it: tell apart "the phone is not connected" and "this App has not been granted wireless data permission" (in both cases the request reports "no network"). Read-only, no alert
static void upd_net_why(void) {
    nw_path_monitor_t m = nw_path_monitor_create(); nw_path_monitor_set_queue(m, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    nw_path_monitor_set_update_handler(m, ^(nw_path_t p) {
        static const char *st[] = { "无效", "通", "不通", "要先建连接" }, *why[] = { "系统没给原因", "蜂窝数据许可被拒", "无线局域网数据许可被拒", "本地网络许可被拒", "要先开 VPN" };
        nw_path_status_t s = nw_path_get_status(p); nw_path_unsatisfied_reason_t r = nw_path_get_unsatisfied_reason(p);
        plog("更新：系统网络路径 %s%s%s；无线局域网 %s、蜂窝 %s", s <= 3 ? st[s] : "未知", s == nw_path_status_satisfied ? "" : "，原因：", s == nw_path_status_satisfied ? "" : r <= 4 ? why[r] : "未知",
             nw_path_uses_interface_type(p, nw_interface_type_wifi) ? "在用" : "没用上", nw_path_uses_interface_type(p, nw_interface_type_cellular) ? "在用" : "没用上");
        // 选角页失败提示：系统说「数据许可被拒」= 玩家自己能在设置里开；其余 = 让玩家去浏览器手动更新
        // Failure notice on the character select page: the system says "data permission denied" = the player can turn it on in Settings themselves; anything else = have the player update manually in a browser
        const char *td = getenv("DM_TEST_UPDATE_DENY");   // 测试：=1 当成系统说「数据许可被拒」（Mac 上造不出这种现场） | Test: =1 behaves as if the system said "data permission denied" (this situation cannot be created on Mac)
        int denied = (td && atoi(td)) || (s != nw_path_status_satisfied && (r == nw_path_unsatisfied_reason_cellular_denied || r == nw_path_unsatisfied_reason_wifi_denied));
        dm_update_fail(denied ? 1 : 2); plog("更新：选角页显示%s", denied ? "「无法联网检查更新 · 请在设置里允许无线数据」" : "「检查更新失败 · 浏览器手动更新」");
        nw_path_monitor_cancel(m);
    });
    nw_path_monitor_start(m);
}

// 版本文件 → 要不要提示。返回 nil = 提示（out 填好），否则是不提示的原因（进日志）
// Version file → whether to show a notice. Returns nil = show it (out is filled in); otherwise the reason for not showing it (goes to the log)
static NSString *upd_parse(NSData *d, int *build, NSString **ver, NSString **note, int *urgent) {
    NSDictionary *j = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    if (![j isKindOfClass:NSDictionary.class]) return @"格式不对：不是 JSON 对象";
    NSNumber *b = j[@"build"], *u = j[@"urgent"]; NSString *n = j[@"note"];
    if (![b isKindOfClass:NSNumber.class] || ![u isKindOfClass:NSNumber.class] || ![n isKindOfClass:NSString.class]) return @"格式不对：三个字段缺了或类型不对";
    if (!(b.doubleValue >= 1 && b.doubleValue < 2e9) || b.doubleValue != b.intValue) return @"格式不对：build 不是正整数";
    NSString *v = j[@"version"];   // 可选 | optional
    if (v && (![v isKindOfClass:NSString.class] || !v.length || v.length > 20 || [v rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.+-"].invertedSet].location != NSNotFound)) return @"格式不对：version 不是 ≤ 20 个字符的版本号";
    n = [[n componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet] componentsJoinedByString:@" "];
    if (n.length > 24) n = [n substringWithRange:[n rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, 24)]];   // 说明只有一行的位置：超过 24 个字截掉 | The note has room for only one line: truncate beyond 24 characters
    *build = b.intValue; *ver = v ?: @""; *note = n; *urgent = u.boolValue; return nil;
}
static void upd_fetch(int n);
static void on_select(void) {   // 宿主在游戏主线程调：选角页每出现一次调一次 | Called by the host on the game main thread: once each time the character select page appears
    dispatch_async(dispatch_get_main_queue(), ^{   // 左安全区换成布局单位（2160 高）交给宿主，算法同 front_ios.m 给佣兵头像用的那处 | Convert the left safe area into layout units (2160 high) and hand it to the host; same algorithm as the place in front_ios.m used for the mercenary portrait
        UIWindow *w = nil; for (UIScene *s in UIApplication.sharedApplication.connectedScenes) if ([s isKindOfClass:UIWindowScene.class]) { w = ((UIWindowScene *)s).windows.firstObject; if (w) break; }
        const char *tl = getenv("DM_TEST_SAFE_L"); CGFloat l = tl ? atof(tl) : w.safeAreaInsets.left, hh = w.bounds.size.height; if (hh > 0) dm_update_safe_left((int)lround(l / hh * 2160));
    });
    if (!g_url) return;
    if (atomic_exchange(&g_inflight, 1)) { plog("更新：上一次查询还没回来，这次不再发"); return; }   // 同一时刻只有一个查询（含重试）在途 | Only one query (including retries) is in flight at any moment
    upd_fetch(1);
}
// 一次查询最多试 UPD_TRIES 次（首页出过一次「检查更新失败」，服务端那段时间全是 200，请求多半没出手机）。
// 读代码能列出的客户端一次性失败：① 刚启动 / 刚回前台时系统网络路径还没就绪，waitsForConnectivity = NO 时立刻报 -1009；② 冷连接 DNS + TCP + TLS
// 在蜂窝上超过原先 5 秒的总时限（-1001）；③ 每次新建会话、不复用连接，次次都是冷连接；④ DNS 首查失败（-1003）/ 连接中途丢（-1005）。
// 改：请求 10 秒、整体 15 秒；会话全程复用一个；失败隔 3 秒、6 秒再试，三次都失败才出失败提示（一次失败不出字）；每次都记第几次、错误码。
// 不开 waitsForConnectivity：数据许可被拒时它会一直等到整体时限，玩家要多等十几秒才看到「去设置里允许」。
// 测试：DM_TEST_UPDATE_FAIL_FIRST=N 前 N 次故意查一个不存在的域名（造「一次失败、再试成功」）
// One query tries at most UPD_TRIES times (the home page once showed 「检查更新失败」(Update check failed) while the server returned all 200s during that period, so the request most likely never left the phone).
// One-off client failures that can be listed by reading the code: ① right after launch / returning to the foreground the system network path is not ready yet, and with waitsForConnectivity = NO it reports -1009 immediately; ② cold-connection DNS + TCP + TLS
// on cellular exceeds the former 5-second total limit (-1001); ③ a new session was created every time with no connection reuse, so every attempt was a cold connection; ④ the first DNS lookup fails (-1003) / the connection drops midway (-1005).
// Changes: request 10 seconds, overall 15 seconds; one session reused throughout; retry 3 seconds and 6 seconds after a failure, and show the failure notice only after all three fail (a single failure shows no text); log the attempt number and error code every time.
// waitsForConnectivity is not turned on: when the data permission is denied it waits until the overall limit, and the player would wait ten-odd more seconds before seeing 「去设置里允许」(Allow it in Settings).
// Test: DM_TEST_UPDATE_FAIL_FIRST=N deliberately queries a non-existent domain for the first N attempts (to create "fail once, succeed on retry")
#define UPD_TRIES 3
static void upd_fetch(int n) {
    static NSURLSession *ses; static dispatch_once_t once;
    dispatch_once(&once, ^{ NSURLSessionConfiguration *c = NSURLSessionConfiguration.ephemeralSessionConfiguration; c.timeoutIntervalForRequest = 10; c.timeoutIntervalForResource = 15;
        c.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData; c.waitsForConnectivity = NO; ses = [NSURLSession sessionWithConfiguration:c]; });
    const char *tf = getenv("DM_TEST_UPDATE_FAIL_FIRST"); NSString *us = tf && n <= atoi(tf) ? @"https://nonexistent-dm.invalid/latest.json" : g_url;
    NSURL *url = [NSURL URLWithString:us]; if (!url) { plog("更新：版本文件地址 %s 不是合法地址，不查", us.UTF8String); g_url = nil; atomic_store(&g_inflight, 0); return; }
    [[ses dataTaskWithURL:url completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
        int build = 0, urgent = 0; NSString *ver = nil, *note = nil, *why = nil;
        if (e) why = [NSString stringWithFormat:@"取不到：%@", e.localizedDescription];
        else if ([r isKindOfClass:NSHTTPURLResponse.class] && [(NSHTTPURLResponse *)r statusCode] != 200) why = [NSString stringWithFormat:@"取不到：HTTP %ld", (long)[(NSHTTPURLResponse *)r statusCode]];
        else if (d.length > 4096) why = @"格式不对：超过 4096 字节";
        else why = upd_parse(d, &build, &ver, &note, &urgent);
        if (!e && d.length && d.length <= 4096) {   // 远程退回画法：版本文件里 "render_safe": true → 本局退回原画法；没有这个字段 = 新画法 | Remote fallback of the drawing method: "render_safe": true in the version file → this run falls back to the old drawing method; without this field = the new drawing method
            NSDictionary *j = [NSJSONSerialization JSONObjectWithData:d options:0 error:nil]; NSNumber *rs = [j isKindOfClass:NSDictionary.class] ? j[@"render_safe"] : nil;
            extern int g_dm_rt_ab; if ([rs isKindOfClass:NSNumber.class] && rs.boolValue) { if (g_dm_rt_ab) { g_dm_rt_ab = 0; plog("画法：版本文件 render_safe = true → 本局退回原画法"); } }
            else plog("画法：版本文件没有 render_safe（或为 false）→ 新画法照常"); }
        // 社区入口（dm_community.c）：取到了就把原文交给宿主解析 community 字段（版本号字段不合规也照交，那是另一回事）；三次都取不到才报「没拉到」= 沿用缓存 | community entry (dm_community.c): when fetched, hand the raw text to the host to parse the community field (even when the version fields are invalid, that is a separate matter); only after all attempts fail report "not fetched" = keep the cache
        if (!e && (![r isKindOfClass:NSHTTPURLResponse.class] || [(NSHTTPURLResponse *)r statusCode] == 200) && d.length && d.length <= 4096) dm_community_manifest(d.bytes, d.length);
        else if (n >= UPD_TRIES) dm_community_manifest(NULL, 0);
        if (why) {
            plog("更新：查 %s（第 %d / %d 次）→ %s%s", us.UTF8String, n, UPD_TRIES, why.UTF8String, n < UPD_TRIES ? "，稍后再试" : "，已有的提示不动");
            if (e) plog("更新：第 %d 次取不到的错误码 %s %ld", n, e.domain.UTF8String, (long)e.code);
            if (n < UPD_TRIES) { dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * n) * NSEC_PER_SEC), dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ upd_fetch(n + 1); }); return; }
            atomic_store(&g_inflight, 0);
            if (e) upd_net_why(); else dm_update_fail(2);   // 三次都失败才出字：网络错误由系统网络路径定原因（upd_net_why 里设），HTTP 非 200 / 格式不对 = 其余失败 | Text appears only after all three attempts fail: for a network error the reason is determined by the system network path (set in upd_net_why); HTTP non-200 / bad format = other failures
            return; }
        atomic_store(&g_inflight, 0);
        if (n > 1) plog("更新：第 %d 次取到了（前面 %d 次失败）", n, n - 1);
        dm_update_fail(0);   // 这次取到了：撤掉失败提示 | Fetched successfully this time: remove the failure notice
        if (build <= g_own) { plog("更新：查 %s → 最新构建 %d，不比自己（%d）新，不提示（已有的提示撤掉）", us.UTF8String, build, g_own); dm_update_offer(0, "", "", 0); return; }
        plog("更新：查 %s → 有新构建 %d（版本 %s，%s，自己 %d）：%s", us.UTF8String, build, ver.length ? ver.UTF8String : "没给", urgent ? "建议立即更新" : "普通", g_own, note.UTF8String);
        dm_update_offer(build, ver.UTF8String, note.UTF8String, urgent);
    }] resume];
}
// 社区入口的剪贴板回调（dm_community.c）：宿主在游戏主线程调，这里到主线程写 UIPasteboard（同 dm_sharelog.m 电量快照的做法） | clipboard callback of the community entry (dm_community.c): the host calls it on the game thread; written to UIPasteboard on the main thread (as the battery snapshot in dm_sharelog.m)
static int ios_copy(const char *s) {
    NSString *t = @(s ?: ""); __block int ok = 0;
    void (^w)(void) = ^{ UIPasteboard.generalPasteboard.string = t; ok = [UIPasteboard.generalPasteboard.string isEqualToString:t]; };
    if (NSThread.isMainThread) w(); else dispatch_sync(dispatch_get_main_queue(), w);
    return ok;
}
void dm_update_boot(void) {   // main.m 起宿主前调一次 | main.m calls this once before starting the host
    dm_community_copy_init(ios_copy);
    const char *tb = getenv("DM_TEST_UPDATE_BUILD"), *tu = getenv("DM_TEST_UPDATE_URL");
    NSString *v = tb ? @(tb) : [NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] description]; g_own = v.intValue;
    if (![v isEqualToString:[NSString stringWithFormat:@"%d", g_own]] || g_own < 0) g_own = 0;
    NSString *url = tu ? @(tu) : NSBundle.mainBundle.infoDictionary[@"SWUpdateURL"];
    NSString *label = [NSBundle.mainBundle.infoDictionary[@"DMVersionLabel"] description];   // 没经 run.sh 构建时为空 → 宿主改写构建号 | empty when not built through run.sh → the host shows the build number
    if ([url isKindOfClass:NSString.class] && url.length && g_own > 1) g_url = url;
    if (g_url) dm_update_site([NSURL URLWithString:g_url].host.UTF8String);   // 检查失败时提示玩家去浏览器打开的站点 = 版本文件所在的主机 | The site the player is told to open in a browser when the check fails = the host that serves the version file
    plog("更新：自己的构建 %d（显示「%s」），%s", g_own, label.length ? label.UTF8String : "没有", g_url ? "到选角页后查一次新版本" : g_own > 1 ? "包里没有版本文件地址，不查新版本" : "开发包，不查新版本");
    dm_update_init(g_own, label.UTF8String, on_select);
}
