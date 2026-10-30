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
                r = DiagUploadClient.attempt(new File(dir),
                        DiagUploadClient.urlConnection(BuildConfig.DM_DIAGNOSTIC_ORIGIN, "D2R-Mobile-Plus-Diagnostics/1"), seed,
                        DiagUploadClient.metadata(BuildConfig.VERSION_NAME, String.valueOf(BuildConfig.VERSION_CODE), BuildConfig.DM_VERSION_LABEL, Build.VERSION.RELEASE, Locale.getDefault().toLanguageTag()),
                        () -> collect(fence));
            } catch (IOException e) { r = new DiagUploadClient.Result(DiagUploadClient.PREPARE, e.getMessage() != null && e.getMessage().startsWith("key_") ? e.getMessage() : "identity", 0, 0); }
            nativeResult(token, r.outcome, r.detail, r.flags, r.retryAfter);
        }, "dm-crash-report");
        t.setPriority(Thread.MIN_PRIORITY); t.start();
    }
    private static String collect(String fence) throws IOException {
        File out = new File(app.getCacheDir(), "crash_report_build.txt");
        try {
            if (!nativeBuild(out.getPath())) throw new IOException("build");
            String body = new String(Files.readAllBytes(out.toPath()), StandardCharsets.UTF_8);
            return DiagUploadClient.scrubUpload("D2R Mobile Plus crash report (automatic)\n" + fence + "\n\n" + body);
        } finally { out.delete(); }
    }
}
