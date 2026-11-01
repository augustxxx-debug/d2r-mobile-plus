package com.xiaoboluo.d2r;

import android.content.Context;
import android.os.Build;
import android.provider.Settings;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

// 设备与使用统计的 Android 接线（策略 dm/host/dm_telemetry.c，C 外壳 dm/android/telemetry_android.c，协议 TelemetryClient）。地址同自动上报崩溃（BuildConfig.DM_DIAGNOSTIC_ORIGIN，源码里没有）；渠道 BuildConfig.DM_CHANNEL 出包注入，源码缺省 unknown。设备码 = ANDROID_ID，不取 IMEI / 序列号 / 广告标识。M0 只有前台闲时通道，WorkManager 留后续。
// Android wiring of the device and usage statistics (policy: dm_telemetry.c; C shell: telemetry_android.c; protocol: TelemetryClient). The same origin as crash reports (no address in the source); channel injected at packaging, unknown in the source. Device code = ANDROID_ID, never IMEI / serial / advertising id. M0 has only the foreground idle channel; WorkManager comes later.
final class TelemetryBridge {
    private static Context app;
    private TelemetryBridge() {}
    static native void nativeConfigure(boolean configured);
    static native void nativeDevice(String appVersion, String build, String revision, String channel, String model, String soc, String osVersion, String locale, String deviceCode);
    static native void nativeNet(int token, boolean metered);
    static native void nativeResult(int token, int outcome, String detail, int retryAfter);
    static native void nativeHttp(int token, int status, byte[] body, int retryAfter);

    static void init(Context context) {
        app = context.getApplicationContext();
        String id = Settings.Secure.getString(app.getContentResolver(), Settings.Secure.ANDROID_ID);
        nativeDevice(BuildConfig.VERSION_NAME, String.valueOf(BuildConfig.VERSION_CODE), BuildConfig.DM_SOURCE_COMMIT, BuildConfig.DM_CHANNEL, Build.MANUFACTURER + " " + Build.MODEL, Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : "", Build.VERSION.RELEASE, Locale.getDefault().toLanguageTag(), id != null ? id : "");
        nativeConfigure(DiagUploadClient.validOrigin(BuildConfig.DM_DIAGNOSTIC_ORIGIN, BuildConfig.DEBUG));
    }
    private static boolean metered() { try { android.net.ConnectivityManager cm = app.getSystemService(android.net.ConnectivityManager.class); return cm == null || cm.isActiveNetworkMetered(); } catch (RuntimeException e) { return true; } }   // 读不出来按计量算：只会少发 | unreadable counts as metered: only ever sends less
    // 普通启动不拉清单：探网络时现拉一次看总开关（CrashReportBridge 的 native 共用） | a normal start fetches no manifest: fetch one here for the server switch
    static void probe(int token) {
        Thread t = new Thread(() -> {
            CrashReportBridge.nativeManifest(DiagUploadClient.fetchManifest(CommunityFetch.urls(BuildConfig.DM_UPDATE_URLS), CommunityFetch.urlConnection("D2RMobilePlus-Android/" + BuildConfig.VERSION_NAME)));
            nativeNet(token, metered());
        }, "dm-telemetry");
        t.setPriority(Thread.MIN_PRIORITY); t.start();
    }
    private static DiagUploadClient.Http http() { return DiagUploadClient.urlConnection(BuildConfig.DM_DIAGNOSTIC_ORIGIN, "D2R-Mobile-Plus-Telemetry/1", "telemetry/v1/"); }
    private static void bg(Runnable r) { Thread t = new Thread(r, "dm-telemetry"); t.setPriority(Thread.MIN_PRIORITY); t.start(); }
    static void challenge(int token) {
        bg(() -> {
            try { DiagUploadClient.Response r = http().send("GET", "challenge", null); nativeHttp(token, r.code, r.body, r.retryAfter); }
            catch (IOException e) { nativeResult(token, DiagUploadClient.UNAVAILABLE, "network", 0); }
        });
    }
    static void send(int token, String challenge, byte[] body) {
        bg(() -> {
            byte[] seed;
            try { seed = DiagUploadClient.loadOrCreateSeed(new File(app.getNoBackupFilesDir(), "tele_identity.key")); }
            catch (IOException e) { nativeResult(token, DiagUploadClient.PREPARE, "identity", 0); return; }
            try { DiagUploadClient.Response r = http().send("POST", "events", TelemetryClient.envelope(seed, challenge, body)); nativeHttp(token, r.code, r.body, r.retryAfter); }
            catch (IOException e) { nativeResult(token, DiagUploadClient.UNCONFIRMED, "response_lost", 0); }
        });
    }
    static void forget() { new File(app.getNoBackupFilesDir(), "tele_identity.key").delete(); }
}
