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
    static native boolean nativeMuted();
    static native void nativeSetMuted(boolean muted);
    static native boolean nativeToggleMuted();
    static native String nativeUi(String key);
    static native String nativeMusicName();
    static native String nativeBgName(float w, float h);
    static native String nativeLogoName();
    static native double[] nativeLayout(float w, float h, float l, float t, float r, float b);
    static native String[] nativeText();
    static native int nativeColor(int which);
    static native String nativeLastLine();
    static native String[] nativeManifest(byte[] body);
    static native void nativeCommunity(String qq, String github);
    static native String nativeCopyValue();
    static native void nativeCopied(boolean ok, boolean pad);
    static native void nativePad(boolean on);
    static native boolean nativePadMode();

    // 阶段种类（同 dm_loadpage.h 的枚举顺序） | stage kinds (same order as the enum in dm_loadpage.h)
    public static final int DOWNLOAD = 0, IMPORT = 1, PATCH = 2, ASSETS = 3, OVERLAY = 4, PACKS = 5, SHADERS = 6, INSTALL = 7, VERIFY = 8, PREPARE = 9;
    public static final int IDLE = 0, RUNNING = 1, DONE = 2, FAILED = 3;
    // nativeLayout 数组下标 | nativeLayout array indices
    private static final int L_LAND = 0, L_BGW = 1, L_BGH = 2, L_BGSRC = 3, L_BGDST = 7, L_BAR = 11, L_FILL = 15, L_TITLE = 19, L_PCT = 23, L_BODY = 27, L_SUB = 31, L_BRAND = 35, L_TITLEPT = 39, L_BODYPT = 40, L_SUBPT = 41, L_BRANDPT = 42, L_RADIUS = 43, L_LOGO = 44, L_LOGOW = 48, L_LOGOH = 49, L_MUTE = 50, L_COMM = 54, L_COMM1 = 55, L_COMM2 = 59, L_COPY = 63;
    private static final int C_TITLE = 0, C_BODY = 1, C_SUB = 2, C_PCT = 3, C_FILL = 4, C_TRACK = 5, C_EDGE = 6;

    // ---- 公共入口（任何线程；状态在原生侧） | public entry points (any thread; state is on the native side) ----
    public static void begin(int[] kinds, int[] weightsMs) { nativeBegin(kinds, weightsMs); }
    public static void set(int stage, double frac, long itemsDone, long itemsTotal, long bytesDone, long bytesTotal) { nativeSet(stage, frac, itemsDone, itemsTotal, bytesDone, bytesTotal); }
    public static void note(String text) { nativeNote(text); }
    public static void fail(String reason) { nativeFail(reason); }
    public static void finish() { nativeFinish(); }
    public static int state() { return nativeState(); }
    public static int percent() { return nativePercent(); }
    public static String lastLine() { return nativeLastLine(); }   // 最近一次完成 / 失败那一行 | the latest finish / failure line
    // 社区信息（dm_loadpage.h）：清单校验走原生 dm_manifest_parse；拿到合法清单后交社区两项，页面底部显示官方免费渠道与复制钮
    // community info (dm_loadpage.h): manifests are validated by native dm_manifest_parse; once valid, the two items go to the page, which shows the official free channels and a copy button at the bottom
    public static String[] manifest(byte[] body) { return nativeManifest(body); }
    public static void community(String qq, String github) { nativeCommunity(qq, github); }

    private final float density;
    private Bitmap bg; private String bgName; private Bitmap logo; private boolean logoTried;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Typeface serif = Typeface.create(Typeface.SERIF, Typeface.BOLD), sans = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL);
    private final Rect src = new Rect(); private final RectF dst = new RectF();
    private float insetL, insetT, insetR, insetB;   // 挖孔 / 系统栏（dp） | cutout / system bars (dp)
    private MediaPlayer music; private boolean musicOn; private float musicVol = 0.55f; private float vol; private double[] lastLayout;
    private final android.content.SharedPreferences prefs;   // 静音开关的选择（键 muted，缺省有声音） | the mute toggle choice (key muted, sound on by default)
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
        prefs = ctx.getSharedPreferences("dm_loadpage", Context.MODE_PRIVATE); nativeSetMuted(prefs.getBoolean("muted", false)); muteLabel();
        setClickable(true);
    }
    private void muteLabel() { setContentDescription(nativeUi(nativeMuted() ? "dmLoadMusicOn" : "dmLoadMusicOff")); }
    // 点右上角静音钮：翻转、记住；判定区由公共核心定（图标四周各大 12 dp） | tap on the top-right mute button: flip and remember; the hit area comes from the shared core (12 dp larger on each side)
    // 点页脚的复制钮：把群号 / 链接放进剪贴板（判定区四周各大 8 dp）。摸屏幕 = 触屏模式（复制钮回到平常样子）
    // tap on the footer's copy button: put the group number / link on the clipboard (hit area 8 dp larger each side). Touching the screen = touch mode (the copy button looks normal again)
    @Override public boolean onTouchEvent(android.view.MotionEvent e) {
        if (e.getActionMasked() == android.view.MotionEvent.ACTION_DOWN && nativePadMode()) { nativePad(false); invalidate(); }
        if (e.getActionMasked() != android.view.MotionEvent.ACTION_UP || lastLayout == null) return true;
        float x = e.getX() / density, y = e.getY() / density; double[] L = lastLayout; double pad = 12;
        if (x >= L[L_MUTE] - pad && x <= L[L_MUTE] + L[L_MUTE + 2] + pad && y >= L[L_MUTE + 1] - pad && y <= L[L_MUTE + 1] + L[L_MUTE + 3] + pad) {
            boolean m = nativeToggleMuted(); prefs.edit().putBoolean("muted", m).apply(); muteLabel(); musicTick(); invalidate(); performClick();
        } else if (L[L_COMM] > 0 && x >= L[L_COPY] - 8 && x <= L[L_COPY] + L[L_COPY + 2] + 8 && y >= L[L_COPY + 1] - 8 && y <= L[L_COPY + 1] + L[L_COPY + 3] + 8) {
            copy(false); performClick();
        }
        return true;
    }
    // 手柄（§2.7）：任一手柄键 = 手柄模式（复制钮画成焦点态、前面加 A 键图标），A 键松开时复制。返回 true = 吃掉这个键
    // Controller (§2.7): any controller key = controller mode (the copy button drawn focused with an A glyph in front); A copies on release. Returns true = the key is consumed
    public boolean padKey(android.view.KeyEvent e) {
        if ((e.getSource() & (android.view.InputDevice.SOURCE_GAMEPAD | android.view.InputDevice.SOURCE_JOYSTICK)) == 0 && !android.view.KeyEvent.isGamepadButton(e.getKeyCode())) return false;
        if (!nativePadMode()) { nativePad(true); invalidate(); }
        if (e.getKeyCode() != android.view.KeyEvent.KEYCODE_BUTTON_A || lastLayout == null || lastLayout[L_COMM] <= 0) return false;
        if (e.getAction() == android.view.KeyEvent.ACTION_UP) copy(true);
        return true;
    }
    private void copy(boolean pad) {
        String v = nativeCopyValue(); boolean ok = false;
        if (!v.isEmpty()) try {
            android.content.ClipboardManager cm = (android.content.ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null) { cm.setPrimaryClip(android.content.ClipData.newPlainText("D2R Mobile Plus", v)); ok = true; }
        } catch (RuntimeException ex) { Log.w("dm", "加载页：剪贴板写不进去：" + ex); }
        nativeCopied(ok, pad); invalidate();
    }
    @Override public boolean performClick() { return super.performClick(); }
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); post(tick); }
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
        double[] L = nativeLayout(w, h, insetL, insetT, insetR, insetB); String[] T = nativeText(); lastLayout = L;
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
        muteIcon(c, r(L, L_MUTE, new RectF()), nativeMuted());
        loadLogo();
        if (logo != null && L[L_LOGO + 2] > 0) c.drawBitmap(logo, null, r(L, L_LOGO, new RectF()), paint);   // 字标图：等比居中（矩形由公共布局算） | wordmark: aspect kept, centred (rect from the shared layout)
        else line(c, T[4], brand, (float) L[L_BRANDPT], nativeColor(C_TITLE), false, serif);
        line(c, T[0], title, (float) L[L_TITLEPT], nativeColor(C_TITLE), false, serif);
        line(c, T[1], pct, (float) L[L_TITLEPT], nativeColor(C_PCT), true, serif);
        line(c, T[2], body, (float) L[L_BODYPT], nativeColor(C_BODY), false, sans);
        line(c, T[3], sub, (float) L[L_SUBPT], nativeColor(C_SUB), false, sans);
        if (L[L_COMM] > 0) footer(c, L, T);
        c.restore();
    }
    // 社区页脚（副文字号与颜色，§2.6）：两行字左对齐；右边复制钮 = 暗底圆角框 + 金色细边，字用正文色；手柄模式边框加亮、字前一个 A 键圆标；
    // 已复制 / 复制失败时字换掉 2 秒（核心给）。T[5..8] = 第一行、第二行、钮上的字、复制状态
    // Community footer (sub size and colour, §2.6): two left-aligned lines; on the right the copy button = dark rounded box + thin gold edge, label in the body colour;
    // in controller mode the edge brightens and an A disc precedes the label; copied / failed swaps the label for 2 s (from the core). T[5..8] = line 1, line 2, button label, copy state
    private void footer(Canvas c, double[] L, String[] T) {
        float pt = (float) L[L_SUBPT];
        line(c, T[5], r(L, L_COMM1, new RectF()), pt, nativeColor(C_SUB), false, sans);
        line(c, T[6], r(L, L_COMM2, new RectF()), pt, nativeColor(C_SUB), false, sans);
        RectF b = r(L, L_COPY, new RectF()); boolean pad = nativePadMode(); float rad = 6;
        paint.setStyle(Paint.Style.FILL); paint.setColor(pad ? 0x99201208 : 0x73000000); c.drawRoundRect(b, rad, rad, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(pad ? 1.6f : 1.0f); paint.setColor(rgb(nativeColor(pad ? C_TITLE : C_EDGE)));
        if (pad) paint.setShadowLayer(5, 0, 0, 0x99c8a850);
        c.drawRoundRect(new RectF(b.left + 0.5f, b.top + 0.5f, b.right - 0.5f, b.bottom - 0.5f), rad, rad, paint); paint.clearShadowLayer(); paint.setStyle(Paint.Style.FILL);
        text.setTypeface(sans); text.setTextSize(pt); text.setShadowLayer(2.5f, 0, 1, 0xd9000000);
        String label = T[7]; boolean feedback = !"0".equals(T[8]);
        float glyph = pad && !feedback ? pt + 6 : 0, tw = text.measureText(label), x = b.centerX() - (tw + glyph) / 2;
        if (glyph > 0) {   // A 键圆标 | A disc
            float cx = x + pt / 2, cy = b.centerY();
            paint.setColor(rgb(nativeColor(C_TITLE))); c.drawCircle(cx, cy, pt / 2 + 1, paint);
            text.setColor(0xff1a1008); text.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)); text.setTextSize(pt * 0.8f); text.clearShadowLayer();
            Paint.FontMetrics fm = text.getFontMetrics(); c.drawText("A", cx - text.measureText("A") / 2, cy - (fm.ascent + fm.descent) / 2, text);
            text.setTypeface(sans); text.setTextSize(pt); text.setShadowLayer(2.5f, 0, 1, 0xd9000000);
        }
        text.setColor(rgb(nativeColor("-1".equals(T[8]) ? C_FILL : feedback ? C_TITLE : C_BODY)));
        Paint.FontMetrics fm = text.getFontMetrics(); c.drawText(label, x + glyph, b.centerY() - (fm.ascent + fm.descent) / 2, text);
    }
    // 右上角静音钮：暗色圆底 + 金色细边 + 喇叭；有声音两道声波，静音一个叉（与 dm_loadpage_cg.m 同一画法） | top-right mute button: dark round base + thin gold edge + speaker; two waves when on, a cross when muted (same drawing as dm_loadpage_cg.m)
    private void muteIcon(Canvas c, RectF r, boolean muted) {
        float cx = r.centerX(), cy = r.centerY(), s = r.width() / 40f, x0 = cx - 9 * s;
        paint.setStyle(Paint.Style.FILL); paint.setColor(0x73000000); c.drawOval(r, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(1.2f); paint.setColor(rgb(nativeColor(C_EDGE))); c.drawOval(new RectF(r.left + 0.6f, r.top + 0.6f, r.right - 0.6f, r.bottom - 0.6f), paint);
        android.graphics.Path p = new android.graphics.Path();
        p.moveTo(x0, cy - 4 * s); p.lineTo(x0 + 5 * s, cy - 4 * s); p.lineTo(x0 + 11 * s, cy - 9.5f * s); p.lineTo(x0 + 11 * s, cy + 9.5f * s); p.lineTo(x0 + 5 * s, cy + 4 * s); p.lineTo(x0, cy + 4 * s); p.close();
        paint.setStyle(Paint.Style.FILL); paint.setColor(rgb(nativeColor(C_TITLE))); c.drawPath(p, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2 * s); paint.setStrokeCap(Paint.Cap.ROUND);
        if (muted) { float a = x0 + 15 * s, b = 7 * s, hb = b / 2 * 1.2f; c.drawLine(a, cy - hb, a + b, cy + hb, paint); c.drawLine(a + b, cy - hb, a, cy + hb, paint); }
        else for (int k = 0; k < 2; k++) { float rr = (5.5f + k * 4.5f) * s; c.drawArc(new RectF(x0 + 11 * s - rr, cy - rr, x0 + 11 * s + rr, cy + rr), -48.7f, 97.4f, false, paint); }
        paint.setStyle(Paint.Style.FILL); paint.setStrokeCap(Paint.Cap.BUTT);
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
    // ---- 音乐：跟着「该不该放」（进行中且没静音）——要放就淡入 0.6 秒，不要就淡出后暂停（再打开接着放）；页面摘掉时释放 | music follows "should it play" (running and not muted): fade in over 0.6 s, or fade out then pause (resumes when turned on again); released when the page is detached ----
    private MediaPlayer player() {
        if (music != null) return music;
        try (AssetFileDescriptor fd = getContext().getAssets().openFd(nativeMusicName())) {
            music = new MediaPlayer(); music.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
            music.setLooping(true); vol = 0; music.setVolume(0, 0); music.prepare();
        } catch (IOException | RuntimeException e) { Log.w("dm", "加载页：音乐打不开，不放：" + e); if (music != null) { music.release(); music = null; } }
        return music;
    }
    private void musicTick() {   // 每 100 ms：音量向目标走一步（0.6 秒走完），到 0 就暂停 | every 100 ms: one volume step toward the target (0.6 s in all), pause at 0
        boolean want = nativeMusicWanted();
        if (want && !musicOn) { MediaPlayer m = player(); if (m == null) return; musicOn = true; try { m.start(); } catch (RuntimeException e) { Log.w("dm", "加载页：音乐放不了：" + e); } }
        if (music == null) return;
        float target = want ? musicVol : 0, step = musicVol / 6;
        vol = want ? Math.min(target, vol + step) : Math.max(0, vol - step);
        try { music.setVolume(vol, vol); if (!want && musicOn && vol <= 0) { musicOn = false; music.pause(); } } catch (RuntimeException e) { /* 已释放 | already released */ }
    }
    private void musicStop() { musicOn = false; if (music != null) { try { music.stop(); } catch (RuntimeException e) { /* 已停 | already stopped */ } music.release(); music = null; } }
}
