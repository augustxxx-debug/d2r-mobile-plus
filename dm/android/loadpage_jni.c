// 加载页的安卓胶水：把 dm/host/dm_loadpage.c（两端共用的状态机 / 进度合成 / 文案 / 布局）包成一个很小的独立库 libdmload.so，给 LoadPageView.java 用。
// 为什么单独一个库：导入包还没拷进来之前宿主库 libdm.so 装不了（它加载时读 derived.tsv），而加载页正是在那之前显示的。
// 时间用 CLOCK_MONOTONIC 毫秒；语言由 Java 按系统语言给（dm_lang_from_tag → 语言组），不读存档设置。
// Android glue for the loading page: wraps dm/host/dm_loadpage.c (state machine / progress / text / layout shared by both platforms) into a tiny standalone library libdmload.so for LoadPageView.java.
// Why a separate library: the host library libdm.so cannot be loaded before the import pack is copied in (it reads derived.tsv at load time), and the loading page is shown exactly before that.
// Time is CLOCK_MONOTONIC milliseconds; the language is given by Java from the system locale (dm_lang_from_tag → language group), the save's settings are not read.
#include <jni.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "dm_loadpage.h"
#include "dm_i18n.h"
#include "dm_manifest.h"

static uint64_t now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull; }
#define F(name) JNIEXPORT JNICALL Java_com_xiaoboluo_d2r_LoadPageView_##name

