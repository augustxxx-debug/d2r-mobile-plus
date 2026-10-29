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
    static native void nativePrompt(String titleKey, String body, String subKey);
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
    static native void nativeSetWordmark(boolean on);
    static native String[] nativeManifest(byte[] body);
    static native void nativeCommunity(String qq, String github, String site);
    static native String nativeCopyValue();
    static native void nativeCopied(boolean ok, boolean pad);
    static native void nativePad(boolean on);
    static native boolean nativePadMode();

    // 阶段种类（同 dm_loadpage.h 的枚举顺序） | stage kinds (same order as the enum in dm_loadpage.h)
    public static final int DOWNLOAD = 0, IMPORT = 1, PATCH = 2, ASSETS = 3, OVERLAY = 4, PACKS = 5, SHADERS = 6, INSTALL = 7, VERIFY = 8, PREPARE = 9;
    public static final int IDLE = 0, RUNNING = 1, DONE = 2, FAILED = 3;
    // nativeLayout 数组下标 | nativeLayout array indices
    private static final int L_LAND = 0, L_BGW = 1, L_BGH = 2, L_BGSRC = 3, L_BGDST = 7, L_BAR = 11, L_FILL = 15, L_TITLE = 19, L_PCT = 23, L_BODY = 27, L_SUB = 31, L_BRAND = 35, L_TITLEPT = 39, L_BODYPT = 40, L_SUBPT = 41, L_BRANDPT = 42, L_RADIUS = 43, L_LOGO = 44, L_LOGOW = 48, L_LOGOH = 49, L_MUTE = 50, L_COMM = 54, L_COMM1 = 55, L_COMM2 = 59, L_COPY = 63, L_COMMPT = 67, L_COMMALPHA = 68, L_SHOWBAR = 69;
    private static final int C_TITLE = 0, C_BODY = 1, C_SUB = 2, C_PCT = 3, C_FILL = 4, C_TRACK = 5, C_EDGE = 6;

    // ---- 公共入口（任何线程；状态在原生侧） | public entry points (any thread; state is on the native side) ----
    public static void begin(int[] kinds, int[] weightsMs) { nativeBegin(kinds, weightsMs); }
    public static void set(int stage, double frac, long itemsDone, long itemsTotal, long bytesDone, long bytesTotal) { nativeSet(stage, frac, itemsDone, itemsTotal, bytesDone, bytesTotal); }
    public static void note(String text) { nativeNote(text); }
    // 提示页（权限页与加载页同一套样子）：标题 / 副文是多语键，正文是串；点屏幕（静音钮以外）调 onTap | notice page (the permission page looks like the loading page): title / sub are i18n keys, the body a string; a tap (outside the mute button) calls onTap
    public static void prompt(String titleKey, String body, String subKey) { nativePrompt(titleKey, body, subKey); }
    public static String ui(String key) { return nativeUi(key); }
    private Runnable onTap; public void setOnTap(Runnable r) { onTap = r; }
    public static void fail(String reason) { nativeFail(reason); }
    public static void finish() { nativeFinish(); }
    public static int state() { return nativeState(); }
    public static int percent() { return nativePercent(); }
    public static String lastLine() { return nativeLastLine(); }   // 最近一次完成 / 失败那一行 | the latest finish / failure line
    // 社区信息（dm_loadpage.h）：清单校验走原生 dm_manifest_parse；拿到合法清单后交社区两项，页面底部显示官方免费渠道与复制钮
    // community info (dm_loadpage.h): manifests are validated by native dm_manifest_parse; once valid, the two items go to the page, which shows the official free channels and a copy button at the bottom
    public static String[] manifest(byte[] body) { return nativeManifest(body); }
    public static void community(String qq, String github, String site) { nativeCommunity(qq, github, site); }

    private final float density;
    private Bitmap bg; private String bgName; private int baseColor = 0xff050403; private Bitmap logo; private boolean logoTried;
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
        nativeSetWordmark(false);   // 背景是 App 图标美术（带 MOBILE PLUS），不再画金色字标（用户 2026-10-29） | the background is the App icon art (reads MOBILE PLUS), so no gold wordmark (user 2026-10-29)
        setOnApplyWindowInsetsListener((v, insets) -> {
            android.graphics.Insets s = insets.getInsets(WindowInsets.Type.displayCutout());
            insetL = s.left / density; insetT = s.top / density; insetR = s.right / density; insetB = s.bottom / density; invalidate(); return insets;
        });
        setKeepScreenOn(true);
        prefs = ctx.getSharedPreferences("dm_loadpage", Context.MODE_PRIVATE); nativeSetMuted(prefs.getBoolean("muted", false)); muteLabel();
        setClickable(true);
        // 关掉系统缺省焦点高亮：可点的整屏视图在按了手柄键（离开触摸模式）后会被系统盖一层半透明灰，整页看着「泛白」，再摸屏才消失（用户 2026-10-29 反馈）。
        // 手柄模式下哪里有焦点由页脚复制钮自己的焦点态表示
        // Turn off the system default focus highlight: after a controller key (leaving touch mode) the system lays a translucent grey over a clickable full-screen view,
        // so the whole page "washes out" until the next touch (user report 2026-10-29). In controller mode the footer copy button shows focus by its own focused look
        setDefaultFocusHighlightEnabled(false);
    }
    private void muteLabel() { setContentDescription(nativeUi(nativeMuted() ? "dmLoadMusicOn" : "dmLoadMusicOff")); }
    // 点右上角静音钮：翻转、记住；判定区由公共核心定（图标四周各大 12 dp） | tap on the top-right mute button: flip and remember; the hit area comes from the shared core (12 dp larger on each side)
    // 点页脚的复制图标：把群号 / 链接放进剪贴板（判定区 copyHit，至少 48 dp）。摸屏幕 = 触屏模式（复制图标回到平常样子）
    // tap on the footer's copy icon: put the group number / link on the clipboard (hit area copyHit, at least 48 dp). Touching the screen = touch mode (the copy button looks normal again)
    @Override public boolean onTouchEvent(android.view.MotionEvent e) {
        if (e.getActionMasked() == android.view.MotionEvent.ACTION_DOWN && nativePadMode()) { nativePad(false); invalidate(); }
        if (e.getActionMasked() != android.view.MotionEvent.ACTION_UP || lastLayout == null) return true;
        float x = e.getX() / density, y = e.getY() / density; double[] L = lastLayout; double pad = 12;
        if (L[L_MUTE + 2] > 0 && x >= L[L_MUTE] - pad && x <= L[L_MUTE] + L[L_MUTE + 2] + pad && y >= L[L_MUTE + 1] - pad && y <= L[L_MUTE + 1] + L[L_MUTE + 3] + pad) {
            boolean m = nativeToggleMuted(); prefs.edit().putBoolean("muted", m).apply(); muteLabel(); musicTick(); invalidate(); performClick();
        } else if (L[L_COMM] > 0 && copyHit.contains(x, y)) {
            copy(false); performClick();
        } else if (onTap != null) { onTap.run(); performClick(); }
        return true;
    }
    // 手柄（§2.7）：任一手柄键 = 手柄模式（复制钮画成焦点态、前面加 A 键图标），A 键松开时复制。返回 true = 吃掉这个键
    // Controller (§2.7): any controller key = controller mode (the copy button drawn focused with an A glyph in front); A copies on release. Returns true = the key is consumed
    public boolean padKey(android.view.KeyEvent e) {
        // 来源要整组位都对上：SOURCE_GAMEPAD 含 SOURCE_CLASS_BUTTON 这一位，键盘也有，按位与非零会把音量键、键盘当成手柄（六审 Q6-01）
        // the whole source mask must match: SOURCE_GAMEPAD includes the SOURCE_CLASS_BUTTON bit that keyboards also carry, so a non-zero AND would treat volume keys and keyboards as a controller (sixth review Q6-01)
        int src = e.getSource();
        boolean pad = (src & android.view.InputDevice.SOURCE_GAMEPAD) == android.view.InputDevice.SOURCE_GAMEPAD || (src & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK
                || android.view.KeyEvent.isGamepadButton(e.getKeyCode());
        if (!pad) return false;
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
        if (bg != null) {   // 底色取背景图四角的平均色：图不铺满时（图标背景按图标外框缩放）图边与底色接得上、看不出矩形 | base colour = the mean of the four corners, so where the image does not fill the screen its edges blend in
            int[] px = { bg.getPixel(2, 2), bg.getPixel(bg.getWidth() - 3, 2), bg.getPixel(2, bg.getHeight() - 3), bg.getPixel(bg.getWidth() - 3, bg.getHeight() - 3) }; int rr = 0, gg = 0, bb = 0;
            for (int p : px) { rr += Color.red(p); gg += Color.green(p); bb += Color.blue(p); }
            baseColor = Color.rgb(rr / 4, gg / 4, bb / 4);
        }
    }
    private static RectF r(double[] L, int i, RectF out) { out.set((float) L[i], (float) L[i + 1], (float) (L[i] + L[i + 2]), (float) (L[i + 1] + L[i + 3])); return out; }
    private static int rgb(int c) { return 0xff000000 | c; }

    @Override protected void onDraw(Canvas c) {
        float w = getWidth() / density, h = getHeight() / density; if (w <= 0 || h <= 0) return;
        c.save(); c.scale(density, density);   // 之后全用 dp | everything in dp from here
        double[] L = nativeLayout(w, h, insetL, insetT, insetR, insetB); String[] T = nativeText(); lastLayout = L;
        loadBg(w, h);
        c.drawColor(baseColor);
        if (bg != null) {   // cover 裁切：图的实际像素尺寸与布局假定的不同也照样裁对 | cover crop: still right when the real pixel size differs from what the layout assumes
            double sx = (double) bg.getWidth() / L[L_BGW], sy = (double) bg.getHeight() / L[L_BGH];
            src.set((int) Math.round(L[L_BGSRC] * sx), (int) Math.round(L[L_BGSRC + 1] * sy), (int) Math.round((L[L_BGSRC] + L[L_BGSRC + 2]) * sx), (int) Math.round((L[L_BGSRC + 1] + L[L_BGSRC + 3]) * sy));
            c.drawBitmap(bg, src, r(L, L_BGDST, dst), paint);
        }
        RectF title = r(L, L_TITLE, new RectF()), pct = r(L, L_PCT, new RectF()), body = r(L, L_BODY, new RectF()), sub = r(L, L_SUB, new RectF()), brand = r(L, L_BRAND, new RectF());
        RectF bar = r(L, L_BAR, new RectF()), fill = r(L, L_FILL, new RectF());
        // 底部渐暗底板 | bottom darkening plate
        float y0 = title.top - 48; paint.setShader(new LinearGradient(0, y0, 0, h, 0x00000000, 0x9e000000, Shader.TileMode.CLAMP)); c.drawRect(0, y0, w, h, paint); paint.setShader(null);
        // 进度条（提示页不画） | the bar (not on a notice page)
        float rad = (float) L[L_RADIUS];
        if (L[L_SHOWBAR] > 0) {
        paint.setStyle(Paint.Style.FILL); paint.setColor(rgb(nativeColor(C_TRACK))); paint.setAlpha(230); c.drawRoundRect(bar, rad, rad, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(1); paint.setColor(rgb(nativeColor(C_EDGE))); c.drawRoundRect(new RectF(bar.left - 0.5f, bar.top - 0.5f, bar.right + 0.5f, bar.bottom + 0.5f), rad + 0.5f, rad + 0.5f, paint);
        paint.setStyle(Paint.Style.FILL);
        if (fill.width() > 1) {
            paint.setColor(rgb(nativeColor(C_FILL))); paint.setShadowLayer(6, 0, 0, 0x99ff7319); c.drawRoundRect(fill, rad, rad, paint); paint.clearShadowLayer();
            paint.setColor(0x8cffc773); if (fill.width() > 4) c.drawRect(fill.left + 2, fill.top + 1, fill.right - 2, fill.top + 2, paint);
        }
        }
        if (L[L_MUTE + 2] > 0) {   // 提示页没有静音钮；和页脚一样半透明（用户 2026-10-29） | no mute button on a notice page; translucent like the footer (user 2026-10-29)
            RectF mr = r(L, L_MUTE, new RectF()); mr.inset(-3, -3); int sc = c.saveLayerAlpha(mr, (int) L[L_COMMALPHA]);   // 图层边界放宽 3 dp，别裁掉圆的抗锯齿边 | layer bounds 3 dp wider so the circle's antialiased edge is not clipped muteIcon(c, r(L, L_MUTE, new RectF()), nativeMuted()); c.restoreToCount(sc);
        }
        if (L[L_LOGO + 2] > 0) loadLogo();   // 关字标时不读字标图 | the wordmark image is not read with the wordmark off
        if (logo != null && L[L_LOGO + 2] > 0) c.drawBitmap(logo, null, r(L, L_LOGO, new RectF()), paint);   // 字标图：等比居中（矩形由公共布局算） | wordmark: aspect kept, centred (rect from the shared layout)
        else line(c, T[4], brand, (float) L[L_BRANDPT], nativeColor(C_TITLE), false, serif);
        line(c, T[0], title, (float) L[L_TITLEPT], nativeColor(C_TITLE), false, serif);
        line(c, T[1], pct, (float) L[L_TITLEPT], nativeColor(C_PCT), true, serif);
        line(c, T[2], body, (float) L[L_BODYPT], nativeColor(C_BODY), false, sans);
        line(c, T[3], sub, (float) L[L_SUBPT], nativeColor(C_SUB), false, sans);
        if (L[L_COMM] > 0) {   // 页脚 0.4 秒淡入（清单晚到时不「蹦」出来） | the footer fades in over 0.4 s (no pop when the manifest arrives late)
            long now = android.os.SystemClock.uptimeMillis(); if (footerSince == 0) footerSince = now;
            float fade = Math.min(1f, (now - footerSince) / 400f);
            if (fade < 1f) { int sc = c.saveLayerAlpha(0, 0, w, h, (int) (255 * fade)); footer(c, L, T); c.restoreToCount(sc); } else footer(c, L, T);
        } else footerSince = 0;
        c.restore();
    }
    // 社区页脚（不抢眼：12 点、半透明，用户 2026-10-29「太乱，字小一些、按钮小一些、文字加透明度」）：两行字左对齐，副文色。
    // 复制是一个小图标，紧跟在第一行字后面（用户 2026-10-29「复制按钮太偏，缩成小图标」），同样半透明；点它（判定区至少 48 dp）复制。
    // 手柄模式：图标不透明、带金色圈，后面一个 A 键圆标 + 「复制群号」；复制后图标后面写「已复制 / 复制失败」2 秒（核心给，六套语言）。T[5..8] = 第一行、第二行、钮上的字、复制状态
    // Community footer (unobtrusive: 12 pt, translucent; user 2026-10-29 "too busy - smaller text, smaller button, more transparent text"): two left-aligned lines in the sub colour.
    // Copying is a small icon right after the first line (user 2026-10-29 "the copy button is too far off, make it a small icon"), equally translucent; tapping it (hit area at least 48 dp
    // each side) copies. Controller mode: the icon opaque with a gold ring, followed by an A disc + the "copy number" label; after copying, "Copied / Copy failed" follows the icon for 2 s
    // (from the core, six languages). T[5..8] = line 1, line 2, button label, copy state
    private final RectF copyHit = new RectF();
    private void footer(Canvas c, double[] L, String[] T) {
        float pt = (float) L[L_COMMPT]; int alpha = (int) L[L_COMMALPHA];
        RectF l1 = r(L, L_COMM1, new RectF());
        RectF l1t = new RectF(l1.left, l1.top, Math.max(l1.left, l1.right - pt * 1.6f), l1.bottom);   // 第一行按「行宽减图标位」省略，与下面的图标定位同一个宽度（七审 Q7-13） | line 1 ellipsized to the width minus the icon room, the same width the icon is placed by (seventh review Q7-13)
        lineAlpha = alpha;
        line(c, T[5], l1t, pt, nativeColor(C_SUB), false, sans);
        line(c, T[6], r(L, L_COMM2, new RectF()), pt, nativeColor(C_SUB), false, sans);
        lineAlpha = 255;
        text.setTypeface(sans); text.setTextSize(pt);
        CharSequence shown = TextUtils.ellipsize(T[5], text, l1t.width(), TextUtils.TruncateAt.END);   // 与上面画出来的第一行同宽同省略 | the same width and ellipsis as the line drawn above
        float iconS = pt + 4, ix = l1.left + text.measureText(shown, 0, shown.length()) + pt * 0.6f, iy = l1.centerY() - iconS / 2;
        boolean pad = nativePadMode(), feedback = !"0".equals(T[8]);
        float hp = Math.max(10, (48 - iconS) / 2); copyHit.set(ix - hp, iy - hp, ix + iconS + hp, iy + iconS + hp);   // 点击区至少 48 dp（七审 Q7-14） | hit area at least 48 dp (seventh review Q7-14)
        // 复制图标：两张错开的圆角纸 | copy icon: two offset rounded sheets
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(1.3f); paint.setColor(rgb(nativeColor(pad ? C_TITLE : C_BODY))); paint.setAlpha(pad || feedback ? 255 : alpha);
        float u = iconS / 16f, rr = 2 * u;
        c.drawRoundRect(new RectF(ix + 5 * u, iy + 1 * u, ix + 15 * u, iy + 11 * u), rr, rr, paint);
        paint.setStyle(Paint.Style.FILL); paint.setColor(0xff0a0705); c.drawRoundRect(new RectF(ix + 1 * u, iy + 5 * u, ix + 11 * u, iy + 15 * u), rr, rr, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setColor(rgb(nativeColor(pad ? C_TITLE : C_BODY))); paint.setAlpha(pad || feedback ? 255 : alpha);
        c.drawRoundRect(new RectF(ix + 1 * u, iy + 5 * u, ix + 11 * u, iy + 15 * u), rr, rr, paint);
        if (pad) { paint.setStrokeWidth(1.2f); paint.setShadowLayer(4, 0, 0, 0x80c8a850); c.drawCircle(ix + iconS / 2, iy + iconS / 2, iconS * 0.85f, paint); paint.clearShadowLayer(); }
        paint.setStyle(Paint.Style.FILL); paint.setAlpha(255);
        // 图标后面：反馈（已复制 / 复制失败）或手柄模式的「A 复制群号」；触屏平常什么都不写 | after the icon: the feedback, or "A copy number" in controller mode; nothing in touch mode normally
        if (!feedback && !pad) return;
        float x = ix + iconS + (pad ? iconS * 0.6f : pt * 0.5f), cy = l1.centerY();
        text.setShadowLayer(2f, 0, 1, 0xd9000000);
        if (pad && !feedback) {   // A 键圆标 | A disc
            float cx = x + pt / 2;
            paint.setColor(rgb(nativeColor(C_TITLE))); c.drawCircle(cx, cy, pt / 2, paint);
            text.setColor(0xff1a1008); text.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)); text.setTextSize(pt * 0.75f); text.clearShadowLayer();
            Paint.FontMetrics fm = text.getFontMetrics(); c.drawText("A", cx - text.measureText("A") / 2, cy - (fm.ascent + fm.descent) / 2, text);
            text.setTypeface(sans); text.setTextSize(pt); text.setShadowLayer(2f, 0, 1, 0xd9000000);
            x += pt + 4;
        }
        text.setColor(rgb(nativeColor("-1".equals(T[8]) ? C_FILL : feedback ? C_TITLE : C_BODY))); text.setAlpha(255);
        Paint.FontMetrics fm = text.getFontMetrics(); c.drawText(T[7], x, cy - (fm.ascent + fm.descent) / 2, text);
    }
    private long footerSince;   // 页脚第一次画出的时刻（淡入用） | when the footer was first drawn (for the fade-in)
    private int lineAlpha = 255;   // line() 画字的不透明度（页脚用半透明） | text opacity for line() (translucent for the footer)
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
        text.setTypeface(face); text.setTextSize(pt); text.setColor(rgb(color)); text.setAlpha(lineAlpha); text.setShadowLayer(2.5f, 0, 1, (lineAlpha * 0xd9 / 255) << 24);
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
