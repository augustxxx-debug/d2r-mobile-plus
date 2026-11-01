// 自动上报崩溃的 Android 外壳（策略 dm/host/dm_crashreport.c；Java 侧 CrashReportBridge / DiagUploadClient）。这里只有接线：地址是否合格、把尝试交给 Java 线程、把结果交回公共状态机。系统退出原因（ApplicationExitInfo）由 DmActivity 写进 exit_reasons.txt，dm_sharelog_build 附进报告。
// Android shell of automatic crash reports (policy: dm_crashreport.c; Java: CrashReportBridge / DiagUploadClient). Wiring only: whether the origin is valid, handing an attempt to a Java thread, handing the result back to the shared state machine. System exit reasons (ApplicationExitInfo) are written by DmActivity to exit_reasons.txt and dm_sharelog_build attaches them.
#include <jni.h>
#include <android/log.h>
#include <stdatomic.h>
#include "dm_crashreport.h"
#include "dm_diagup.h"

static _Atomic int g_configured;
static JavaVM *g_vm; static jclass g_bridge; static jmethodID g_start, g_start_manual;
static int cr_configured(void) { return atomic_load(&g_configured); }
static void cr_start(unsigned token, const char *fence, const char *dir) {   // 游戏主线程调，立即返回 | called on the game main thread, returns at once
    JNIEnv *env = NULL; int attached = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK;
    if (attached && (*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) { dm_crashreport_result(token, 9, "jni", 0, 0); return; }
    jstring jf = (*env)->NewStringUTF(env, fence), jd = (*env)->NewStringUTF(env, dir);
    if (jf && jd) (*env)->CallStaticVoidMethod(env, g_bridge, g_start, (jint)token, jf, jd);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); dm_crashreport_result(token, 9, "jni", 0, 0); }
    if (jf) (*env)->DeleteLocalRef(env, jf); if (jd) (*env)->DeleteLocalRef(env, jd);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}
static const dm_crashreport_shell_t k_shell = { cr_configured, cr_start, NULL, NULL, 1 };   // checks_switch = 1：普通启动不拉清单，Java 每次尝试前现拉一次再判总开关（宿主这边只读缓存会一直停在暂停） | a normal start fetches no manifest, so Java fetches one before each attempt and checks the server switch (the host alone would stay paused on its cache)   // Java 线程有自己的超时（单次 15 秒），看门狗到点只作废 token | the Java thread has its own timeouts; the watchdog just retires the token

// 暂停菜单「分享诊断日志」的手动上传（状态机 dm/host/dm_diagup.c）：导出 = 原分享面板，复制 = 社区框同一条路（jni_android.c） | manual upload from Share Diagnostics (state machine dm_diagup.c): export = the original share sheet, copy = the community box's path (jni_android.c)
void dm_android_diag_export(void); int dm_android_diag_copy(const char *text);
static void up_start(unsigned token, unsigned job) {
    JNIEnv *env = NULL; int attached = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK;
    if (attached && (*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) { dm_diagup_result(token, DM_DIAGUP_PREPARE, "jni", 0, 0, 0, 0); return; }
    (*env)->CallStaticVoidMethod(env, g_bridge, g_start_manual, (jint)token, (jint)job);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); dm_diagup_result(token, DM_DIAGUP_PREPARE, "jni", 0, 0, 0, 0); }
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}
static const dm_diagup_shell_t k_up_shell = { cr_configured, up_start, NULL, dm_android_diag_export, dm_android_diag_copy };

JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_CrashReportBridge_nativeConfigure(JNIEnv *env, jclass cls, jboolean configured) {
    if ((*env)->GetJavaVM(env, &g_vm) != JNI_OK) return;
    if (!g_bridge) { g_bridge = (*env)->NewGlobalRef(env, cls); g_start = (*env)->GetStaticMethodID(env, cls, "start", "(ILjava/lang/String;Ljava/lang/String;)V"); g_start_manual = (*env)->GetStaticMethodID(env, cls, "startManual", "(II)V"); }
    atomic_store(&g_configured, configured && g_start && g_start_manual);
    void dm_update_note_only(void); dm_update_note_only();   // 结果提示用选角页那一行（dm_update.c） | the result notice uses the select-page line (dm_update.c)
    dm_crashreport_init(&k_shell); dm_diagup_init(&k_up_shell);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_CrashReportBridge_nativeResult(JNIEnv *env, jclass cls, jint token, jint outcome, jstring detail, jint flags, jint retry_after) {
    (void)cls; const char *d = detail ? (*env)->GetStringUTFChars(env, detail, NULL) : NULL;
    dm_crashreport_result((unsigned)token, outcome, d ? d : "", flags, retry_after);
    if (d) (*env)->ReleaseStringUTFChars(env, detail, d);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_CrashReportBridge_nativeManualResult(JNIEnv *env, jclass cls, jint token, jint outcome, jstring detail, jlong expires_at, jint flags, jint retry_after, jint retention) {
    (void)cls; const char *d = detail ? (*env)->GetStringUTFChars(env, detail, NULL) : NULL;
    dm_diagup_result((unsigned)token, outcome, d ? d : "", (long long)expires_at, flags, retry_after, retention);
    if (d) (*env)->ReleaseStringUTFChars(env, detail, d);
}
int dm_community_telemetry_only(const char *json, size_t len);   // dm_community.c：只取总开关 | the server switch only
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_CrashReportBridge_nativeManifest(JNIEnv *env, jclass cls, jbyteArray json) {
    (void)cls; if (!json) { dm_crashreport_switch_no_fetch(); return; }
    jsize n = (*env)->GetArrayLength(env, json); jbyte *b = (*env)->GetByteArrayElements(env, json, NULL); if (!b) return;
    dm_community_telemetry_only((const char *)b, (size_t)n); (*env)->ReleaseByteArrayElements(env, json, b, JNI_ABORT);
}
JNIEXPORT jboolean JNICALL Java_com_xiaoboluo_d2r_CrashReportBridge_nativePaused(JNIEnv *env, jclass cls, jlong wall) { (void)env; (void)cls; return dm_crashreport_switch_paused((long long)wall) ? JNI_TRUE : JNI_FALSE; }
int dm_android_build_report(const char *out_path);   // jni_android.c
JNIEXPORT jboolean JNICALL Java_com_xiaoboluo_d2r_CrashReportBridge_nativeBuild(JNIEnv *env, jclass cls, jstring out) {
    (void)cls; const char *p = (*env)->GetStringUTFChars(env, out, NULL); int ok = p && dm_android_build_report(p);
    if (p) (*env)->ReleaseStringUTFChars(env, out, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}
