#include <jni.h>
#include "touch_android.h"
#include "viewport.h"
#include "viewport_android.h"

/* 使用命名JNI入口，与宿主已有JNI_OnLoad兼容。 | Named JNI entry points coexist with the host's JNI_OnLoad. */
#include "pad_android.h"
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeEvent(JNIEnv *env, jclass cls, jint phase, jint slot, jfloat x, jfloat y) {
    (void)env; (void)cls;
    if (phase == 0) dm_android_pad_touched();   /* 碰了屏幕 → 触屏模式（同 iOS dm_gamepad_touched）| the screen was touched -> touch mode (as iOS dm_gamepad_touched) */
    dm_android_touch_event(phase,slot,x,y);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeResize(JNIEnv *env, jclass cls, jfloat width, jfloat height, jfloat left, jfloat right, jfloat bottom, jboolean tablet) {
    (void)env; (void)cls;
    dm_android_touch_tablet(tablet);
    dm_android_touch_resize(width,height,left,right,bottom);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeLayout(JNIEnv *env, jclass cls, jfloatArray dst) {
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env,dst)<DM_ANDROID_TOUCH_DRAW_FLOATS) return;
    float data[DM_ANDROID_TOUCH_DRAW_FLOATS];
    dm_android_touch_layout(data);
    (*env)->SetFloatArrayRegion(env,dst,0,DM_ANDROID_TOUCH_DRAW_FLOATS,data);
}
/* 画面在窗口里的矩形（窗口像素）：{x, y, w, h}。返回 false = 还没呈现过，按整个视图算（今天的行为）。
   控件的摆位与按下都按它换算，黑边上的按下不占手指。
   The picture's rectangle inside the window, in window pixels: {x, y, w, h}. False = nothing presented yet, use the
   whole view (today's behaviour). Controls are placed and presses converted through it, and a press on the black
   bars takes no finger. */
JNIEXPORT jboolean JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeViewport(JNIEnv *env, jclass cls, jfloatArray out) {
    (void)cls;
    int x=0,y=0,w=0,h=0;
    if (!out || (*env)->GetArrayLength(env,out)<4) return JNI_FALSE;
    if (!dm_android_vk_viewport(&x,&y,&w,&h)) return JNI_FALSE;
    float v[4]={(float)x,(float)y,(float)w,(float)h};
    (*env)->SetFloatArrayRegion(env,out,0,4,v);
    return JNI_TRUE;
}
/* 窗口点 → 画面矩形内的点（窗口像素 → 矩形内像素）。p = {窗口 x, 窗口 y}，回来填 {矩形内 x, 矩形内 y, 落在画面里}，
   夹进矩形（拖出画面的手指落在最近的边上）。换算只有 dm_vp_map 一处，与显示的 blit 矩形同一个来源；黑边上的
   按下由 Java 看第三个值丢掉。
   Window point -> point inside the picture rectangle (window px -> px inside the rectangle). p = {window x, window y};
   the call fills {x inside the rectangle, y inside the rectangle, inside}. Clamped into the rectangle (a finger dragged
   out lands on the nearest edge). The conversion is dm_vp_map only, the same source as the display's blit rectangle;
   Java drops a press on the bars by the third value. */
JNIEXPORT jboolean JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeMap(JNIEnv *env, jclass cls, jfloatArray p) {
    (void)cls;
    int x=0,y=0,w=0,h=0; float v[2]={0,0};
    if (!p || (*env)->GetArrayLength(env,p)<5) return JNI_FALSE;
    (*env)->GetFloatArrayRegion(env,p,0,2,v);
    float out[3];
    if (!dm_android_vk_viewport(&x,&y,&w,&h)) {   /* 还没呈现过：整窗即画面 | nothing presented yet: the window is the picture */
        out[0]=v[0]; out[1]=v[1]; out[2]=1;
    } else {
        dm_vp_rect r={x,y,w,h}; double u=0,nv=0;
        int inside=dm_vp_map(r,v[0],v[1],&u,&nv);
        out[0]=(float)(u*r.w); out[1]=(float)(nv*r.h); out[2]=inside?1.0f:0.0f;
    }
    (*env)->SetFloatArrayRegion(env,p,2,3,out);
    return JNI_TRUE;
}
