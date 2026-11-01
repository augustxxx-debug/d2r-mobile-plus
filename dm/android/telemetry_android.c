// 设备与使用统计的 Android 外壳（策略 dm/host/dm_telemetry.c；Java 侧 TelemetryBridge / TelemetryClient）。这里只有接线：设备信息、把探网络与发送交给 Java 线程、把结果交回公共状态机。
// Android shell of the device and usage statistics (policy: dm_telemetry.c; Java: TelemetryBridge / TelemetryClient). Wiring only: device facts, handing the probe and the send to Java threads, handing results back to the shared state machine.
#include <jni.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/system_properties.h>
#include "dm_telemetry.h"

static _Atomic int g_configured;
static JavaVM *g_vm; static jclass g_bridge; static jmethodID g_probe, g_challenge, g_send, g_forget;
static char g_ver[32], g_build[24], g_rev[48], g_chan[24], g_model[48], g_soc[40], g_os[32], g_loc[32], g_code[48];
static int te_configured(void) { return atomic_load(&g_configured); }
static void te_device(dm_tele_device_t *d) {
    d->platform = "android"; d->app_version = g_ver; d->build = g_build; d->source_revision = g_rev; d->channel = g_chan; d->model = g_model; d->soc = g_soc; d->os_version = g_os; d->locale = g_loc; d->device_code = g_code; d->font = "lxgw";
}
static JNIEnv *te_env(int *attached) { JNIEnv *env = NULL; *attached = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK; if (*attached && (*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return NULL; return env; }
static void te_probe(unsigned token) {
    int at; JNIEnv *env = te_env(&at); if (!env) { dm_telemetry_net(token, 1); return; }
    (*env)->CallStaticVoidMethod(env, g_bridge, g_probe, (jint)token);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); dm_telemetry_net(token, 1); }
    if (at) (*g_vm)->DetachCurrentThread(g_vm);
}
static void te_challenge(unsigned token) {
    int at; JNIEnv *env = te_env(&at); if (!env) { dm_telemetry_result(token, 9, "jni", 0); return; }
    (*env)->CallStaticVoidMethod(env, g_bridge, g_challenge, (jint)token);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); dm_telemetry_result(token, 9, "jni", 0); }
    if (at) (*g_vm)->DetachCurrentThread(g_vm);
}
static void te_send(unsigned token, const char *challenge, const char *body, size_t n) {
    int at; JNIEnv *env = te_env(&at); if (!env) { dm_telemetry_result(token, 9, "jni", 0); return; }
    jstring c = (*env)->NewStringUTF(env, challenge); jbyteArray a = (*env)->NewByteArray(env, (jsize)n);
    if (a && c) { (*env)->SetByteArrayRegion(env, a, 0, (jsize)n, (const jbyte *)body); (*env)->CallStaticVoidMethod(env, g_bridge, g_send, (jint)token, c, a); }
    if (a) (*env)->DeleteLocalRef(env, a); if (c) (*env)->DeleteLocalRef(env, c);
    if (!a || !c || (*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); dm_telemetry_result(token, 9, "jni", 0); }
    if (at) (*g_vm)->DetachCurrentThread(g_vm);
}
static void te_forget(void) {
    int at; JNIEnv *env = te_env(&at); if (!env) return;
    (*env)->CallStaticVoidMethod(env, g_bridge, g_forget); if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (at) (*g_vm)->DetachCurrentThread(g_vm);
}
static const dm_telemetry_shell_t k_shell = { te_configured, te_device, te_probe, te_challenge, te_send, NULL, te_forget, 1 };   // fetches_manifest = 1：普通启动不拉清单，探网络时现拉；没有 cancel：Java 线程靠 15 秒超时自行结束，迟到结果按 token 丢弃 | a normal start fetches no manifest, the probe does; no cancel: the Java thread ends by its 15 s timeouts, late results are dropped by token

JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TelemetryBridge_nativeConfigure(JNIEnv *env, jclass cls, jboolean configured) {
    if ((*env)->GetJavaVM(env, &g_vm) != JNI_OK) return;
    if (!g_bridge) { g_bridge = (*env)->NewGlobalRef(env, cls); g_probe = (*env)->GetStaticMethodID(env, cls, "probe", "(I)V"); g_challenge = (*env)->GetStaticMethodID(env, cls, "challenge", "(I)V"); g_send = (*env)->GetStaticMethodID(env, cls, "send", "(ILjava/lang/String;[B)V"); g_forget = (*env)->GetStaticMethodID(env, cls, "forget", "()V"); }
    atomic_store(&g_configured, configured && g_probe && g_challenge && g_send && g_forget);
    dm_telemetry_init(&k_shell);
}
static void te_copy(JNIEnv *env, jstring s, char *out, size_t n) { const char *c = s ? (*env)->GetStringUTFChars(env, s, NULL) : NULL; snprintf(out, n, "%s", c ? c : ""); if (c) (*env)->ReleaseStringUTFChars(env, s, c); }
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TelemetryBridge_nativeDevice(JNIEnv *env, jclass cls, jstring ver, jstring build, jstring rev, jstring chan, jstring model, jstring soc, jstring os, jstring loc, jstring code) {
    (void)cls; te_copy(env, ver, g_ver, sizeof g_ver); te_copy(env, build, g_build, sizeof g_build); te_copy(env, rev, g_rev, sizeof g_rev); te_copy(env, chan, g_chan, sizeof g_chan); te_copy(env, model, g_model, sizeof g_model); te_copy(env, soc, g_soc, sizeof g_soc); te_copy(env, os, g_os, sizeof g_os); te_copy(env, loc, g_loc, sizeof g_loc); te_copy(env, code, g_code, sizeof g_code);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TelemetryBridge_nativeNet(JNIEnv *env, jclass cls, jint token, jboolean metered) { (void)env; (void)cls; dm_telemetry_net((unsigned)token, metered); }
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TelemetryBridge_nativeResult(JNIEnv *env, jclass cls, jint token, jint outcome, jstring detail, jint retry_after) {
    (void)cls; const char *d = detail ? (*env)->GetStringUTFChars(env, detail, NULL) : NULL;
    dm_telemetry_result((unsigned)token, outcome, d ? d : "", retry_after);
    if (d) (*env)->ReleaseStringUTFChars(env, detail, d);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TelemetryBridge_nativeHttp(JNIEnv *env, jclass cls, jint token, jint status, jbyteArray body, jint retry_after) {
    (void)cls; jsize n = body ? (*env)->GetArrayLength(env, body) : 0; jbyte *b = n ? (*env)->GetByteArrayElements(env, body, NULL) : NULL;
    dm_telemetry_http((unsigned)token, status, (const char *)b, b ? (size_t)n : 0, retry_after);
    if (b) (*env)->ReleaseByteArrayElements(env, body, b, JNI_ABORT);
}
