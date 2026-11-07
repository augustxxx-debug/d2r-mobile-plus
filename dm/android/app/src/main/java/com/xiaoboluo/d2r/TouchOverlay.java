package com.xiaoboluo.d2r;

import android.content.Context;
import android.graphics.BlendMode;
import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.LinearGradient;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.view.Choreographer;
import android.view.MotionEvent;
import android.view.View;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Locale;
import org.json.JSONObject;
import org.json.JSONArray;

/** 透明悬浮触摸层：与游戏Surface同尺寸，不挤占游戏高度。 | Transparent overlay sharing the game Surface bounds without reducing its height. */
public final class TouchOverlay extends View {
    private static native void nativeEvent(int phase, int slot, float x, float y);
    private static native void nativeResize(float width, float height, float left, float right, float bottom, boolean tablet);
    private static native void nativeLayout(float[] out);
    // 画面在窗口里的矩形（窗口像素）：前 4 个 = 控件坐标系（自适应全屏下是界面矩形，其余情况就是画面），
    // 后 4 个 = 画面矩形。控件按前者摆、按下按前者换，世界点选按后者归一。
    // The rectangles in window pixels: the first four are the control coordinate system (the UI rectangle in
    // adaptive fullscreen, the picture otherwise), the last four the picture rectangle. Controls are placed and
    // presses converted through the first, world taps normalised by the second.
    private static native boolean nativeViewport(float[] out);
    // 画面矩形与界面矩形在画面里的偏移（dp）：世界点选要真实画面归一化坐标。
    // The picture rectangle and the UI rectangle's offset inside it, in dp: world taps need picture-normalised coords.
    private static native void nativePicture(float wDp, float hDp, float xDp, float yDp);
    private static native void nativeCutouts(float[] rectsDp, float gestureBottomDp);
    // 窗口点 → 矩形内的点：p = {窗口 x, 窗口 y, 矩形内 x, 矩形内 y, 在画面里}。黑边上的按下第三个值为 0。
    // Window point -> point inside the rectangle: p = {window x, window y, x inside, y inside, inside}.
    private static native boolean nativeMap(float[] p);
    private static native int nativeFpsHud();   // 「显示帧数」：要画的数，0 = 不画（fps_hud.h 每 0.5 秒更新）| "Show frame rate": the number to draw, 0 = none (fps_hud.h updates every 0.5 s)
    private int fpsHud, hudMask=7;
    private static native int nativeHudMask();   // 各模式画什么（pad_android.h DM_PAD_HUD_*）：手柄模式只画经验行 / 生命法力文字，触点照收（碰屏回触屏模式）| what to draw per mode (pad_android.h DM_PAD_HUD_*): controller mode draws only the experience / life / mana texts, touches still received (a touch returns to touch mode)
    private final float density;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final int GLYPHS=119, HUD=167, DRAW_FLOATS=185;
    private final float[] layout = new float[DRAW_FLOATS];
    // 每个刷新周期比较完整绘图快照；坐标、可见性、按下状态与技能组任一变化才重画。
    // The native layout is checked once per refresh; the view redraws only when it differs from what was last drawn
    // Coordinates, visibility, pressed state and skill set all participate in that snapshot.
    private final float[] probe = new float[DRAW_FLOATS], drawn = new float[DRAW_FLOATS];
    private boolean drawnValid;
    // 重绘对齐游戏上屏：内容变了也要等游戏交换链出了新的一帧才重绘（每个游戏帧最多一次）。Choreographer 跟着屏幕 120 Hz 走、游戏 60 帧时，
    // 原先触控层在游戏两帧之间的节拍上也重绘，系统按「任一图层更新」多合成一次（奥丁实测：游戏层 59.7 / 窗口层 26.5 → 合成 68.8 次 / 秒，系统帧率监控显示的就是它）。
    // 游戏卡住（交换链 200 ms 没新帧：读取画面、暂停）时照常按内容变化重绘，触控不受影响。
    // Redraws follow the game's presents: even when the content changed, wait until the game's swapchain presented a new frame (at most once per game frame). With
    // Choreographer at the screen's 120 Hz and the game at 60, the overlay used to redraw on ticks between two game frames and the system composed once more for "any
    // layer updated" (Thor: game layer 59.7 / window layer 26.5 -> 68.8 compositions / s, which is what the system frame-rate monitor shows). When the game is stalled
    // (no new swapchain frame for 200 ms: loading screen, pause) the overlay redraws on content changes as before, so touch is unaffected.
    private static native long nativePresentCount();
    private long lastPresent=-1, seenPresent=-1, seenPresentNs;   // lastPresent：上次重绘时的游戏帧号；seenPresent / seenPresentNs：最近一次看到游戏帧号变化的时刻（判游戏卡住） | lastPresent: the game frame number at the last redraw; seenPresent / seenPresentNs: when the frame number last changed (stall detection)
    private final Choreographer.FrameCallback poll = new Choreographer.FrameCallback() {
        @Override public void doFrame(long frameTimeNanos) {
            if (!running) return;
            long pc=nativePresentCount(); if (pc!=seenPresent) { seenPresent=pc; seenPresentNs=frameTimeNanos; }
            boolean gameFrame = pc!=lastPresent || frameTimeNanos-seenPresentNs>200_000_000L;
            if (inputEnabled && isShown() && gameFrame) {
                // 首个游戏帧 / 折叠 / 旋转 / 分辨率档变了：先取消全部触点（Java 槽位、原生、原始触屏），再重摆。
                // First frame, fold, rotation, resolution step: cancel every touch (Java slots, native, raw) first,
                // then lay out again.
                if (viewportChanged()) { resizeNative(); drawnValid=false; }
                nativeLayout(probe); int f=nativeFpsHud(), hm=nativeHudMask(); if (!drawnValid || f!=fpsHud || hm!=hudMask || !Arrays.equals(probe, drawn)) { fpsHud=f; hudMask=hm; lastPresent=pc; invalidate(); } }
            Choreographer.getInstance().postFrameCallback(this);
        }
    };
    private final TouchDispatch touches;
    // 画面矩形（窗口像素）与换算用的暂存：矩形由原生（dm_vp_fit / dm_ui_rect）给，画与触摸都用同一份。
    // 前 4 个 = 控件坐标系（自适应全屏下是界面矩形，其余情况就是画面），后 4 个 = 画面矩形。
    // The rectangles (window pixels) and scratch for the conversion: native (dm_vp_fit / dm_ui_rect) gives them and
    // both drawing and touch use that one copy. The first four are the control coordinate system (the UI rectangle
    // in adaptive fullscreen, the picture otherwise), the last four the picture rectangle.
    private final float[] vp = new float[8], vpProbe = new float[8];
    private final float[] mapP = new float[5];
    private boolean vpValid;
    private final String[] gameLabels, menuLabels;
    private final File overlayRoot;
    private final HashMap<String,Bitmap> images=new HashMap<>(), art=new HashMap<>(), glyphs=new HashMap<>();
    private final HashMap<String,int[]> glowOffsets=new HashMap<>();
    private final Bitmap[] skillAtlases=new Bitmap[9], silAtlases=new Bitmap[9];
    private Bitmap mapArt, circleArt, socketArt;
    private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final String lifeFormat, manaFormat, levelFormat, cancelLabel;
    private JSONObject hudUi;
    private float safeLeft, safeRight, safeBottom;
    private boolean running, inputEnabled;

