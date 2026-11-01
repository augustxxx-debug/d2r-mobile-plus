package com.xiaoboluo.d2r;

import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;

// 自动上报崩溃的 Android 接线（策略 dm/host/dm_crashreport.c，C 外壳 dm/android/crashreport_android.c，协议 DiagUploadClient）。地址不进源码：出包时 DM_DIAGNOSTIC_ORIGIN → gradle -PdmDiagnosticOrigin → BuildConfig.DM_DIAGNOSTIC_ORIGIN；没配 = 不联网。
// Android wiring for automatic crash reports (policy: dm_crashreport.c; C shell: crashreport_android.c; protocol: DiagUploadClient). No address in the source: DM_DIAGNOSTIC_ORIGIN → gradle -PdmDiagnosticOrigin → BuildConfig; none = no network.
final class CrashReportBridge {
    private static Context app;
    private CrashReportBridge() {}
    static native void nativeConfigure(boolean configured);
    static native void nativeResult(int token, int outcome, String detail, int flags, int retryAfter);
    static native void nativeManifest(byte[] json);      // 总开关（dm_crashreport.h）：拉到的清单；null = 没拉到，沿用缓存 | the server switch: the fetched manifest; null = not fetched, keep the cache
    static native boolean nativePaused(long wall);
    static native void nativeManualResult(int token, int outcome, String detail, long expiresAt, int flags, int retryAfter, int retention);
    static native boolean nativeBuild(String outPath);   // 组装 + 导出脱敏（sharelog_android.c 同一份）写到 outPath | assemble + export scrub into outPath

    static void init(Context context) {
        app = context.getApplicationContext();
        nativeConfigure(DiagUploadClient.validOrigin(BuildConfig.DM_DIAGNOSTIC_ORIGIN, BuildConfig.DEBUG));
    }
    // C 外壳从游戏主线程调，立即返回；一次只有一个尝试（dm_crashreport.c 保证）| called by the C shell on the game main thread, returns at once; one attempt at a time
    static void start(int token, String fence, String dir) {
        Thread t = new Thread(() -> {
            DiagUploadClient.Result r;
            try {
                File noBackup = app.getNoBackupFilesDir();
                byte[] seed = DiagUploadClient.loadOrCreateSeed(new File(noBackup, "diag_identity.key"));
                java.util.Map<String, String> meta = meta();
                DiagUploadClient.Collect col = () -> collect(fence);
                DiagUploadClient.prepare(new File(dir), meta, col);   // 先打包存盘：上一局日志下次启动就被覆盖，暂停时也要留 | pack first: the previous log is overwritten at the next launch, also while paused
                // 普通启动不拉清单（DmActivity），这里现拉一次看总开关；拉不到沿用缓存 | a normal start fetches no manifest, so fetch one here for the server switch; not fetched = the cache
                nativeManifest(DiagUploadClient.fetchManifest(CommunityFetch.urls(BuildConfig.DM_UPDATE_URLS), CommunityFetch.urlConnection("D2RMobilePlus-Android/" + BuildConfig.VERSION_NAME)));
                if (nativePaused(System.currentTimeMillis() / 1000)) r = new DiagUploadClient.Result(DiagUploadClient.MAINTENANCE, "switch_off", 0, 0);
                else r = DiagUploadClient.attempt(new File(dir), DiagUploadClient.urlConnection(BuildConfig.DM_DIAGNOSTIC_ORIGIN, "D2R-Mobile-Plus-Diagnostics/1"), seed, meta, col, metered());
            } catch (IOException e) { r = new DiagUploadClient.Result(DiagUploadClient.PREPARE, e.getMessage() != null && e.getMessage().startsWith("key_") ? e.getMessage() : "identity", 0, 0); }
            nativeResult(token, r.outcome, r.detail, r.flags, r.retryAfter);
        }, "dm-crash-report");
        t.setPriority(Thread.MIN_PRIORITY); t.start();
    }
    // 手动上传（dm_diagup.c 的一次尝试）：同一 job = 重试，发盘上同一份；看门狗放弃后迟到的线程排在锁后面，结果按 token 丢弃 | manual upload (one attempt of dm_diagup.c): the same job = a retry of the stored report; a late thread after the watchdog waits on the lock, and its result is dropped by token
    private static final Object MANUAL_LOCK = new Object();
    private static int manualJob = -1;
    static void startManual(int token, int job) {
        Thread t = new Thread(() -> {
            DiagUploadClient.Result r;
            synchronized (MANUAL_LOCK) {
                File dir = new File(app.getFilesDir(), "diag_manual");
                try {
                    if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("dir");
                    if (job != manualJob) { DiagUploadClient.clear(dir); manualJob = job; }
                    byte[] seed = DiagUploadClient.loadOrCreateSeed(new File(app.getNoBackupFilesDir(), "diag_identity.key"));
                    r = DiagUploadClient.attemptManual(dir, DiagUploadClient.urlConnection(BuildConfig.DM_DIAGNOSTIC_ORIGIN, "D2R-Mobile-Plus-Diagnostics/1"), seed, meta(), () -> collect(null));
                    if (r.outcome == DiagUploadClient.OK) DiagUploadClient.clear(dir);
                } catch (IOException e) { r = new DiagUploadClient.Result(DiagUploadClient.PREPARE, e.getMessage() != null && e.getMessage().startsWith("key_") ? e.getMessage() : "identity", 0, 0); }
            }
            nativeManualResult(token, r.outcome, r.detail, r.expiresAt, r.flags, r.retryAfter, r.retention);
        }, "dm-diag-upload");
        t.setPriority(Thread.MIN_PRIORITY); t.start();
    }
    // 计量网络（蜂窝等）只发摘要版；读不出来按计量算，只会少发 | metered networks (cellular etc.) only get the summary; unreadable counts as metered, which only sends less
    private static boolean metered() { try { android.net.ConnectivityManager cm = app.getSystemService(android.net.ConnectivityManager.class); return cm == null || cm.isActiveNetworkMetered(); } catch (RuntimeException e) { return true; } }
    private static java.util.Map<String, String> meta() { return DiagUploadClient.metadata(BuildConfig.VERSION_NAME, String.valueOf(BuildConfig.VERSION_CODE), BuildConfig.DM_VERSION_LABEL, Build.VERSION.RELEASE, Locale.getDefault().toLanguageTag()); }
    private static String collect(String fence) throws IOException {
        File out = new File(app.getCacheDir(), "crash_report_build.txt");
        try {
            if (!nativeBuild(out.getPath())) throw new IOException("build");
            String body = new String(Files.readAllBytes(out.toPath()), StandardCharsets.UTF_8);
            return DiagUploadClient.scrubUpload(fence == null ? body : "D2R Mobile Plus crash report (automatic)\n" + fence + "\n\n" + body);
        } finally { out.delete(); }
    }
}
