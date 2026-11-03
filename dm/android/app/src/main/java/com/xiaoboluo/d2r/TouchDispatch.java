package com.xiaoboluo.d2r;

import java.util.Arrays;

/** 触摸槽位状态机：按下 / 拖拽 / 抬起 / 取消、原始触屏整组下发，以及「画面矩形变了先取消全部触点」。
    不依赖 Android，宿主 JVM 直接跑（dm/android/tests/TouchDispatchTest.java）；TouchOverlay 只做 MotionEvent 拆包，
    于是这几条规则可以在没有手机、没有 Android 运行时的情况下逐点断言。
    The touch slot state machine: press / drag / release / cancel, the whole raw touch set, and "a changed picture
    rectangle cancels every touch first". No Android dependency, so a host JVM runs it directly
    (dm/android/tests/TouchDispatchTest.java) and TouchOverlay only unpacks MotionEvent; these rules can then be
    asserted point by point without a phone or an Android runtime.

    单位（两条通道各只有一处换算）：
      · nativeEvent（原生触控层）用 dp = 画面矩形内像素 / density —— 按下、拖动、抬起、取消四种事件同一单位，
        所以普通手机上逐点与改动前一致，取消也不会突然变成 density 倍。
      · DmActivity.nativeTouch（原始触屏整组）用画面矩形内的 0..1。
    Units (exactly one conversion per channel):
      · nativeEvent (the native touch layer) takes dp = pixels inside the picture rectangle / density, for press,
        drag, release *and* cancel, so a phone's points are unchanged and a cancel is not density times larger.
      · DmActivity.nativeTouch (the whole raw touch set) takes 0..1 inside the picture rectangle. */
final class TouchDispatch {
    /** 原生触控事件与原始触屏整组的下发口（TouchOverlay 接到 JNI）。 | Delivery to JNI (wired by TouchOverlay). */
    interface Out {
        void event(int phase, int slot, float x, float y);
        void raw(int count, int[] ids, float[] xs, float[] ys);
    }

    /** 窗口点 → 画面矩形内的像素；false = 落在黑边上（out 已夹到最近的边）。与显示的 blit 矩形同一来源。
        Window point -> pixels inside the picture rectangle; false = on a black bar (out is clamped to the nearest
        edge). Same source as the display's blit rectangle. */
    interface Projection {
        boolean map(float viewX, float viewY, float[] out);
    }

    static final int MAX = 16;
    private final float density;
    private final Out out;
    private final Projection projection;
    private final int[] ids = new int[MAX];               // 每个槽的 pointer id；-1 = 空 | pointer id per slot; -1 = free
    private final float[] px = new float[MAX], py = new float[MAX];   // 画面矩形内像素 | pixels inside the rectangle
    private final int[] rawIds = new int[MAX];
    private final float[] rawXs = new float[MAX], rawYs = new float[MAX];
    private final float[] rect = new float[4];            // 画面矩形（窗口像素）| picture rect (window px)
    private final float[] pt = new float[2];
    private boolean rectValid;
    private int viewW, viewH;

    TouchDispatch(float density, Out out, Projection projection) {
        if (!(density > 0)) throw new IllegalArgumentException("density " + density);
        if (out == null || projection == null) throw new IllegalArgumentException("out / projection");
        this.density = density;
        this.out = out;
        this.projection = projection;
        Arrays.fill(ids, -1);
    }

    /** 视图像素尺寸：还没有画面矩形时（首帧前）原始触屏按整窗归一化，与改动前一致。
        View size in pixels: before the first picture rectangle, raw touches are normalized by the whole view, as before. */
    void setViewSize(int wpx, int hpx) {
        viewW = Math.max(0, wpx);
        viewH = Math.max(0, hpx);
    }