    /** labels为本地化表中的15项：12键、摇杆、换组、难度；menuLabels为导航态前五键。 | Pass 15 localized game labels and five navigation labels from the shared string table. */
    public TouchOverlay(Context context, String[] labels, String[] menuLabels) {
        super(context);
        density = getResources().getDisplayMetrics().density;
        touches = new TouchDispatch(density, new TouchDispatch.Out() {
            @Override public void event(int phase,int slot,float x,float y) { nativeEvent(phase,slot,x,y); }
            @Override public void raw(int count,int[] ids,float[] xs,float[] ys) { DmActivity.nativeTouch(count,ids,xs,ys); }
        }, new TouchDispatch.Projection() {
            @Override public boolean map(float vx,float vy,float[] out) {
                mapP[0]=vx; mapP[1]=vy;
                if (!nativeMap(mapP)) { out[0]=vx; out[1]=vy; return true; }   // 没有矩形（还没呈现过）：整窗即画面 | no rectangle yet: the window is the picture
                out[0]=mapP[2]; out[1]=mapP[3];
                return mapP[4]!=0;
            }
        });
        gameLabels = labels == null ? new String[15] : labels.clone();
        this.menuLabels = menuLabels == null ? new String[5] : menuLabels.clone();
        lifeFormat=context.getString(R.string.dmHudLife); manaFormat=context.getString(R.string.dmHudMana);
        levelFormat=context.getString(R.string.dmAndroidHudLevel); cancelLabel=context.getString(R.string.dmTcCancel);
        overlayRoot=new File(context.getFilesDir(),"dm/overlay");
        setWillNotDraw(false);
        setFocusable(false);
        setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
    }
    private final java.util.ArrayList<android.graphics.Rect> cutRects = new java.util.ArrayList<>();
    private int gestureBottomPx;
    /** 挖孔矩形（窗口像素）与底部手势条高；由 resizeNative 换到控件坐标交给原生（B140）。
        Cutout rectangles (window px) and the bottom gesture bar; resizeNative converts them to control coordinates (B140). */
    public void setCutouts(java.util.List<android.graphics.Rect> rects, int gestureBottom) {
        cutRects.clear(); if (rects != null) for (android.graphics.Rect r : rects) if (r != null && !r.isEmpty() && cutRects.size() < 4) cutRects.add(new android.graphics.Rect(r));
        gestureBottomPx = Math.max(0, gestureBottom);
    }
    public void setSafeInsets(int leftPx, int rightPx, int bottomPx) {
        safeLeft=leftPx/density; safeRight=rightPx/density; safeBottom=bottomPx/density;
        resizeNative();
    }
    /** 仅在libdm加载且guest启动成功后开启；暂停时先取消触点。 | Enable only after libdm loads and the guest starts; disabling cancels first. */
    public void setInputEnabled(boolean enabled) {
        if (inputEnabled==enabled) return;
        if (!enabled) cancelTouches();
        inputEnabled=enabled;
        drawnValid=false; invalidate();
        if (enabled) {
            images.clear(); art.clear(); glyphs.clear(); glowOffsets.clear();
            for(int i=0;i<skillAtlases.length;i++) {
                skillAtlases[i]=image("touchart/dm_native_skill_"+i+".png"); silAtlases[i]=image("hud_v4_ui/sil_dm_native_skill_"+i+".png");
            }
            mapArt=image("touchart/dm_native_map.png"); circleArt=image("touchart/dm_native_circle.png"); socketArt=image("touchart/dm_mobile_socket.png");
            try { hudUi=new JSONObject(new String(Files.readAllBytes(new File(overlayRoot,"hud_v4_ui/ui.json").toPath()),StandardCharsets.UTF_8)); }
            catch(Exception ignored) { hudUi=null; }
            resizeNative();
        }
    }
    private void resizeNative() {
        if (!(inputEnabled && getWidth()>0 && getHeight()>0)) return;
        int wpx=getWidth(), hpx=getHeight();
        touches.setViewSize(wpx,hpx);
        // 控件坐标系每次重新问：内外屏切换、旋转、渲染分辨率档变化都会改它（自适应全屏下是界面上那块 16:9），
        // 控件按它里面的点数摆。画面矩形跟着一起取（世界点选要用它的归一化坐标）。
        // Ask for the coordinate system every time: a fold switch, a rotation or a render-resolution step changes it
        // (in adaptive fullscreen it is the 16:9 UI block), and the controls are laid out in its points. The picture
        // rectangle comes along (world taps need its normalised coordinates).
        vpValid = nativeViewport(vp) && vp[2]>=1 && vp[3]>=1;
        float x=vpValid?vp[0]:0, y=vpValid?vp[1]:0, w=vpValid?vp[2]:wpx, h=vpValid?vp[3]:hpx;
        // 画面矩形（后 4 个）；没取到就按控件那块算（旧行为：两者相同）。
        // The picture rectangle (the last four); if it did not come back, use the control block (the old behaviour:
        // the two were the same).
        float pw=vpValid&&vp[6]>=1?vp[6]:w, ph=vpValid&&vp[7]>=1?vp[7]:h;
        float px=vpValid&&vp[6]>=1?vp[4]:x, py=vpValid&&vp[7]>=1?vp[5]:y;
        nativePicture(pw/density,ph/density,(x-px)/density,(y-py)/density);
        // 安全区从窗口边缘量；画面矩形把黑边挡掉的那部分先减掉，剩下的才是矩形内的安全距离。
        // Safe insets are measured from the window edges; subtract what the bars already cover, and what is left is
        // the safe distance inside the rectangle.
        float l=Math.max(0,safeLeft-x/density), r=Math.max(0,safeRight-(wpx-x-w)/density), b=Math.max(0,safeBottom-(hpx-y-h)/density);
        // 挖孔矩形换到控件坐标（dp）；手势条从窗口底边量，减掉控件区下面已经让出的那部分。
        // Cutout rectangles in control coordinates (dp); the gesture bar is measured from the window bottom, minus what the
        // control area already leaves below it.
        float[] cuts = new float[cutRects.size() * 4];
        for (int i = 0; i < cutRects.size(); i++) { android.graphics.Rect c = cutRects.get(i);
            cuts[4*i] = (c.left - x)/density; cuts[4*i+1] = (c.top - y)/density; cuts[4*i+2] = (c.right - x)/density; cuts[4*i+3] = (c.bottom - y)/density; }
        nativeCutouts(cuts, Math.max(0, gestureBottomPx - (hpx - y - h))/density);
        nativeResize(w/density,h/density,l,r,b,
                getResources().getConfiguration().smallestScreenWidthDp>=600);
    }
    private float viewW() { return vpValid ? vp[2]/density : getWidth()/density; }
    private float viewH() { return vpValid ? vp[3]/density : getHeight()/density; }
    /** 画面矩形变了（首个游戏帧、折叠、旋转、分辨率档）：先取消全部触点，再按新矩形重摆控件。
        The rectangle changed: cancel every touch first, then lay the controls out again. */
    private boolean viewportChanged() {
        if (!nativeViewport(vpProbe) || vpProbe[2]<1 || vpProbe[3]<1) return false;
        if (!touches.updatePicture(vpProbe)) return false;   // 内部先 cancel：手指按住不动时也不会留旧坐标 | cancels inside
        System.arraycopy(vpProbe,0,vp,0,8); vpValid=true;
        return true;
    }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) {
        cancelTouches(); resizeNative();
    }
    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow(); running=true; drawnValid=false; invalidate(); Choreographer.getInstance().postFrameCallback(poll);
    }
    @Override protected void onDetachedFromWindow() {
        running=false; Choreographer.getInstance().removeFrameCallback(poll); cancelTouches(); super.onDetachedFromWindow();
    }
    public void cancelTouches() {
        if (!inputEnabled) { touches.reset(); return; }
        touches.cancel();   // Java 槽位 + 原生 phase 3 + 原始触屏空集 | Java slots + native phase 3 + empty raw set
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
        if (!inputEnabled) return false;
        int action=e.getActionMasked(), index=e.getActionIndex();
        if (action==MotionEvent.ACTION_DOWN || action==MotionEvent.ACTION_POINTER_DOWN) {
            // 黑边上的按下在 TouchDispatch 里丢掉（不占手指、不发事件）：那里没有画面，按钮也不该被它碰到。
            // A press on the black bars is dropped in TouchDispatch (no finger taken, no event sent): there is no
            // picture there, and it must not reach a button.
            touches.down(e.getPointerId(index),e.getX(index),e.getY(index));
        } else if (action==MotionEvent.ACTION_MOVE) {
            for (int i=0;i<e.getPointerCount();++i) touches.move(e.getPointerId(i),e.getX(i),e.getY(i));
        } else if (action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) {
            touches.up(e.getPointerId(index),e.getX(index),e.getY(index));
            if (action==MotionEvent.ACTION_UP) performClick();
        } else if (action==MotionEvent.ACTION_CANCEL) cancelTouches();
        return true;
    }
    @Override public boolean performClick() { super.performClick(); return true; }
    private String label(int i,boolean navigation) {
        if (i>=15) return Integer.toString(i-14);
        String[] source=navigation && i<5?menuLabels:gameLabels;
        String text=i<source.length && source[i]!=null?source[i]:"";
        // 换组字段同时决定文字，避免快照变了却仍画相同的组别。 | The skill-set field also changes the label so a changed snapshot displays the new group.
        if(i==13) return layout[3]!=0?"Ⅱ":"Ⅰ";
        if(i==11 && layout[HUD+15]!=0) return cancelLabel;
        return text;
    }
    // 图像只从用户导入的已校验资源读取，不放进安装包。
    // Images are read only from the user's validated imported resources, never from the APK.
    private Bitmap image(String name) {
        if(images.containsKey(name)) return images.get(name);
        File file=new File(overlayRoot,name);
        Bitmap bitmap=file.isFile() && file.length()<=8*1024*1024 ? BitmapFactory.decodeFile(file.getAbsolutePath()):null;
        images.put(name,bitmap); return bitmap;
    }
    // ---- 触屏外观：逐项移植 iOS 的 dm/ios/Sources/DmTouchControlArt.h 与 DmTouchOverlay.m（参数与 iOS 逐个相同，改一边要改另一边）----
    // ---- Touch art: ported item by item from iOS dm/ios/Sources/DmTouchControlArt.h and DmTouchOverlay.m (same parameters; change both together) ----
    private static int rgba(double r,double g,double b,double a) {
        return Color.argb((int)Math.round(a*255),(int)Math.round(r*255),(int)Math.round(g*255),(int)Math.round(b*255));
    }
    private static RectF inset(float side,float d) { return new RectF(d,d,side-d,side-d); }
    private static Shader vgrad(RectF r,int... colors) { return new LinearGradient(0,r.top,0,r.bottom,colors,null,Shader.TileMode.CLAMP); }
    private static Paint stroke(int color,float width) {
        Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);p.setStyle(Paint.Style.STROKE);p.setColor(color);p.setStrokeWidth(width);return p;
    }
    private interface Art { void draw(Canvas c,float side,Bitmap target,float scale); }
    // 静态底图按像素尺寸缓存（同 iOS controlArt）：移动时只换摇杆头位置，不每帧建渐变。
    // Static art is cached at pixel size (as iOS controlArt): moving only changes where the thumb goes, no gradients per frame.
    private Bitmap artBitmap(String key,float side,Art a) {
        int px=Math.max(1,Math.round(side*density)); String k=key+":"+px; Bitmap b=art.get(k);
        if(b==null) {
            if(art.size()>=64) art.clear();
            b=Bitmap.createBitmap(px,px,Bitmap.Config.ARGB_8888); Canvas bc=new Canvas(b); float scale=px/side; bc.scale(scale,scale);
            a.draw(bc,side,b,scale); art.put(k,b);
        }
        return b;
    }
    private void drawArt(Canvas c,String key,float x,float y,float side,Art a) {
        c.drawBitmap(artBitmap(key,side,a),null,new RectF(x,y,x+side,y+side),bitmapPaint);
    }
    // dm_button_draw：窄铁边、黑灰内槽；空槽淡底加细十字。 | dm_button_draw: narrow iron rim, black-grey well; empty slots get a faint base and a thin cross.
    private static void buttonArt(Canvas c,float side,boolean held,boolean empty,boolean primary) {
        RectF rim=inset(side,side*.018f), face=inset(side,side*.055f); Paint f=new Paint(Paint.ANTI_ALIAS_FLAG);
        f.setShader(vgrad(rim,rgba(.26,.28,.28,empty?.22:.9),rgba(.055,.065,.07,empty?.22:.95),rgba(.16,.11,.10,empty?.16:.7))); c.drawOval(rim,f);
        c.drawOval(rim,stroke(rgba(.008,.012,.015,empty?.38:.9),side*.025f));
        f.setShader(vgrad(face,rgba(.021,.021,.021,empty?.08:.94),rgba(.05,.05,.05,empty?.16:.94))); c.drawOval(face,f);
        int edge=empty?rgba(held?.72:.62,held?.72:.62,held?.72:.62,held?.80:.60):rgba(held?.64:.26,held?.64:.29,held?.64:.28,held?.72:.55);
        c.drawOval(face,stroke(edge,side*(held?.018f:(empty?.012f:.009f))));
        if(primary && !empty) c.drawOval(inset(side,side*.035f),stroke(rgba(.31,.23,.22,.52),side*.012f));
        if(empty) {
            float arm=side*.055f,m=side/2; Paint p=stroke(rgba(.60,.65,.64,held?.75:.25),side*.018f); p.setStrokeCap(Paint.Cap.ROUND);
            c.drawLine(m-arm,m,m+arm,m,p); c.drawLine(m,m-arm,m,m+arm,p);
        }
    }
    // CoreGraphics 的「饱和度」混合叠一层 50% 灰 = 每个像素换成它的亮度（0.3R+0.59G+0.11B），透明处按透明度混向 50% 灰（不透明）。
    // CoreGraphics "saturation" blending with 50% grey = each pixel becomes its luminosity (0.3R+0.59G+0.11B); transparent parts blend
    // towards 50% grey by their transparency (and become opaque).
    private static int neutral(int argb) {
        double a=Color.alpha(argb)/255.0, l=(.3*Color.red(argb)+.59*Color.green(argb)+.11*Color.blue(argb))/255.0;
        int v=(int)Math.round(((1-a)*.5+a*l)*255); return Color.argb(255,v,v,v);
    }
    // dm_socket_neutralize：底座图内槽（内缩 8% 的椭圆）去色；按椭圆边的覆盖率混合。
    // dm_socket_neutralize: the socket image's well (the ellipse inset by 8%) loses its colour, blended by the ellipse edge's coverage.
    private static void neutralizeWell(Bitmap b,float side,float scale) {
        int w=b.getWidth(),h=b.getHeight(); Bitmap mask=Bitmap.createBitmap(w,h,Bitmap.Config.ALPHA_8);
        Canvas mc=new Canvas(mask); mc.scale(scale,scale); Paint mp=new Paint(Paint.ANTI_ALIAS_FLAG); mc.drawOval(inset(side,side*.08f),mp);
        int[] px=new int[w*h]; b.getPixels(px,0,w,0,0,w,h); byte[] cov=new byte[w*h]; mask.copyPixelsToBuffer(java.nio.ByteBuffer.wrap(cov)); mask.recycle();
        for(int i=0;i<px.length;i++) {
            double m=(cov[i]&255)/255.0; if(m==0) continue;
            int o=px[i],n=neutral(o); double oa=Color.alpha(o)/255.0, ra=m+(1-m)*oa;
            int r=(int)Math.round((m*Color.red(n)+(1-m)*oa*Color.red(o))/ra), g=(int)Math.round((m*Color.green(n)+(1-m)*oa*Color.green(o))/ra), bl=(int)Math.round((m*Color.blue(n)+(1-m)*oa*Color.blue(o))/ra);
            px[i]=Color.argb((int)Math.round(ra*255),r,g,bl);
        }
        b.setPixels(px,0,w,0,0,w,h);
    }
    // drawButton：有底座图且不是空槽 → 底座图 + 内槽去色（按下再加银灰细环）；否则 dm_button_draw。
    // drawButton: with the socket image and not empty → socket image + neutral well (plus a silver ring when held); otherwise dm_button_draw.
    private Bitmap buttonBitmap(float side,boolean held,boolean empty,boolean primary,boolean image) {
        return artBitmap("skill:"+(held?1:0)+(empty?1:0)+(primary?1:0)+(image?1:0),side,(bc,s,target,scale)->{
            if(image) {
                bc.drawBitmap(socketArt,null,new RectF(0,0,s,s),bitmapPaint); neutralizeWell(target,s,scale);
                if(held) bc.drawOval(inset(s,s*.052f),stroke(rgba(.64,.64,.64,.72),s*.015f));
            } else buttonArt(bc,s,held,empty,primary);
        });
    }
    private void button(Canvas c,float x,float y,float r,boolean held,boolean empty,boolean primary) {
        c.drawBitmap(buttonBitmap(2*r,held,empty,primary,socketArt!=null && !empty),null,new RectF(x-r,y-r,x+r,y+r),bitmapPaint);
    }
    // 调试包：各控件底图与剪影银灰化结果画成 PNG（3 倍像素），dm/android/tools/art_compare.sh 拿去和 iOS 同参数的离屏渲染逐像素比，防两边画法走开。
    // Debug builds: each control's art and the silvered silhouettes are written as PNGs (3x pixels); dm/android/tools/art_compare.sh compares them
    // pixel by pixel with an offscreen render of the iOS code with the same parameters, so the two do not drift apart.
    void dumpArt(File dir) throws java.io.IOException {
        socketArt=image("touchart/dm_mobile_socket.png"); silAtlases[0]=image("hud_v4_ui/sil_dm_native_skill_0.png");
        if(socketArt==null || silAtlases[0]==null) throw new java.io.IOException("overlay art missing");
        if(!dir.isDirectory() && !dir.mkdirs()) throw new java.io.IOException("mkdir "+dir);
        HashMap<String,Bitmap> out=new HashMap<>();
        for(int h=0;h<2;h++) for(int e=0;e<2;e++) for(int p=0;p<2;p++) out.put("button_h"+h+"e"+e+"p"+p,buttonBitmap(80,h==1,e==1,p==1,false));
        for(int h=0;h<2;h++) out.put("socket_h"+h,buttonBitmap(80,h==1,false,false,true));
        for(int a=0;a<2;a++) { boolean act=a==1;
            out.put("joybase_a"+a,artBitmap("stick:1:"+a,120,(bc,side,t,sc)->joyBaseArt(bc,side,act)));
            out.put("joythumb_a"+a,artBitmap("stick:0:"+a,46,(bc,side,t,sc)->joyThumbArt(bc,side,act))); }
        Bitmap sil=silAtlases[0]; out.put("silver0",glyph(0,0,0,sil.getWidth(),sil.getHeight()));
        for(java.util.Map.Entry<String,Bitmap> en:out.entrySet())
            try(java.io.FileOutputStream f=new java.io.FileOutputStream(new File(dir,en.getKey()+".png"))) { en.getValue().compress(Bitmap.CompressFormat.PNG,100,f); }
    }
    // dm_joystick_base_draw：向边缘渐隐的底盘、四段细弧与四个方向刻纹。 | dm_joystick_base_draw: a base fading to the edge, four thin arcs and four direction marks.
    private static void joyBaseArt(Canvas c,float side,boolean active) {
        float m=side/2; Paint f=new Paint(Paint.ANTI_ALIAS_FLAG);
        f.setShader(new RadialGradient(m,m,side*.48f,new int[]{rgba(.025,.025,.025,active?.10:.045),rgba(.025,.025,.025,active?.22:.12),rgba(.025,.025,.025,0)},
                new float[]{0,.78f,1},Shader.TileMode.CLAMP));
        c.drawCircle(m,m,side*.48f,f);
        Paint arc=stroke(rgba(.63,.63,.63,active?.12:.055),side*.004f); arc.setStrokeCap(Paint.Cap.ROUND);
        Paint mark=stroke(rgba(.68,.68,.68,active?.25:.13),side*.004f); mark.setStrokeCap(Paint.Cap.ROUND);
        float R=side*.416f; RectF ring=new RectF(m-R,m-R,m+R,m+R);
        for(int i=0;i<4;i++) {
            double angle=i*Math.PI/2;
            c.drawArc(ring,(float)Math.toDegrees(angle+.20),(float)Math.toDegrees(Math.PI/2-.40),false,arc);
            c.save(); c.translate(m,m); c.rotate(i*90f);
            float x=side*.414f,k=side*.028f; Path p=new Path();
            p.moveTo(x+k,0); p.lineTo(x-k,-k*.65f); p.lineTo(x-k*.42f,0); p.lineTo(x-k,k*.65f); p.close(); c.drawPath(p,mark);
            c.drawLine(x-k*.42f,0,x-k*2.1f,0,mark);
            c.restore();
        }
    }
    // dm_joystick_thumb_draw：低反光的黑灰摇杆头，上缘两道弧形高光。 | dm_joystick_thumb_draw: a low-sheen black-grey thumb with two arc highlights on top.
    private static void joyThumbArt(Canvas c,float side,boolean active) {
        RectF disc=inset(side,side*.055f); Paint f=new Paint(Paint.ANTI_ALIAS_FLAG); float m=side/2;
        f.setShader(vgrad(disc,rgba(.23,.23,.23,active?.78:.46),rgba(.14,.14,.14,active?.82:.49),rgba(.065,.065,.065,active?.84:.52))); c.drawOval(disc,f);
        c.drawOval(disc,stroke(rgba(.025,.025,.025,active?.65:.32),side*.035f));
        float a=side*.437f; c.drawArc(new RectF(m-a,m-a,m+a,m+a),(float)Math.toDegrees(Math.PI*1.07),(float)Math.toDegrees(Math.PI*.84),false,stroke(rgba(.64,.64,.64,active?.40:.22),side*.014f));
        float b=side*.410f; c.drawArc(new RectF(m-b,m-b,m+b,m+b),(float)Math.toDegrees(Math.PI*1.12),(float)Math.toDegrees(Math.PI*.76),false,stroke(rgba(.42,.42,.42,active?.20:.10),side*.012f));
    }
    // 圆钮上的字：同 iOS caption（Palatino 金白字、居中、文字框顶对齐）；安卓用衬线体。
    // Button text: as iOS caption (Palatino in pale gold, centred, top-aligned in its box); Android uses the serif face.
    private void caption(Canvas c,String value,float cx,float top,float size,float alpha) {
        paint.setTypeface(Typeface.SERIF); paint.setTextSize(size); paint.setTextAlign(Paint.Align.CENTER); paint.setColor(rgba(.85,.79,.65,alpha));
        c.drawText(value,cx,top-paint.ascent(),paint); paint.setTypeface(null);
    }
    // skillImage：同帧位的银色剪影（无剪影图时退回原版图集、四边各裁 8%），保留明暗、统一为银灰。
    // skillImage: the silver silhouette at the same frame (without it, the original atlas trimmed 8% on each side), keeping the shading, all silver grey.
    private Bitmap glyph(int atlas,int sx,int sy,int dm,int sh) {
        Bitmap sil=silAtlases[atlas], src=sil!=null?sil:skillAtlases[atlas]; if(src==null) return null;
        int trim=sil!=null?0:Math.round(dm*.08f); sx+=trim; sy+=trim; dm-=2*trim; sh-=2*trim;
        if(dm<=0 || sh<=0 || sx<0 || sy<0 || sx+dm>src.getWidth() || sy+sh>src.getHeight()) return null;
        String key=atlas+":"+sx+":"+sy+":"+dm+":"+sh; Bitmap g=glyphs.get(key); if(g!=null) return g;
        g=Bitmap.createBitmap(src,sx,sy,dm,sh).copy(Bitmap.Config.ARGB_8888,true);
        if(sil!=null) {
            int[] px=new int[dm*sh]; g.getPixels(px,0,dm,0,0,dm,sh);
            for(int i=0;i<px.length;i++) { int n=neutral(px[i]); px[i]=(px[i]&0xff000000)|(n&0x00ffffff); }   // 去色后用原图透明度恢复边缘 | restore the edges with the original alpha
            g.setPixels(px,0,dm,0,0,dm,sh);
        }
        if(glyphs.size()>=96) { glyphs.clear(); glowOffsets.clear(); } glyphs.put(key,g); return g;
    }
    // 图标外一圈淡光（iOS 用阴影：偏移 0、模糊 r×0.055，按下 r×0.10，65% 灰、32% 不透明）。
    // A faint glow around the icon (iOS uses a shadow: zero offset, blur r×0.055, r×0.10 when held, 65% grey at 32% opacity).
    private Bitmap glow(Bitmap g,RectF dst,float blur,int[] offset) {
        int w=Math.max(1,Math.round(dst.width()*density)),h=Math.max(1,Math.round(dst.height()*density));
        String key="glow:"+System.identityHashCode(g)+":"+w+":"+h+":"+Math.round(blur*density*10); Bitmap out=glyphs.get(key);
        if(out==null) {
            // CoreGraphics 的模糊值约为高斯 σ 的 2 倍；Skia 的模糊半径 r 对应 σ = 0.57735r + 0.5。 | CoreGraphics blur ≈ 2σ; Skia radius r means σ = 0.57735r + 0.5.
            Bitmap scaled=Bitmap.createScaledBitmap(g,w,h,true); Paint p=new Paint();
            float sigma=blur*density/2; p.setMaskFilter(new BlurMaskFilter(Math.max(.5f,(sigma-.5f)/.57735f),BlurMaskFilter.Blur.NORMAL));
            int[] o=new int[2]; out=scaled.extractAlpha(p,o); scaled.recycle(); glyphs.put(key,out); glowOffsets.put(key,o);
        }
        int[] o=glowOffsets.get(key); offset[0]=o[0]; offset[1]=o[1]; return out;
    }
    private void skill(Canvas c,int i,float x,float y,float r,boolean held,boolean navigation) {
        int p=GLYPHS+i*8, atlas=(int)layout[p+1], status=(int)layout[p];
        Bitmap g=status==2 && atlas>=0 && atlas<9 ? glyph(atlas,(int)layout[p+2],(int)layout[p+3],(int)layout[p+4],(int)layout[p+5]) : null;
        // 无图：主钮与「空」状态画空槽（绑定入口）；未知槽画底座 + F 标签图（同 iOS lbl_*.png）。 | No icon: the main button and empty slots draw the empty
        // socket (the binding entry); an unknown slot draws the socket with its F label image (as iOS lbl_*.png).
        boolean empty=g==null && (i==0 || status==1);
        button(c,x,y,r,held,empty,i==0);
        if(empty) return;
        if(g==null) {
            Bitmap lbl=image("hud_v4_ui/"+(i==0?"lbl_main.png":"lbl_f"+(i+(layout[3]!=0?4:0))+".png"));
            if(lbl!=null) { float lh=r*(i==0?.30f:.36f),lw=lbl.getWidth()*lh/lbl.getHeight(); c.drawBitmap(lbl,null,new RectF(x-lw/2,y+r*.50f-lh/2,x+lw/2,y+r*.50f+lh/2),bitmapPaint); }
            return;
        }
        float size=2*r*.76f, fit=size/Math.max(g.getWidth(),g.getHeight());
        RectF ir=new RectF(x-g.getWidth()*fit/2,y-g.getHeight()*fit/2,x+g.getWidth()*fit/2,y+g.getHeight()*fit/2);
        boolean spent=((int)layout[p+6]&2)!=0 && layout[p+7]<=0;
        c.save(); Path clip=new Path(); clip.addCircle(x,y,r*.82f,Path.Direction.CW); c.clipPath(clip);
        Paint ip=new Paint(bitmapPaint);
        if(spent) {   // 充能用完：灰底上按亮度贴图标（去色）再压一层暗 | out of charges: luminosity-blend the icon on grey, then darken
            Paint f=new Paint(Paint.ANTI_ALIAS_FLAG); f.setColor(rgba(.30,.30,.30,1)); c.drawCircle(x,y,r*.84f,f);
            ip.setBlendMode(BlendMode.LUMINOSITY); ip.setAlpha(140); c.drawBitmap(g,null,ir,ip);
            f.setColor(rgba(0,0,0,.35)); c.drawCircle(x,y,r*.88f,f);
        } else {
            int[] o=new int[2]; Bitmap gl=glow(g,ir,held?r*.10f:r*.055f,o);
            Paint gp=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG); gp.setColor(rgba(.65,.65,.65,.32));
            c.save(); c.translate(ir.left,ir.top); c.scale(1/density,1/density); c.drawBitmap(gl,o[0],o[1],gp); c.restore();
            ip.setAlpha(held?255:240); c.drawBitmap(g,null,ir,ip);
        }
        c.restore();
        // 副手组角标「Ⅱ」（暗底金边小圆）与充能剩余次数（腰带件数那套数字图）。 | Off-hand set badge "Ⅱ" (small dark disc with a gold ring)
        // and charges left (the belt-count digit images).
        int kind=(int)layout[p+6];
        if((kind&1)!=0) {
            float br=r*.30f,bx=x+r*.74f,by=y-r*.74f; Paint f=new Paint(Paint.ANTI_ALIAS_FLAG); f.setColor(rgba(.11,.094,.078,.95)); c.drawCircle(bx,by,br,f);
            float lw=Math.max(1,br*.14f),in=Math.max(1,br*.08f); c.drawCircle(bx,by,br-in,stroke(rgba(.63,.50,.27,1),lw));
            float ts=br*1.15f; caption(c,"Ⅱ",bx,by-ts*.62f,ts,1);
        }
        if((kind&2)!=0 && hudUi!=null) {
            float u=viewH()/1320, W=viewW();
            try { JSONObject bd=hudUi.getJSONObject("badge"); hudText(c,Integer.toString(Math.max(0,(int)layout[p+7])),bd.getJSONObject("glyphs"),(x+r*.66f-W/2)/u+1434,(y+r*.42f)/u,true,u); }
            catch(Exception ignored) { }
        }
    }
    private float hudX(double x,float u) { return viewW()/2+((float)x-1434)*u; }
    private void hudImage(Canvas c,String name,double x,double y,float u) {
        Bitmap b=image("hud_v4_ui/"+name); if(b==null)return;
        float left=hudX(x,u),top=(float)y*u;
        paint.setColor(Color.WHITE);c.drawBitmap(b,null,new RectF(left,top,left+b.getWidth()*u,top+b.getHeight()*u),paint);
    }
    // 字形位置、底图和数值基线来自与iOS相同的生成器，游戏层仍负责药水与球液。
    // Glyph placement, base art and baselines come from the iOS generator; the game still owns potions and orb liquid.
    private void hudText(Canvas c,String value,JSONObject glyphs,double x,double baseline,boolean right,float u) throws Exception {
        double advance=0;
        for(int i=0;i<value.length();i++) { JSONObject g=glyphs.optJSONObject(value.substring(i,i+1)); if(g!=null)advance+=g.optDouble("adv"); }
        double pen=x-(right?advance:advance/2);
        for(int i=0;i<value.length();i++) {
            JSONObject g=glyphs.optJSONObject(value.substring(i,i+1));if(g==null)continue;
            if(!g.isNull("file")) hudImage(c,g.getString("file"),pen+g.optDouble("dx"),baseline+g.optDouble("dy"),u);
            pen+=g.optDouble("adv");
        }
    }
    private void hud(Canvas c) {
        if(hudUi==null || layout[HUD]==0) return;
        float u=viewH()/1320; boolean bar=(hudMask&2)!=0, text=(hudMask&4)!=0;   // 手柄模式：原版手柄底栏自己在，只补文字 | controller mode: the original controller bar is there, only the texts are added
        try {
            JSONObject base=hudUi.getJSONObject("base");if(bar)hudImage(c,"base_full.png",base.getDouble("x"),base.getDouble("y"),u);
            JSONArray slots=hudUi.getJSONArray("slots"), inner=hudUi.getJSONArray("inner"), menu=slots.getJSONArray(5);
            boolean alert=layout[HUD+12]>0 || layout[HUD+13]>0, held=layout[4+10*5+4]!=0;
            Bitmap button=image("hud_v4_ui/"+(alert?"lvl_":"lvl_d")+(held?1:0)+".png");
            if(bar && button!=null) c.drawBitmap(button,null,new RectF(hudX(menu.getDouble(0),u),(float)menu.getDouble(1)*u,hudX(menu.getDouble(2),u),(float)menu.getDouble(3)*u),paint);
            JSONObject e=hudUi.getJSONObject("exp");
            float x=hudX(e.getDouble("x0"),u),y=(float)e.getDouble("y")*u,w=(float)(e.getDouble("x1")-e.getDouble("x0"))*u,h=(float)e.getDouble("h")*u;
            if(bar){paint.setColor(0xff16120e);c.drawRect(x,y,x+w,y+h,paint);
            paint.setColor(0xffc4a046);c.drawRect(x+u,y+u,x+u+(w-2*u)*Math.max(0,Math.min(1000,layout[HUD+2]))/1000,y+h-u,paint);
            paint.setColor(0xff463a28);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(u);c.drawRect(x,y,x+w,y+h,paint);paint.setStyle(Paint.Style.FILL);
            paint.setColor(0xff16120e);for(int i=1;i<20;i++)c.drawRect(x+w*i/20,y+u,x+w*i/20+u,y+h-u,paint);}
            JSONObject lv=hudUi.getJSONObject("lv"), vital=hudUi.getJSONObject("vital"),badge=hudUi.getJSONObject("badge");
            // 显示帧数（同 iOS DmTouchOverlay）：接在经验行末尾、同一套字形，不另起标签 | Show frame rate (as iOS DmTouchOverlay): appended to the experience line with the same glyphs, no separate label
            String lvs=String.format(Locale.ROOT,levelFormat,(int)layout[HUD+1],(int)layout[HUD+2]/10); if(fpsHud>0) lvs=lvs+" · "+fpsHud+" FPS";
            if(text)hudText(c,lvs,lv.getJSONObject("glyphs"),lv.getDouble("x"),lv.getDouble("baseline"),false,u);
            if(!bar) return;   // 手柄模式：原版手柄底栏自带生命 / 法力数字，这里不再画（画了会叠字）| controller mode: the original controller bar has its own life / mana numbers, drawing ours overlaps them
            hudText(c,String.format(Locale.ROOT,lifeFormat,(int)layout[HUD+8],(int)layout[HUD+9]),vital.getJSONObject("glyphs"),vital.getDouble("left_x"),vital.getDouble("baseline"),false,u);
            hudText(c,String.format(Locale.ROOT,manaFormat,(int)layout[HUD+10],(int)layout[HUD+11]),vital.getJSONObject("glyphs"),vital.getDouble("right_x"),vital.getDouble("baseline"),false,u);
            for(int i=1;i<=4;i++) if(layout[HUD+2+i]>0) {
                JSONArray s=slots.getJSONArray(i);
                hudText(c,Integer.toString((int)layout[HUD+2+i]),badge.getJSONObject("glyphs"),s.getDouble(0)+inner.getDouble(2)+badge.getDouble("dx"),s.getDouble(1)+inner.getDouble(3)+badge.getDouble("dy"),true,u);
            }
            int players=(int)layout[HUD+7];
            if(players>=1 && players<=8) {
                JSONArray s=slots.getJSONArray(0);Bitmap b=image("hud_v4_ui/diff_"+players+".png");
                if(b!=null) hudImage(c,"diff_"+players+".png",s.getDouble(0)+(inner.getDouble(0)+inner.getDouble(2)+1-b.getWidth())/2,s.getDouble(1)+(inner.getDouble(1)+inner.getDouble(3)+1-b.getHeight())/2,u);
            }
            // 难度选择条（展开时）：同 iOS drawV4，8 格 slot.png 加数字，当前档不透明、其余 82%。 | Difficulty picker (when open): as iOS drawV4,
            // 8 slot.png tiles with digits, the current one opaque and the rest at 82%.
            if(layout[4+15*5+3]!=0) {
                JSONObject pk=hudUi.getJSONObject("pick"); Bitmap tile=image("hud_v4_ui/slot.png");
                for(int i=0;i<8;i++) {
                    double cx=pk.getDouble("center_x")+(i-3.5)*pk.getDouble("pitch"), cy=pk.getDouble("y"); int alpha=i+1==players?255:209;
                    paint.setAlpha(alpha);
                    if(tile!=null) c.drawBitmap(tile,null,new RectF(hudX(cx-tile.getWidth()/2.0,u),(float)(cy-tile.getHeight()/2.0)*u,hudX(cx+tile.getWidth()/2.0,u),(float)(cy+tile.getHeight()/2.0)*u),paint);
                    Bitmap d=image("hud_v4_ui/diff_"+(i+1)+".png");
                    if(d!=null) c.drawBitmap(d,null,new RectF(hudX(cx-d.getWidth()/2.0,u),(float)(cy-d.getHeight()/2.0)*u,hudX(cx+d.getWidth()/2.0,u),(float)(cy+d.getHeight()/2.0)*u),paint);
                    paint.setAlpha(255);
                }
            }
        } catch(Exception ignored) { /* 缺损美术不制造HUD数据。 | Missing art never fabricates HUD values. */ }
    }
    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas); if (!inputEnabled) return; nativeLayout(layout); System.arraycopy(layout, 0, drawn, 0, layout.length); drawnValid=true;
        canvas.save(); canvas.scale(density,density);
        // 控件画在画面矩形里，黑边不画东西：与按下换算同一个矩形，按钮画在哪就能按在哪。
        // Controls are drawn inside the picture rectangle and nothing on the bars: the same rectangle the presses are
        // converted through, so a button is where it is drawn.
        if (vpValid) { canvas.translate(vp[0]/density,vp[1]/density); canvas.clipRect(0,0,vp[2]/density,vp[3]/density); }
        if ((hudMask&1)==0) { hud(canvas); canvas.restore(); return; }   // 手柄模式：不画触屏控件与摇杆，只画 hud() 里的文字部分 | controller mode: no touch controls or joystick, only the text part of hud()
        boolean navigation=layout[2]!=0;
        float k=layout[4+12*5+2]/60;   // 与 iOS 同一个字号 / 粗细单位：摇杆半径 / 60 | the same size unit as iOS: joystick radius / 60
        // 同 iOS drawRect：底栏六格（腰带 / 十字）由底栏画、这里只留命中区；难度与 1–8 选择条也由底栏画（hud）。
        // As iOS drawRect: the six bar cells (belt / cross) are drawn by the bar, only their hit areas here; difficulty and the 1-8 picker too (hud).
        for (int i=0;i<14;++i) {
            int p=4+i*5; if (layout[p+3]==0 || i==12) continue;
            float x=layout[p], y=layout[p+1], r=layout[p+2];
            if (r<=0 || (i>5 && i<=10)) continue;
            boolean held=layout[p+4]!=0;
            if(!navigation && i<=5) { skill(canvas,i,x,y,r,held,navigation); continue; }
            if(!navigation && i==13) { button(canvas,x,y,r,held,false,false); float ts=12*k; caption(canvas,label(i,false),x,y-ts*.65f,ts,held?1:.86f); continue; }
            // 其余圆钮：原版圆框图（按下更实）+ 图标或字。地图钮平时画原版地图图标（36×36 一格，按住第 1 格）；拖技能时变「取消」：暗红底 + 字。
            // Other buttons: the original circle image (more opaque when held) + icon or text. The map button draws the original map icon (36x36 frames,
            // frame 1 held); while a skill is dragged it becomes "Cancel": a dark red base + text.
            RectF b=new RectF(x-r,y-r,x+r,y+r);
            boolean cancel=!navigation && i==11 && layout[HUD+15]!=0;
            if(cancel) { Paint f=new Paint(); f.setColor(rgba(.55,.05,.04,.36)); canvas.drawRect(b,f); }
            if(circleArt!=null) { bitmapPaint.setAlpha(held?153:71); canvas.drawBitmap(circleArt,null,b,bitmapPaint); bitmapPaint.setAlpha(255); }
            if(!navigation && i==11 && !cancel && mapArt!=null && mapArt.getWidth()>=2*mapArt.getHeight()) {
                int f=mapArt.getHeight(),sx=held?f:0; float half=r*.62f;
                bitmapPaint.setAlpha(held?255:209); canvas.drawBitmap(mapArt,new Rect(sx,0,sx+f,f),new RectF(x-half,y-half,x+half,y+half),bitmapPaint); bitmapPaint.setAlpha(255);
                continue;
            }
            String text=navigation && i==5 ? "" : label(i,navigation); float ts=(i==0?13:10)*k;
            caption(canvas,text,x,y-ts*.6f,ts,held?1:.84f);
        }
        // 摇杆：面板开着、死亡时不画；底盘在起点，摇杆头按推量偏移（同 iOS）。 | Joystick: not drawn with a panel open or when dead; base at the origin, thumb offset by the deflection (as iOS).
        int jp=4+12*5;
        if (layout[jp+3]!=0 && !navigation && layout[HUD+14]==0 && layout[jp+2]>0) {
            float jx=layout[jp],jy=layout[jp+1],jr=layout[jp+2]; boolean active=layout[jp+4]!=0;
            drawArt(canvas,"stick:1:"+(active?1:0),jx-jr,jy-jr,2*jr,(bc,side,t,sc)->joyBaseArt(bc,side,active));
            float tx=jx+layout[HUD+16]/32767f*jr, ty=jy-layout[HUD+17]/32767f*jr, th=23*k;
            drawArt(canvas,"stick:0:"+(active?1:0),tx-th,ty-th,2*th,(bc,side,t,sc)->joyThumbArt(bc,side,active));
        }
        hud(canvas);
        canvas.restore();
    }
}
