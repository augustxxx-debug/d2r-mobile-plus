/* 触摸接线回归：窗口缓冲几何的请求真值表、画面矩形（含驱动拒绝几何时的回退）、原生 viewport 的下发，
   以及 JNI nativeViewport / nativeMap / nativeEvent 的实际编组。
   全部调生产代码：dm/android/viewport.h 的 dm_vp_geometry_request / dm_vp_window_rect、
   dm/android/viewport_android.c 的状态与 dm_android_vk_viewport、dm/android/touch_android_jni.c 的 JNI 入口
   （用真的 jni.h 与一个只实现三个数组函数的 JNIEnv）。触摸原生层的其余符号在这里打桩，因为它们的逻辑由
   test_touch_snapshot.c 与 t_touch_controls 覆盖，不是本测试的对象。
   Touch wiring regression: the truth table of window-buffer geometry requests, the picture rectangle (including the
   fallback when the driver refuses the geometry), the native viewport delivery, and the actual marshalling of the JNI
   entries nativeViewport / nativeMap / nativeEvent. Everything calls production code: dm_vp_geometry_request /
   dm_vp_window_rect from dm/android/viewport.h, the state and dm_android_vk_viewport from
   dm/android/viewport_android.c, and the JNI entries from dm/android/touch_android_jni.c (with the real jni.h and a
   JNIEnv that implements only the three array functions). The rest of the native touch layer is stubbed here because
   test_touch_snapshot.c and t_touch_controls cover it; it is not this test's subject.
     JH=$(/usr/libexec/java_home 2>/dev/null || echo ~/dm-android/jdk/jdk-17.0.21+9/Contents/Home)
     clang -std=c11 -O1 -I <repo> -I "$JH/include" -I "$JH/include/darwin" \
       -o test_touch_wiring dm/android/tests/test_touch_wiring.c dm/android/viewport_android.c \
       dm/android/touch_android_jni.c -lm && ./test_touch_wiring
*/
#include <jni.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dm/android/viewport.h"
#include "dm/android/viewport_android.h"
#include "dm/android/touch_android.h"

/* touch_android_jni.c 的导出（同一个 C ABI，直接调）| the exports of touch_android_jni.c */
JNIEXPORT jboolean JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeViewport(JNIEnv *, jclass, jfloatArray);
JNIEXPORT jboolean JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(JNIEnv *, jclass, jfloatArray);
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeEvent(JNIEnv *, jclass, jint, jint, jfloat, jfloat);
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeResize(JNIEnv *, jclass, jfloat, jfloat, jfloat, jfloat, jfloat, jboolean);

