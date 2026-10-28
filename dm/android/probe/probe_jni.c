// 探针 App 的 JNI 入口：在应用进程里跑 dm_probe_run，把整份报告交回 Java（Java 侧写文件、显示、打 logcat）。
// JNI entry of the probe App: runs dm_probe_run in the app process and hands the whole report back to Java (which writes the file, shows it and logs it).
#include "dm_probe.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>

JNIEXPORT jstring JNICALL Java_com_xiaoboluo_d2r_probe_ProbeActivity_nativeRun(JNIEnv *env, jclass cls, jstring scratch) {
    (void)cls;
    const char *s = scratch ? (*env)->GetStringUTFChars(env, scratch, NULL) : NULL;
    char *buf = NULL; size_t len = 0; FILE *f = open_memstream(&buf, &len);
    if (!f) return (*env)->NewStringUTF(env, "FAIL  open_memstream\nRESULT FAIL 1\n");
    dm_probe_run(f, s);
    fclose(f);
    if (s) (*env)->ReleaseStringUTFChars(env, scratch, s);
    // 报告只含 ASCII 与 UTF-8 中文；NewStringUTF 要 modified UTF-8，这里的内容没有 NUL 与代理对，直接用
    // The report is ASCII plus UTF-8 Chinese; NewStringUTF wants modified UTF-8, and the content has no NUL or surrogates, so it is used as is
    jstring r = (*env)->NewStringUTF(env, buf ? buf : "");
    free(buf);
    return r;
}
