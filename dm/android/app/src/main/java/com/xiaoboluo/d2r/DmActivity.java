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

    // 资源在玩家放的位置读（缺省 Download/S2X/D2R Mobile/data），要「所有文件访问」权限；没有就引导去系统设置页，回来再启动
    // Resources are read where the player put them (default Download/S2X/D2R Mobile/data), which needs "all files access"; without it, guide the player to the system settings page and start after returning
    private boolean ensureFileAccess() {
        if (Environment.isExternalStorageManager()) return true;
        TextView t = new TextView(this);
        t.setText(R.string.dmAndroidPermission);
        t.setTextSize(20); t.setPadding(64, 64, 64, 64);
        t.setOnClickListener(v -> startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName()))));
        // 手柄 A / START 也能打开设置（只有手柄的玩家；TextView 自己只认方向键中键与回车） | controller A / START open Settings too (controller-only players; the TextView itself takes only D-pad centre and Enter)
        t.setFocusable(true); t.setDefaultFocusHighlightEnabled(false);   // 同加载页：手柄键后整屏不盖系统灰色焦点层 | as on the loading page: no system grey focus layer over the whole screen after a controller key
        t.setOnKeyListener((v, code, ev) -> {
            if (ev.getAction() != android.view.KeyEvent.ACTION_UP || (code != android.view.KeyEvent.KEYCODE_BUTTON_A && code != android.view.KeyEvent.KEYCODE_BUTTON_START)) return false;
            v.performClick(); return true; });
        setContentView(t); t.requestFocus();
        return false;
    }

    // 导入包：玩家在电脑上用 dm/tools/d2rm_android.py 和自己的游戏文件做出 android 文件夹，拷到 Download/S2X/D2R Mobile/ 下；
    // 这里把它拷进内部目录 files/dm/（out、shaders、overlay、derived.tsv、pack_info.txt）。pack_info.txt 与已导入的逐字相同就不再拷。
    // 先拷到 files/dm/import.part/，齐了再逐项换上，pack_info.txt 最后放：中途被杀或空间不够，下次打开从头再导，不会用到半份。
    // 原生库加载之前做：不含游戏内容的发布包，几张表在库加载时就从 derived.tsv 读（dm/host/dm_derived_rt.h）。
    // Import pack: the player makes the android folder on a computer with dm/tools/d2rm_android.py and their own game files and copies it to
    // Download/S2X/D2R Mobile/; this copies it into the internal files/dm/ (out, shaders, overlay, derived.tsv, pack_info.txt). Nothing is copied
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

    // true = 可以接着启动；false = 正在导入或已显示错误
    // true = startup can continue; false = importing, or an error is shown
    private boolean ensureImported(File source) {
        if (importing) return false;
        File runtime = new File(getFilesDir(), "dm");
        String want = readText(new File(source, "pack_info.txt")), have = readText(new File(runtime, "pack_info.txt"));
        if (want != null && !want.equals(have)) {
            String product = packProduct(want);
            if (!BuildConfig.DM_PRODUCT.equals(product)) { showStartError(getString(R.string.dmAndroidImportVersion, product)); return false; }
            importing = true;
            // 加载页（LoadPageView，核心在 dm/host/dm_loadpage.c）：单阶段「导入」，进度按字节报，页面自己算速度与剩余时间 | the loading page (LoadPageView, core in dm/host/dm_loadpage.c): the single stage "import", progress in bytes, the page computes speed and ETA itself
            showLoadPage();
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
        // 没有导入包、也没导入过，或导入的是给别的版本做的：App 带了生成器（-PdmGenRuntime 出的包）就在手机上用玩家自己的游戏文件现做，不用电脑。
        // 开发时直接推进来的（有 out/main.patched.bin、没有 pack_info.txt）不动
        // No import pack and nothing imported, or what was imported was made for another version: when the App carries the generator (built with -PdmGenRuntime)
        // it makes the pack on the phone from the player's own game files, without a computer. Files pushed directly during development (out/main.patched.bin, no pack_info.txt) are left alone;
        // an unfinished swap of a generated pack (install.pending / import.part left over) is not a push and is generated again (fourth audit A4-04)
        String product = packProduct(have);
        File ext = getExternalFilesDir(null);
        boolean pushed = PackGenerator.pushedByDeveloper(have, runtime, ext == null ? null : new File(ext, "dm"));
        if (generating || (want == null && !pushed && !BuildConfig.DM_PRODUCT.equals(product) && generatorAvailable())) { generate(source.getParentFile(), runtime); return false; }
        // 已导入的是给别的版本做的（App 升级了、导入包没重做）：同样要重做导入包。开发时直接推进来的没有 pack_info.txt，不核
        // The imported pack was made for another version (the app was updated, the pack was not remade): it has to be remade too.
        // Files pushed directly during development have no pack_info.txt and are not checked
        if (have != null && !BuildConfig.DM_PRODUCT.equals(product)) { showStartError(getString(R.string.dmAndroidImportVersion, product)); return false; }
        return true;
    }

    private interface Progress { void bytes(long done, long total); }

    // ---- 手机端生成导入包（PackGenerator）：自动开始，进度与失败原因显示在这一页；生成线程不持有 Activity，界面每半秒从静态状态取一次 ----
    // ---- On-phone import pack generation (PackGenerator): starts by itself, progress and failure reasons on this screen; the generation thread holds no Activity and the screen polls the static state every half second ----
    private static volatile boolean generating;
    private static final PackGenerator.State genState = new PackGenerator.State();
    private static volatile String genFailure;   // 生成失败时给玩家看的原因（已按语言取好） | the failure reason shown to the player (already localised)
    private final android.os.Handler genHandler = new android.os.Handler(android.os.Looper.getMainLooper());

    private boolean generatorAvailable() {
        if (!new File(getApplicationInfo().nativeLibraryDir, "libdmgen_py.so").canExecute()) return false;
        try (InputStream in = getAssets().open("dmgen/runtime.txt")) { return true; } catch (IOException e) { return false; }
    }

    private void showGenScreen(String title, String body, String hint) {
        android.widget.LinearLayout box = new android.widget.LinearLayout(this);
        box.setOrientation(android.widget.LinearLayout.VERTICAL); box.setPadding(64, 48, 64, 48);
        box.setGravity(android.view.Gravity.CENTER_VERTICAL);
        // 三层：标题 / 正文 / 副文，字号 24 / 18 / 14（相邻比 1.29–1.33，同 CLAUDE 文案原则的层级） | three levels: title / body / sub at 24 / 18 / 14 (neighbouring ratio 1.29-1.33)
        TextView t = new TextView(this); t.setTextSize(24); t.setText(title); box.addView(t);
        TextView b = new TextView(this); b.setTextSize(18); b.setText(body); b.setPadding(0, 24, 0, 0); box.addView(b);
        if (hint != null) { TextView h = new TextView(this); h.setTextSize(14); h.setText(hint); h.setPadding(0, 24, 0, 0); box.addView(h); }
        setContentView(box);
    }


    // 生成进度画在加载页上（LoadPageView，核心 dm/host/dm_loadpage.c）：七个阶段与 PackGenerator.Step 一一对应（收尾那步记作「安装」），着色器那步按已生成的
    // .xgs 个数报几成与「第 N / M 个」，其余步骤只报阶段（剩余时间由核心按权重外推）。失败时加载页记失败，但仍换成文字页把完整原因（含 gen_log 位置）给玩家看。
    // Generation progress is drawn on the loading page (LoadPageView, core dm/host/dm_loadpage.c): seven stages one-to-one with PackGenerator.Step (the closing step counts
    // as "install"); the shader step reports its fraction and "N / M" from the .xgs files made so far, the other steps only their stage (the core extrapolates the time
    // left from the weights). On failure the loading page records it, but the screen switches to the text page so the player gets the full reason (with the gen_log location).
    static final int[] GEN_STAGES = {LoadPageView.PREPARE, LoadPageView.PATCH, LoadPageView.ASSETS, LoadPageView.OVERLAY, LoadPageView.PACKS, LoadPageView.SHADERS, LoadPageView.INSTALL};
    private final Runnable genPoll = new Runnable() {
        @Override public void run() {
            if (genFailure != null) { LoadPageView.fail(genFailure); showGenScreen(getString(R.string.dmAndroidGenFailed), genFailure, null); return; }
            if (!generating) { LoadPageView.finish(); if (resumed) startHost(); return; }
            PackGenerator.State s = genState;
            if (s.step == PackGenerator.Step.SHADERS && s.shaderDir != null) s.shadersMade = PackGenerator.count(new File(s.shaderDir), ".xgs");
            if (s.step == PackGenerator.Step.SHADERS) LoadPageView.set(PackGenerator.loadStage(s.step), Math.min(1.0, (double) s.shadersMade / PackGenerator.SHADERS), s.shadersMade, PackGenerator.SHADERS, 0, 0);
            else LoadPageView.set(PackGenerator.loadStage(s.step), 0, 0, 0, 0, 0);
            genHandler.postDelayed(this, 500);
        }
    };

    // base = Download/S2X/D2R Mobile（玩家放 ExeFS 与 data 的地方），runtime = files/dm | base = Download/S2X/D2R Mobile (where the player puts ExeFS and data), runtime = files/dm
    private void generate(File base, File runtime) {
        genHandler.removeCallbacks(genPoll);
        if (genFailure != null) { showGenScreen(getString(R.string.dmAndroidGenFailed), genFailure, null); return; }
        showLoadPage();   // 阶段状态在原生侧（静态），Activity 重建时只换一个新视图接上 | stage state lives natively (static); a recreated Activity just attaches a new view
        if (generating) { genHandler.post(genPoll); return; }   // Activity 重建：生成线程还在跑，只接回界面 | the Activity was recreated: the generation thread is still running; only reattach the screen
        // 资源目录与 startHost 同一条规则（缺省 data，没有就沿用早先的 Download/data） | resource directory by the same rule as startHost (data by default, else the earlier Download/data)
        File data = new File(base, "data"), earlier = new File(base.getParentFile().getParentFile(), "data");
        if (!data.isDirectory() && earlier.isDirectory()) data = earlier;
        java.util.List<String> missing = PackGenerator.missingInputs(base);
        if (data != new File(base, "data") && new File(data, "build_manifest.psv").isFile()) missing.remove("data/build_manifest.psv");
        if (!missing.isEmpty()) { fail(getString(R.string.dmAndroidGenNeedFiles, String.join(", ", missing))); return; }
        File gen = new File(getFilesDir(), "dmgen");
        long free = new android.os.StatFs(getFilesDir().getPath()).getAvailableBytes() + treeBytes(new File(gen, "root"));
        if (free < PackGenerator.SPACE_NEEDED) { fail(getString(R.string.dmAndroidGenNoSpace, android.text.format.Formatter.formatShortFileSize(this, free))); return; }
        generating = true; genState.step = PackGenerator.Step.PREPARE; genState.shaderDir = null; genState.shadersMade = 0;
        LoadPageView.begin(GEN_STAGES, PackGenerator.LOAD_WEIGHTS_MS); genHandler.post(genPoll);
        final File dataDir = data; final String nativeDir = getApplicationInfo().nativeLibraryDir;
        final String appHome = getFilesDir().getParent(), apkDir = new File(nativeDir).getParentFile().getParent(), appId = getPackageName();   // gen_log.txt 脱敏用 | for scrubbing gen_log.txt
        final android.content.res.AssetManager assets = getAssets(); final android.content.Context app = getApplicationContext();
        final int jobs = Math.max(1, Runtime.getRuntime().availableProcessors());
        new Thread(() -> {
            String failure = null; java.util.Deque<String> tail = PackGenerator.newTail(); File log = new File(gen, "gen.log");
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
                Os.symlink(dataDir.getPath(), new File(game, "RomFS/data").getPath());
                File out = new File(gen, "out"); PackGenerator.deleteTree(out);
                int exit = PackGenerator.run(PackGenerator.command(new File(nativeDir), gen, game, out), PackGenerator.environment(new File(nativeDir), gen, jobs), new File(gen, "src"), log, genState, tail);
                PackGenerator.Failure kind = PackGenerator.classify(exit, new java.util.ArrayList<>(tail));
                if (kind == PackGenerator.Failure.NONE) {
                    PackGenerator.install(new File(out, "android"), runtime);
                    for (String d : new String[]{"root", "out", "game"}) PackGenerator.deleteTree(new File(gen, d));   // 缓存与中间产物约 1 GB，做完就删 | caches and intermediates are about 1 GB; removed when done
                } else {
                    failure = kind == PackGenerator.Failure.NEED_FILES ? app.getString(R.string.dmAndroidGenNeedFiles, PackGenerator.missingDetail(new java.util.ArrayList<>(tail)))
                            : kind == PackGenerator.Failure.NO_SPACE ? app.getString(R.string.dmAndroidGenNoSpace, android.text.format.Formatter.formatShortFileSize(app, new android.os.StatFs(gen.getPath()).getAvailableBytes()))
                            : kind == PackGenerator.Failure.STEP_FAILED ? app.getString(R.string.dmAndroidGenStepFailed, stepLabelFor(app, genState.step))
                            : app.getString(R.string.dmAndroidGenInterrupted, "exit " + exit);
                }
            } catch (IOException | ErrnoException | InterruptedException | RuntimeException e) {
                String m = e.getMessage() == null ? e.toString() : e.getMessage();
                failure = m.contains("No space left") || m.contains("ENOSPC")
                        ? app.getString(R.string.dmAndroidGenNoSpace, android.text.format.Formatter.formatShortFileSize(app, new android.os.StatFs(gen.getPath()).getAvailableBytes()))
                        : app.getString(R.string.dmAndroidGenInterrupted, m);
                tail.addLast("[java] " + e);
            }
            if (failure != null) {   // 日志放到玩家看得到的地方，反馈时附上 | put the log where the player can see it, to attach when reporting
                try (OutputStream o = new FileOutputStream(new File(base, "gen_log.txt"))) {   // 脱敏后再写（四审 A4-09）| scrubbed before writing (fourth audit A4-09)
                    String text = (log.isFile() ? new String(Files.readAllBytes(log.toPath()), StandardCharsets.UTF_8) : "") + "\n" + String.join("\n", tail) + "\n";
                    o.write(PackGenerator.scrubLog(text, apkDir, appHome, appId).getBytes(StandardCharsets.UTF_8));
                } catch (IOException ignored) {}
            }
            genFailure = failure;
            generating = false;
        }, "dm-generate").start();
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

    private void fail(String reason) { genFailure = reason; showGenScreen(getString(R.string.dmAndroidGenFailed), reason, null); }

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
        try (InputStream in = assets.open(stamp)) { want = new String(in.readAllBytes(), StandardCharsets.UTF_8); }
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
    private void showLoadPage() { loadPage = new LoadPageView(this); setContentView(loadPage); startCommunityFetch(); }
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
                    .run((qq, gh, from) -> LoadPageView.community(qq, gh)); }
            catch (InterruptedException | RuntimeException e) { android.util.Log.w("dm", "社区信息：拉取线程出错，不显示：" + e); }
        }, "dm-community").start();
    }

    private void startHost() {
        if (bootFailure != 0) { showBootFailure(); return; }
        if (loadPageDemoRun()) return;
        if (!started && !importing && resumed) prefetchCommunity();
        if (started || importing || !resumed || !ensureFileAccess()) return;
        File downloads = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (!libraryLoaded && !ensureImported(new File(downloads, "S2X/D2R Mobile/android"))) return;
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
        // 资源目录缺省 Download/S2X/D2R Mobile/data；没有这个目录、而早先的 Download/data 在时沿用早先的位置，已装好的手机不必搬数据。
        // 按目录判断而不按 .build.config：它也可以由导入的覆盖层提供，资源目录里就没有。
        // The resource directory defaults to Download/S2X/D2R Mobile/data; when that directory does not exist but the earlier
        // Download/data does, the earlier one is kept, so existing installs need not move their data. The test is the directory,
        // not .build.config: the imported overlay can provide that file, and then the resource directory has none.
        File data = new File(downloads, "S2X/D2R Mobile/data"), earlier = new File(downloads, "data");
        if (!data.isDirectory() && earlier.isDirectory()) data = earlier;
        String src = data.getAbsolutePath();
        { File ext2 = getExternalFilesDir(null), internal2 = new File(getFilesDir(), "dm");   // 与上面 / jni_android.c 同一条运行目录规则 | same runtime-directory rule as above / jni_android.c
          writeExitReasons(new File(internal2, "out/main.patched.bin").canRead() || ext2 == null ? internal2 : new File(ext2, "dm")); }   // 分享诊断日志附系统退出原因 | exit reasons for share diagnostics
        // 本进程里加载页走过（导入 / 手机端生成）就把它的完成 / 失败行交给宿主写进 boot.log（分享诊断日志的「加载页：」摘要） | if the loading page ran in this process
        // (import / on-phone generation), hand its finish / failure line to the host for boot.log (the 「加载页：」 share-diagnostics summary)
        try { String lp = LoadPageView.lastLine(); if (lp != null && !lp.isEmpty()) Os.setenv("DM_LOADPAGE_LAST", lp, true); } catch (ErrnoException | UnsatisfiedLinkError ignored) {}
        try { if (!communityLast.isEmpty()) Os.setenv("DM_COMMUNITY_LAST", communityLast, true); } catch (ErrnoException ignored) {}   // 社区信息拉取的最后一行，同上进 boot.log | the community fetch's last line, into boot.log likewise
        int result = nativeStart(getFilesDir().getAbsolutePath(), ext.getAbsolutePath(), src, Locale.getDefault().toLanguageTag(), BuildConfig.DM_VERSION_LABEL);
        if (result < 0) {
            int resource = switch (result) {
                case -2 -> R.string.dmAndroidMissingData;
                case -3 -> R.string.dmAndroidMissingModules;
                case -4 -> R.string.dmAndroidMissingShaders;
                case -5 -> R.string.dmAndroidMissingConfig;
                case -9 -> R.string.dmAndroidMissingOverlay;
                case -10 -> R.string.dmAndroidInvalidOverlay;
                default -> 0;
            };
            showStartError(resource == 0 ? getString(R.string.dmAndroidStartFailed, result) : getString(resource)); return;
        }
        started = true;
        touchOverlay.setInputEnabled(true);
        nativeFocus(3);
        modeHandler.removeCallbacks(modePoll); modeHandler.post(modePoll);
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
        // 首启生成 / 导入的加载页上吞掉返回与手柄 B（玩家按 B 习惯是「取消」，原来会把 App 切到后台、久了可能被系统清掉；离开仍可按 Home）
        // On the first-launch generation / import loading page, swallow BACK and controller B (players press B to cancel; it used to send the App to the
        // background, where the system may later clean it up; Home still leaves)
        // 加载页上的手柄（§2.7）：切到手柄模式；页脚显示时 A 键 = 复制群号 / 链接 | controller on the loading page (§2.7): switch to controller mode; with the footer shown, A = copy the number / link
        if (!started && (generating || importing || loadPageDemo) && loadPage != null && loadPage.isAttachedToWindow() && loadPage.padKey(e)) return true;
        if (!started && (generating || importing || loadPageDemo) && (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK || e.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_B)) return true;
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
    @Override protected void onResume() { super.onResume(); resumed = true; immersive(); if (!generating) genFailure = null; startHost(); updateRefresh(); if (started) { nativeFocus(3); if (touchOverlay != null && bootFailure == 0) touchOverlay.setInputEnabled(true); } }
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