void F(nativeLang)(JNIEnv *env, jclass cls, jstring tag) {
    (void)cls; const char *t = tag ? (*env)->GetStringUTFChars(env, tag, NULL) : NULL;
    dm_loadpage_ui_set_group(dm_lang_group_of(dm_lang_from_tag(t ? t : "")));
    if (t) (*env)->ReleaseStringUTFChars(env, tag, t);
}
void F(nativeBegin)(JNIEnv *env, jclass cls, jintArray kinds, jintArray weights) {
    (void)cls; int k[DM_LP_MAX_STAGES], w[DM_LP_MAX_STAGES]; int n = kinds ? (*env)->GetArrayLength(env, kinds) : 0; if (n > DM_LP_MAX_STAGES) n = DM_LP_MAX_STAGES;
    if (n) (*env)->GetIntArrayRegion(env, kinds, 0, n, k);
    int hw = weights && (*env)->GetArrayLength(env, weights) >= n; if (hw && n) (*env)->GetIntArrayRegion(env, weights, 0, n, w);
    dm_loadpage_begin(k, n, hw ? w : NULL, now_ms());
}
void F(nativeSet)(JNIEnv *env, jclass cls, jint stage, jdouble frac, jlong items_done, jlong items_total, jlong bytes_done, jlong bytes_total) {
    (void)env; (void)cls; dm_lp_detail d = { items_done, items_total, bytes_done, bytes_total }; dm_loadpage_set(stage, frac, &d, now_ms());
}
void F(nativePrompt)(JNIEnv *env, jclass cls, jstring title_key, jstring body, jstring sub_key) {   // 提示页（权限）：键要常驻，复制进静态缓冲 | notice page (permission): keys must stay valid, so copy them into static buffers
    (void)cls; static char tk[64], sk[64]; const char *a = title_key ? (*env)->GetStringUTFChars(env, title_key, NULL) : NULL, *b = body ? (*env)->GetStringUTFChars(env, body, NULL) : NULL, *c = sub_key ? (*env)->GetStringUTFChars(env, sub_key, NULL) : NULL;
    snprintf(tk, sizeof tk, "%s", a ? a : ""); snprintf(sk, sizeof sk, "%s", c ? c : "");
    dm_loadpage_prompt(tk, b, c ? sk : NULL);
    if (a) (*env)->ReleaseStringUTFChars(env, title_key, a); if (b) (*env)->ReleaseStringUTFChars(env, body, b); if (c) (*env)->ReleaseStringUTFChars(env, sub_key, c);
}
void F(nativeNote)(JNIEnv *env, jclass cls, jstring text) {
    (void)cls; const char *t = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL; dm_loadpage_note(t); if (t) (*env)->ReleaseStringUTFChars(env, text, t);
}
void F(nativeFail)(JNIEnv *env, jclass cls, jstring reason) {
    (void)cls; const char *t = reason ? (*env)->GetStringUTFChars(env, reason, NULL) : NULL; dm_loadpage_fail(t ? t : "", now_ms()); if (t) (*env)->ReleaseStringUTFChars(env, reason, t);
}
void F(nativeFinish)(JNIEnv *env, jclass cls) { (void)env; (void)cls; dm_loadpage_finish(now_ms()); }
jint F(nativeState)(JNIEnv *env, jclass cls) { (void)env; (void)cls; return dm_loadpage_state(); }
jint F(nativePercent)(JNIEnv *env, jclass cls) { (void)env; (void)cls; return dm_loadpage_percent(); }
jboolean F(nativeMuted)(JNIEnv *env, jclass cls) { (void)env; (void)cls; return dm_loadpage_muted() ? JNI_TRUE : JNI_FALSE; }
void F(nativeSetMuted)(JNIEnv *env, jclass cls, jboolean m) { (void)env; (void)cls; dm_loadpage_set_muted(m); }
jboolean F(nativeToggleMuted)(JNIEnv *env, jclass cls) { (void)env; (void)cls; return dm_loadpage_toggle_muted() ? JNI_TRUE : JNI_FALSE; }
jstring F(nativeUi)(JNIEnv *env, jclass cls, jstring key) { (void)cls; const char *k = (*env)->GetStringUTFChars(env, key, NULL); jstring r = (*env)->NewStringUTF(env, dm_loadpage_ui(k)); (*env)->ReleaseStringUTFChars(env, key, k); return r; }
jboolean F(nativeMusicWanted)(JNIEnv *env, jclass cls) { (void)env; (void)cls; return dm_loadpage_music_wanted() ? JNI_TRUE : JNI_FALSE; }
jstring F(nativeMusicName)(JNIEnv *env, jclass cls) { (void)cls; return (*env)->NewStringUTF(env, dm_loadpage_music_name()); }
jstring F(nativeLogoName)(JNIEnv *env, jclass cls) { (void)cls; return (*env)->NewStringUTF(env, dm_loadpage_logo_name(NULL, NULL)); }
jstring F(nativeBgName)(JNIEnv *env, jclass cls, jfloat w, jfloat h) { (void)cls; return (*env)->NewStringUTF(env, dm_loadpage_bg_name(w, h, NULL, NULL)); }
// 布局摊平成 double[70]：landscape, bg_w, bg_h, bg_src×4, bg_dst×4, bar×4, bar_fill×4, title×4, percent×4, body×4, sub×4, brand×4, title_pt, body_pt, sub_pt, brand_pt, bar_radius,
//   logo×4, logo_w, logo_h, mute×4, comm, comm1×4, comm2×4, copy×4, comm_pt, comm_alpha, show_bar（顺序对应 LoadPageView.java 的常量）
// Layout flattened into double[70] (order matches the constants in LoadPageView.java)
jdoubleArray F(nativeLayout)(JNIEnv *env, jclass cls, jfloat w, jfloat h, jfloat l, jfloat t, jfloat r, jfloat b) {
    (void)cls; dm_lp_screen s = { w, h, l, t, r, b }; dm_lp_layout L; dm_loadpage_layout(&s, &L);
    const dm_lp_rect *rects[] = { &L.bg_src, &L.bg_dst, &L.bar, &L.bar_fill, &L.title, &L.percent, &L.body, &L.sub, &L.brand };
    double v[72]; int o = 0; v[o++] = L.landscape; v[o++] = L.bg_w; v[o++] = L.bg_h;
    for (unsigned i = 0; i < sizeof rects / sizeof *rects; i++) { v[o++] = rects[i]->x; v[o++] = rects[i]->y; v[o++] = rects[i]->w; v[o++] = rects[i]->h; }
    v[o++] = L.title_pt; v[o++] = L.body_pt; v[o++] = L.sub_pt; v[o++] = L.brand_pt; v[o++] = L.bar_radius;
    v[o++] = L.logo.x; v[o++] = L.logo.y; v[o++] = L.logo.w; v[o++] = L.logo.h; v[o++] = L.logo_w; v[o++] = L.logo_h;
    v[o++] = L.mute.x; v[o++] = L.mute.y; v[o++] = L.mute.w; v[o++] = L.mute.h;
    v[o++] = L.comm; const dm_lp_rect *cr[] = { &L.comm1, &L.comm2, &L.copy };   // 社区页脚：标志 + comm1×4 + comm2×4 + copy×4 | community footer: flag + comm1×4 + comm2×4 + copy×4
    for (unsigned i = 0; i < 3; i++) { v[o++] = cr[i]->x; v[o++] = cr[i]->y; v[o++] = cr[i]->w; v[o++] = cr[i]->h; }
    v[o++] = L.comm_pt; v[o++] = L.comm_alpha; v[o++] = L.show_bar;
    jdoubleArray a = (*env)->NewDoubleArray(env, o); (*env)->SetDoubleArrayRegion(env, a, 0, o, v); return a;
}
jobjectArray F(nativeText)(JNIEnv *env, jclass cls) {   // { 标题, 百分数, 正文, 副文, 字标, 社区第一行, 社区第二行, 复制钮, 复制状态 } | { title, percent, body, sub, brand, community 1, community 2, copy button, copy state }
    (void)cls; dm_lp_text t; dm_loadpage_text(&t, now_ms()); char cs[4]; snprintf(cs, sizeof cs, "%d", t.copy_state);
    const char *parts[9] = { t.title, t.percent, t.body, t.sub, t.brand, t.comm1, t.comm2, t.copy, cs };
    jobjectArray a = (*env)->NewObjectArray(env, 9, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < 9; i++) (*env)->SetObjectArrayElement(env, a, i, (*env)->NewStringUTF(env, parts[i]));
    return a;
}
// 社区信息（dm_loadpage.h）与清单校验（dm_manifest.h） | community info (dm_loadpage.h) and manifest validation (dm_manifest.h)
// nativeManifest：{ "1" 合法 / "0" 不合法, 原因, 群号, 链接, build, 官网 } | nativeManifest: { "1" valid / "0" invalid, reason, group number, link, build, site }
jobjectArray F(nativeManifest)(JNIEnv *env, jclass cls, jbyteArray data) {
    (void)cls; dm_manifest m; memset(&m, 0, sizeof m); char why[192] = "", b[16] = ""; int ok = 0;
    jsize n = data ? (*env)->GetArrayLength(env, data) : 0;
    if (n <= 0) snprintf(why, sizeof why, "空内容");
    else if (n > DM_MANIFEST_MAX) snprintf(why, sizeof why, "超过 %d 字节（%d）", DM_MANIFEST_MAX, (int)n);
    else { char buf[DM_MANIFEST_MAX]; (*env)->GetByteArrayRegion(env, data, 0, n, (jbyte *)buf); ok = dm_manifest_parse(buf, (size_t)n, &m, why, sizeof why); }
    if (ok) snprintf(b, sizeof b, "%d", m.build);
    const char *parts[6] = { ok ? "1" : "0", why, ok ? m.qq : "", ok ? m.github : "", b, ok ? m.site : "" };
    jobjectArray a = (*env)->NewObjectArray(env, 6, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < 6; i++) (*env)->SetObjectArrayElement(env, a, i, (*env)->NewStringUTF(env, parts[i]));
    return a;
}
void F(nativeCommunity)(JNIEnv *env, jclass cls, jstring qq, jstring gh, jstring site) {
    (void)cls; const char *q = qq ? (*env)->GetStringUTFChars(env, qq, NULL) : NULL, *g = gh ? (*env)->GetStringUTFChars(env, gh, NULL) : NULL, *w = site ? (*env)->GetStringUTFChars(env, site, NULL) : NULL;
    dm_loadpage_community(q, g, w);
    if (q) (*env)->ReleaseStringUTFChars(env, qq, q); if (g) (*env)->ReleaseStringUTFChars(env, gh, g); if (w) (*env)->ReleaseStringUTFChars(env, site, w);
}
jstring F(nativeCopyValue)(JNIEnv *env, jclass cls) { (void)cls; return (*env)->NewStringUTF(env, dm_loadpage_copy_value()); }
void F(nativeCopied)(JNIEnv *env, jclass cls, jboolean ok, jboolean pad) { (void)env; (void)cls; dm_loadpage_copied(ok, pad, now_ms()); }
void F(nativePad)(JNIEnv *env, jclass cls, jboolean on) { (void)env; (void)cls; dm_loadpage_pad(on); }
jboolean F(nativePadMode)(JNIEnv *env, jclass cls) { (void)env; (void)cls; return dm_loadpage_pad_mode() ? JNI_TRUE : JNI_FALSE; }
jint F(nativeColor)(JNIEnv *env, jclass cls, jint which) {   // 颜色统一取自 dm_loadpage.h 的枚举 | colours come from the enum in dm_loadpage.h
    (void)env; (void)cls; static const int c[] = { DM_LP_COL_TITLE, DM_LP_COL_BODY, DM_LP_COL_SUB, DM_LP_COL_PERCENT, DM_LP_COL_BAR_FILL, DM_LP_COL_BAR_TRACK, DM_LP_COL_BAR_EDGE };
    return which >= 0 && which < (int)(sizeof c / sizeof *c) ? c[which] : 0xffffff;
}
jstring F(nativeLastLine)(JNIEnv *env, jclass cls) { (void)cls; return (*env)->NewStringUTF(env, dm_loadpage_last_line()); }   // 进游戏前交给宿主写进 boot.log | handed to the host before the game starts, for boot.log
