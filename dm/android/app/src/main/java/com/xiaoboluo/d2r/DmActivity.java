// 安卓外壳：一个全屏横屏的 SurfaceView，原生层（libdm.so，dm/android/jni_android.c）跑宿主与游戏。
// 对应 iOS 的 main.m（启动链）与 front_ios.m（视图、触屏、生命周期）；本类只做系统对接，游戏逻辑与界面都在原生层。
// Android shell: a full-screen landscape SurfaceView; the native layer (libdm.so, dm/android/jni_android.c) runs the host and the game.
// Counterpart of iOS's main.m (startup chain) and front_ios.m (view, touch, lifecycle); this class only does system integration, game logic and UI are all in the native layer.
package com.xiaoboluo.d2r;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.system.ErrnoException;
import android.system.Os;
import android.text.Editable;
import android.text.InputType;
import android.text.SpanWatcher;
import android.text.Spannable;
import android.text.Spanned;
import android.text.TextWatcher;
import android.view.inputmethod.BaseInputConnection;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.EditText;
import android.widget.FrameLayout;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

public class DmActivity extends Activity implements SurfaceHolder.Callback, DisplayManager.DisplayListener, android.hardware.input.InputManager.InputDeviceListener {
    static native int nativeStart(String filesDir, String extDir, String romfsSrc, String lang, String versionLabel);
    static native void nativeSurface(Object surface, int w, int h);
    static native void nativeRefresh(float hz, float maxHz);
    static native int nativeFpsTarget();                  // 帧率档生效目标 30 / 60 / 120（每秒问一次，决定首选显示模式）| effective frame-rate target 30 / 60 / 120 (asked once a second; picks the preferred display mode)
    static native void nativeTouch(int n, int[] ids, float[] xs, float[] ys);
    static native void nativeFocus(int ev);
    static native void nativeMemoryWarning(int level);   // 崩溃围栏：系统内存告警 | crash fence: system memory warning
    static native void nativeShutdown();                  // 宿主优雅退出（Activity 结束时）| graceful host exit (when the Activity finishes)
    static native void nativeTextResult(long id, boolean accepted, String text);
    static native void nativeSaveInit(String downloads, String commit, String build);   // 存档管理：备份另存的 Download 目录（没有所有文件访问权限时为空）、来源身份 | save management: the Download folder for the second backup copy (empty without all files access), source identity
    static native void nativeSavePicked(String path);      // null = 没选 | null = nothing picked
    static native void nativeSavePickedFrom(String path, String origin);   // origin = SAF 文档 ID（外部存储的带路径），宿主按它判联机（审计 20261104 P1-3）| origin = the SAF document id (with a path on external storage), for the host's co-op check
    static native void nativeSaveExported(boolean saved);
    // 实体手柄（pad_android.c）：按键 / 轴事件交给原生，true = 归手柄（不再交给系统，B 发的 BACK 不退出）| physical controllers (pad_android.c): key / axis events go native, true = the controller's (not passed on; BACK from B does not exit)
    static native boolean nativePadKey(int device, int vendor, int product, int source, int keycode, boolean down);
    static native boolean nativePadMotion(int device, int vendor, int product, int source, float lx, float ly, float rx, float ry, float lt, float rt, float hx, float hy);
    static native void nativePadDrop(int device);

    private static WeakReference<DmActivity> current = new WeakReference<>(null);
    private static volatile int bootFailure;
    private AlertDialog inputDialog;

