package com.xiaoboluo.d2r;

import java.util.Arrays;

/* Java 侧触摸接线的宿主回归（不需要手机 / Android 运行时）：按下、拖拽、抬起、取消，黑边上的按下，
   按住不动时画面矩形变化，以及普通手机上四种事件的坐标逐点一致。
   跑的是生产代码 TouchDispatch（TouchOverlay 的 onTouchEvent / cancelTouches / viewportChanged 都调它），
   不是另写一份算法。
   Host regression for the Java side of the touch wiring (no phone / Android runtime needed): press, drag,
   release, cancel, a press on the black bars, the picture rectangle changing while a finger is held still, and
   the four events' coordinates being point-identical on an ordinary phone. It runs the production TouchDispatch
   (called by TouchOverlay's onTouchEvent / cancelTouches / viewportChanged), not a copy of the algorithm.
     javac -d <out> dm/android/app/src/main/java/com/xiaoboluo/d2r/TouchDispatch.java dm/android/tests/TouchDispatchTest.java
     java -cp <out> com.xiaoboluo.d2r.TouchDispatchTest
*/
public final class TouchDispatchTest {
    private static int cases, asserts;
    private static String current = "?";
    private static void section(String name) { ++cases; current = name; }

    private static void check(boolean ok, String what) {
        ++asserts;
        if (!ok) throw new AssertionError("case=" + current + " " + what);
    }
    private static void eq(float want, float got, String what) {
        ++asserts;
        if (want != got && Math.abs(want - got) > 0.0001f)
            throw new AssertionError("case=" + current + " " + what + " want=" + want + " got=" + got);
    }
    private static void eq(int want, int got, String what) {
        ++asserts;
        if (want != got) throw new AssertionError("case=" + current + " " + what + " want=" + want + " got=" + got);
    }

    /** 记录下发的事件与原始触屏整组 | Records the delivered events and the raw set. */
    static final class Recorder implements TouchDispatch.Out {
        int n;                       // 事件条数 | events seen
        final int[] phase = new int[64], slot = new int[64];
        final float[] x = new float[64], y = new float[64];
        int rawCount = -1;           // 最近一次原始触屏的条数 | last raw count
        int[] rawIds = new int[0];
        float[] rawXs = new float[0], rawYs = new float[0];

        public void event(int p, int s, float vx, float vy) {
            if (n < phase.length) { phase[n] = p; slot[n] = s; x[n] = vx; y[n] = vy; }
            ++n;
        }
        public void raw(int count, int[] ids, float[] xs, float[] ys) {
            rawCount = count;
            rawIds = Arrays.copyOf(ids, count);
            rawXs = Arrays.copyOf(xs, count);
            rawYs = Arrays.copyOf(ys, count);
        }
        void reset() { n = 0; rawCount = -1; rawIds = new int[0]; rawXs = new float[0]; rawYs = new float[0]; }
    }

    /** 测试替身：模拟原生 nativeMap（矩形内换算并夹边）；生产映射在 test_touch_wiring.c 里经 JNI 断言。
        Test double for the native map (rect conversion with clamping); the production mapping is asserted through
        JNI in test_touch_wiring.c. */
    static final class RectProjection implements TouchDispatch.Projection {
        float x, y, w, h;
        RectProjection(float x, float y, float w, float h) { set(x, y, w, h); }
        void set(float x, float y, float w, float h) { this.x = x; this.y = y; this.w = w; this.h = h; }
        public boolean map(float vx, float vy, float[] out) {
            boolean inside = vx >= x && vy >= y && vx < x + w && vy < y + h;
            out[0] = Math.max(0, Math.min(1, (vx - x) / w)) * w;
            out[1] = Math.max(0, Math.min(1, (vy - y) / h)) * h;
            return inside;
        }
    }