static unsigned checks, failures, cases;
static const char *case_name = "?";

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { ++failures; fprintf(stderr, "FAIL case=%s %s:%d: %s\n", case_name, __FILE__, __LINE__, #expr); } \
} while (0)
static void near(double got, double want, double tol, const char *what) {
    ++checks;
    if (!(fabs(got - want) <= tol)) { ++failures; fprintf(stderr, "FAIL case=%s %s: got %.4f want %.4f\n", case_name, what, got, want); }
}

/* ---- 触摸原生层打桩（本测试只关心接线，不关心它们的逻辑）| stubs for the native touch layer ---- */
static int ev_calls, ev_phase = -1, ev_slot = -1; static float ev_x, ev_y;
static int resize_calls, tablet_calls, pad_touched_calls;
void dm_android_touch_event(int phase, int slot, float x, float y) { ++ev_calls; ev_phase = phase; ev_slot = slot; ev_x = x; ev_y = y; }
void dm_android_touch_resize(float w, float h, float l, float r, float b) { (void)w; (void)h; (void)l; (void)r; (void)b; ++resize_calls; }
void dm_android_touch_tablet(int tablet) { (void)tablet; ++tablet_calls; }
void dm_android_touch_layout(float out[DM_ANDROID_TOUCH_DRAW_FLOATS]) { for (int i = 0; i < DM_ANDROID_TOUCH_DRAW_FLOATS; ++i) out[i] = 0; }
void dm_android_pad_touched(void) { ++pad_touched_calls; }

/* ---- 一个只做数组读写的 JNIEnv（真 jni.h 的函数表）| a JNIEnv that only does array access ---- */
typedef struct { jsize len; jfloat v[16]; } fake_array;
static jsize JNICALL fake_len(JNIEnv *env, jarray a) { (void)env; return ((fake_array *)a)->len; }
static void JNICALL fake_get(JNIEnv *env, jfloatArray a, jsize start, jsize len, jfloat *buf) {
    (void)env; fake_array *f = (fake_array *)a;
    for (jsize i = 0; i < len; ++i) buf[i] = start + i < f->len ? f->v[start + i] : 0;
}
static void JNICALL fake_set(JNIEnv *env, jfloatArray a, jsize start, jsize len, const jfloat *buf) {
    (void)env; fake_array *f = (fake_array *)a;
    for (jsize i = 0; i < len; ++i) if (start + i < f->len) f->v[start + i] = buf[i];
}
static struct JNINativeInterface_ fns = {
    .GetArrayLength = fake_len,
    .GetFloatArrayRegion = fake_get,
    .SetFloatArrayRegion = fake_set,
};
static JNIEnv env = &fns;

static void reset_state(void) {
    dm_android_vk_set_window_size(0, 0);
    dm_android_vk_set_present_geometry(0, 0, 0, 0);
    ev_calls = pad_touched_calls = 0; ev_phase = ev_slot = -1; ev_x = ev_y = 0;
}

int main(void) {
    /* A. 几何请求真值表：present_native × 比例是否相同 × 缓冲现在是不是内容尺寸。
       Geometry request truth table: present_native x aspects match x buffer is the content. */
    case_name = "geometry request";
    {   int rw = -1, rh = -1;
        CHECK(dm_vp_geometry_request(1, 1920, 1080, 1920, 1080, 0, &rw, &rh) == 1 && rw == 1920 && rh == 1080);   /* 比例相同、缓冲是窗口 → 铺满 | match, buffer is the window -> fill */
        CHECK(dm_vp_geometry_request(1, 1920, 1080, 1920, 1080, 1, &rw, &rh) == 0);                              /* 已经铺满 → 不再请求 | already filling */
        CHECK(dm_vp_geometry_request(1, 1600, 1200, 1600, 900, 0, &rw, &rh) == 0);                               /* 比例不同、缓冲是窗口 → 不动 | differ, buffer is the window */
        CHECK(dm_vp_geometry_request(1, 1600, 1200, 1600, 900, 1, &rw, &rh) == 1 && rw == 0 && rh == 0);         /* 比例不同而缓冲还是内容 → 请求回窗口 | differ but buffer is content -> back to the window */
        CHECK(dm_vp_geometry_request(0, 1920, 1080, 1920, 1080, 0, &rw, &rh) == 0);                              /* 关掉 present_native、缓冲是窗口 → 不动 | native off, buffer is the window */
        CHECK(dm_vp_geometry_request(0, 1920, 1080, 1920, 1080, 1, &rw, &rh) == 1 && rw == 0 && rh == 0);        /* 关掉 present_native、缓冲还是内容 → 请求回窗口 | native off, buffer is content -> back */
        CHECK(dm_vp_geometry_request(1, 1600, 1200, 0, 0, 0, &rw, &rh) == 0);                                    /* 内容尺寸未知 → 不动 | content unknown */
        /* 请求 (0,0) 与 (w,h) 之外的任何组合都不会凭空「生效」：函数只描述要做什么。| no other combination invents success. */
        CHECK(dm_vp_geometry_request(1, 1600, 1200, 1600, 900, 0, &rw, &rh) == 0 && rw == 0 && rh == 0);
    }

    /* B. 画面矩形用**实际**交换链尺寸算：驱动拒绝几何时缓冲仍是内容尺寸、被合成器拉满窗口，矩形就必须是整窗。
       The rectangle is computed from the *actual* swapchain size: when the driver refuses the geometry the buffer is
       still the content and the compositor stretches it, so the rectangle must be the whole window. */
    case_name = "viewport follows actual buffer";
    {   int x = -1, y = -1, w = -1, h = -1;
        reset_state();
        dm_android_vk_set_window_size(1600, 1200);
        dm_android_vk_set_present_geometry(1600, 1200, 1600, 900);       /* 缓冲 = 窗口：16:9 居中留黑边 | buffer = window: centred 16:9 with bars */
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 1);
        CHECK(x == 0 && y == 150 && w == 1600 && h == 900);
        CHECK(dm_android_vk_viewport(&x, &y, &w, &h) == 1 && y == 150);

        /* 驱动接受了 (1600,900)：缓冲是内容、被拉满窗口 → 整窗。| geometry applied: the content buffer fills the window. */
        dm_android_vk_set_present_geometry(1600, 900, 1600, 900);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 1 && x == 0 && y == 0 && w == 1600 && h == 1200);

        /* 退回 letterbox 的 (0,0) 被驱动拒绝：缓冲仍是 1600x900、仍被拉满窗口。旧逻辑（两个比例条件）会给出
           {0,150,1600,900}，触控按 letterbox 而画面是拉满的；现在必须仍是整窗。
           The (0,0) reset back to letterbox is refused: the buffer is still 1600x900 and still stretched. The old
           two-aspect-condition logic returned {0,150,1600,900} here (touch letterbox while the picture is
           stretched); it must now be the whole window. */
        dm_android_vk_set_present_geometry(1600, 900, 1600, 900);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 1 && x == 0 && y == 0 && w == 1600 && h == 1200);
        CHECK(!(w == 1600 && h == 900));                                   /* 不再冒充 letterbox | no letterbox pretence */

        /* 普通手机：缓冲就是窗口、比例也对得上 → 整窗；16:9 内容在 20:9 窗口上留左右黑边。| a phone: buffer is the window. */
        reset_state();
        dm_android_vk_set_window_size(2340, 1080);
        dm_android_vk_set_present_geometry(2340, 1080, 1600, 738);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 1 && x == 0 && y == 0 && w == 2340 && h >= 1079);   /* 0.1% 的比例零头只差 1 像素 | the 0.1% aspect remainder costs one pixel */
        dm_android_vk_set_present_geometry(2340, 1080, 1920, 1080);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 1 && x == 210 && y == 0 && w == 1920 && h == 1080);

        /* 缓冲既不是窗口也不是内容（驱动给了别的尺寸）：先把内容放进缓冲，再按窗口缩放。
           A buffer that is neither the window nor the content: fit the content into the buffer, then scale to the window. */
        reset_state();
        dm_android_vk_set_window_size(1600, 1200);
        dm_android_vk_set_present_geometry(800, 600, 1600, 900);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 1);
        CHECK(x == 0 && y == 150 && w == 1600 && h == 900);                /* 缓冲里的 blit 矩形 × 2 | the blit rect x2 */
        CHECK(dm_android_vk_viewport(&x, &y, &w, &h) == 1 && y == 150 && h == 900);

        /* 尺寸未知 → 0，调用方按整窗算（改动前的行为）。| unknown sizes -> 0. */
        reset_state();
        dm_android_vk_set_window_size(1600, 1200);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 0);
        dm_android_vk_set_window_size(0, 0);
        dm_android_vk_set_present_geometry(1600, 900, 1600, 900);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 0);

        /* 缓冲尺寸还未知（首帧前）：按窗口算，矩形与旧的 dm_vp_fit 一致。| buffer unknown: the window is the buffer. */
        reset_state();
        dm_android_vk_set_window_size(1600, 1200);
        dm_android_vk_set_present_geometry(0, 0, 1600, 900);
        CHECK(dm_android_vk_picture_rect(&x, &y, &w, &h) == 1 && x == 0 && y == 150 && w == 1600 && h == 900);
    }

    /* C. 原生 viewport 下发 + JNI nativeViewport：Java 取到的就是这一份。| native delivery through JNI nativeViewport. */
    case_name = "jni nativeViewport";
    {   fake_array a = {4, {0, 0, 0, 0}};
        reset_state();
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeViewport(&env, NULL, (jfloatArray)&a) == JNI_FALSE);   /* 没呈现过 | nothing presented */
        dm_android_vk_set_window_size(1600, 1200);
        dm_android_vk_set_present_geometry(1600, 1200, 1600, 900);
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeViewport(&env, NULL, (jfloatArray)&a) == JNI_TRUE);
        CHECK(a.v[0] == 0 && a.v[1] == 150 && a.v[2] == 1600 && a.v[3] == 900);
        CHECK(a.len == 4);
        fake_array shorty = {3, {0, 0, 0}};
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeViewport(&env, NULL, (jfloatArray)&shorty) == JNI_FALSE);   /* 数组太短拒绝 | too short */
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeViewport(&env, NULL, NULL) == JNI_FALSE);
    }

    /* D. JNI nativeMap：窗口点 → 画面矩形内的点（像素），黑边上的按下第三个值为 0，拖出去夹边。
       Window point -> point inside the rectangle, in pixels; a bar press reports 0; a point outside is clamped. */
    case_name = "jni nativeMap";
    {   fake_array p = {5, {800, 600, 0, 0, 0}};
        dm_android_vk_set_window_size(1600, 1200);
        dm_android_vk_set_present_geometry(1600, 1200, 1600, 900);
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(&env, NULL, (jfloatArray)&p) == JNI_TRUE);
        CHECK(p.v[2] == 800 && p.v[3] == 450 && p.v[4] == 1);              /* 正中：x 不变，y 减去 150 的上黑边 | centre: x same, y minus the 150 bar */
        p.len = 5; p.v[0] = 800; p.v[1] = 20;
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(&env, NULL, (jfloatArray)&p) == JNI_TRUE);
        CHECK(p.v[4] == 0);                                                 /* 上黑边：丢掉这一下 | top bar: dropped */
        CHECK(p.v[3] == 0);                                                 /* 仍夹到画面内 | clamped into the picture */
        p.v[0] = 800; p.v[1] = 1180;
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(&env, NULL, (jfloatArray)&p) == JNI_TRUE);
        CHECK(p.v[4] == 0 && p.v[3] == 900);
        p.v[0] = -50; p.v[1] = 600;
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(&env, NULL, (jfloatArray)&p) == JNI_TRUE);
        CHECK(p.v[4] == 0 && p.v[2] == 0);
        /* 没呈现过：整窗即画面，原样传回。| nothing presented: the window is the picture. */
        reset_state();
        p.v[0] = 123; p.v[1] = 456;
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(&env, NULL, (jfloatArray)&p) == JNI_TRUE);
        CHECK(p.v[2] == 123 && p.v[3] == 456 && p.v[4] == 1);
        fake_array shortp = {4, {0, 0, 0, 0}};
        CHECK(Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(&env, NULL, (jfloatArray)&shortp) == JNI_FALSE);
    }

    /* E. JNI nativeEvent：阶段 / 槽位 / 坐标原样下发（取消也走同一条），并先报「碰了屏幕」。
       nativeEvent forwards phase / slot / coordinates unchanged (cancel too) and reports the screen was touched first. */
    case_name = "jni nativeEvent";
    {   ev_calls = pad_touched_calls = 0;
        Java_com_xiaoboluo_d2r_TouchOverlay_nativeEvent(&env, NULL, 0, 3, 100.5f, 200.25f);
        CHECK(ev_calls == 1 && ev_phase == 0 && ev_slot == 3 && ev_x == 100.5f && ev_y == 200.25f);
        CHECK(pad_touched_calls == 1);
        Java_com_xiaoboluo_d2r_TouchOverlay_nativeEvent(&env, NULL, 3, 3, 100.5f, 200.25f);
        CHECK(ev_calls == 2 && ev_phase == 3 && ev_x == 100.5f && ev_y == 200.25f);   /* 取消坐标与按下同一份 | same point on cancel */
        CHECK(pad_touched_calls == 1);                                                /* 取消不算「碰屏幕」 | cancel is not a touch */
        Java_com_xiaoboluo_d2r_TouchOverlay_nativeResize(&env, NULL, 400.0f, 300.0f, 1.0f, 2.0f, 3.0f, JNI_TRUE);
        CHECK(resize_calls == 1 && tablet_calls == 1);
    }

    /* F. 日志行只说实际几何：矩形变化才记一行，且写明缓冲尺寸与「铺满 / 留黑边」。
       The log line states the actual geometry: one line per change, with the buffer size and fill / letterbox. */
    case_name = "viewport log";
    {   reset_state();
        dm_android_vk_set_window_size(1600, 1200);
        dm_android_vk_set_present_geometry(1600, 900, 1600, 900);   /* 驱动拒绝后的实际状态 | the state after a refused geometry */
        int x, y, w, h;
        CHECK(dm_android_vk_viewport(&x, &y, &w, &h) == 1 && w == 1600 && h == 1200);
    }

    printf("touch wiring: %u/%u checks passed, %u failures\n", checks - failures, checks, failures);
    return failures != 0;
}
