// 加载页（安卓画法）：导入游戏内容 / 手机端生成着色器缓存时的整屏页面。状态机、进度合成、文案、素材选择与布局全在公共宿主 dm/host/dm_loadpage.c（经 libdmload.so），
// 这里只按它给的矩形把背景图（cover 裁切）、底部渐暗底板、进度条、四行字画出来，并放循环音乐；每 100 毫秒重画一次。素材在 APK 的 assets 根（dm/assets/loadpage/，自制）。
// 用法：view = new LoadPageView(activity); LoadPageView.begin(kinds, null); LoadPageView.set(stage, frac, itemsDone, itemsTotal, bytesDone, bytesTotal); … LoadPageView.finish() / fail(reason)。
// Loading page (Android drawing): the full-screen page shown while importing game content / generating the shader cache on the phone. State machine, progress composition, text,
// art selection and layout all live in the shared host dm/host/dm_loadpage.c (through libdmload.so); this class only draws the background (cover crop), the darkening plate at the
// bottom, the progress bar and the four text lines by the rectangles it gives, and plays the loop music; redrawn every 100 ms. Assets are at the APK's assets root (dm/assets/loadpage/, self-made).
package com.xiaoboluo.d2r;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.media.MediaPlayer;
import android.text.TextPaint;
import android.text.TextUtils;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;
import java.io.IOException;
import java.util.Locale;

public final class LoadPageView extends View {
    static { System.loadLibrary("dmload"); }
    static native void nativeLang(String tag);
    static native void nativeBegin(int[] kinds, int[] weightsMs);
    static native void nativeSet(int stage, double frac, long itemsDone, long itemsTotal, long bytesDone, long bytesTotal);
    static native void nativeNote(String text);
    static native void nativeFail(String reason);
    static native void nativeFinish();
    static native int nativeState();
    static native int nativePercent();
    static native boolean nativeMusicWanted();
    static native String nativeMusicName();
    static native String nativeBgName(float w, float h);
    static native String nativeLogoName();
    static native double[] nativeLayout(float w, float h, float l, float t, float r, float b);
    static native String[] nativeText();
    static native int nativeColor(int which);

    // 阶段种类（同 dm_loadpage.h 的枚举顺序） | stage kinds (same order as the enum in dm_loadpage.h)
    public static final int DOWNLOAD = 0, IMPORT = 1, PATCH = 2, ASSETS = 3, OVERLAY = 4, PACKS = 5, SHADERS = 6, INSTALL = 7, VERIFY = 8;
    public static final int IDLE = 0, RUNNING = 1, DONE = 2, FAILED = 3;
    // nativeLayout 数组下标 | nativeLayout array indices
    private static final int L_LAND = 0, L_BGW = 1, L_BGH = 2, L_BGSRC = 3, L_BGDST = 7, L_BAR = 11, L_FILL = 15, L_TITLE = 19, L_PCT = 23, L_BODY = 27, L_SUB = 31, L_BRAND = 35, L_TITLEPT = 39, L_BODYPT = 40, L_SUBPT = 41, L_BRANDPT = 42, L_RADIUS = 43, L_LOGO = 44, L_LOGOW = 48, L_LOGOH = 49;
    private static final int C_TITLE = 0, C_BODY = 1, C_SUB = 2, C_PCT = 3, C_FILL = 4, C_TRACK = 5, C_EDGE = 6;

    // ---- 公共入口（任何线程；状态在原生侧） | public entry points (any thread; state is on the native side) ----
    public static void begin(int[] kinds, int[] weightsMs) { nativeBegin(kinds, weightsMs); }
    public static void set(int stage, double frac, long itemsDone, long itemsTotal, long bytesDone, long bytesTotal) { nativeSet(stage, frac, itemsDone, itemsTotal, bytesDone, bytesTotal); }
    public static void note(String text) { nativeNote(text); }
    public static void fail(String reason) { nativeFail(reason); }
    public static void finish() { nativeFinish(); }
    public static int state() { return nativeState(); }
    public static int percent() { return nativePercent(); }

