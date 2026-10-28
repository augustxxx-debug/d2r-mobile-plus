// 探针 App：启动即在后台线程跑原生探针（libdmprobe.so，应用身份），报告写到 files/probe.txt 与外部私有目录 files/probe.txt（adb pull 取），打 logcat（标签 dmprobe），并在屏幕上显示。
// 报告头部附 Java 侧能拿到的身份：型号、系统、本 APK 的 SHA-256（支持记录里的「包哈希」）。不含用户名、账号、路径以外的个人信息。
// Probe App: on start it runs the native probe (libdmprobe.so, app identity) on a background thread, writes the report to files/probe.txt and the external private files/probe.txt
// (fetch with adb pull), logs it (tag dmprobe) and shows it on screen. The header adds the identity Java can see: model, system, this APK's SHA-256 (the "package hash" of a support record).
package com.xiaoboluo.d2r.probe;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

public class ProbeActivity extends Activity {
    static { System.loadLibrary("dmprobe"); }
    static native String nativeRun(String scratchDir);
    private TextView text;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ScrollView sv = new ScrollView(this); text = new TextView(this); text.setTextSize(11); text.setPadding(24, 48, 24, 48); sv.addView(text); setContentView(sv);
        text.setText("探针运行中…");
        new Thread(() -> {
            String head = header();
            String body;
            try { body = nativeRun(getFilesDir().getAbsolutePath()); } catch (Throwable t) { body = "FAIL  nativeRun: " + t + "\nRESULT FAIL 1\n"; }
            final String report = head + body;
            for (String line : report.split("\n")) Log.i("dmprobe", line);
            write(new File(getFilesDir(), "probe.txt"), report);
            File ext = getExternalFilesDir(null); if (ext != null) write(new File(ext, "probe.txt"), report);
            runOnUiThread(() -> text.setText(report));
        }, "dm-probe").start();
    }

    private String header() {
        StringBuilder b = new StringBuilder();
        b.append("KV    java.model=").append(Build.MODEL).append('\n');
        b.append("KV    java.manufacturer=").append(Build.MANUFACTURER).append('\n');
        b.append("KV    java.device=").append(Build.DEVICE).append('\n');
        b.append("KV    java.hardware=").append(Build.HARDWARE).append('\n');
        b.append("KV    java.soc=").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        b.append("KV    java.release=").append(Build.VERSION.RELEASE).append(" sdk=").append(Build.VERSION.SDK_INT).append('\n');
        b.append("KV    java.fingerprint=").append(Build.FINGERPRINT).append('\n');
        b.append("KV    java.security_patch=").append(Build.VERSION.SECURITY_PATCH).append('\n');
        b.append("KV    java.abis=").append(String.join(",", Build.SUPPORTED_ABIS)).append('\n');
        try { ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE); ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo(); am.getMemoryInfo(mi);
            b.append("KV    java.total_mem_mib=").append(mi.totalMem >> 20).append('\n'); b.append("KV    java.memory_class_mb=").append(am.getMemoryClass()).append(" large=").append(am.getLargeMemoryClass()).append('\n');
            b.append("KV    java.low_ram_device=").append(am.isLowRamDevice()).append('\n'); } catch (Throwable ignored) {}
        b.append("KV    java.apk_sha256=").append(apkSha256()).append('\n');
        b.append("KV    java.target_sdk=").append(getApplicationInfo().targetSdkVersion).append('\n');
        return b.toString();
    }
    private String apkSha256() {
        try (FileInputStream in = new FileInputStream(getApplicationInfo().sourceDir)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256"); byte[] buf = new byte[65536]; int n; while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            StringBuilder s = new StringBuilder(); for (byte x : md.digest()) s.append(String.format("%02x", x)); return s.toString();
        } catch (Exception e) { return "?"; }
    }
    private static void write(File f, String s) { try (FileOutputStream out = new FileOutputStream(f)) { out.write(s.getBytes(StandardCharsets.UTF_8)); } catch (Exception e) { Log.w("dmprobe", "write " + f + ": " + e); } }
}
