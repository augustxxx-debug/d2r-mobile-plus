/* 画面在窗口里的矩形（viewport）：显示与触控共用这一份换算。
   The picture's rectangle inside the window: the one conversion presentation and touch share.
   纯函数、无平台依赖。呈现（vulkan_present.cpp 的 blit 目标矩形）与安卓触控层（把窗口点换回画面点）
   都调这里，于是画面比例对不上窗口（折叠屏内屏近 4:3 而游戏输出 16:9 之类）时，按钮画在哪、按下算在哪
   不会各用一套尺寸；窗口旋转、内外屏切换、像素不整除也都走同一条取整。
   Pure functions with no platform dependency. Presentation (the blit destination rectangle in
   vulkan_present.cpp) and the Android touch layer (window point back to picture point) both call these,
   so when the picture's aspect does not match the window (a fold inner screen near 4:3 while the game
   outputs 16:9) where a button is drawn and where a press counts cannot use two different sizes; window
   rotation, inner/outer screen switches and sizes that do not divide evenly all take the same rounding. */
#pragma once
#include <stdint.h>

typedef struct { int x, y, w, h; } dm_vp_rect;

/* 按内容比例放进窗口、居中；取整与原先的呈现代码逐位相同（scale = min(窗口÷内容)，尺寸截断、偏移整除）。
   Fit the content into the window keeping its aspect, centred; the rounding is bit-for-bit the one the
   present code used (scale = min(window/content), sizes truncated, offsets integer-divided). */
static inline dm_vp_rect dm_vp_fit(int win_w, int win_h, int content_w, int content_h) {
    dm_vp_rect r = {0, 0, win_w > 0 ? win_w : 0, win_h > 0 ? win_h : 0};
    if (win_w <= 0 || win_h <= 0 || content_w <= 0 || content_h <= 0) return r;
    double sx = (double)win_w / content_w, sy = (double)win_h / content_h, s = sx < sy ? sx : sy;
    int w = (int)(content_w * s), h = (int)(content_h * s);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > win_w) w = win_w;
    if (h > win_h) h = win_h;
    r.w = w; r.h = h; r.x = (win_w - w) / 2; r.y = (win_h - h) / 2;
    return r;
}