    /** 画面矩形（窗口像素）{x,y,w,h} 变了：**先**取消全部触点（Java 槽位、原生 phase 3、原始触屏清空），
        再换新矩形。首帧（还没有矩形）也算变了。返回 true = 变了，调用方接着按新矩形重摆控件。
        A changed picture rectangle {x,y,w,h} in window pixels: cancel every touch *first* (Java slots, native
        phase 3, the raw set cleared), then take the new rectangle. The first frame (no rectangle yet) counts as a
        change. true = changed, and the caller lays the controls out again. */
    boolean updatePicture(float[] v) {
        if (v == null || v.length < 4) return false;
        boolean changed = !rectValid
                || Math.abs(v[0] - rect[0]) >= 0.5f || Math.abs(v[1] - rect[1]) >= 0.5f
                || Math.abs(v[2] - rect[2]) >= 0.5f || Math.abs(v[3] - rect[3]) >= 0.5f;
        if (!changed) return false;
        // 先取消：旧手指的槽位、原生槽位与原始触屏都不留（手指静止时不会有新的 MotionEvent 来纠正它们）。
        cancel();
        rect[0] = v[0]; rect[1] = v[1]; rect[2] = v[2]; rect[3] = v[3];
        rectValid = v[2] >= 1 && v[3] >= 1;
        publishRaw();   // 新矩形下再下发一次（此刻没有手指，就是空集）| deliver again under the new rect (empty now)
        return true;
    }

    /** 按下：黑边上的按下不占手指、不发事件（那个位置本来没有画面）。 | Press: a press on the bars takes no finger and sends nothing. */
    boolean down(int pointerId, float viewX, float viewY) {
        boolean taken = false;
        if (slot(pointerId) < 0 && projection.map(viewX, viewY, pt)) {
            for (int s = 0; s < MAX; ++s) if (ids[s] < 0) {
                ids[s] = pointerId; px[s] = pt[0]; py[s] = pt[1];
                send(0, s); taken = true;
                break;
            }
        }
        publishRaw();   // 每个 MotionEvent 都下发一次整组（黑边上的按下 = 空集）| every MotionEvent delivers the set
        return taken;
    }

    void move(int pointerId, float viewX, float viewY) {
        int s = slot(pointerId);
        if (s >= 0) {
            projection.map(viewX, viewY, pt);   // 拖出画面：夹到最近的边 | dragged out: clamped to the nearest edge
            px[s] = pt[0]; py[s] = pt[1];
            send(1, s);
        }
        publishRaw();
    }

    void up(int pointerId, float viewX, float viewY) {
        int s = slot(pointerId);
        if (s >= 0) {
            projection.map(viewX, viewY, pt);
            px[s] = pt[0]; py[s] = pt[1];
            send(2, s); ids[s] = -1;
        }
        publishRaw();
    }

    /** 取消全部触点：phase 3 的坐标与按下 / 拖动 / 抬起同一单位（dp）。 | Cancel all: phase 3 carries the same dp unit. */
    void cancel() {
        for (int s = 0; s < MAX; ++s) if (ids[s] >= 0) { send(3, s); ids[s] = -1; }
        publishRaw();
    }

    /** 不发声地清空（触控还没开启时用：JNI 里还没有可取消的槽位）。 | Clear silently (used before touch is enabled). */
    void reset() {
        Arrays.fill(ids, -1);
    }

    int active() {
        int n = 0;
        for (int s = 0; s < MAX; ++s) if (ids[s] >= 0) ++n;
        return n;
    }

    private int slot(int pointerId) {
        for (int s = 0; s < MAX; ++s) if (ids[s] == pointerId) return s;
        return -1;
    }

    private void send(int phase, int slot) {
        out.event(phase, slot, px[slot] / density, py[slot] / density);
    }

    private void publishRaw() {
        float rw = rectValid ? rect[2] : viewW, rh = rectValid ? rect[3] : viewH;
        int n = 0;
        if (rw > 0 && rh > 0) for (int s = 0; s < MAX; ++s) if (ids[s] >= 0) {
            rawIds[n] = s; rawXs[n] = px[s] / rw; rawYs[n] = py[s] / rh; ++n;
        }
        out.raw(n, rawIds, rawXs, rawYs);
    }
}
