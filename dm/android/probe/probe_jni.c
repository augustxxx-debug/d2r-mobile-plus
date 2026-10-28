// 探针 App 的 JNI 入口：在应用进程里跑 dm_probe_run，把整份报告交回 Java（Java 侧写文件、显示、打 logcat）。
// JNI entry of the probe App: runs dm_probe_run in the app process and hands the whole report back to Java (which writes the file, shows it and logs it).
#include "dm_probe.h"
#include <android/log.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>

// 探针输出直接写进追加模式打开的报告文件（dm_probe_run 把它设成无缓冲）：驱动把进程弄崩时，已跑过的项不丢
// Probe output goes straight into the report file opened for appending (dm_probe_run makes it unbuffered): nothing already run is lost when the driver crashes the process
// report：报告文件路径（追加；Java 侧先写头部）；返回失败数 | report: the report file path (appended; Java writes the header first); returns the failure count
JNIEXPORT jint JNICALL Java_com_xiaoboluo_d2r_probe_ProbeActivity_nativeRun(JNIEnv *env, jclass cls, jstring scratch, jstring ext, jstring report) {
    (void)cls;
    const char *s = scratch ? (*env)->GetStringUTFChars(env, scratch, NULL) : NULL;
    const char *x = ext ? (*env)->GetStringUTFChars(env, ext, NULL) : NULL;
    const char *rp = report ? (*env)->GetStringUTFChars(env, report, NULL) : NULL;
    FILE *f = rp ? fopen(rp, "a") : NULL;
    int rc = f ? dm_probe_run(f, s, x) : 1;
    if (f) fclose(f); else __android_log_write(ANDROID_LOG_ERROR, "dmprobe", "report file could not be opened");
    if (s) (*env)->ReleaseStringUTFChars(env, scratch, s);
    if (x) (*env)->ReleaseStringUTFChars(env, ext, x);
    if (rp) (*env)->ReleaseStringUTFChars(env, report, rp);
    return rc;
}