static inline int dm_vp_inside(dm_vp_rect r, double x, double y) {
    return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* 窗口点 → 画面矩形内的归一化坐标（0..1）。返回 1 = 在画面里，0 = 落在黑边上；两种都夹进 [0,1]，
   于是「黑边上的按下」可以由调用方丢掉（返回 0），拖出画面的手指仍落在最近的边上。
   window point -> normalized coordinates inside the picture rectangle (0..1). 1 = inside the picture,
   0 = on a black bar; both clamp into [0,1], so the caller can drop a press on the bars (return 0) while a
   finger dragged out of the picture still lands on the nearest edge. */
static inline int dm_vp_map(dm_vp_rect r, double x, double y, double *u, double *v) {
    int inside = dm_vp_inside(r, x, y);
    *u = r.w > 0 ? (x - r.x) / r.w : 0;
    *v = r.h > 0 ? (y - r.y) / r.h : 0;
    if (*u < 0) *u = 0;
    if (*u > 1) *u = 1;
    if (*v < 0) *v = 0;
    if (*v > 1) *v = 1;
    return inside;
}

/* 交换链（窗口缓冲）里的矩形 → 窗口里的矩形。合成器把缓冲按窗口尺寸缩放，所以这一段必须由「实际的缓冲像素」
   算，不能由「请求的缓冲像素」算：setBuffersGeometry 被驱动拒绝时缓冲仍是旧尺寸，被拉满窗口的正是那块缓冲。
   buf_w == win_w && buf_h == win_h 时逐位恒等（普通手机、缓冲本来就是窗口尺寸的情况不变）。
   A rectangle inside the swapchain (window buffer) -> the rectangle inside the window. The compositor scales the
   buffer to the window, so this must start from the buffer's *actual* pixels, never the requested ones: when the
   driver refuses setBuffersGeometry the buffer keeps its old size and that buffer is what gets stretched to the
   window. With buf == window this is the identity (a phone whose buffer already is the window is unchanged). */
static inline dm_vp_rect dm_vp_window_rect(dm_vp_rect buf, int buf_w, int buf_h, int win_w, int win_h) {
    dm_vp_rect r = {0, 0, win_w > 0 ? win_w : 0, win_h > 0 ? win_h : 0};
    if (buf_w <= 0 || buf_h <= 0 || win_w <= 0 || win_h <= 0) return r;
    double sx = (double)win_w / buf_w, sy = (double)win_h / buf_h;
    int x = (int)(buf.x * sx), y = (int)(buf.y * sy);
    int w = (int)(buf.w * sx + 0.5), h = (int)(buf.h * sy + 0.5);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > win_w - x) w = win_w - x;
    if (h > win_h - y) h = win_h - y;
    if (w < 1 || h < 1) { r.w = win_w; r.h = win_h; return r; }
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

/* 两边的比例是否算相同（相对差 1% 以内）。呈现那边只在相同时把窗口缓冲设成游戏出图尺寸、由显示硬件
   铺满（present_native）；不同就按画面矩形 blit 并留黑边，触控按同一个矩形换算。
   Whether the two aspects count as the same (within 1% relative). Presentation takes the window buffer at
   the game's output size and lets the display hardware fill it (present_native) only when they match;
   otherwise it blits into the picture rectangle with bars, and touch uses that same rectangle. */
static inline int dm_vp_same_aspect(int a_w, int a_h, int b_w, int b_h) {
    if (a_w <= 0 || a_h <= 0 || b_w <= 0 || b_h <= 0) return 1;   /* 尺寸还不知道：不改变现在的做法 | size unknown: keep the current behaviour */
    long long l = (long long)a_w * b_h, r = (long long)b_w * a_h;
    long long d = l > r ? l - r : r - l;
    return d * 100 <= r;
}

/* ---- 自适应全屏：窗口比 16:9 窄时按窗口比例出图，界面按 16:9 贴底（与 iOS front_ios.m、鸿蒙 front_ohos.c 同一条路）
   Adaptive fullscreen: a window narrower than 16:9 renders at the window's own aspect and lays the UI out at
   16:9 anchored to the bottom (the same road iOS front_ios.m and HarmonyOS front_ohos.c take). ---- */

/* 窗口比 16:9 窄 1% 以上（与 iOS / 鸿蒙同一个容差，也比 dm_vp_same_aspect 松一档）：折叠内屏、平板、分屏。
   只认横屏窗口（宽 > 高）：本 App 锁 sensorLandscape，竖着的一对（旋转动画中间的瞬时值之类）一律不算，
   界面矩形那条「宽 × 9/16 贴底」的公式在竖屏上没有意义。
   Window narrower than 16:9 by more than 1% (the same tolerance as iOS / HarmonyOS): fold inner screen, tablet,
   split screen. Landscape windows only (width > height): this app is locked to sensorLandscape, and a portrait
   pair (a rotation transient, say) never counts, because "width x 9/16 at the bottom" means nothing vertically. */
static inline int dm_vp_narrower_than_16_9(int w, int h) {
    return w > h && (long long)w * 100 < (long long)h * 176;   /* 16/9 × 0.99 | 16/9 x 0.99 */
}

/* 自适应全屏下的界面矩形（画面 / 窗口像素）：宽 = 画面宽，高 = 宽 × 9 / 16（偶数、不超过画面高），贴底。
   游戏界面与触控控件都在这块里，3D 铺满整个画面；这块就是游戏「屏幕宽高」全局量换算过来的那个矩形。
   The UI rectangle in adaptive fullscreen (frame / window pixels): width = the frame width, height =
   width * 9 / 16 (even, not above the frame height), anchored to the bottom. The game UI and the touch
   controls live inside it while the 3D scene fills the whole frame; it is the rectangle the game's
   "screen width / height" globals describe. */
static inline dm_vp_rect dm_vp_ui_band(int w, int h) {
    dm_vp_rect r = {0, 0, w > 0 ? w : 0, h > 0 ? h : 0};
    if (w <= 0 || h <= 0) return r;
    int bh = (int)(((long long)w * 9 / 16) & ~1ll);
    if (bh < 1) bh = 1;
    if (bh > h) bh = h;
    r.y = h - bh;
    r.h = bh;
    return r;
}

/* 前端出图尺寸（主机档）：窗口长边 × 短边 → 出图宽高（偶数、宽 ≤ 4096）。掌机档 / DM_GAME_RES / 像素预算留在前端。
   adaptive = 0：旧做法，窗口比 16:9 窄时按宽取 16:9（上下留黑边）；
   adaptive = 1：自适应全屏，出图比例 = 窗口比例（不裁切、不拉伸），界面按 dm_vp_ui_band 贴底。
   Docked output size: window long side x short side -> output width / height (even, width <= 4096). The
   handheld profile, DM_GAME_RES and the pixel budget stay in the front end.
   adaptive = 0: the old rule, a window narrower than 16:9 takes 16:9 by width (bars top and bottom);
   adaptive = 1: adaptive fullscreen, the output aspect = the window's (no crop, no stretch), with the UI
   laid out per dm_vp_ui_band at the bottom. */
static inline void dm_vp_frontend_res(int win_w, int win_h, int adaptive, unsigned *out_w, unsigned *out_h) {
    unsigned fw = win_w > 0 ? (unsigned)win_w : 0, fh = win_h > 0 ? (unsigned)win_h : 0;
    if (!fw || !fh) { *out_w = 1600; *out_h = 900; return; }   /* 还没有窗口：原版 16:9 | no window yet: the original 16:9 */
    /* 自适应全屏不夹 16:9：出图比例就是窗口比例，帧缓冲与窗口同形，呈现端不再留边、也不裁切。
       Adaptive fullscreen does not clamp to 16:9: the output aspect is the window's, the frame has the
       window's shape, and presentation neither leaves bars nor crops. */
    if ((uint64_t)fw * 9 < (uint64_t)fh * 16 && !adaptive) fh = (unsigned)(((uint64_t)fw * 9 / 16) & ~1ull);   /* 比 16:9 窄：按屏宽取 16:9 | narrower than 16:9: 16:9 by width */
    fw &= ~1u; fh &= ~1u;
    if (fw > 4096) { fh = (unsigned)(((uint64_t)fh * 4096 / fw) & ~1ull); fw = 4096; }
    *out_w = fw; *out_h = fh;
}

/* 触控 / 覆盖层的坐标系（窗口像素）：自适应全屏（画面铺满整窗、窗口比 16:9 窄）时是界面矩形，其余情况
   就是画面矩形（旧行为逐位不变）。
   The coordinate system for touch and the overlay (window px): the UI rectangle when the picture fills the
   window and the window is narrower than 16:9 (adaptive fullscreen); the picture rectangle otherwise
   (bit-for-bit the old behaviour). */
static inline dm_vp_rect dm_vp_control_rect(dm_vp_rect pic, int win_w, int win_h) {
    /* 画面铺满整窗（自适应全屏的比例对上了）且窗口比 16:9 窄 → 控件按界面矩形摆，与游戏 HUD 同一块；
       留边的画面（旧做法 / 比例不一致）→ 就是画面矩形，与改动前逐位相同。
       The picture fills the window (adaptive fullscreen's aspects match) and the window is narrower than
       16:9 -> the controls follow the UI rectangle, the same block as the game HUD; a letterboxed picture
       (the old rule / mismatched aspects) -> the picture rectangle, bit-for-bit as before. */
    if (dm_vp_narrower_than_16_9(win_w, win_h) && dm_vp_same_aspect(pic.w, pic.h, win_w, win_h)) return dm_vp_ui_band(win_w, win_h);
    return pic;
}

/* 窗口点 → 控件坐标系里的像素（控件坐标，单位与控件布局同一份）；返回 1 = 落在画面里。
   横向夹进控件矩形（拖出手指落在最近的边上），纵向夹进**画面**：界面上方那条 3D 带也属于画面，
   算一只手（纵向为负，控件一个都碰不到），黑边（旧做法）照样丢掉。
   Window point -> pixels in the control coordinate system (the same one the controls are laid out in);
   1 = inside the picture. Clamped horizontally into the control rectangle (a dragged-out finger lands on the
   nearest edge) and vertically into the *picture*: the 3D strip above the UI rectangle is part of the picture
   too, so it counts as a finger (negative y, touching no control), while a black bar (the old rule) is still
   dropped. */
static inline int dm_vp_map_ctrl(dm_vp_rect pic, dm_vp_rect ctrl, double x, double y, double *cx, double *cy) {
    int inside = dm_vp_inside(pic, x, y);
    if (ctrl.w <= 0 || ctrl.h <= 0) { *cx = x; *cy = y; return inside; }
    double px = x - ctrl.x, py = y - ctrl.y;
    double top = (double)pic.y - ctrl.y, bottom = (double)pic.y + pic.h - ctrl.y;   /* 画面在控件坐标里的纵向范围 | the picture in control coordinates */
    if (px < 0) px = 0;
    if (px > ctrl.w) px = ctrl.w;
    if (py < top) py = top;
    if (py > bottom) py = bottom;
    *cx = px; *cy = py;
    return inside;
}

/* 控件坐标（像素，相对控件矩形左上角）→ 画面归一化坐标：世界点选用（点怪、点箱子、点地走路）。
   界面的归一化坐标相对界面矩形，世界的归一化坐标相对整个画面 —— 自适应全屏下两者不是一块，
   所以这里单独一处换算，别处不再各算一遍。
   Control coordinates (px, relative to the control rectangle's top-left) -> normalized coordinates in the
   picture: for world taps (monsters, chests, walk-to-ground). The UI's normalized coordinates are relative
   to the UI rectangle while the world's are relative to the whole picture -- in adaptive fullscreen those
   are two different rectangles, so this is the one conversion and nowhere else re-does it. pic_w / pic_h <= 0
   (nothing presented yet) means the control rectangle is the picture. */
static inline void dm_vp_pic_norm(double x, double y, double ctrl_x, double ctrl_y, double pic_w, double pic_h, double *nx, double *ny) {
    if (!(pic_w > 0) || !(pic_h > 0)) { *nx = 0; *ny = 0; return; }
    *nx = (x + ctrl_x) / pic_w;
    *ny = (y + ctrl_y) / pic_h;
}

/* 要不要改写窗口缓冲尺寸（present_native 铺满 / 退回窗口尺寸），以及写成多少。present 每帧调用一次；
   native_on = 窗口缓冲**实际**就是游戏出图尺寸（由 ANativeWindow_getWidth/Height 读回，不按下一次请求记账）。
   返回 1 = 调 ANativeWindow_setBuffersGeometry(*req_w,*req_h,0)；req 为 0,0 表示回到窗口尺寸。
   请求被驱动拒绝时这里不改任何「以为已经生效」的状态：下一帧读回的实际尺寸仍是旧值，于是继续请求；
   呈现与触控按实际尺寸走，不会出现「画面拉满而触控按 letterbox」。
   Whether to rewrite the window buffer size (present_native fill, or back to the window size) and to what.
   present calls it once per frame; native_on = the window buffer's *actual* size is the game's output (read back
   with ANativeWindow_getWidth/Height, never from the previous request). 1 = call ANativeWindow_setBuffersGeometry
   with the request; 0,0 means back to the window size. A refused request changes no "assume applied" state here:
   the next frame reads the old geometry back and asks again, and presentation and touch both follow the actual
   size, so "picture stretched while touch uses letterbox" cannot arise. */
static inline int dm_vp_geometry_request(int present_native,int win_w,int win_h,int content_w,int content_h,int native_on,int *req_w,int *req_h) {
    *req_w = 0; *req_h = 0;
    if (content_w <= 0 || content_h <= 0) return 0;
    if (present_native && dm_vp_same_aspect(win_w,win_h,content_w,content_h)) {
        if (native_on) return 0;
        *req_w = content_w; *req_h = content_h;
        return 1;
    }
    return native_on ? 1 : 0;   /* 请求 (0,0)：回到窗口尺寸 | request (0,0): back to the window size */
}

/* B141：请求过、驱动也回了 0 的缓冲几何算「已生效」，直到出图尺寸再变、或者换了一块 surface（dm_vp_geom_rebind）。
   有的机器（Fold8 / One UI）setBuffersGeometry 返回 0、交换链也按新尺寸建了，ANativeWindow_getWidth 却仍读回窗口尺寸；
   只认读回值时 native_on 永远是 0，present 每帧再请求一次，每次都扰动 surface、逼着交换链逐帧重建（每 35 ms 一次），
   启动与进局都被拖慢、整机发热。被拒绝（返回非 0）的请求不记，下一帧照旧重试，原来那条「不冒充成功」的约束不变。
   B141: a buffer geometry that was requested and accepted (0 from the driver) counts as in effect until the output size
   changes again or the surface is replaced (dm_vp_geom_rebind). On some phones (Fold8 / One UI) setBuffersGeometry returns 0 and the swapchain is built at the
   new size, yet ANativeWindow_getWidth still reads back the window size; trusting the read-back alone left native_on at 0
   forever, present asked again every frame, and each call disturbed the surface and forced a swapchain rebuild per frame
   (every 35 ms), slowing start-up and entering a game and heating the phone. A refused request (non-zero) is not recorded
   and is retried next frame, so the old "never pass a failure off as success" rule stands. */
/* stale = 这个 ANativeWindow 上曾被改成非窗口尺寸、还没退回去（surfaceChanged 换代时记账清掉，但同一个 ANativeWindow 上的
   缓冲几何还在，见 dm_vp_geom_frame）。 | stale = this ANativeWindow was set to a non-window size and not yet put back (a surfaceChanged
   generation clears the memo, but the buffer geometry stays on the same ANativeWindow, see dm_vp_geom_frame). */
typedef struct { int req_w, req_h, win_w, win_h, stale; unsigned long long surface; } dm_vp_geom_memo;

/* 记账只对「接受它的那块 surface」有效：切后台回来、内外屏切换会换一个新的 ANativeWindow，新窗口的缓冲几何回到窗口尺寸，
   必须清掉重新请求（交叉审 P1：不清会让读回正常的手机切一次后台就永久丢掉 present_native）。present 每帧先调一次。
   The memo only holds for the surface that accepted it: a return from the background or a fold switch brings a new ANativeWindow
   whose buffer geometry is back at the window size, so it must be cleared and requested again (cross-review P1: otherwise a phone
   whose read-back is correct loses present_native for good after one trip to the background). Present calls it first every frame. */
static inline void dm_vp_geom_rebind(dm_vp_geom_memo *m, unsigned long long surface) {
    if (m->surface != surface) { m->stale = m->stale || m->req_w > 0 || m->req_h > 0; m->req_w = m->req_h = m->win_w = m->win_h = 0; m->surface = surface; }
}

static inline int dm_vp_native_on(int buf_w, int buf_h, int content_w, int content_h, int win_w, int win_h, const dm_vp_geom_memo *m) {
    if (content_w <= 0 || content_h <= 0) return 0;
    if (buf_w == content_w && buf_h == content_h) return 1;
    /* 只认出图尺寸、不认窗口：折到外屏时缓冲仍是上一次接受的尺寸（读回看不出来），必须照样算 native_on，
       dm_vp_geometry_request 才会发那一次「回到窗口尺寸」；同比例缩放窗口时缓冲几何本来也仍然有效。
       Keyed on the output size, not the window: after folding to the outer screen the buffer still has the last accepted size
       (the read-back cannot show it) and must count as native_on, so dm_vp_geometry_request sends the one "back to the window
       size"; for a same-aspect window resize the buffer geometry is still valid anyway. */
    (void)win_w; (void)win_h;
    return m && m->req_w == content_w && m->req_h == content_h;
}

/* 一次 setBuffersGeometry 之后记账：返回 0 且请求的是出图尺寸 → 记住；返回 0 且请求 0,0（回到窗口尺寸）→ 清掉；
   被拒绝 → 什么都不改（缓冲还是原样，下一帧照旧重试同一请求）。
   Bookkeeping after one setBuffersGeometry: 0 for an output-size request -> remember it; 0 for a 0,0 request (back to the
   window size) -> forget; a refusal -> change nothing (the buffer is as it was, and the same request is retried next frame). */
static inline void dm_vp_geom_note(dm_vp_geom_memo *m, int result, int req_w, int req_h, int win_w, int win_h) {
    if (result != 0) return;
    m->stale = 0;   /* 被接受的请求就是窗口上现在的几何 | an accepted request is what the window now has */
    if (req_w > 0 && req_h > 0) { m->req_w = req_w; m->req_h = req_h; m->win_w = win_w; m->win_h = win_h; }
    else { m->req_w = m->req_h = m->win_w = m->win_h = 0; }
}

/* present 每帧的几何决定，合在一处（present 与单测调同一段）：换 surface 先清记账 → 算 native_on → 要不要请求、请求多少。
   返回 1 = 调 ANativeWindow_setBuffersGeometry(*req_w,*req_h,0)，之后用 dm_vp_geom_note 记账。buf = 读回的缓冲尺寸。
   Present's per-frame geometry decision in one place (present and the unit tests run the same code): a new surface clears the
   memo -> native_on -> whether to request and what. 1 = call ANativeWindow_setBuffersGeometry(*req_w,*req_h,0), then
   dm_vp_geom_note. buf = the buffer size read back. */
static inline int dm_vp_geom_frame(dm_vp_geom_memo *m, unsigned long long surface, int present_native, int win_w, int win_h,
                                   int content_w, int content_h, int buf_w, int buf_h, int *req_w, int *req_h) {
    dm_vp_geom_rebind(m, surface);
    *req_w = 0; *req_h = 0;
    if (content_w <= 0 || content_h <= 0) return 0;
    if (present_native && dm_vp_same_aspect(win_w, win_h, content_w, content_h)) {
        int native_on = dm_vp_native_on(buf_w, buf_h, content_w, content_h, win_w, win_h, m);
        return dm_vp_geometry_request(present_native, win_w, win_h, content_w, content_h, native_on, req_w, req_h);
    }
    /* 不铺满时：只要我们改过的几何还在（记账非空，或换代前改过、还没退回），就请求一次 (0,0)。不能用「记账 == 当前出图」
       判断（B140 增量审 P1-1）：折到外屏时出图尺寸也变了，记账对不上，(0,0) 就永远不发，画面被合成器拉伸、触控按旧矩形。
       也不看读回 != 窗口：读回与窗口尺寸来源不同，某台机器上一直不等就会逐帧请求（B141 那种逐帧重建）。
       Not filling: while a geometry we set may still be in place (memo non-empty, or set before a generation change and not put back),
       request (0,0) once. Not "memo == current output" (B140 incremental review P1-1): folding to the outer screen changes the output
       size too, the memo no longer matches, (0,0) was never sent, and the compositor stretched the picture while touch used the old
       rectangle. Nor "read-back != window": the two sizes come from different places, and a phone where they always differ would
       request every frame (the per-frame rebuild of B141). */
    (void)buf_w; (void)buf_h;
    return m->stale || m->req_w > 0 || m->req_h > 0;
}
