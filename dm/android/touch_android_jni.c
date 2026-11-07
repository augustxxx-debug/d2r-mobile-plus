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
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeCutouts(JNIEnv *env, jclass cls, jfloatArray rects, jfloat gestureBottom) {
    (void)cls; float buf[16] = {0}; int n = 0;
    if (rects) { jsize len = (*env)->GetArrayLength(env, rects); if (len > 16) len = 16; (*env)->GetFloatArrayRegion(env, rects, 0, len, buf); n = (int)(len / 4); }
    dm_android_touch_cutouts(buf, n, gestureBottom);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativePicture(JNIEnv *env, jclass cls, jfloat w, jfloat h, jfloat x, jfloat y) {
    (void)env; (void)cls;
    dm_android_touch_picture(w, h, x, y);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeLayout(JNIEnv *env, jclass cls, jfloatArray dst) {
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env,dst)<DM_ANDROID_TOUCH_DRAW_FLOATS) return;
    float data[DM_ANDROID_TOUCH_DRAW_FLOATS];
    dm_android_touch_layout(data);
    (*env)->SetFloatArrayRegion(env,dst,0,DM_ANDROID_TOUCH_DRAW_FLOATS,data);
}
/* 触控层用的两块矩形（窗口像素）：前 4 个 = 控件坐标系（自适应全屏下是界面矩形，其余情况就是画面），
   后 4 个 = 画面矩形（世界点选与「在不在画面里」用）。数组只有 4 个时只填前一块。
   The two rectangles the touch layer uses (window px): the first four are the control coordinate system (the UI
   rectangle in adaptive fullscreen, the picture otherwise), the last four the picture rectangle (world taps and
   "is this inside the picture"). A four-element array only gets the first block. */
JNIEXPORT jboolean JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeViewport(JNIEnv *env, jclass cls, jfloatArray out) {
    (void)cls;
    int x=0,y=0,w=0,h=0,px=0,py=0,pw=0,ph=0;
    if (!out || (*env)->GetArrayLength(env,out)<4) return JNI_FALSE;
    if (!dm_android_vk_control_rect(&x,&y,&w,&h)) return JNI_FALSE;
    float v[4]={(float)x,(float)y,(float)w,(float)h};
    (*env)->SetFloatArrayRegion(env,out,0,4,v);
    if ((*env)->GetArrayLength(env,out)>=8 && dm_android_vk_picture_rect(&px,&py,&pw,&ph)) {
        float p[4]={(float)px,(float)py,(float)pw,(float)ph};
        (*env)->SetFloatArrayRegion(env,out,4,4,p);
    }
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
    if (!dm_android_vk_picture_rect(&x,&y,&w,&h)) {   /* 还没呈现过：整窗即画面 | nothing presented yet: the window is the picture */
        out[0]=v[0]; out[1]=v[1]; out[2]=1;
    } else {
        int cx=0,cy=0,cw=0,ch=0; dm_vp_rect pic={x,y,w,h}, ctrl=pic;
        if (!dm_android_vk_control_rect(&cx,&cy,&cw,&ch) || cw<1 || ch<1) { cx=x; cy=y; cw=w; ch=h; }
        ctrl = (dm_vp_rect){cx,cy,cw,ch};
        double px=0,py=0;
        int inside=dm_vp_map_ctrl(pic,ctrl,v[0],v[1],&px,&py);
        out[0]=(float)px; out[1]=(float)py; out[2]=inside?1.0f:0.0f;
    }
    (*env)->SetFloatArrayRegion(env,p,2,3,out);
    return JNI_TRUE;
}