    public static void main(String[] args) {
        final float[] rect = {0, 150, 1600, 900};          // 4:3 窗口里的 16:9 画面 | 16:9 picture in a 4:3 window
        Recorder rec = new Recorder();
        RectProjection proj = new RectProjection(rect[0], rect[1], rect[2], rect[3]);
        TouchDispatch d = new TouchDispatch(3f, rec, proj);
        d.setViewSize(1600, 1200);
        check(d.updatePicture(rect), "first picture counts as a change");
        check(rec.rawCount == 0, "first picture: raw empty");

        // 1. 按下：窗口点 (300,450) 在画面 (0,150) 内 → 画面内 (300,300) 像素 → /3 = (100,100) dp。
        section("press");
        rec.reset();
        check(d.down(0, 300, 450), "press inside takes the finger");
        eq(1, rec.n, "one event");
        eq(0, rec.phase[0], "phase down");
        eq(100f, rec.x[0], "down x (dp)");
        eq(100f, rec.y[0], "down y (dp)");
        eq(1, rec.rawCount, "raw one finger");
        eq(0, rec.rawIds[0], "raw slot 0");
        eq(0.1875f, rec.rawXs[0], "raw x is 300/1600");
        eq(0.333333f, rec.rawYs[0], "raw y is 300/900");

        // 2. 拖拽：同一单位；拖出画面夹到边上。 | drag: same unit; dragged out clamps to the edge.
        section("drag");
        rec.reset();
        d.move(0, 600, 750);
        eq(1, rec.n, "one move event");
        eq(1, rec.phase[0], "phase move");
        eq(200f, rec.x[0], "move x");
        eq(200f, rec.y[0], "move y");
        rec.reset();
        d.move(0, -40, 5000);
        eq(0f, rec.x[0], "dragged out left clamps to 0");
        eq(300f, rec.y[0], "dragged out below clamps to the picture's height");

        // 3. 抬起。 | release.
        section("release");
        rec.reset();
        d.up(0, 900, 450);
        eq(2, rec.phase[0], "phase up");
        eq(300f, rec.x[0], "up x");
        eq(100f, rec.y[0], "up y");
        eq(0, d.active(), "no slot left");
        eq(0, rec.rawCount, "raw empty after release");

        // 4. 黑边上的按下：不占手指、不发事件（也不发非空原始触屏）。
        section("bar press");
        rec.reset();
        check(!d.down(1, 800, 20), "press on the top bar is refused");
        eq(0, rec.n, "no event for a bar press");
        eq(0, d.active(), "no finger taken");
        eq(0, rec.rawCount, "raw stays empty");

        // 5. 取消：两根手指，phase 3 的坐标与按下时同一单位（dp），原始触屏清空。
        section("cancel");
        rec.reset();
        d.down(7, 300, 450);
        d.down(8, 600, 750);
        rec.reset();
        d.cancel();
        eq(2, rec.n, "a cancel event per finger");
        eq(3, rec.phase[0], "phase cancel");
        eq(3, rec.phase[1], "phase cancel (second)");
        eq(100f, rec.x[0], "cancel x in dp, same as down");
        eq(100f, rec.y[0], "cancel y in dp, same as down");
        eq(200f, rec.x[1], "second cancel x");
        eq(200f, rec.y[1], "second cancel y");
        eq(0, d.active(), "all slots cleared");
        eq(0, rec.rawCount, "raw empty after cancel");
        rec.reset();
        d.move(7, 100, 100);
        eq(0, rec.n, "a cancelled finger cannot move back in");

        // 6. 普通手机：同一物理点的 down/move/up/cancel 逐点一致（P2 的单位回归）。
        section("unit parity");
        rec.reset();
        d.down(3, 320, 480);
        d.move(3, 320, 480);
        d.up(3, 320, 480);
        d.down(4, 320, 480);
        d.cancel();
        eq(5, rec.n, "five events");
        for (int i = 0; i < 5; ++i) {
            eq(rec.x[0], rec.x[i], "x identical across down/move/up/cancel at #" + i);
            eq(rec.y[0], rec.y[i], "y identical across down/move/up/cancel at #" + i);
        }
        eq(0, rec.phase[0], "first is down");
        eq(1, rec.phase[1], "second is move");
        eq(2, rec.phase[2], "third is up");
        eq(0, rec.phase[3], "fourth is down");
        eq(3, rec.phase[4], "fifth is cancel");

        // 7. 按住不动而画面矩形变了（P1-1）：先取消（旧坐标、同一单位），槽位清空、原始触屏空集；
        //    手指仍然按着，随后的 MOVE 不得回到槽位；新矩形下重新按下才生效。
        section("rect change while held");
        rec.reset();
        d.down(5, 300, 450);
        rec.reset();
        float[] moved = {0, 100, 1600, 1000};   // 折叠/旋转后的新矩形 | the new rectangle after a fold/rotation
        proj.set(moved[0], moved[1], moved[2], moved[3]);   // 原生那边此时也已是新矩形 | native already reports the new one
        check(d.updatePicture(moved), "rectangle change reported");
        eq(1, rec.n, "exactly one cancel event");
        eq(3, rec.phase[0], "phase cancel on the rectangle change");
        eq(100f, rec.x[0], "cancel carries the held finger's last point (dp)");
        eq(100f, rec.y[0], "cancel carries the held finger's last point (dp), y");
        eq(0, d.active(), "held finger released by the rectangle change");
        eq(0, rec.rawCount, "raw set empty after the rectangle change");
        rec.reset();
        d.move(5, 300, 500);                    // 手指没抬、系统继续送 MOVE | still touching; Android keeps sending MOVE
        eq(0, rec.n, "a still-held finger cannot move into the new rectangle");
        eq(0, rec.rawCount, "raw stays empty");
        check(d.down(5, 300, 500), "a fresh press takes the finger again");
        eq(0, rec.phase[0], "phase down after the new press");
        eq(400f/3f, rec.y[0], "(500-100) px inside the new rectangle / 3 dp");

        // 8. 同一个矩形重复上报不算变化（每帧都会问，不能每帧取消）。 | the same rectangle is not a change.
        section("same rectangle");
        rec.reset();
        check(!d.updatePicture(moved), "an unchanged rectangle reports no change");
        eq(0, rec.n, "no cancel for an unchanged rectangle");

        // 9. 16 个槽位满后不再收第 17 根手指。 | a 17th finger is refused.
        section("slots full");
        d.cancel();                             // 前面那根手指先抬起 | lift the finger left from the previous section
        rec.reset();
        for (int i = 0; i < 16; ++i) check(d.down(100 + i, 500, 500), "finger " + i + " takes a slot");
        check(!d.down(999, 500, 500), "the 17th finger is refused");
        eq(16, d.active(), "16 slots in use");
        d.cancel();

        System.out.println("RESULT PASS cases=" + cases + " assertions=" + asserts);
    }
}