    private final float density;
    private Bitmap bg; private String bgName; private Bitmap logo; private boolean logoTried;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Typeface serif = Typeface.create(Typeface.SERIF, Typeface.BOLD), sans = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL);
    private final Rect src = new Rect(); private final RectF dst = new RectF();
    private float insetL, insetT, insetR, insetB;   // 挖孔 / 系统栏（dp） | cutout / system bars (dp)
    private MediaPlayer music; private boolean musicOff; private float musicVol = 0.55f;
    private final Runnable tick = new Runnable() { @Override public void run() { if (!isAttachedToWindow()) return; musicTick(); invalidate(); postDelayed(this, 100); } };

    public LoadPageView(Context ctx) {
        super(ctx);
        density = ctx.getResources().getDisplayMetrics().density;
        nativeLang(Locale.getDefault().toLanguageTag());
        setOnApplyWindowInsetsListener((v, insets) -> {
            android.graphics.Insets s = insets.getInsets(WindowInsets.Type.displayCutout());
            insetL = s.left / density; insetT = s.top / density; insetR = s.right / density; insetB = s.bottom / density; invalidate(); return insets;
        });
        setKeepScreenOn(true);
    }
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); post(tick); musicStart(); }
    @Override protected void onDetachedFromWindow() { removeCallbacks(tick); musicStop(); if (bg != null) { bg.recycle(); bg = null; bgName = null; } if (logo != null) { logo.recycle(); logo = null; } logoTried = false; super.onDetachedFromWindow(); }
    private void loadLogo() {
        if (logoTried) return; logoTried = true;
        try (java.io.InputStream in = getContext().getAssets().open(nativeLogoName())) { logo = BitmapFactory.decodeStream(in); }
        catch (IOException e) { Log.e("dm", "加载页：字标图读不到，退回文字字标：" + e); }
    }

    private void loadBg(float wdp, float hdp) {
        String name = nativeBgName(wdp, hdp); if (name.equals(bgName)) return;
        if (bg != null) bg.recycle(); bg = null; bgName = name;
        try (java.io.InputStream in = getContext().getAssets().open(name)) { bg = BitmapFactory.decodeStream(in); }
        catch (IOException e) { Log.e("dm", "加载页：背景图 " + name + " 读不到（APK 的 assets 里没有？）：" + e); }
    }
    private static RectF r(double[] L, int i, RectF out) { out.set((float) L[i], (float) L[i + 1], (float) (L[i] + L[i + 2]), (float) (L[i + 1] + L[i + 3])); return out; }
    private static int rgb(int c) { return 0xff000000 | c; }

    @Override protected void onDraw(Canvas c) {
        float w = getWidth() / density, h = getHeight() / density; if (w <= 0 || h <= 0) return;
        c.save(); c.scale(density, density);   // 之后全用 dp | everything in dp from here
        double[] L = nativeLayout(w, h, insetL, insetT, insetR, insetB); String[] T = nativeText();
        c.drawColor(0xff050403);
        loadBg(w, h);
        if (bg != null) {   // cover 裁切：图的实际像素尺寸与布局假定的不同也照样裁对 | cover crop: still right when the real pixel size differs from what the layout assumes
            double sx = (double) bg.getWidth() / L[L_BGW], sy = (double) bg.getHeight() / L[L_BGH];
            src.set((int) Math.round(L[L_BGSRC] * sx), (int) Math.round(L[L_BGSRC + 1] * sy), (int) Math.round((L[L_BGSRC] + L[L_BGSRC + 2]) * sx), (int) Math.round((L[L_BGSRC + 1] + L[L_BGSRC + 3]) * sy));
            c.drawBitmap(bg, src, r(L, L_BGDST, dst), paint);
        }
        RectF title = r(L, L_TITLE, new RectF()), pct = r(L, L_PCT, new RectF()), body = r(L, L_BODY, new RectF()), sub = r(L, L_SUB, new RectF()), brand = r(L, L_BRAND, new RectF());
        RectF bar = r(L, L_BAR, new RectF()), fill = r(L, L_FILL, new RectF());
        // 底部渐暗底板 | bottom darkening plate
        float y0 = title.top - 48; paint.setShader(new LinearGradient(0, y0, 0, h, 0x00000000, 0x9e000000, Shader.TileMode.CLAMP)); c.drawRect(0, y0, w, h, paint); paint.setShader(null);
        // 进度条 | the bar
        float rad = (float) L[L_RADIUS];
        paint.setStyle(Paint.Style.FILL); paint.setColor(rgb(nativeColor(C_TRACK))); paint.setAlpha(230); c.drawRoundRect(bar, rad, rad, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(1); paint.setColor(rgb(nativeColor(C_EDGE))); c.drawRoundRect(new RectF(bar.left - 0.5f, bar.top - 0.5f, bar.right + 0.5f, bar.bottom + 0.5f), rad + 0.5f, rad + 0.5f, paint);
        paint.setStyle(Paint.Style.FILL);
        if (fill.width() > 1) {
            paint.setColor(rgb(nativeColor(C_FILL))); paint.setShadowLayer(6, 0, 0, 0x99ff7319); c.drawRoundRect(fill, rad, rad, paint); paint.clearShadowLayer();
            paint.setColor(0x8cffc773); if (fill.width() > 4) c.drawRect(fill.left + 2, fill.top + 1, fill.right - 2, fill.top + 2, paint);
        }
        loadLogo();
        if (logo != null && L[L_LOGO + 2] > 0) c.drawBitmap(logo, null, r(L, L_LOGO, new RectF()), paint);   // 字标图：等比居中（矩形由公共布局算） | wordmark: aspect kept, centred (rect from the shared layout)
        else line(c, T[4], brand, (float) L[L_BRANDPT], nativeColor(C_TITLE), false, serif);
        line(c, T[0], title, (float) L[L_TITLEPT], nativeColor(C_TITLE), false, serif);
        line(c, T[1], pct, (float) L[L_TITLEPT], nativeColor(C_PCT), true, serif);
        line(c, T[2], body, (float) L[L_BODYPT], nativeColor(C_BODY), false, sans);
        line(c, T[3], sub, (float) L[L_SUBPT], nativeColor(C_SUB), false, sans);
        c.restore();
    }
    // 一行字：框内垂直居中，右对齐 = 百分数；超宽截尾省略；带投影 | one line: vertically centred in the box, right aligned for the percent; ellipsised when too wide; with a shadow
    private void line(Canvas c, String s, RectF box, float pt, int color, boolean right, Typeface face) {
        if (s == null || s.isEmpty() || box.width() <= 0) return;
        text.setTypeface(face); text.setTextSize(pt); text.setColor(rgb(color)); text.setShadowLayer(2.5f, 0, 1, 0xd9000000);
        CharSequence t = TextUtils.ellipsize(s, text, box.width(), TextUtils.TruncateAt.END);
        float width = text.measureText(t, 0, t.length());
        Paint.FontMetrics fm = text.getFontMetrics(); float baseline = box.top + (box.height() - (fm.descent - fm.ascent)) / 2 - fm.ascent;
        c.drawText(t, 0, t.length(), right ? box.right - width : box.left, baseline, text);
    }
    // ---- 音乐：循环，页面在就放；完成 / 失败淡出停 | music: loops while the page is up; fades out on done / failure ----
    private void musicStart() {
        if (music != null || "1".equals(System.getProperty("dm.loadpage.nomusic"))) return;
        try (AssetFileDescriptor fd = getContext().getAssets().openFd(nativeMusicName())) {
            music = new MediaPlayer(); music.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
            music.setLooping(true); music.setVolume(musicVol, musicVol); music.prepare(); music.start(); musicOff = false;
        } catch (IOException | RuntimeException e) { Log.w("dm", "加载页：音乐打不开，不放：" + e); if (music != null) { music.release(); music = null; } }
    }
    private void musicTick() {
        if (music == null || musicOff || nativeMusicWanted()) return;
        musicOff = true; final MediaPlayer m = music; music = null;
        final int[] step = {0};
        Runnable fade = new Runnable() { @Override public void run() {
            step[0]++; float v = Math.max(0, musicVol * (1 - step[0] / 12f));
            try { if (step[0] >= 12) { m.stop(); m.release(); } else { m.setVolume(v, v); postDelayed(this, 100); } } catch (RuntimeException e) { m.release(); }
        } };
        post(fade);
    }
    private void musicStop() { if (music != null) { try { music.stop(); } catch (RuntimeException e) { /* 已停 | already stopped */ } music.release(); music = null; } }
}