    private SurfaceView view;
    private FrameLayout gameFrame;
    private TouchOverlay touchOverlay;
    private boolean started;
    private boolean importing;
    private boolean libraryLoaded;
    private boolean resumed;
    private DisplayManager displays;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        // 调试包：am start … --ez dmArtDump true 只把触屏控件底图写到 files/dm/artdump/ 就退出（dm/android/tools/art_compare.sh 用）
        // Debug builds: am start … --ez dmArtDump true only writes the touch control art to files/dm/artdump/ and exits (used by dm/android/tools/art_compare.sh)
        if (BuildConfig.DEBUG && getIntent().getBooleanExtra("dmArtDump", false)) {
            try { new TouchOverlay(this, null, null).dumpArt(new File(getFilesDir(), "dm/artdump")); } catch (IOException e) { android.util.Log.e("dm", "art dump: " + e); }
            finish(); return;
        }
        current = new WeakReference<>(this);
        displays = getSystemService(DisplayManager.class);
        if (displays != null) displays.registerDisplayListener(this, null);
        { android.hardware.input.InputManager im = getSystemService(android.hardware.input.InputManager.class); if (im != null) im.registerInputDeviceListener(this, null); }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        gameFrame = new FrameLayout(this);
        gameFrame.addView(view, new FrameLayout.LayoutParams(-1, -1));
        setContentView(gameFrame);
        immersive();
    }

    private void immersive() {
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) { c.hide(WindowInsets.Type.systemBars()); c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); }
    }

    // 资源在玩家放的位置读（缺省 Download/S2X/D2R Mobile Plus/data），要「所有文件访问」权限；没有就引导去系统设置页，回来再启动
    // Resources are read where the player put them (default Download/S2X/D2R Mobile Plus/data), which needs "all files access"; without it, guide the player to the system settings page and start after returning
    private boolean ensureFileAccess() {
        if (Environment.isExternalStorageManager()) { prompting = false; return true; }
        // 提示页与加载页同一套样子（用户 2026-10-29「权限页没改」）：背景、字标、标题 / 正文 / 副文；点屏幕任何地方（静音钮以外）或手柄 A / START 打开系统设置
        // The prompt looks like the loading page (user 2026-10-29 "the permission page was not changed"): background, wordmark, title / body / sub; a tap anywhere (outside the mute button) or controller A / START opens system settings
        LoadPageView v = new LoadPageView(this);
        LoadPageView.prompt("dmLoadPermTitle", LoadPageView.ui("dmLoadPermBody"), "dmLoadPermHint");
        v.setOnTap(this::openFileAccessSettings);
        loadPage = v; prompting = true; setContentView(v);
        return false;
    }
    private boolean prompting;
    private void openFileAccessSettings() {
        try { startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName()))); }
        catch (android.content.ActivityNotFoundException e) {   // 这台机没有按包名的那一页：退回总列表；也没有就记一行，不闪退（七审 Q7-01） | no per-app page on this device: fall back to the list; none either = log, no crash (seventh review Q7-01)
            try { startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)); }
            catch (android.content.ActivityNotFoundException e2) { android.util.Log.w("dm", "权限提示页：系统没有「所有文件访问」设置页：" + e2); }
        }
    }

    // 导入包：玩家在电脑上用 dm/tools/d2rm_android.py 和自己的游戏文件做出 android 文件夹，拷到 Download/S2X/D2R Mobile Plus/ 下；
    // 这里把它拷进内部目录 files/dm/（out、shaders、overlay、derived.tsv、pack_info.txt）。pack_info.txt 与已导入的逐字相同就不再拷。
    // 先拷到 files/dm/import.part/，齐了再逐项换上，pack_info.txt 最后放：中途被杀或空间不够，下次打开从头再导，不会用到半份。
    // 原生库加载之前做：不含游戏内容的发布包，几张表在库加载时就从 derived.tsv 读（dm/host/dm_derived_rt.h）。
    // Import pack: the player makes the android folder on a computer with dm/tools/d2rm_android.py and their own game files and copies it to
    // Download/S2X/D2R Mobile Plus/; this copies it into the internal files/dm/ (out, shaders, overlay, derived.tsv, pack_info.txt). Nothing is copied
    // when pack_info.txt is the same as the imported one. It goes to files/dm/import.part/ first and replaces the items one by one once complete,
    // pack_info.txt last: if the app is killed or runs out of space, the next start imports again and half a copy is never used.
    // Done before the native library loads: in the release APK without game content, a few tables are read from derived.tsv at load time.
    private static final String[] IMPORT_ITEMS = {"out", "shaders", "overlay", "derived.tsv", "pack_info.txt"};

    private static String readText(File f) {
        try { return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8); } catch (IOException e) { return null; }
    }

    private static String packProduct(String info) {
        if (info != null) for (String line : info.split("\n")) if (line.startsWith("PRODUCT=")) return line.substring(8).trim();
        return null;
    }

    // nativeStart 的「换一份内容就能修好」返回码（jni_android.c）：缺程序 / 着色器 / 触屏资源（缺失或不配套）。
    // 缺数据（-2）与缺 build 配置（-5）是玩家资源目录的问题，生成修不好，不在这里重开进程（P2-1）。
    // nativeStart's "a regenerated pack can fix it" codes (jni_android.c): program / shaders / touch resources (missing or mismatched).
    // Missing data (-2) and build configuration (-5) are the player's resource directory and generation cannot fix them, so they do not relaunch here (P2-1).
    static boolean contentMissing(int code) { return code == -3 || code == -4 || code == -9 || code == -10; }

    // true = 可以接着启动；false = 正在导入 / 生成或已显示加载页、失败页
    // true = startup can continue; false = importing / generating, or the loading / failure page is shown
    private boolean ensureImported(File source) {
        if (importing) return false;
        File runtime = new File(getFilesDir(), "dm");
        File ext = getExternalFilesDir(null);
        // 判定全在 StartupDecision（javac 可单测）：不配套的旧导入包不会再触发生成（P0-1），换上一半的内容不进启动（P1-1）
        // The decision lives in StartupDecision (unit-testable with javac): an incompatible old import pack no longer triggers generation (P0-1), and
        // half-installed content does not boot (P1-1)
        StartupDecision.Action action = StartupDecision.decide(source, runtime, ext == null ? null : new File(ext, "dm"), BuildConfig.DM_PRODUCT, generating);
        if (action == StartupDecision.Action.GENERATE) { generate(source.getParentFile(), runtime); return false; }
        if (action == StartupDecision.Action.IMPORT) {
            importing = true;
            // 加载页（LoadPageView，核心在 dm/host/dm_loadpage.c）：单阶段「导入」，进度按字节报，页面自己算速度与剩余时间 | the loading page (LoadPageView, core in dm/host/dm_loadpage.c): the single stage "import", progress in bytes, the page computes speed and ETA itself
            showLoadPage();
            LoadPageView.title(null);   // 回到阶段标题（解压页可能用过覆盖）| back to the stage title (the zip page may have overridden it)
            LoadPageView.begin(new int[] {LoadPageView.IMPORT}, null);
            new Thread(() -> {
                String error = null;
                try { importPack(source, runtime, (done, total) -> LoadPageView.set(0, total > 0 ? (double) done / total : 0, 0, 0, done, total)); }
                catch (IOException e) { error = e.getMessage() == null ? e.toString() : e.getMessage(); }
                String failure = error;
                runOnUiThread(() -> {
                    importing = false;
                    if (failure != null) { LoadPageView.fail(failure); showStartError(getString(R.string.dmAndroidImportFailed, failure)); }
                    else { LoadPageView.finish(); startHost(); }
                });
            }, "dm-import").start();
            return false;
        }
        return true;
    }

    // 触发条件（纯函数 S2xZip.shouldOffer，单测在 S2xZipTest）：游戏文件没放齐且找到了包才走解压；文件齐了完全不看 zip。
    // 返回 true = 可以继续现有启动 / 导入 / 生成判定；false = 正在解压 / 暂停 / 失败页。
    // Trigger (the pure S2xZip.shouldOffer, unit-tested in S2xZipTest): extract only when the game files are incomplete and a pack was found;
    // complete game files never look at the zip. true = the existing boot / import / generate decision may continue; false = extracting / paused / failure page.
    private boolean ensureZipUnpacked(File downloads, File gameBase) {
        java.util.List<String> missing = PackGenerator.missingInputs(gameBase);
        // 游戏文件已齐（或本进程已经解过一次）就不碰 zip，现有判定原样走 | complete game files (or one pass already done) never touch the zip; the existing decision runs unchanged
        File zip = missing.isEmpty() || zipDone ? null : S2xZip.find(downloads);
        if (!S2xZip.shouldOffer(missing, zip)) return true;
        if (zipFailure != null) { showZipFailure(); return false; }
        if (zipPaused) { if (!zipPausedShown) { showZipPaused(); zipPausedShown = true; } return false; }
        if (zipRunning) { if (zipPageShown && (loadPage == null || loadPage.getParent() == null)) showLoadPage(); return false; }
        startZip(zip, gameBase);
        return false;
    }

    // 解压全部在后台线程（"dm-s2x-zip"）：plan 只读中央目录 → 结构 / 空间判定 → 加载页 → extract；主线程只换界面。
    // 状态是静态的：App 被杀后重开自然再次触发并续传；Activity 重建时由 ensureZipUnpacked 接回。
    // Extraction runs entirely on a background thread ("dm-s2x-zip"): plan reads only the central directory -> structure / space checks -> loading
    // page -> extract; the main thread only updates the screen. The state is static: after the app is killed the next start triggers again and
    // resumes; a recreated Activity reattaches through ensureZipUnpacked.
    private void startZip(File zip, File dest) {
        zipCancel.set(false);
        zipRunning = true; zipPageShown = false; zipPaused = false; zipPausedShown = false;
        zipFailTitle = null; zipFailure = null;
        final android.content.Context app = getApplicationContext();
        final String progressFormat = app.getString(R.string.dmAndroidZipProgress);
        new Thread(() -> {
            S2xZip.Plan plan;
            try { plan = S2xZip.plan(zip); }
            catch (IOException e) { finishZipFailure(app.getString(R.string.dmAndroidZipFailTitle), app.getString(R.string.dmAndroidZipFailed, message(e))); return; }
            if (!plan.ok()) {   // 结构不对：列出缺项，或给出整包拒绝的原因（zip slip 等）| structurally wrong: list missing items or the whole-pack rejection (zip slip etc.)
                String body = plan.reject != null ? app.getString(R.string.dmAndroidZipReject, plan.reject)
                        : app.getString(R.string.dmAndroidZipNeedFiles, String.join(", ", plan.missing));
                finishZipFailure(app.getString(R.string.dmAndroidZipFailTitle), body); return;
            }
            // 空间：还要写的字节（续传时扣掉已完成的）+ 200 MB 余量，跟目标目录所在存储的可用空间比 | space: bytes still to write (finished files deducted on
            // resume) + 200 MB headroom against the destination's usable space
            long need = S2xZip.spaceNeeded(plan, dest);
            if (need > usableSpace(dest)) {
                finishZipFailure(app.getString(R.string.dmAndroidZipFailTitle), app.getString(R.string.dmAndroidZipNoSpace, gb(need))); return;
            }
            zipPageShown = true;
            LoadPageView.begin(new int[]{LoadPageView.IMPORT}, null);   // 与生成同一套进度条 / 版式，标题换成解压 | same bar / layout as generation, title switched to extraction
            LoadPageView.title(app.getString(R.string.dmAndroidZipTitle));
            postUi(a -> a.showLoadPage());
            S2xZip.Result result;
            try {
                result = S2xZip.extract(plan, dest, (done, total, current) -> {
                    LoadPageView.set(0, total > 0 ? (double) done / total : 0, 0, 0, done, total);
                    LoadPageView.note(String.format(Locale.ROOT, progressFormat, current, gb(done), gb(total)));   // 正文：当前文件名 + 已解 / 总 GB | body: current file + done / total GB
                }, zipCancel);
            } catch (IOException e) {
                finishZipFailure(app.getString(R.string.dmAndroidZipFailTitle), app.getString(R.string.dmAndroidZipFailed, message(e))); return;
            }
            if (result == S2xZip.Result.OK) {
                zipRunning = false; zipPageShown = false; zipDone = true;   // zip 保留；成功照常进现有流程 | the zip is kept; success follows the existing flow
                postUi(a -> { LoadPageView.finish(); if (a.resumed) a.startHost(); });
            } else if (result == S2xZip.Result.CANCELLED) {
                zipRunning = false; zipPageShown = false; zipPaused = true;   // 已完成文件保留，核心下次续传 | finished files stay; the core resumes next time
                postUi(a -> { a.zipPausedShown = true; a.showZipPaused(); });
            } else {
                finishZipFailure(app.getString(R.string.dmAndroidZipFailTitle), app.getString(R.string.dmAndroidZipFailed, app.getString(R.string.dmAndroidZipCheckFailed)));
            }
        }, "dm-s2x-zip").start();
    }

    // 后台线程的结果只改静态状态 + 投递界面工作；没有 Activity（被杀 / 重建中）时状态照样留着，下次 onResume 接着处理。
    // Worker results only change static state and post UI work; without an Activity (killed / recreating) the state stays for the next onResume.
    private static void finishZipFailure(String title, String body) {
        zipRunning = false; zipPageShown = false; zipPaused = false;
        zipFailTitle = title; zipFailure = body;
        postUi(DmActivity::showZipFailure);
    }
    private static void postUi(java.util.function.Consumer<DmActivity> work) {
        DmActivity a = current.get();
        if (a == null) return;
        a.runOnUiThread(() -> { if (!a.isDestroyed()) work.accept(a); });
    }
    private static String message(IOException e) { return e.getMessage() == null ? e.toString() : e.getMessage(); }
    private static String gb(long bytes) { return String.format(Locale.ROOT, "%.1f", bytes / 1e9); }
    // 目标目录可能还不存在：往上找到第一个存在的目录再问可用空间 | the destination may not exist yet: walk up to the first existing directory for the usable space
    private static long usableSpace(File f) {
        File d = f;
        while (d != null && !d.exists()) d = d.getParentFile();
        return d == null ? 0 : d.getUsableSpace();
    }

    // 解压暂停页：已解的部分留着，触屏点「继续解压」或手柄 A 回到第 2 步（核心续传）| extraction paused page: what was extracted stays; a tap on
    // "Resume" or controller A re-enters step 2 (the core resumes)
    private void showZipPaused() {
        android.widget.LinearLayout box = new android.widget.LinearLayout(this);
        box.setOrientation(android.widget.LinearLayout.VERTICAL); box.setPadding(64, 48, 64, 48);
        box.setGravity(android.view.Gravity.CENTER_VERTICAL);
        TextView t = new TextView(this); t.setTextSize(24); t.setText(R.string.dmAndroidZipPaused); box.addView(t);
        TextView b = new TextView(this); b.setTextSize(18); b.setText(R.string.dmAndroidZipPausedBody); b.setPadding(0, 24, 0, 0); box.addView(b);
        android.widget.Button btn = new android.widget.Button(this); btn.setText(R.string.dmAndroidZipResume); btn.setAllCaps(false); btn.setOnClickListener(v -> resumeZip());
        android.widget.LinearLayout.LayoutParams lp = new android.widget.LinearLayout.LayoutParams(android.view.ViewGroup.LayoutParams.WRAP_CONTENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 32; box.addView(btn, lp); btn.requestFocus();
        setContentView(box);
    }
    private void resumeZip() {
        if (!zipPaused) return;
        zipPaused = false; zipPausedShown = false;
        startHost();   // 重新走触发条件 → 重新 plan → extract 续传 | re-enter the trigger -> plan again -> extract resumes
    }
    private void showZipFailure() {
        String t = zipFailTitle != null ? zipFailTitle : getString(R.string.dmAndroidZipFailTitle);
        showGenScreen(t, zipFailure, null);
    }

    private interface Progress { void bytes(long done, long total); }

    // ---- 手机端生成导入包（PackGenerator）：自动开始，进度与失败原因显示在这一页；生成线程不持有 Activity，界面每半秒从静态状态取一次 ----
    // ---- On-phone import pack generation (PackGenerator): starts by itself, progress and failure reasons on this screen; the generation thread holds no Activity and the screen polls the static state every half second ----
    private static volatile boolean generating;
    private static final PackGenerator.State genState = new PackGenerator.State();
    private static volatile String genFailure;   // 生成失败时给玩家看的原因（已按语言取好） | the failure reason shown to the player (already localised)
    private static volatile String genFailTitle;   // 失败页标题：按失败类别（游戏文件不完整 / 版本不对 / 空间不够 / 生成出错），缺陷台账 B026 | failure screen title by kind (defect ledger B026)
    private static volatile String genLogText;     // 已脱敏的 gen_log 全文，「分享诊断日志」打包用 | the scrubbed gen_log text, packed by "Share Diagnostics"
    private static volatile boolean genShareOpen;  // 分享面板开着：回来时的 onResume 不清失败、不重新生成 | the share sheet is open: the onResume on return keeps the failure and does not regenerate
    private static volatile long genStartMs = Long.MAX_VALUE;   // 这次生成开始的时刻：分享只附这之后写的步骤日志 | when this generation started: sharing attaches only step logs written since
    private final android.os.Handler genHandler = new android.os.Handler(android.os.Looper.getMainLooper());

    // ---- S2X.zip 首启自动解压（REQ28，B023）：游戏文件没放齐、Download 根目录里有 S2X.zip 时，先自己解开，再照常走原有启动 / 导入 / 生成 ----
    // ---- First-launch automatic extraction of S2X.zip (REQ28, B023): when the game files are incomplete and Download holds S2X.zip, unpack it
    // first, then follow the existing boot / import / generate path unchanged ----
    private static volatile boolean zipRunning;     // 解压线程在跑 | an extraction thread is running
    private static volatile boolean zipPaused;      // 等玩家按「继续解压」 | waiting for the player to resume
    private static volatile boolean zipPageShown;   // 加载页已到（Activity 重建时接回，不重新 begin） | the loading page is up (reattach on recreation, do not begin again)
    private static volatile boolean zipDone;        // 本进程已完整解过一次：不再重复触发；zip 保留不删 | one full pass this process: never trigger again; the zip is kept
    private static final AtomicBoolean zipCancel = new AtomicBoolean(false);   // 解压中 B / 返回键置位，核心自带续传 | set by B / Back while extracting; the core resumes
    private static volatile String zipFailTitle, zipFailure;   // 失败页（静态：后台线程与重建后的界面都能显示） | failure page (static: the worker and a recreated screen can both show it)
    private boolean zipPausedShown;                 // 本实例已显示暂停页 | this instance already shows the paused page

    // 熔断（P2-4）：同一版本最多自动生成 2 次。生成返回成功、但 contentReady 仍判不齐时，以前会立刻再生成，没有上限；
    // 第 3 次不再空跑，落到失败页并给「分享诊断日志」入口。
    // Circuit breaker (P2-4): at most 2 automatic builds per version. When a build returns success but contentReady still rejects the result, the old
    // code started another build at once with no limit; the third start no longer runs and lands on the failure page with the "Share Diagnostics" entry.
    static final int GEN_TRIES_MAX = 2;
    private boolean generationLimitReached() {
        android.content.SharedPreferences sp = getSharedPreferences("dm_regen", MODE_PRIVATE);
        return sp.getLong("gen_tries_for", 0) == BuildConfig.VERSION_CODE && sp.getInt("gen_tries", 0) >= GEN_TRIES_MAX;
    }
    private void countGenerationStart() {
        android.content.SharedPreferences sp = getSharedPreferences("dm_regen", MODE_PRIVATE);
        int n = sp.getLong("gen_tries_for", 0) == BuildConfig.VERSION_CODE ? sp.getInt("gen_tries", 0) : 0;
        sp.edit().putLong("gen_tries_for", BuildConfig.VERSION_CODE).putInt("gen_tries", n + 1).commit();
    }

    private boolean generatorAvailable() {
        if (!new File(getApplicationInfo().nativeLibraryDir, "libdmgen_py.so").canExecute()) return false;
        try (InputStream in = getAssets().open("dmgen/runtime.txt")) { return true; } catch (IOException e) { return false; }
    }

    private void showGenScreen(String title, String body, String hint) { showGenScreen(title, body, hint, false); }
    private void showGenScreen(String title, String body, String hint, boolean share) {
        android.widget.LinearLayout box = new android.widget.LinearLayout(this);
        box.setOrientation(android.widget.LinearLayout.VERTICAL); box.setPadding(64, 48, 64, 48);
        box.setGravity(android.view.Gravity.CENTER_VERTICAL);
        // 三层：标题 / 正文 / 副文，字号 24 / 18 / 14（相邻比 1.29–1.33，同 CLAUDE 文案原则的层级） | three levels: title / body / sub at 24 / 18 / 14 (neighbouring ratio 1.29-1.33)
        TextView t = new TextView(this); t.setTextSize(24); t.setText(title); box.addView(t);
        TextView b = new TextView(this); b.setTextSize(18); b.setText(body); b.setPadding(0, 24, 0, 0); box.addView(b);
        if (share) {   // 失败页：「分享诊断日志」（触屏点按钮；手柄 A 见 dispatchKeyEvent）| failure screen: "Share Diagnostics" (tap the button; controller A, see dispatchKeyEvent)
            android.widget.Button btn = new android.widget.Button(this); btn.setText(R.string.dmAndroidGenShare); btn.setAllCaps(false); btn.setOnClickListener(v -> shareGenLog());
            android.widget.LinearLayout.LayoutParams lp = new android.widget.LinearLayout.LayoutParams(android.view.ViewGroup.LayoutParams.WRAP_CONTENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.topMargin = 32; box.addView(btn, lp); btn.requestFocus();
            hint = getString(R.string.dmAndroidGenShareHint);
        }
        if (hint != null) { TextView h = new TextView(this); h.setTextSize(14); h.setText(hint); h.setPadding(0, 24, 0, 0); box.addView(h); }
        setContentView(box);
    }
    private void showGenFailure() { String t = genFailTitle != null ? genFailTitle : getString(R.string.dmAndroidGenFailed); showGenScreen(t, genFailure, null, genLogText != null); }

    // 分享生成失败的诊断：zip 里是 gen_log.txt（结论在第一屏）+ logs/ 下各步骤的完整日志，都按 gen_log 的规则脱敏，经 DiagProvider 交给系统分享面板（缺陷台账 B026）
    // share the generation failure diagnostics: a zip with gen_log.txt (the verdict on the first screen) + every step's full log under logs/, all scrubbed by the gen_log
    // rules, handed to the system share sheet through DiagProvider (defect ledger B026)
    private void shareGenLog() {
        final String text = genLogText; if (text == null) return;
        final File gen = new File(getFilesDir(), "dmgen"), zip = new File(new File(getFilesDir(), "share"), "D2R-Mobile-Plus-gen-" + new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new java.util.Date()) + ".zip");
        final String nativeDir = getApplicationInfo().nativeLibraryDir, appHome = getFilesDir().getParent(), apkDir = new File(nativeDir).getParentFile().getParent(), appId = getPackageName();
        new Thread(() -> {
            try {
                File[] old = zip.getParentFile().listFiles((d, n) -> n.startsWith("D2R-Mobile-Plus-gen-")); if (old != null) for (File f : old) f.delete();
                java.util.Map<String, byte[]> e = new java.util.LinkedHashMap<>(); e.put("gen_log.txt", text.getBytes(StandardCharsets.UTF_8));
                for (File f : PackGenerator.stepLogs(gen, genStartMs)) {
                    String name = "logs/" + f.getParentFile().getName() + "_" + f.getName();
                    e.put(name, PackGenerator.scrubLog(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8), apkDir, appHome, appId).getBytes(StandardCharsets.UTF_8));
                }
                PackGenerator.writeZip(zip, e);
                runOnUiThread(() -> {
                    Intent send = new Intent(Intent.ACTION_SEND); send.setType("application/zip"); send.putExtra(Intent.EXTRA_STREAM, DiagProvider.uriFor(this, zip));
                    send.putExtra(Intent.EXTRA_SUBJECT, zip.getName()); send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    genShareOpen = true;
                    try { startActivity(Intent.createChooser(send, getString(R.string.dmAndroidGenShare))); } catch (Exception x) { genShareOpen = false; android.util.Log.w("dm", "share gen: " + x); }
                });
            } catch (IOException x) { android.util.Log.w("dm", "share gen: " + x); }
        }, "dm-gen-share").start();
    }

    // 环境段（脱敏：不含设备名、序列号、账号）：App 版本、机型、安卓版本、SoC / GPU 驱动、ABI、内存、两处剩余空间、产物代号 | the environment section (scrubbed: no
    // device name, serial or account): App version, model, Android version, SoC / GPU driver, ABI, memory, free space in both places, product code
    static String envReport(Context c, File internal, File shared, String product) {
        StringBuilder b = new StringBuilder();
        b.append("App：").append(BuildConfig.DM_VERSION_LABEL).append("（版本 ").append(BuildConfig.VERSION_NAME).append("，构建号 ").append(BuildConfig.VERSION_CODE).append("）\n");
        b.append("机型：").append(android.os.Build.MANUFACTURER).append(' ').append(android.os.Build.MODEL).append("\n");
        b.append("安卓：").append(android.os.Build.VERSION.RELEASE).append("（API ").append(android.os.Build.VERSION.SDK_INT).append("）\n");
        String soc = android.os.Build.VERSION.SDK_INT >= 31 ? android.os.Build.SOC_MANUFACTURER + " " + android.os.Build.SOC_MODEL : android.os.Build.HARDWARE;
        b.append("SoC：").append(soc).append("；平台 ").append(prop("ro.board.platform")).append("；GPU 驱动 vulkan=").append(prop("ro.hardware.vulkan")).append(" egl=").append(prop("ro.hardware.egl")).append("\n");
        b.append("ABI：").append(String.join(", ", android.os.Build.SUPPORTED_ABIS)).append("\n");
        try { ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE); ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo(); am.getMemoryInfo(mi);
              b.append(String.format(Locale.ROOT, "内存：共 %.1f GB，可用 %.1f GB\n", mi.totalMem / 1073741824.0, mi.availMem / 1073741824.0)); } catch (RuntimeException ignored) {}
        b.append(String.format(Locale.ROOT, "剩余空间：内部存储 %.1f GB，共享存储 %.1f GB\n", freeGb(internal), freeGb(shared)));
        b.append("产物代号：").append(product != null ? product : BuildConfig.DM_PRODUCT).append("\n");
        return b.toString();
    }
    private static double freeGb(File f) { try { return new android.os.StatFs(f.getPath()).getAvailableBytes() / 1073741824.0; } catch (RuntimeException e) { return -1; } }
    private static String prop(String key) {   // getprop 读系统属性（App 可读的公开属性；读不到写 ?） | read a system property with getprop (public properties an App may read; ? when unreadable)
        try { Process p = new ProcessBuilder("getprop", key).redirectErrorStream(true).start();
              try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8))) { String v = r.readLine(); p.waitFor(); return v == null || v.trim().isEmpty() ? "?" : v.trim(); } }
        catch (IOException | InterruptedException e) { return "?"; }
    }


    // 生成进度画在加载页上（LoadPageView，核心 dm/host/dm_loadpage.c）：七个阶段与 PackGenerator.Step 一一对应（收尾那步记作「安装」），着色器那步按已生成的
    // .xgs 个数报几成与「第 N / M 个」，其余步骤只报阶段（剩余时间由核心按权重外推）。失败时加载页记失败，但仍换成文字页把完整原因（含 gen_log 位置）给玩家看。
    // Generation progress is drawn on the loading page (LoadPageView, core dm/host/dm_loadpage.c): seven stages one-to-one with PackGenerator.Step (the closing step counts
    // as "install"); the shader step reports its fraction and "N / M" from the .xgs files made so far, the other steps only their stage (the core extrapolates the time
    // left from the weights). On failure the loading page records it, but the screen switches to the text page so the player gets the full reason (with the gen_log location).
    static final int[] GEN_STAGES = {LoadPageView.PREPARE, LoadPageView.PATCH, LoadPageView.ASSETS, LoadPageView.OVERLAY, LoadPageView.PACKS, LoadPageView.SHADERS, LoadPageView.INSTALL};
    private final Runnable genPoll = new Runnable() {
        @Override public void run() {
            if (isDestroyed()) return;   // 已销毁的实例不再处理完成 / 失败，也不再投递（外审 EXT6-05） | a destroyed instance no longer handles finish / failure nor reposts (external review EXT6-05)
            if (genFailure != null) { LoadPageView.fail(genFailTitle != null ? genFailTitle : genFailure); showGenFailure(); return; }
            if (!generating) { LoadPageView.finish(); if (resumed) startHost(); return; }
            PackGenerator.State s = genState;
            if (s.step == PackGenerator.Step.SHADERS && s.shaderDir != null) s.shadersMade = PackGenerator.count(new File(s.shaderDir), ".xgs");
            if (s.step == PackGenerator.Step.SHADERS) LoadPageView.set(PackGenerator.loadStage(s.step), Math.min(1.0, (double) s.shadersMade / PackGenerator.SHADERS), s.shadersMade, PackGenerator.SHADERS, 0, 0);
            else LoadPageView.set(PackGenerator.loadStage(s.step), 0, 0, 0, 0, 0);
            genHandler.postDelayed(this, 500);
        }
    };

    // base = PackGenerator.gameBase（玩家放 ExeFS 与 data 的地方），runtime = files/dm | base = PackGenerator.gameBase (where the player puts ExeFS and data), runtime = files/dm
    private void generate(File base, File runtime) {
        genHandler.removeCallbacks(genPoll);
        if (genFailure != null) { showGenFailure(); return; }
        // 不带生成器的包（精简版 / 资源包）：内容不全也修不好。给专门说明，不再说「重新打开会接着生成」，也不进加载页空等（P2-2）
        // A package without the generator (slim build / resource-only pack) cannot be fixed by generating. Show a dedicated explanation, never say
        // "reopen to continue", and do not wait on the loading page (P2-2)
        if (!generatorAvailable()) { genFailTitle = getString(R.string.dmAndroidGenFailed); genFailure = getString(R.string.dmAndroidGenNoGenerator); genLogText = null; showGenFailure(); return; }
        // 熔断：第 3 次不再进加载页空跑（只在真正要开新一轮时判；重建界面接回现有生成不计数）| circuit breaker: the third start does not
        // enter the loading page (checked only for a new round; reattaching a recreated Activity to the running build does not count)
        if (!generating && generationLimitReached()) { failGenerationLimit(base); return; }
        showLoadPage();   // 阶段状态在原生侧（静态），Activity 重建时只换一个新视图接上 | stage state lives natively (static); a recreated Activity just attaches a new view
        if (generating) { genHandler.post(genPoll); return; }   // Activity 重建：生成线程还在跑，只接回界面 | the Activity was recreated: the generation thread is still running; only reattach the screen
        File data = PackGenerator.gameData(base);   // 资源目录与 startHost 同一条规则 | resource directory by the same rule as startHost
        java.util.List<String> missing = PackGenerator.missingInputs(base);
        if (data != new File(base, "data") && new File(data, "build_manifest.psv").isFile()) missing.remove("data/build_manifest.psv");
        if (!missing.isEmpty()) { failEarly(base, data, getString(R.string.dmAndroidGenFailFiles), getString(R.string.dmAndroidGenNeedFiles, String.join(", ", missing)), "缺 " + String.join("、", missing)); return; }
        File gen = new File(getFilesDir(), "dmgen");
        long free = new android.os.StatFs(getFilesDir().getPath()).getAvailableBytes() + treeBytes(new File(gen, "root"));
        // 调试包演练「空间不足」（缺陷台账 B026 先红后绿）：am start … --el dmTestFreeBytes <字节> 当作剩余空间；正式包不认 | debug builds rehearse "out of space"
        // (defect ledger B026 red-then-green): am start ... --el dmTestFreeBytes <bytes> is taken as the free space; release builds ignore it
        if (BuildConfig.DEBUG && getIntent().hasExtra("dmTestFreeBytes")) free = getIntent().getLongExtra("dmTestFreeBytes", free);
        if (free < PackGenerator.SPACE_NEEDED) { String f = android.text.format.Formatter.formatShortFileSize(this, free);
            failEarly(base, data, getString(R.string.dmAndroidGenFailSpace), getString(R.string.dmAndroidGenNoSpace, f), String.format(Locale.ROOT, "剩余空间 %s，生成需要约 %d GB（缓存 + 中间产物 + 成品）", f, PackGenerator.SPACE_NEEDED >> 30)); return; }
        countGenerationStart();   // 真开跑才计次：缺文件 / 空间不够的早停不算一次 | count only a real run: an early stop for missing files / space does not count
        generating = true; genState.reset(); genLogText = null; genFailTitle = null; genStartMs = System.currentTimeMillis() - 2000;
        LoadPageView.title(null);   // 回到阶段标题（解压页可能用过覆盖）| back to the stage title (the zip page may have overridden it)
        LoadPageView.begin(GEN_STAGES, PackGenerator.LOAD_WEIGHTS_MS); genHandler.post(genPoll);
        final File dataDir = data; final String nativeDir = getApplicationInfo().nativeLibraryDir;
        final String appHome = getFilesDir().getParent(), apkDir = new File(nativeDir).getParentFile().getParent(), appId = getPackageName();   // gen_log.txt 脱敏用 | for scrubbing gen_log.txt
        final android.content.res.AssetManager assets = getAssets(); final android.content.Context app = getApplicationContext();
        final int jobs = Math.max(1, Runtime.getRuntime().availableProcessors());
        new Thread(() -> {
            String failure = null, title = null; java.util.Deque<String> tail = PackGenerator.newTail(); File log = new File(gen, "gen.log");
            try {
                unpackIfChanged(assets, "dmgen/runtime.zip", "dmgen/runtime.txt", new File(gen, "rt"));
                unpackIfChanged(assets, "dmgen/src.zip", "dmgen/src.sha256", new File(gen, "src"));
                File queries = new File(gen, "queries"); PackGenerator.deleteTree(queries); queries.mkdirs();
                for (String q : assets.list("dmgen/queries")) try (InputStream in = assets.open("dmgen/queries/" + q)) { Files.copy(in, new File(queries, q).toPath()); }
                // 开源楷体（有就带，见 PackGenerator.environment）：字体、OFL 许可证、font.txt | the bundled open-licence Kai font when present (see PackGenerator.environment): font, OFL licence, font.txt
                File font = new File(gen, "font"); PackGenerator.deleteTree(font); String[] fonts = assets.list("dmgen/font");
                if (fonts != null && fonts.length > 0) { font.mkdirs(); for (String q : fonts) try (InputStream in = assets.open("dmgen/font/" + q)) { Files.copy(in, new File(font, q).toPath()); } }
                // 脚本要的目录形状：<游戏>/ExeFS、<游戏>/RomFS/data，用符号链接指到玩家放的地方（只读，清理时不跟链接） | the layout the script wants: <game>/ExeFS, <game>/RomFS/data, as symlinks to where the player put them (read only; cleaning never follows links)
                File game = new File(gen, "game"); PackGenerator.deleteTree(game); new File(game, "RomFS").mkdirs();
                Os.symlink(new File(base, "ExeFS").getPath(), new File(game, "ExeFS").getPath());
                linkData(dataDir, new File(game, "RomFS/data"));   // 逐个文件链接，不带点的 build.config 链成 .build.config（八审 P1-1） | per-file links; an undotted build.config is linked as .build.config
                File out = new File(gen, "out"); PackGenerator.deleteTree(out);
                int exit = PackGenerator.run(PackGenerator.command(new File(nativeDir), gen, game, out), PackGenerator.environment(new File(nativeDir), gen, jobs), new File(gen, "src"), log, genState, tail);
                PackGenerator.Failure kind = PackGenerator.classify(exit, new java.util.ArrayList<>(tail), genState.failKind);
                if (kind == PackGenerator.Failure.NONE) {
                    PackGenerator.install(new File(out, "android"), runtime);
                    for (String d : new String[]{"root", "out", "game"}) PackGenerator.deleteTree(new File(gen, d));   // 缓存与中间产物约 1 GB，做完就删 | caches and intermediates are about 1 GB; removed when done
                } else {
                    String[] tb = failureText(app, kind, exit, new java.util.ArrayList<>(tail), gen); title = tb[0]; failure = tb[1];
                }
            } catch (IOException | ErrnoException | InterruptedException | RuntimeException e) {
                String m = e.getMessage() == null ? e.toString() : e.getMessage();
                boolean space = m.contains("No space left") || m.contains("ENOSPC");
                title = app.getString(space ? R.string.dmAndroidGenFailSpace : R.string.dmAndroidGenFailed);
                failure = space ? app.getString(R.string.dmAndroidGenNoSpace, android.text.format.Formatter.formatShortFileSize(app, new android.os.StatFs(gen.getPath()).getAvailableBytes()))
                        : app.getString(R.string.dmAndroidGenInterrupted, m);
                tail.addLast("[java] " + e);
            }
            if (failure != null) {   // 日志放到玩家看得到的地方，反馈时附上；结论在第一屏（缺陷台账 B026） | put the log where the player can see it, to attach when reporting; the verdict on the first screen (defect ledger B026)
                String output; try { output = log.isFile() ? new String(Files.readAllBytes(log.toPath()), StandardCharsets.UTF_8) : "（生成脚本没有输出）\n"; } catch (IOException e) { output = "（读不到 gen.log：" + e + "）\n"; }
                for (String l : tail) if (l.startsWith("[java] ")) output += l + "\n";
                String text = PackGenerator.scrubLog(PackGenerator.genLog(title, failure, genState.summary, envReport(app, getFilesDirStatic(app), base, genState.product),
                        PackGenerator.listInputs(new File(base, "ExeFS"), dataDir), output), apkDir, appHome, appId);
                genLogText = text;
                try (OutputStream o = new FileOutputStream(PackGenerator.genLogFile(base))) { o.write(text.getBytes(StandardCharsets.UTF_8)); }   // 脱敏后再写（四审 A4-09）| scrubbed before writing (fourth audit A4-09)
                catch (IOException ignored) {}
            }
            genFailTitle = title;
            genFailure = failure;
            generating = false;
        }, "dm-generate").start();
    }

    // 生成器看到的 <游戏>/RomFS/data：玩家 data 里每个文件链一个同名链接（只读，清理时不跟链接）；只有不带点的 build.config 时链成 .build.config
    // （电脑上常被存成无点名，运行时也认它，见 jni_android.c；生成器只读带点的，八审 P1-1）
    // <game>/RomFS/data as the generator sees it: one link per file in the player's data (read only; cleaning never follows links); when there is only an undotted
    // build.config it is linked as .build.config (often saved without the dot on a computer; the runtime accepts it too, see jni_android.c; the generator reads only the dotted name, eighth review P1-1)
    private static void linkData(File data, File into) throws IOException, ErrnoException {
        if (!into.mkdirs() && !into.isDirectory()) throw new IOException("mkdir " + into);
        String[] names = data.list(); if (names == null) return;
        boolean dotted = new File(data, ".build.config").isFile();
        for (String n : names) {
            String as = !dotted && n.equals("build.config") ? ".build.config" : n;
            Os.symlink(new File(data, n).getPath(), new File(into, as).getPath());
        }
    }

    private static String stepLabelFor(android.content.Context c, PackGenerator.Step step) {
        int id;
        switch (step) {
            case PATCH: id = R.string.dmAndroidGenStepPatch; break;
            case ASSETS: id = R.string.dmAndroidGenStepAssets; break;
            case OVERLAY: id = R.string.dmAndroidGenStepOverlay; break;
            case PACKS: id = R.string.dmAndroidGenStepPacks; break;
            case SHADERS: id = R.string.dmAndroidGenStepShaders; break;
            case FINISH: id = R.string.dmAndroidGenStepFinish; break;
            default: id = R.string.dmAndroidGenStepPrepare;
        }
        return c.getString(id);
    }

    // 熔断页（P2-4）：同一版本已经自动生成 2 次仍未就绪。写一份带结论的 gen_log 并给分享入口，不再第三次空跑。
    // Circuit-breaker page (P2-4): two automatic builds for this version still did not leave ready content. Write a gen_log with a verdict and offer
    // the share entry instead of a third pointless run.
    private void failGenerationLimit(File base) {
        File data = new File(base, "data"), earlier = new File(base.getParentFile().getParentFile(), "data");
        if (!data.isDirectory() && earlier.isDirectory()) data = earlier;
        String title = getString(R.string.dmAndroidGenFailed), reason = getString(R.string.dmAndroidGenLimit, GEN_TRIES_MAX);
        genState.reset(); genState.summary.add(reason); genStartMs = Long.MAX_VALUE;   // 没开始新一轮：不附步骤日志 | no new run: no step logs
        String nativeDir = getApplicationInfo().nativeLibraryDir;
        String text = PackGenerator.scrubLog(PackGenerator.genLog(title, reason, genState.summary, envReport(this, getFilesDir(), base, null),
                PackGenerator.listInputs(new File(base, "ExeFS"), data), "（没有开始生成：同一版本已自动生成 " + GEN_TRIES_MAX + " 次）\n"),
                new File(nativeDir).getParentFile().getParent(), getFilesDir().getParent(), getPackageName());
        try (OutputStream o = new FileOutputStream(new File(base, "gen_log.txt"))) { o.write(text.getBytes(StandardCharsets.UTF_8)); } catch (IOException ignored) {}
        genFailTitle = title; genFailure = reason; genLogText = text; showGenFailure();
    }

    // 开始生成之前就停下（缺 main / sdk / build_manifest、空间不够）：同样写 gen_log（结论 + 环境 + 输入文件）并给「分享诊断日志」| stopped before generating (missing
    // main / sdk / build_manifest, not enough space): write gen_log all the same (verdict + environment + input files) and offer "Share Diagnostics"
    private void failEarly(File base, File data, String title, String reason, String why) {
        genState.reset(); genState.summary.add(why); genStartMs = Long.MAX_VALUE;   // 没跑生成：不附任何步骤日志 | nothing was generated: no step logs attached
        String nativeDir = getApplicationInfo().nativeLibraryDir;
        String text = PackGenerator.scrubLog(PackGenerator.genLog(title, reason, genState.summary, envReport(this, getFilesDir(), base, null),
                PackGenerator.listInputs(new File(base, "ExeFS"), data), "（没有开始生成）\n"), new File(nativeDir).getParentFile().getParent(), getFilesDir().getParent(), getPackageName());
        try (OutputStream o = new FileOutputStream(PackGenerator.genLogFile(base))) { o.write(text.getBytes(StandardCharsets.UTF_8)); } catch (IOException ignored) {}
        genFailTitle = title; genFailure = reason; genLogText = text; showGenFailure();
    }
    private static File getFilesDirStatic(Context c) { return c.getFilesDir(); }

    // 失败类别 → 标题与正文（玩家看得懂的说法 + 下一步，缺陷台账 B026）：文件问题列出生成器核出的那几句 | failure kind -> title and body (in players' words + the
    // next step, defect ledger B026): for file problems, list the sentences the generator's check produced
    private static String[] failureText(Context app, PackGenerator.Failure kind, int exit, java.util.List<String> tail, File gen) {
        StringBuilder files = new StringBuilder();
        synchronized (genState.summary) { for (String s : genState.summary) if (((s.startsWith("游戏文件核对：") && !s.contains("都在")) || s.contains("内容不对")) && !s.contains("不影响")) files.append(s.replace("游戏文件核对：", "")).append('\n'); }
        switch (kind) {
            case NEED_FILES: return new String[]{app.getString(R.string.dmAndroidGenFailFiles), app.getString(R.string.dmAndroidGenNeedFiles, PackGenerator.missingDetail(tail))};
            case BAD_FILES: return new String[]{app.getString(R.string.dmAndroidGenFailFiles), files + app.getString(R.string.dmAndroidGenFailFilesNext)};
            case VERSION: return new String[]{app.getString(R.string.dmAndroidGenFailVersion), files + app.getString(R.string.dmAndroidGenFailVersionNext)};
            case NO_SPACE: return new String[]{app.getString(R.string.dmAndroidGenFailSpace), app.getString(R.string.dmAndroidGenNoSpace, android.text.format.Formatter.formatShortFileSize(app, new android.os.StatFs(gen.getPath()).getAvailableBytes()))};
            case STEP_FAILED: return new String[]{app.getString(R.string.dmAndroidGenFailInternal), app.getString(R.string.dmAndroidGenFailInternalNext, stepLabelFor(app, genState.step))};
            default: return new String[]{app.getString(R.string.dmAndroidGenFailed), app.getString(R.string.dmAndroidGenInterrupted, "exit " + exit)};
        }
    }

    private static long treeBytes(File f) {
        if (java.nio.file.Files.isSymbolicLink(f.toPath())) return 0;
        if (!f.isDirectory()) return f.length();
        long n = 0; File[] list = f.listFiles();
        if (list != null) for (File c : list) n += treeBytes(c);
        return n;
    }

    // 资产里的 zip 与它的版本记录：记录变了才重新解开（先解到 .part 再改名） | a zip in the assets and its version record: unpacked again only when the record changed (into .part, then renamed)
    private static void unpackIfChanged(android.content.res.AssetManager assets, String zip, String stamp, File dest) throws IOException {
        String want;
        try (InputStream in = assets.open(stamp)) {   // InputStream.readAllBytes 要 API 33，minSdk 是 31（B104） | InputStream.readAllBytes needs API 33, minSdk is 31 (B104)
            java.io.ByteArrayOutputStream all = new java.io.ByteArrayOutputStream(); byte[] b = new byte[256];
            for (int n; (n = in.read(b)) > 0; ) all.write(b, 0, n);
            want = all.toString(StandardCharsets.UTF_8.name());
        }
        File mark = new File(dest, ".stamp");
        if (want.equals(readText(mark))) return;
        File part = new File(dest.getPath() + ".part");
        PackGenerator.deleteTree(part); part.mkdirs();
        try (InputStream in = assets.open(zip)) { PackGenerator.unzip(in, part); }
        Files.write(new File(part, ".stamp").toPath(), want.getBytes(StandardCharsets.UTF_8));
        PackGenerator.deleteTree(dest);
        if (!part.renameTo(dest)) throw new IOException("rename " + dest.getName());
    }

    private static void importPack(File source, File runtime, Progress progress) throws IOException {
        for (String item : IMPORT_ITEMS) if (!new File(source, item).exists()) throw new IOException("missing " + item);
        File part = new File(runtime, "import.part");
        deleteTree(part);
        long sum = 0;
        for (String item : IMPORT_ITEMS) sum += treeSize(new File(source, item));
        final long total = sum;
        long[] done = {0, 0};   // 已拷字节、上次回报时的字节 | bytes copied, bytes at the last report
        byte[] buffer = new byte[1 << 20];
        for (String item : IMPORT_ITEMS) copyTree(new File(source, item), new File(part, item), buffer, bytes -> {
            done[0] += bytes;
            if (done[0] - done[1] >= (4L << 20) || done[0] == total) { done[1] = done[0]; progress.bytes(done[0], total); }   // 每 4 MB 报一次（页面按字节算速度与剩余） | report every 4 MB (the page computes speed and ETA from bytes)
        });
        for (String item : IMPORT_ITEMS) {
            File from = new File(part, item), to = new File(runtime, item);
            deleteTree(to);
            if (!from.renameTo(to)) throw new IOException("rename " + item);
        }
        deleteTree(part);
    }

    private interface Copied { void bytes(long n); }

    private static long treeSize(File f) {
        if (!f.isDirectory()) return f.length();
        long n = 0; File[] list = f.listFiles();
        if (list != null) for (File c : list) n += treeSize(c);
        return n;
    }

    private static void copyTree(File from, File to, byte[] buffer, Copied copied) throws IOException {
        if (from.isDirectory()) {
            if (!to.isDirectory() && !to.mkdirs()) throw new IOException("mkdir " + to.getName());
            File[] list = from.listFiles();
            if (list == null) throw new IOException("list " + from.getName());
            for (File c : list) copyTree(c, new File(to, c.getName()), buffer, copied);
            return;
        }
        File parent = to.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("mkdir " + parent.getName());
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(to)) {
            for (int n; (n = in.read(buffer)) > 0; ) { out.write(buffer, 0, n); copied.bytes(n); }
        }
    }

    private static void deleteTree(File f) throws IOException {
        File[] list = f.isDirectory() ? f.listFiles() : null;
        if (list != null) for (File c : list) deleteTree(c);
        if (f.exists() && !f.delete()) throw new IOException("delete " + f.getName());
    }

    // 调试包：am start … --ez dmLoadPageDemo true 用模拟进度走一遍手机端生成的六个阶段（改写程序 → 整理资源 → 界面覆盖层 → 着色器打包 → 着色器缓存 N/6931 → 安装，缺省权重按奥丁实测 371 秒分；演示压缩到约 45 秒）；不起宿主，走完 3 秒后退出。
    // 真实接线（并线后）：PackGenerator.State 的 step / shadersMade 逐步调 LoadPageView.set(阶段, 几成, 已做, 总数)，第二次启动不报这些阶段
    // Debug builds: am start … --ez dmLoadPageDemo true runs the loading page through the six on-phone generation stages (patch → assets → overlay → packs → shaders N/6931 → install, default weights by the 371 s Thor measurement; compressed to about 45 s for the demo); the host is not started, the activity exits 3 s after the end.
    // Real wiring (after the merge): PackGenerator.State's step / shadersMade call LoadPageView.set(stage, fraction, done, total) step by step; a second launch does not report these stages
    private boolean loadPageDemo;
    private boolean loadPageDemoRun() {
        if (!BuildConfig.DEBUG || !getIntent().getBooleanExtra("dmLoadPageDemo", false)) return false;
        if (loadPageDemo) return true; loadPageDemo = true;
        showLoadPage();
        LoadPageView.title(null);
        LoadPageView.begin(new int[] {LoadPageView.PATCH, LoadPageView.ASSETS, LoadPageView.OVERLAY, LoadPageView.PACKS, LoadPageView.SHADERS, LoadPageView.INSTALL}, null);
        new Thread(() -> {
            try {
                for (int st = 0; st < 4; st++) for (int i = 0; i <= 20; i++) { LoadPageView.set(st, i / 20.0, 0, 0, 0, 0); Thread.sleep(150); }   // 前四步各 3 秒 | the first four steps 3 s each
                int sec = Math.max(5, getIntent().getIntExtra("dmLoadPageDemoSec", 30)), step = Math.max(1, 6931 / (sec * 10));   // --ei dmLoadPageDemoSec N：着色器那段走 N 秒（验页脚用） | shader stage lasts N s (for checking the footer)
                for (int i = 0; i <= 6931; i += step) { LoadPageView.set(4, i / 6931.0, i, 6931, 0, 0); Thread.sleep(100); }                  // 着色器 6931 条，缺省约 30 秒 | 6931 shaders, about 30 s by default
                for (int i = 0; i <= 10; i++) { LoadPageView.set(5, i / 10.0, 0, 0, 0, 0); Thread.sleep(200); }
                LoadPageView.finish(); Thread.sleep(3000);
            } catch (InterruptedException e) { /* 退出 | exiting */ }
            runOnUiThread(this::finish);
        }, "dm-loadpage-demo").start();
        return true;
    }

    // 加载页（导入 / 首启生成 / 调试演示）：换上页面，同时（每个进程一次）在后台按「自己服务器 → GitHub raw」取一次清单给页脚的官方免费渠道（CommunityFetch）。
    // 普通启动不走这里，不联网。调试包可用 am start … --es dmUpdateUrls "<地址…>" 换地址（验「只配 raw」「全失败」等）
    // Loading page (import / first-launch generation / debug demo): put the page up and, once per process, fetch the manifest in the background from "own server → GitHub raw"
    // for the footer's official free channels (CommunityFetch). A normal start never comes here and does not touch the network. Debug builds can swap the addresses with
    // am start … --es dmUpdateUrls "<addresses…>" (to check "raw only", "all fail" and so on)
    private LoadPageView loadPage;
    private static boolean communityStarted;
    static volatile String communityLast = "";
    private void showLoadPage() { prompting = false; loadPage = new LoadPageView(this); setContentView(loadPage); startCommunityFetch(); }
    // 首启要生成 / 导入时尽早开始取（权限提示页那一刻就开始，玩家去系统设置的几秒里清单通常已经回来，加载页一出来页脚就在）；
    // 判据同 ensureImported：内部目录里还没有本版本的导入内容。普通启动不满足，不联网
    // Start fetching as early as possible when the first launch will generate / import (already at the permission prompt: the manifest is usually back during the seconds
    // the player spends in system settings, so the footer is there as soon as the loading page appears); same test as ensureImported: the internal directory has no content
    // for this version yet. A normal start does not qualify and makes no network access
    private void prefetchCommunity() {
        if (communityStarted || libraryLoaded || started) return;
        String have = readText(new File(getFilesDir(), "dm/pack_info.txt"));
        if (!BuildConfig.DM_PRODUCT.equals(packProduct(have))) startCommunityFetch();
    }
    private void startCommunityFetch() {
        if (communityStarted) return; communityStarted = true;
        String cfg = BuildConfig.DEBUG && getIntent().getStringExtra("dmUpdateUrls") != null ? getIntent().getStringExtra("dmUpdateUrls") : BuildConfig.DM_UPDATE_URLS;
        final java.util.List<String> urls = CommunityFetch.urls(cfg); final File cache = new File(getFilesDir(), "community_manifest.json");
        final String ua = "D2RMobilePlus-Android/" + BuildConfig.VERSION_NAME;
        new Thread(() -> {
            try { new CommunityFetch(urls, cache, CommunityFetch.urlConnection(ua), LoadPageView::manifest, line -> { android.util.Log.i("dm", line); communityLast = line; }, Thread::sleep)
                    .run((qq, gh, site, from) -> LoadPageView.community(qq, gh, site)); }
            catch (InterruptedException | RuntimeException e) { android.util.Log.w("dm", "社区信息：拉取线程出错，不显示：" + e); }
        }, "dm-community").start();
    }

    private void startHost() {
        if (bootFailure != 0) { showBootFailure(); return; }
        if (loadPageDemoRun()) return;
        if (!started && !importing && resumed) prefetchCommunity();
        if (started || importing || !resumed || !ensureFileAccess()) return;
        File downloads = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        File gameBase = PackGenerator.gameBase(downloads);
        // 有权限、准备判定之前：游戏文件没放齐而 Download 里有 S2X.zip，就先在加载页自己解开（REQ28，B023）；文件齐了它直接放行
        // With permission granted and before the preparation decision: when the game files are incomplete and Download holds S2X.zip, unpack it
        // on the loading page first (REQ28, B023); complete files pass straight through
        if (!libraryLoaded && !ensureZipUnpacked(downloads, gameBase)) return;
        if (!libraryLoaded && !ensureImported(new File(gameBase, "android"))) return;
        if (gameFrame.getParent() == null) setContentView(gameFrame);
        if (!libraryLoaded) {
            // 与 jni_android.c 选运行目录同一条规则：内部目录有主程序就用内部目录，否则用外部私有目录
            // Same rule as jni_android.c's choice of runtime directory: internal storage when it has the main module, otherwise external private storage
            File ext = getExternalFilesDir(null), internal = new File(getFilesDir(), "dm");
            File runtime = new File(internal, "out/main.patched.bin").canRead() || ext == null ? internal : new File(ext, "dm");
            try { Os.setenv("DM_DERIVED", new File(runtime, "derived.tsv").getAbsolutePath(), true); } catch (ErrnoException e) { showStartError(getString(R.string.dmAndroidStorage)); return; }
            try { System.loadLibrary("dm"); libraryLoaded = true; }
            catch (UnsatisfiedLinkError error) { showStartError(getString(R.string.dmAndroidLibrary)); return; }
        }
        if (touchOverlay == null) {
            int[] gameIds = {R.string.dmAndroidAttack, R.string.dmAndroidSkill1, R.string.dmAndroidSkill2, R.string.dmAndroidSkill3, R.string.dmAndroidSkill4,
                R.string.dmAndroidPortal, R.string.dmAndroidPotion1, R.string.dmAndroidPotion2, R.string.dmAndroidPotion3, R.string.dmAndroidPotion4,
                R.string.dmAndroidMenu, R.string.dmAndroidMap, R.string.dmAndroidMove, R.string.dmAndroidSwap, R.string.dmAndroidDifficulty};
            int[] navIds = {R.string.dmConfirm, R.string.dmAndroidBack, R.string.dmAndroidAction, R.string.dmAndroidBind, R.string.dmAndroidNext};
            String[] gameLabels = new String[gameIds.length], navLabels = new String[navIds.length];
            for (int i = 0; i < gameIds.length; i++) gameLabels[i] = getString(gameIds[i]);
            for (int i = 0; i < navIds.length; i++) navLabels[i] = getString(navIds[i]);
            touchOverlay = new TouchOverlay(this, gameLabels, navLabels);
            touchOverlay.setOnApplyWindowInsetsListener((v, insets) -> {
                android.graphics.Insets safe = insets.getInsets(WindowInsets.Type.displayCutout());
                touchOverlay.setSafeInsets(safe.left, safe.right, safe.bottom);
                return insets;
            });
            gameFrame.addView(touchOverlay, new FrameLayout.LayoutParams(-1, -1));
            touchOverlay.requestApplyInsets();
        }
        if (!view.getHolder().getSurface().isValid() || view.getWidth() <= 0 || view.getHeight() <= 0) return;
        File ext = getExternalFilesDir(null);
        if (ext == null) { showStartError(getString(R.string.dmAndroidStorage)); return; }
        nativeSurface(view.getHolder().getSurface(), view.getWidth(), view.getHeight());
        updateRefresh();
        // 资源目录缺省 Download/S2X/D2R Mobile Plus/data（旧目录见 PackGenerator.gameBase）；没有这个目录、而早先的 Download/data 在时沿用早先的位置，已装好的手机不必搬数据。
        // 按目录判断而不按 .build.config：它也可以由导入的覆盖层提供，资源目录里就没有。
        // The resource directory defaults to Download/S2X/D2R Mobile Plus/data (old directory: PackGenerator.gameBase); when that directory does not exist but the earlier
        // Download/data does, the earlier one is kept, so existing installs need not move their data. The test is the directory,
        // not .build.config: the imported overlay can provide that file, and then the resource directory has none.
        String src = PackGenerator.gameData(gameBase).getAbsolutePath();
        { File ext2 = getExternalFilesDir(null), internal2 = new File(getFilesDir(), "dm");   // 与上面 / jni_android.c 同一条运行目录规则 | same runtime-directory rule as above / jni_android.c
          writeExitReasons(new File(internal2, "out/main.patched.bin").canRead() || ext2 == null ? internal2 : new File(ext2, "dm")); }   // 分享诊断日志附系统退出原因 | exit reasons for share diagnostics
        // 本进程里加载页走过（导入 / 手机端生成）就把它的完成 / 失败行交给宿主写进 boot.log（分享诊断日志的「加载页：」摘要） | if the loading page ran in this process
        // (import / on-phone generation), hand its finish / failure line to the host for boot.log (the 「加载页：」 share-diagnostics summary)
        try { String lp = LoadPageView.lastLine(); if (lp != null && !lp.isEmpty()) Os.setenv("DM_LOADPAGE_LAST", lp, true); } catch (ErrnoException | UnsatisfiedLinkError ignored) {}
        try { if (!communityLast.isEmpty()) Os.setenv("DM_COMMUNITY_LAST", communityLast, true); } catch (ErrnoException ignored) {}   // 社区信息拉取的最后一行，同上进 boot.log | the community fetch's last line, into boot.log likewise
        saveDeleteTree(new File(new File(getFilesDir(), "temp"), "save_import_tree"));   // 同下 | as below
        saveDeleteTree(new File(new File(getFilesDir(), "temp"), "save_pick"));   // 同下 | as below
        new File(new File(getFilesDir(), "temp"), "save_import_pick.zip").delete();   // 上一局选的包（内容已校验进待应用目录）| the package picked last run (already staged)
        nativeSaveInit(Environment.isExternalStorageManager() ? downloads.getAbsolutePath() : "", BuildConfig.DM_SOURCE_COMMIT, String.valueOf(BuildConfig.VERSION_CODE));
        TelemetryBridge.init(this);   // 设备与使用统计（dm_telemetry.c 在宿主第一帧判定，开关缺省关） | device and usage statistics (decided on the host's first frame, off by default)
        CrashReportBridge.init(this);   // 自动上报崩溃：地址合不合格（dm_crashreport.c 在宿主第一帧判定，开关缺省关） | automatic crash reports: whether the origin is valid (decided at the host's first frame; the switch is off by default)
        int result = nativeStart(getFilesDir().getAbsolutePath(), ext.getAbsolutePath(), src, Locale.getDefault().toLanguageTag(), BuildConfig.DM_VERSION_LABEL);
        if (result < 0) {
            // 触屏资源缺失 / 与这版不配套（App 升级后覆盖层的输入变了）：带生成器的包把已生成内容标成过期、重启进程，重启后自动在加载页重新生成。
            // 每个版本只自动重试一次，再不行就看下面的「没有可用内容」分支
            // Touch overlay missing / not matching this version (the overlay inputs changed with an app update): a package with the generator marks the generated content stale and restarts
            // the process, which then regenerates it on the loading page. At most one automatic retry per version; after that the "no usable content" branch below applies
            if ((result == -9 || result == -10) && regenerateAfterUpdate()) return;
            // 缺游戏数据（-2）/ 缺 build 配置（-5）：玩家资源目录的问题，生成修不好，也不白重开一次进程；直接告诉玩家文件该放哪（P2-1）
            // Missing game data (-2) / build configuration (-5): the player's resource directory, which generation cannot fix; do not relaunch for
            // nothing, just tell the player where the file goes (P2-1)
            if (result == -2 || result == -5) { showStartError(getString(result == -2 ? R.string.dmAndroidMissingData : R.string.dmAndroidMissingConfig)); return; }
            // 没有可用游戏内容（缺程序 / 着色器 / 触屏资源）：不再停在旧版「电脑端导入」提示上，换一个干净进程重来；
            // 下次启动在加载原生库之前由 ensureImported 看运行目录的实际内容：不全就进加载页由手机端生成，齐全就再试一次启动。
            // 每个版本只这样重开一次（整包重做很慢，也防止「生成成功但原生仍说缺」时来回重开）；之后落到通用启动失败页。
            // No usable game content (missing program / shaders / touch resources): never the legacy computer-import prompt;
            // relaunch a clean process. Before the native library loads, ensureImported re-checks the runtime directory: incomplete content goes to the
            // loading page and is generated on the phone, complete content gets one more boot attempt. At most one such relaunch per version (a full
            // rebuild is slow, and it also stops a "generation succeeded but the native layer still says missing" loop); after that the generic
            // start-failure page shows.
            if (contentMissing(result) && !generating && relaunchForMissingContent()) return;
            showStartError(getString(R.string.dmAndroidStartFailed, result)); return;
        }
        started = true;
        touchOverlay.setInputEnabled(true);
        nativeFocus(3);
        modeHandler.removeCallbacks(modePoll); modeHandler.post(modePoll);
    }

    private boolean regenerateAfterUpdate() {
        File info = new File(getFilesDir(), "dm/pack_info.txt"); String have = readText(info);
        android.content.SharedPreferences sp = getSharedPreferences("dm_regen", MODE_PRIVATE); long tried = sp.getLong("overlay_regen_for", 0);
        if (have == null || !generatorAvailable() || tried == BuildConfig.VERSION_CODE) return false;
        try { Files.write(info.toPath(), PackGenerator.markStale(have).getBytes(StandardCharsets.UTF_8)); }
        catch (IOException e) { android.util.Log.w("dm", "触屏资源与这版不配套，标记过期失败：" + e); return false; }
        sp.edit().putLong("overlay_regen_for", BuildConfig.VERSION_CODE).commit();
        android.util.Log.i("dm", "触屏资源与这版 App 不配套（App 升级过）：已标记过期，重启后自动重新生成");
        relaunchClean();   // 原生库已加载，生成要在加载之前做：换一个干净进程 | the native library is loaded and generation must run before that: start a clean process
        return true;
    }

    // 原生启动时说「没有可用游戏内容」：本进程的库已加载（derived.tsv 在库加载时读进表），不能再在这里生成；
    // 每个版本只重开一次，下次启动前 ensureImported 按运行目录的实际内容决定进加载页还是直接启动。
    // The native boot says "no usable game content": this process has already loaded the library (derived.tsv is read into tables at load time), so a pack
    // cannot be generated here. At most one relaunch per version; before the next launch ensureImported decides from the actual runtime directory whether
    // to enter the loading page or boot directly.
    private boolean relaunchForMissingContent() {
        android.content.SharedPreferences sp = getSharedPreferences("dm_regen", MODE_PRIVATE);
        if (sp.getLong("content_regen_for", 0) == BuildConfig.VERSION_CODE) return false;
        sp.edit().putLong("content_regen_for", BuildConfig.VERSION_CODE).commit();
        android.util.Log.i("dm", "启动时没有可用游戏内容：换一个干净进程，下次启动前在加载页重做");
        relaunchClean();
        return true;
    }

    private void relaunchClean() {
        Intent i = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (i != null) { i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK); startActivity(i); }
        finishAffinity(); Runtime.getRuntime().exit(0);
    }

    private void showStartError(String message) {
        TextView t = new TextView(this);
        t.setText(message); t.setTextSize(20); t.setPadding(64, 64, 64, 64);
        setContentView(t);
    }

    // 原生线程只投递主线程工作，不持有 Activity，也不直接操作视图。
    // Native threads only post UI work; they neither retain the Activity nor touch views.
    private static void showNativeFailure(int code) {
        bootFailure = code;
        DmActivity activity = current.get();
        if (activity != null) activity.runOnUiThread(() -> {
            if (!activity.isDestroyed()) activity.showBootFailure();
        });
    }

    private void showBootFailure() {
        releaseTouches();
        showStartError(getString(bootFailure == -6 ? R.string.dmAndroidGraphicsFailed : R.string.dmAndroidHostStopped));
    }

    private static void showNativeInput(long id, String title, String sub, String guide, String initial,
                                        int maxChars, int nameRule, int numericMax) {
        DmActivity activity = current.get();
        if (activity == null) { nativeTextResult(id, false, ""); return; }
        activity.runOnUiThread(() -> activity.openInput(id, title, sub, guide, initial, maxChars, nameRule != 0, numericMax));
    }

    private void openInput(long id, String title, String sub, String guide, String initial,
                           int maxChars, boolean nameRule, int numericMax) {
        if (!resumed || isFinishing() || isDestroyed() || inputDialog != null) { nativeTextResult(id, false, ""); return; }
        releaseTouches();
        EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setInputType(numericMax >= 0 ? InputType.TYPE_CLASS_NUMBER : InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        input.setHint(nameRule ? getString(R.string.dmNameBlankRandom) : guide);
        input.setText(TextInputRules.normalize(initial, maxChars, nameRule, numericMax));
        input.setSelection(input.length());
        String message = sub + (sub.isEmpty() || guide.isEmpty() ? "" : "\n") + guide;
        if (nameRule) message += (message.isEmpty() ? "" : "\n") + getString(R.string.dmNameRule);
        if (numericMax >= 0) message = getString(R.string.dmAndroidQuantityRange, numericMax);
        if (title.isEmpty()) title = getString(nameRule ? R.string.dmNameTitle : numericMax >= 0 ? R.string.dmQtyTitle : R.string.dmInputTitle);
        final boolean[] finished = {false};
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle(title).setMessage(message).setView(input)
            .setPositiveButton(R.string.dmConfirm, null).setNegativeButton(R.string.dmCancel, (d, which) -> {})
            .create();
        inputDialog = dialog;
        dialog.setOnDismissListener(d -> {
            if (inputDialog == dialog) inputDialog = null;
            if (!finished[0]) { finished[0] = true; nativeTextResult(id, false, ""); }
            if (resumed) immersive();
        });
        dialog.setOnShowListener(d -> {
            Runnable check = () -> {
                boolean composing = BaseInputConnection.getComposingSpanStart(input.getText()) >= 0;
                int error = nameRule ? TextInputRules.nameError(input.getText().toString()) : 0;
                input.setError(composing ? getString(R.string.dmNamePickIme) : error == 1 ? getString(R.string.dmNameTooShort) : error == 2 ? getString(R.string.dmNameEdge) : null);
                dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(!composing && error == 0);
            };
            Runnable settle = () -> {
                Editable text = input.getText();
                if (BaseInputConnection.getComposingSpanStart(text) < 0) {
                    String normalized = TextInputRules.normalize(text.toString(), maxChars, nameRule, numericMax);
                    if (!normalized.contentEquals(text)) { input.setText(normalized); input.setSelection(input.length()); return; }
                }
                check.run();
            };
            // 输入法结束组字（点候选、按完成）时常常只去掉组字标记、文字不变：TextWatcher 收不到，「确定」一直灰、提示停在「先选完字」。
            // 组字状态一变就和文字变化一样处理（规整 + 重判）：SpanWatcher 挂在文字上，Editable.Factory 让每次 setText 换上的新文字也挂着它。
            // When the IME finishes composing (picking a candidate, Done) it often only removes the composing span and keeps the text: TextWatcher is not
            // called, so OK stays disabled with "finish choosing characters". A change of composing state is handled like a text change (normalize + check):
            // a SpanWatcher sits on the text, and an Editable.Factory attaches it to the new text after every setText.
            SpanWatcher composingWatch = new SpanWatcher() {
                boolean composing = BaseInputConnection.getComposingSpanStart(input.getText()) >= 0;
                private void changed(Spannable text) {
                    boolean now = BaseInputConnection.getComposingSpanStart(text) >= 0;
                    if (now != composing) { composing = now; input.post(settle); }
                }
                public void onSpanAdded(Spannable text, Object what, int start, int end) { changed(text); }
                public void onSpanRemoved(Spannable text, Object what, int start, int end) { changed(text); }
                public void onSpanChanged(Spannable text, Object what, int ostart, int oend, int nstart, int nend) { changed(text); }
            };
            input.setEditableFactory(new Editable.Factory() {
                @Override public Editable newEditable(CharSequence source) {
                    Editable e = super.newEditable(source);
                    e.setSpan(composingWatch, 0, e.length(), Spanned.SPAN_INCLUSIVE_INCLUSIVE);
                    return e;
                }
            });
            input.getText().setSpan(composingWatch, 0, input.length(), Spanned.SPAN_INCLUSIVE_INCLUSIVE);
            input.addTextChangedListener(new TextWatcher() {
                public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                public void onTextChanged(CharSequence s, int start, int before, int count) {}
                public void afterTextChanged(Editable text) { settle.run(); }
            });
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                finished[0] = true;
                nativeTextResult(id, true, input.getText().toString());
                dialog.dismiss();
            });
            check.run();
            input.requestFocus();
            if (dialog.getWindow() != null) dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        });
        dialog.show();
    }

    private void releaseTouches() {
        if (touchOverlay != null) touchOverlay.cancelTouches();
        if (started) nativeTouch(0, new int[0], new float[0], new float[0]);
    }

    private void updateRefresh() {
        if (!libraryLoaded) return;
        android.view.Display d = view.getDisplay(); if (d == null) { nativeRefresh(0, 0); return; }
        nativeRefresh(d.getRefreshRate(), maxRefresh(d));
    }
    // 与当前模式同尺寸的最高刷新率（120 档要不要出现、生不生效）| the highest refresh rate among the modes with the current mode's size (whether the 120 stop appears and takes effect)
    private static float maxRefresh(android.view.Display d) {
        android.view.Display.Mode cur = d.getMode(); float best = d.getRefreshRate();
        for (android.view.Display.Mode m : d.getSupportedModes()) if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight() && m.getRefreshRate() > best) best = m.getRefreshRate();
        return best;
    }
    // 帧率档 120 时把窗口的首选显示模式切到同尺寸的最高刷新率（≥ 119 Hz）；其余档回系统默认（0）。每秒问一次宿主，变了才设（设置页「应用」后一秒内生效）
    // With the 120 stop, switch the window's preferred display mode to the highest refresh rate of the same size (>= 119 Hz); the other stops go back to the system default (0).
    // The host is asked once a second and the mode is set only on change (takes effect within a second of Apply on the settings page)
    private int preferredModeId = -1;
    private final android.os.Handler modeHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable modePoll = new Runnable() { @Override public void run() { applyPreferredMode(); modeHandler.postDelayed(this, 1000); } };
    private void applyPreferredMode() {
        if (!started || view.getDisplay() == null) return;
        android.view.Display d = view.getDisplay(); int want = 0;
        if (nativeFpsTarget() == 120) { android.view.Display.Mode cur = d.getMode(); float best = 0;
            for (android.view.Display.Mode m : d.getSupportedModes()) if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight() && m.getRefreshRate() >= 119f && m.getRefreshRate() > best) { best = m.getRefreshRate(); want = m.getModeId(); } }
        if (want == preferredModeId) return;
        preferredModeId = want; WindowManager.LayoutParams lp = getWindow().getAttributes(); lp.preferredDisplayModeId = want; getWindow().setAttributes(lp);
        android.util.Log.i("dm", "display mode: preferredDisplayModeId=" + want + " (fps target " + nativeFpsTarget() + ")");
    }
    // 实体手柄：按键与轴都先交原生（pad_android.c 判断来源与键位；手柄来源的 BACK 当 B 吃掉）；不归手柄的照常交给系统
    // Physical controllers: keys and axes go native first (pad_android.c decides source and mapping; BACK from a controller counts as B and is swallowed); anything else goes to the system as usual
    @Override public boolean dispatchKeyEvent(android.view.KeyEvent e) {
        // 首启生成 / 导入 / 解压的加载页上吞掉返回与手柄 B（玩家按 B 习惯是「取消」，原来会把 App 切到后台、久了可能被系统清掉；离开仍可按 Home）。
        // 解压中这两种键 = 取消（置 AtomicBoolean，线程收尾时显示「解压已暂停」，已完成文件保留）；解压暂停页上手柄 A 松开 = 继续解压。
        // On the first-launch generation / import / extraction loading page, swallow BACK and controller B (players press B to cancel; it used to send the
        // App to the background, where the system may later clean it up; Home still leaves). During extraction these two keys are Cancel (set the
        // AtomicBoolean; the worker then shows "extraction paused" with finished files kept); on the paused page controller A on release = resume.
        // 权限提示页：手柄 A / START、确认键或回车松开时打开系统设置（只有手柄或键盘的玩家；有的手柄把确认键映射成 DPAD_CENTER，七审 Q7-02） | permission prompt: controller A / START, confirm or Enter on release opens system settings (controller or keyboard players; some controllers map confirm to DPAD_CENTER, seventh audit Q7-02)
        if (!started && prompting && (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_A || e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_START || e.getKeyCode() == android.view.KeyEvent.KEYCODE_DPAD_CENTER || e.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER || e.getKeyCode() == android.view.KeyEvent.KEYCODE_NUMPAD_ENTER)) {
            if (e.getAction() == android.view.KeyEvent.ACTION_UP) openFileAccessSettings(); return true; }
        if (!started && zipPaused && e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_A) {
            if (e.getAction() == android.view.KeyEvent.ACTION_UP) resumeZip(); return true; }
        // 加载页上的手柄（§2.7）：切到手柄模式；页脚显示时 A 键 = 复制群号 / 链接 | controller on the loading page (§2.7): switch to controller mode; with the footer shown, A = copy the number / link
        if (!started && (generating || importing || loadPageDemo || zipRunning) && loadPage != null && loadPage.isAttachedToWindow() && loadPage.padKey(e)) {
            if (zipRunning && (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK || e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_B)) zipCancel.set(true);
            return true; }
        if (!started && (generating || importing || loadPageDemo) && (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK || e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_B)) return true;
        if (!started && zipRunning && (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK || e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_B)) { zipCancel.set(true); return true; }
        // 生成失败页（§2.7）：手柄 A 松开 = 分享诊断日志（触屏点按钮）| generation failure screen (§2.7): controller A on release = Share Diagnostics (touch taps the button)
        if (!started && !generating && genFailure != null && genLogText != null && e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_A) { if (e.getAction() == android.view.KeyEvent.ACTION_UP) shareGenLog(); return true; }
        if (started && (e.getAction() == android.view.KeyEvent.ACTION_DOWN || e.getAction() == android.view.KeyEvent.ACTION_UP)) {
            android.view.InputDevice d = e.getDevice(); int vendor = d != null ? d.getVendorId() : 0, product = d != null ? d.getProductId() : 0;
            if (nativePadKey(e.getDeviceId(), vendor, product, e.getSource(), e.getKeyCode(), e.getAction() == android.view.KeyEvent.ACTION_DOWN)) return true;
        }
        return super.dispatchKeyEvent(e);
    }
    @Override public boolean dispatchGenericMotionEvent(android.view.MotionEvent e) {
        if (started && (e.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK && e.getAction() == android.view.MotionEvent.ACTION_MOVE) {
            android.view.InputDevice d = e.getDevice(); int vendor = d != null ? d.getVendorId() : 0, product = d != null ? d.getProductId() : 0;
            boolean zrz = d != null && d.getMotionRange(android.view.MotionEvent.AXIS_Z, e.getSource()) != null;   // 右摇杆：有 Z/RZ 用 Z/RZ，否则 RX/RY | right stick: Z/RZ when present, else RX/RY
            float rx = e.getAxisValue(zrz ? android.view.MotionEvent.AXIS_Z : android.view.MotionEvent.AXIS_RX), ry = e.getAxisValue(zrz ? android.view.MotionEvent.AXIS_RZ : android.view.MotionEvent.AXIS_RY);
            float lt = Math.max(e.getAxisValue(android.view.MotionEvent.AXIS_LTRIGGER), e.getAxisValue(android.view.MotionEvent.AXIS_BRAKE)), rt = Math.max(e.getAxisValue(android.view.MotionEvent.AXIS_RTRIGGER), e.getAxisValue(android.view.MotionEvent.AXIS_GAS));
            if (nativePadMotion(e.getDeviceId(), vendor, product, e.getSource(), e.getAxisValue(android.view.MotionEvent.AXIS_X), e.getAxisValue(android.view.MotionEvent.AXIS_Y), rx, ry, lt, rt, e.getAxisValue(android.view.MotionEvent.AXIS_HAT_X), e.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y))) return true;
        }
        return super.dispatchGenericMotionEvent(e);
    }
    @Override public void onInputDeviceAdded(int id) {}
    @Override public void onInputDeviceRemoved(int id) { if (started) nativePadDrop(id); }
    @Override public void onInputDeviceChanged(int id) {}
    @Override public void onDisplayAdded(int id) { updateRefresh(); }
    @Override public void onDisplayRemoved(int id) { updateRefresh(); }
    @Override public void onDisplayChanged(int id) { updateRefresh(); }
    // 回到前台时生成失败的结论作废、重新判断：玩家可能刚补好游戏文件或腾出空间 | back in the foreground a generation failure is dropped and judged afresh: the player may have just added the game files or freed space
    @Override protected void onResume() { super.onResume(); resumed = true; immersive(); if (!generating && !genShareOpen) genFailure = null; if (!zipRunning && !zipPaused) { zipFailure = null; zipFailTitle = null; } genShareOpen = false; startHost(); updateRefresh(); if (started) { nativeFocus(3); if (touchOverlay != null && bootFailure == 0) touchOverlay.setInputEnabled(true); } }
    @Override protected void onPause() { resumed = false; if (inputDialog != null) inputDialog.dismiss(); releaseTouches(); if (touchOverlay != null) touchOverlay.setInputEnabled(false); if (started) nativeFocus(0); super.onPause(); }
    @Override protected void onStart() { super.onStart(); if (started) nativeFocus(2); }
    @Override protected void onStop() { if (started) nativeFocus(1); com.xiaoboluo.d2r.widget.HeroWidgetProvider.requestUpdate(this); super.onStop(); }
    // 崩溃围栏（闸 1）：内存告警记进运行标记；Activity 真正结束时让宿主优雅退出（清标记、_exit），进程不复用
    // Crash fence (gate 1): memory warnings go into the running marker; when the Activity really finishes the host exits gracefully (marker cleared, _exit), the process is not reused
    @Override public void onTrimMemory(int level) { super.onTrimMemory(level); if (started) nativeMemoryWarning(level); }
    @Override public void onLowMemory() { super.onLowMemory(); if (started) nativeMemoryWarning(TRIM_MEMORY_RUNNING_CRITICAL); }
    // 分享诊断日志（原生工作线程经 text_input_jni.c 调）：系统分享面板，文件经 DiagProvider 以临时读权限交出 | share diagnostics (called by the native worker through text_input_jni.c): system share sheet, the file handed over through DiagProvider with a temporary read grant
    static void shareDiagnostics(String path, String title) {
        DmActivity a = current.get(); if (a == null) { android.util.Log.w("dm", "share: no activity"); return; }
        a.runOnUiThread(() -> {
            File f = new File(path); Uri uri = DiagProvider.uriFor(a, f);
            Intent send = new Intent(Intent.ACTION_SEND); send.setType("text/plain"); send.putExtra(Intent.EXTRA_STREAM, uri); send.putExtra(Intent.EXTRA_SUBJECT, f.getName());
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            try { a.startActivity(Intent.createChooser(send, title)); } catch (Exception e) { android.util.Log.w("dm", "share: " + e); }
        });
    }
    // 存档管理的系统面板（text_input_jni.c，工作线程）：SAF 选包 / 存包。选到的包先拷进 files/temp 再交给宿主（宿主按普通文件读），存包从宿主的临时 ZIP 拷到玩家选的位置；拷都在后台线程
    // Save management's system panels (text_input_jni.c, worker thread): SAF pick / save. A picked package is copied into files/temp before going to the host (the host reads plain files); saving copies from the host's temporary ZIP to the place the player picked; copies run on a background thread
    private static final int REQ_SAVE_PICK = 7101, REQ_SAVE_EXPORT = 7102, REQ_SAVE_FOLDER = 7103;
    private String saveExportPath;
    static void savePick() {
        DmActivity a = current.get(); if (a == null) { nativeSavePicked(null); return; }
        a.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");   // 不少文件管理器不给 zip 标类型，选完由宿主校验 | many file managers do not tag zips, the host validates what is picked
            try { a.startActivityForResult(i, REQ_SAVE_PICK); } catch (Exception e) { android.util.Log.w("dm", "save pick: " + e); nativeSavePicked(null); }
        });
    }
    // 上游 save 目录（玩家用 root 或 debug 包拷出来的）：SAF 给不了文件路径，拷到内部临时目录再交给宿主 | an upstream save folder the player copied out: SAF gives no file path, so it is copied to an internal temporary directory for the host
    static void savePickFolder() {
        DmActivity a = current.get(); if (a == null) { nativeSavePicked(null); return; }
        a.runOnUiThread(() -> {
            try { a.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI, Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADownload")), REQ_SAVE_FOLDER); }   // 存储根目录系统不让选，从 Download 开始 | the system refuses the storage root, so start in Download
            catch (Exception e) { android.util.Log.w("dm", "save pick folder: " + e); nativeSavePicked(null); }
        });
    }
    private static void saveDeleteTree(File f) { File[] k = f.listFiles(); if (k != null) for (File c : k) saveDeleteTree(c); f.delete(); }
    // budget 按文件数（同原生 MAX_FILES）与总量封顶，超了整份放弃；单个大文件（游戏资源）跳过，存档没有这么大 | budget caps the file count (native MAX_FILES) and the total, and the whole copy is given up past them; a single large file (game resources) is skipped, no save is that large
    private static final class SaveTooBig extends java.io.IOException {}
    private static void saveCopyTree(android.content.ContentResolver cr, Uri tree, String id, File dst, int depth, SaveCopy.Budget budget, boolean rootLevel) throws java.io.IOException {
        if (depth > 4) return; if (!dst.mkdirs() && !dst.isDirectory()) throw new java.io.IOException("mkdir");
        java.util.List<String[]> kids = new java.util.ArrayList<>();
        try (android.database.Cursor c = cr.query(android.provider.DocumentsContract.buildChildDocumentsUriUsingTree(tree, id), new String[] { android.provider.DocumentsContract.Document.COLUMN_DOCUMENT_ID, android.provider.DocumentsContract.Document.COLUMN_DISPLAY_NAME, android.provider.DocumentsContract.Document.COLUMN_MIME_TYPE, android.provider.DocumentsContract.Document.COLUMN_SIZE }, null, null, null)) {
            while (c != null && c.moveToNext()) kids.add(new String[] { c.getString(0), c.getString(1), c.getString(2), c.getString(3) });
        }
        if (rootLevel) for (String[] k : kids) if ("save".equals(k[1]) && android.provider.DocumentsContract.Document.MIME_TYPE_DIR.equals(k[2])) { saveCopyTree(cr, tree, k[0], new File(dst, "save"), depth + 1, budget, false); return; }   // 选了上级就只拷 save | a parent was picked: only save is copied
        for (String[] k : kids) {
            String nm = k[1]; if (nm == null || nm.startsWith(".") || nm.contains("/")) continue; File to = new File(dst, nm);
            if (android.provider.DocumentsContract.Document.MIME_TYPE_DIR.equals(k[2])) { saveCopyTree(cr, tree, k[0], to, depth + 1, budget, false); continue; }
            long sz = 0; try { if (k[3] != null) sz = Long.parseLong(k[3]); } catch (NumberFormatException ignored) {}
            if (sz > (64L << 20)) continue; if (!budget.fits(sz)) throw new SaveTooBig();
            try (java.io.InputStream in = cr.openInputStream(android.provider.DocumentsContract.buildDocumentUriUsingTree(tree, k[0])); java.io.OutputStream out = new java.io.FileOutputStream(to)) { if (!SaveCopy.copy(in, out, SaveCopy.PICK_MAX)) throw new SaveTooBig(); } budget.spend(to.length());
        }
    }
    static void saveExport(String path) {
        DmActivity a = current.get(); if (a == null) { nativeSaveExported(false); return; }
        a.runOnUiThread(() -> {
            a.saveExportPath = path;
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/zip").putExtra(Intent.EXTRA_TITLE, new File(path).getName());
            try { a.startActivityForResult(i, REQ_SAVE_EXPORT); } catch (Exception e) { android.util.Log.w("dm", "save export: " + e); nativeSaveExported(false); }
        });
    }
    // 选到的是 .d2s / .d2i 时宿主按扩展名和文件名分流，所以留原名；其他一律当包 | for a .d2s / .d2i the host goes by extension and name, so the original name is kept; anything else is treated as a package
    private String saveNameOf(Uri uri) {
        String nm = null; if (uri != null) try (android.database.Cursor c = getContentResolver().query(uri, new String[] { android.provider.OpenableColumns.DISPLAY_NAME }, null, null, null)) { if (c != null && c.moveToFirst()) nm = c.getString(0); } catch (Exception e) { android.util.Log.w("dm", "save pick name: " + e); }
        return nm != null && !nm.contains("/") && !nm.startsWith(".") && (nm.toLowerCase(java.util.Locale.ROOT).endsWith(".d2s") || nm.toLowerCase(java.util.Locale.ROOT).endsWith(".d2i")) ? nm : "save_import_pick.zip";
    }
    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_SAVE_PICK && req != REQ_SAVE_EXPORT && req != REQ_SAVE_FOLDER) return;
        if (!started) { android.util.Log.w("dm", "save panel result after the process was recreated: dropped"); return; }   // 面板开着时进程被系统杀了：这是新进程，原生库没加载，也没有等结果的界面；临时 ZIP 下次启动清 | the process was killed while the panel was open: this is a new one, the native library is not loaded and nothing is waiting; the temporary ZIP is cleared at the next start
        Uri uri = res == RESULT_OK && data != null ? data.getData() : null; String src = saveExportPath; android.content.ContentResolver cr = getContentResolver();
        if (req == REQ_SAVE_FOLDER) {
            if (uri == null) { nativeSavePicked(null); return; }
            File trees = new File(new File(getFilesDir(), "temp"), "save_import_tree");
            SaveCopy.PICKS.execute(() -> {   // 串行、每次一个新目录（审计 20261104 P1-1）| one at a time, a new directory each time
                boolean ok = false; String bad = "!io", origin = null; File dir = null; try { origin = android.provider.DocumentsContract.getTreeDocumentId(uri); dir = SaveCopy.freshDir(trees); saveCopyTree(cr, uri, origin, dir, 0, new SaveCopy.Budget(SaveCopy.PICK_MAX), true); ok = true; } catch (SaveTooBig e) { bad = "!limit"; } catch (Exception e) { android.util.Log.w("dm", "save pick folder copy: " + e); }
                if (!ok && dir != null) saveDeleteTree(dir);   // 同选文件：总量有上限，失败不留半份 | as a picked file: the total is capped and a failure leaves nothing behind
                nativeSavePickedFrom(ok ? dir.getPath() : bad, origin);
            });
        } else if (req == REQ_SAVE_PICK) {
            if (uri == null) { nativeSavePicked(null); return; }
            final Uri pu = uri;
            SaveCopy.PICKS.execute(() -> {   // 串行、每次一个新目录（审计 20261104 P1-1）| one at a time, a new directory each time
                String origin = null; try { origin = android.provider.DocumentsContract.getDocumentId(pu); } catch (Exception ignored) {}   // 外部存储的 ID 带路径（primary:Download/save.coop/…），别的 provider 没有路径 | an external-storage id carries the path; other providers carry none
                File tmp = null; boolean ok = false; String bad = "!io";
                try { tmp = new File(SaveCopy.freshDir(new File(getFilesDir(), "temp/save_pick")), saveNameOf(pu)); } catch (Exception e) { android.util.Log.w("dm", "save pick dir: " + e); }   // 名字在后台线程问系统（provider 可能慢）| the name is asked on the background thread: a provider can be slow
                if (tmp != null) try (java.io.InputStream in = cr.openInputStream(pu); java.io.OutputStream out = new java.io.FileOutputStream(tmp)) { ok = SaveCopy.copy(in, out, SaveCopy.PICK_MAX); if (!ok && in != null) bad = "!limit"; } catch (Exception e) { android.util.Log.w("dm", "save pick copy: " + e); }
                if (!ok && tmp != null) tmp.delete();   // 失败不留半份；成功的副本下次选或下次启动清（save_pick）| a failure leaves nothing; a good copy is cleared at the next pick or start
                nativeSavePickedFrom(ok ? tmp.getPath() : bad, origin);   // 拷失败不是取消：原生那边给失败提示 | a failed copy is not a cancel: the native side shows the failure
            });
        } else {
            if (uri == null || src == null) { nativeSaveExported(false); return; }
            new Thread(() -> {
                boolean ok = false; try (java.io.InputStream in = new java.io.FileInputStream(src); java.io.OutputStream out = cr.openOutputStream(uri, "wt")) { ok = SaveCopy.copy(in, out, Long.MAX_VALUE); } catch (Exception e) { android.util.Log.w("dm", "save export copy: " + e); }
                nativeSaveExported(ok);
            }, "dm-save-export").start();
        }
    }
    // 游戏内社区弹窗的复制（dm_community.c → text_input_jni.c，工作线程）：到界面线程写剪贴板 | the in-game community box's copy (dm_community.c → text_input_jni.c, a worker thread): write the clipboard on the UI thread
    static void copyToClipboard(String text) {
        DmActivity a = current.get(); if (a == null) { android.util.Log.w("dm", "copy: no activity"); return; }
        a.runOnUiThread(() -> {
            try { android.content.ClipboardManager cm = (android.content.ClipboardManager) a.getSystemService(android.content.Context.CLIPBOARD_SERVICE);
                if (cm != null) cm.setPrimaryClip(android.content.ClipData.newPlainText("D2R Mobile Plus", text)); else android.util.Log.w("dm", "copy: no clipboard"); }
            catch (RuntimeException e) { android.util.Log.w("dm", "copy: " + e); }
        });
    }
    // 系统记录的最近退出原因（Android 11+ ApplicationExitInfo，代替 iOS 的 .ips 崩溃日志）：写到 files/dm/exit_reasons.txt，分享诊断日志附上；只读本 App 自己的记录
    // The system's recent exit reasons (Android 11+ ApplicationExitInfo, in place of iOS's .ips crash logs): written to files/dm/exit_reasons.txt and attached to share diagnostics; only this App's own records
    private void writeExitReasons(File runtime) {
        try {
            ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE); StringBuilder b = new StringBuilder();
            java.text.SimpleDateFormat df = new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT);
            for (ApplicationExitInfo e : am.getHistoricalProcessExitReasons(getPackageName(), 0, 5)) {
                b.append("time=").append(df.format(new java.util.Date(e.getTimestamp()))).append(" reason=").append(e.getReason()).append(" status=").append(e.getStatus())
                 .append(" importance=").append(e.getImportance()).append(" pss_kb=").append(e.getPss()).append(" rss_kb=").append(e.getRss()).append(" desc=").append(e.getDescription()).append('\n');
            }
            try (FileOutputStream out = new FileOutputStream(new File(runtime, "exit_reasons.txt"))) { out.write(b.toString().getBytes(StandardCharsets.UTF_8)); }
        } catch (Throwable t) { android.util.Log.w("dm", "exit reasons: " + t); }
    }
    @Override protected void onDestroy() {
        genHandler.removeCallbacks(genPoll);   // 生成轮询跟着这个 Activity 走：重建后由新实例接回（generate），旧实例不再投递（外审 EXT6-05） | the generation poll belongs to this Activity: a recreated one takes over (generate), the old one stops posting (external review EXT6-05)
        modeHandler.removeCallbacks(modePoll);
        if (started && isFinishing()) nativeShutdown();   /* 不返回：宿主 _exit(0) | does not return: the host _exit(0)s */
        if (displays != null) displays.unregisterDisplayListener(this); { android.hardware.input.InputManager im = getSystemService(android.hardware.input.InputManager.class); if (im != null) im.unregisterInputDeviceListener(this); } if (inputDialog != null) inputDialog.dismiss(); if (current.get() == this) current.clear(); super.onDestroy(); }
    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); if (f) immersive(); else releaseTouches(); }

    @Override public void surfaceCreated(SurfaceHolder h) {}
    @Override public void surfaceChanged(SurfaceHolder h, int format, int w, int ht) {
        if (libraryLoaded) nativeSurface(h.getSurface(), w, ht);
        updateRefresh();
        startHost();
    }
    @Override public void surfaceDestroyed(SurfaceHolder h) { releaseTouches(); if (libraryLoaded) nativeSurface(null, 0, 0); }

}
