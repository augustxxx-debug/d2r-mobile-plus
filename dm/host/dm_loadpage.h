// 加载页（游戏内容导入 / 准备）：iOS、安卓、Mac 共用的页面状态机、进度合成、文案、素材选择与布局。
// 平台层（ios/Sources/dm_loadpage_cg.m、android 的 LoadPageView）只做三件事：把一张背景图按这里算出的裁切矩形画满屏、按这里的矩形画进度条和四行字、放这里指定的音乐。
// 这一页在宿主起来之前显示（游戏还没跑），所以不能用游戏自己的控件；文字走编译期多语表 dm_ui()（dm/i18n/ui_text.tsv 的 dmLoad* 行）。
// 时间由平台传入（毫秒单调时钟），模块本身不读时钟、不起线程、不分配堆内存：单测可以完全确定地跑。
// 阶段：平台在开始时报一串阶段（按发生顺序）和各自的预计时长（权重，不给就用缺省），之后只报「第几个阶段走到几成 + 细节」；
// 总进度 = 已完成阶段的权重 + 当前阶段权重 × 几成，并且只增不减（阶段回报倒退、重算都不会让条子往回缩）。iOS 只有一个阶段（下载 / 导入），安卓是导入 → 着色器缓存等多个阶段连续推进。
// Loading page (game content import / preparation): the page state machine, progress composition, text, art selection and layout shared by iOS, Android and the Mac.
// The platform layer (ios/Sources/dm_loadpage_cg.m, Android's LoadPageView) does three things only: draws one background image full screen by the crop rectangle computed here,
// draws the progress bar and the four text lines by the rectangles computed here, and plays the music named here.
// This page is shown before the host starts (the game is not running yet), so it cannot use the game's own widgets; text comes from the compile-time i18n table dm_ui() (the dmLoad* rows of dm/i18n/ui_text.tsv).
// Time is passed in by the platform (monotonic milliseconds); the module itself reads no clock, starts no thread and allocates no heap memory: the unit test runs fully deterministically.
// Stages: at the start the platform reports a list of stages (in the order they happen) and the expected duration of each (weight; defaults when not given); afterwards it only reports
// "stage N is at fraction f + details"; overall = weights of the finished stages + weight of the current stage × fraction, and it never decreases (a stage reporting backwards or recomputing
// never shrinks the bar). iOS has a single stage (download / import), Android advances through import → shader cache etc. continuously.
#pragma once
#include <stdint.h>
#include <stddef.h>

enum {   // 阶段种类（文案键 dmLoadStage*、缺省权重见 dm_loadpage.c k_kind） | stage kinds (text keys dmLoadStage*, default weights in dm_loadpage.c k_kind)
    DM_LP_DOWNLOAD,   // iOS：托管 / 直接下载资源包 | iOS: hosted / direct download of the asset pack
    DM_LP_IMPORT,     // 安卓：把导入包拷进内部目录；iOS：把资源文件夹拷进来后的接手 | Android: copy the import pack into internal storage; iOS: taking over after the resource folder was copied in
    DM_LP_PATCH,      // 安卓手机端生成：改写游戏程序 | Android on-phone generation: rewrite the game program
    DM_LP_ASSETS,     // 安卓手机端生成：整理资源 | Android on-phone generation: prepare assets
    DM_LP_OVERLAY,    // 安卓手机端生成：界面覆盖层 | Android on-phone generation: UI overlay
    DM_LP_PACKS,      // 安卓手机端生成：着色器打包 | Android on-phone generation: shader packing
    DM_LP_SHADERS,    // 着色器缓存（安卓手机端生成的最长一步；iOS 壳包的源码着色器预编译也可用它） | shader cache (the longest step of Android on-phone generation; the iOS shell package's source-shader precompile can use it too)
    DM_LP_INSTALL,    // 安卓手机端生成：成品换进运行目录 | Android on-phone generation: move the result into the runtime directory
    DM_LP_VERIFY,     // 校验（romfs_validate） | verification (romfs_validate)
    DM_LP_PREPARE,    // 导入之后进游戏前的秒级准备（安卓电脑出的导入包：着色器已翻译好，手机上只剩管线现编几秒；缺省权重按「导入 85% → 准备 15%」） | seconds of preparation between import and the game (Android pack made on a computer: shaders already translated, only a few seconds of pipeline builds; default weights follow "import 85% → prepare 15%")
    DM_LP_KIND_N
};
enum { DM_LP_MAX_STAGES = 8 };
enum { DM_LP_IDLE, DM_LP_RUNNING, DM_LP_DONE, DM_LP_FAILED };   // dm_loadpage_state

typedef struct {   // 一次进度回报的细节（不知道的填 0） | details of one progress report (0 when unknown)
    int64_t items_done, items_total;   // 第几个 / 共几个文件 | file N of M
    int64_t bytes_done, bytes_total;   // 已到手 / 总字节（算速度与 GB 显示） | bytes done / total (for speed and the GB display)
} dm_lp_detail;

// 开始：kinds[n] 按发生顺序；weight_ms 可为 NULL（缺省权重）或逐项给（0 = 该项用缺省）。再调一次 = 重新开始（状态清零）
// Begin: kinds[n] in the order they happen; weight_ms may be NULL (default weights) or per stage (0 = default for that one). Calling again = start over (state reset)
void dm_loadpage_begin(const int *kinds, int n, const int *weight_ms, uint64_t now_ms);
// 第 stage 个阶段走到 frac（0..1）；d 可为 NULL。报到后面的阶段 = 前面的阶段算完成；报回前面的阶段不让总进度倒退
// Stage `stage` is at frac (0..1); d may be NULL. Reporting a later stage marks the earlier ones complete; reporting an earlier one again never moves overall backwards
void dm_loadpage_set(int stage, double frac, const dm_lp_detail *d, uint64_t now_ms);
// 提示页（不是进度）：同一套背景 / 字标 / 三层字，不画进度条、不放音乐、不显示页脚；标题与副文是多语键，正文是平台给的串（NULL = 不写）。begin 会清掉它
// A notice page (not progress): the same background / wordmark / three text levels, no bar, no music, no footer; title and sub are i18n keys, the body a string from the platform (NULL = none). begin clears it
void dm_loadpage_prompt(const char *title_key, const char *body, const char *sub_key);
int dm_loadpage_prompting(void);
void dm_loadpage_note(const char *text);                     // 平台给的正文替换（说明 / 指引，如「把资源文件夹拷进…」）；NULL 取消，回到自动细节行 | body override from the platform (instructions); NULL = back to the automatic detail line
void dm_loadpage_hint(const char *key);                      // 副文的多语键替换（NULL = 按阶段种类的缺省提示） | sub-line i18n key override (NULL = the default hint of the stage kind)
void dm_loadpage_fail(const char *reason, uint64_t now_ms);  // 失败：标题换「无法继续」，正文 = reason（平台已取好语言） | failure: title becomes "could not continue", body = reason (already localised by the platform)
void dm_loadpage_finish(uint64_t now_ms);                    // 全部完成：总进度到 1，写一行「加载页：」汇总日志（分享诊断日志摘要取这一行） | all done: overall = 1, one 「加载页：」 summary log line (the share-log summary picks it up)
const char *dm_loadpage_last_line(void);   // 最近一次「加载页：完成 / 失败」那一行，没有时为空串 | the latest 「加载页：」 finish / failure line, empty when none
int dm_loadpage_state(void);
double dm_loadpage_overall(void);                            // 0..1，只增不减 | 0..1, never decreasing
int dm_loadpage_percent(void);                               // 向下取整的百分数 0..100 | floor percentage 0..100
int dm_loadpage_stage(void);                                 // 当前阶段序号（-1 = 没开始） | current stage index (-1 = not started)
int dm_loadpage_stage_kind(int stage);                       // 该序号的种类（-1 = 越界） | kind of that stage index (-1 = out of range)
int dm_loadpage_stage_ms(int stage);                         // 该阶段用了多久（进行中 = 到最后一次回报；没开始 = 0） | how long that stage took (running = up to the last report; not started = 0)
// 一行汇总：「导入 12.3 秒 · 着色器缓存 371.0 秒 · 合计 383.3 秒」（日志用，中文）；返回写入长度 | one summary line for the log (Chinese); returns the length written
int dm_loadpage_summary(char *buf, size_t n, uint64_t now_ms);

// 社区信息（官方免费渠道，加载页底部两行 + 一个复制钮）：只在首次导入 / 生成这段长等待里显示——平台只在这条路上起拉取（dm_manifest.h 校验过的清单），
// 拿到后调 dm_loadpage_community；没调、或两项都空 = 不显示，不内置缺省值。状态不随 begin 清零（清单可能在页面开始之前就到了）。
// 页面进行中 / 失败时显示，完成后不显示。复制钮放进剪贴板的串：有官网时显示官网 + 群号、复制群号（没有群号才复制官网）；没有官网时按语言取——dmLoadCommPrefer 为 qq 的语言（中文）先群号，其余先链接；只有一项就用那一项。
// Community info (official free channels, two lines + one copy button at the bottom of the page): shown only during the first import / generation wait - the platform starts
// the fetch only on that path (a manifest validated by dm_manifest.h) and calls dm_loadpage_community once it has one; never called, or both empty = not shown, no built-in default.
// The state is not reset by begin (the manifest may arrive before the page starts). Shown while running / failed, not after done. The string the copy button puts on the clipboard
// depends on the language: languages whose dmLoadCommPrefer is qq (Chinese) prefer the group number, the others the link; with only one item, that one.
void dm_loadpage_community(const char *qq, const char *github, const char *site);   // NULL / 空串 = 没有这一项；site = 项目官网（有就与群号一起显示） | NULL / empty = no such item; site = project site (shown next to the group number when present)
int dm_loadpage_community_shown(void);                            // 1 = 现在画社区两行与复制钮 | 1 = the community lines and copy button are drawn now
const char *dm_loadpage_copy_value(void);                         // 复制钮要放进剪贴板的串；不显示时为空串 | what the copy button puts on the clipboard; empty when not shown
// 平台写完剪贴板调：ok = 写成没有，pad = 手柄 A 还是触屏点的；按钮字换成「已复制 / 复制失败」2 秒，写一行「加载页：复制…」日志
// Called by the platform after writing the clipboard: ok = whether it worked, pad = controller A or a tap; the button reads "Copied / Copy failed" for 2 s, one 「加载页：复制…」 log line
void dm_loadpage_copied(int ok, int pad, uint64_t now_ms);
// 输入模式（§2.7）：玩家按了手柄键 = 1，摸了屏幕 = 0。手柄模式下复制钮画成焦点态，按钮字前加 A 键图标（平台画），A 键复制
// Input mode (§2.7): the player pressed a controller key = 1, touched the screen = 0. In controller mode the copy button is drawn focused with an A glyph in front (drawn by the platform); A copies
void dm_loadpage_pad(int on);
int dm_loadpage_pad_mode(void);

typedef struct {   // 四行字 + 百分数。层级：标题（阶段动词短语）/ 正文（事实：文件数、字节、速度、剩余）/ 副文（一句祈使句的提示 / 状态） | four lines + percent. Levels: title (stage verb phrase) / body (facts) / sub (one imperative hint / status)
    char title[160], body[320], sub[256], percent[8];
    char comm1[224], comm2[160], copy[64];   // 社区两行（页脚：12 点、半透明）与复制钮上的字；不显示时为空 | the two community lines (sub style) and the copy button label; empty when not shown
    int copy_state;                          // 0 平常 / 1 已复制 / -1 复制失败（2 秒内） | 0 normal / 1 copied / -1 copy failed (within 2 s)
    const char *brand;   // 页面左上角的字标（不翻译） | the wordmark at the top left (not translated)
} dm_lp_text;
void dm_loadpage_text(dm_lp_text *t, uint64_t now_ms);

typedef struct { double x, y, w, h; } dm_lp_rect;
typedef struct { double w, h, inset_l, inset_t, inset_r, inset_b; } dm_lp_screen;   // 逻辑点（iOS pt / 安卓 dp）与安全区内缩 | logical points (iOS pt / Android dp) and safe-area insets
typedef struct {
    int landscape;                 // 1 = 横屏（w >= h） | 1 = landscape (w >= h)
    const char *bg_name;           // 背景图文件名（包内 loadpage/ 目录） | background file name (loadpage/ in the package)
    int bg_w, bg_h;                // 该图的像素尺寸 | that image's pixel size
    dm_lp_rect bg_src;             // 从图里取的矩形（像素）：cover 裁切，短边对齐、长边裁掉，焦点居中略偏下 | rectangle taken from the image (pixels): cover crop, short side fitted, long side cropped, focus centred slightly low
    dm_lp_rect bg_dst;             // 画到屏上的矩形（整屏） | destination on screen (full screen)
    dm_lp_rect bar, bar_fill;      // 进度条底与已走部分（bar_fill.w 按总进度） | bar track and filled part (bar_fill.w by overall)
    dm_lp_rect title, percent, body, sub, brand;   // 文字行的框（左对齐；percent 右对齐到条尾）；brand 只在字标图读不到时用 | text line boxes (left aligned; percent right aligned to the bar end); brand only when the wordmark image is unavailable
    dm_lp_rect mute;   // 右上角静音开关（40×40 点圆钮，安全区内、与字标同一条上沿） | the mute toggle at the top right (40×40 pt round button, inside the safe area, on the same top line as the wordmark area)
    int show_bar;                  // 1 = 画进度条（提示页为 0） | 1 = draw the bar (0 on a notice page)
    int comm;                      // 1 = 画社区两行与复制钮（这时进度那一组整体上移） | 1 = draw the community lines and copy button (the progress group moves up as a whole)
    dm_lp_rect comm1, comm2, copy; // 页脚：社区两行（左对齐到内容左沿）与复制钮（右对齐到内容右沿、跨两行高） | footer: the two community lines (left aligned to the content edge) and the copy button (right aligned to the content edge, two lines tall)
    dm_lp_rect logo; const char *logo_name; int logo_w, logo_h;   // 页面中部的字标图（自制 loadpage_logo.png）：横向居中、等比缩放、在安全区与标题之间 | the wordmark image in the middle of the page (self-made loadpage_logo.png): centred, aspect kept, between the safe area and the title
    double comm_pt; int comm_alpha;   // 页脚字号 12、字的不透明度（0–255，DM_LP_COMM_ALPHA）：比副文再退一级，不抢进度信息 | footer font size 12 and text opacity (0-255): one step below the sub line, so it does not compete with the progress
    double title_pt, body_pt, sub_pt, brand_pt;    // 字号：24 / 18 / 14（相邻比 1.29–1.33，同 §2.6 的三级） | font sizes 24 / 18 / 14 (adjacent ratio 1.29–1.33, the three levels of §2.6)
    double bar_radius;
} dm_lp_layout;
void dm_loadpage_layout(const dm_lp_screen *s, dm_lp_layout *out);
int dm_loadpage_hit_mute(const dm_lp_layout *L, double x, double y);       // 点在开关上（判定区比图标四周各大 12 点）| the tap is on the toggle (hit area 12 pt larger than the icon on each side)
int dm_loadpage_hit_copy(const dm_lp_layout *L, double x, double y);   // 点在复制钮上（判定区四周各大 8 点）；不显示时为 0 | the tap is on the copy button (hit area 8 pt larger each side); 0 when not shown
// 纯函数：图 iw×ih 铺满 dw×dh 的 cover 裁切；焦点 (fx, fy) 为图内 0..1 坐标，裁切框尽量以它为中心、不越界。返回 src（图内像素矩形）
// Pure function: cover crop of an iw×ih image onto dw×dh; the focus (fx, fy) in 0..1 image coordinates is kept as the crop centre where possible without leaving the image. Returns src (image pixel rect)
void dm_loadpage_cover(double iw, double ih, double dw, double dh, double fx, double fy, dm_lp_rect *src);
const char *dm_loadpage_bg_name(double w, double h, int *bg_w, int *bg_h);   // 横 / 竖两张里选一张 | pick one of the landscape / portrait images
const char *dm_loadpage_logo_name(int *w, int *h);                          // 字标图文件名与像素尺寸 | wordmark file name and pixel size
const char *dm_loadpage_music_name(void);                                  // 循环音乐文件名（包内 loadpage/ 目录） | loop music file name (loadpage/ in the package)
// 静音开关（页面右上角）：缺省有声音（用户 2026-10-29：右上角给开关，缺省放音乐）；平台记住玩家的选择，启动时用 set_muted 告诉核心。begin 不改它
// Mute toggle (top right of the page): sound on by default (user 2026-10-29: a toggle at the top right, music on by default); the platform remembers the player's choice and tells the core with set_muted at start. begin leaves it alone
void dm_loadpage_set_muted(int muted);
int dm_loadpage_muted(void);
int dm_loadpage_toggle_muted(void);                                        // 点一下：翻转并写一行日志，返回新值 | one tap: flip, log one line, return the new value
int dm_loadpage_music_wanted(void);                                        // 1 = 该放音乐（页面在、没完成 / 失败、且没静音） | 1 = music should play (page up, not done / failed, not muted) | 1 = music should play (running); done / failed / idle = 0

// 颜色（0xRRGGBB）：游戏外画不到原版色票，这里用其近似值——标题 ≈ 面板标题金黄 $FontColorGoldYellow，正文 ≈ 弹窗正文 $FontColorLightGold，副文 ≈ 说明小字 $FontColorLightGray；
// 进度条用原版红 / 暗底。改颜色只改这里，两端同步
// Colours (0xRRGGBB): outside the game the original colour tokens cannot be referenced, so their approximations are used - title ≈ panel-title gold $FontColorGoldYellow, body ≈ modal body $FontColorLightGold,
// sub ≈ hint grey $FontColorLightGray; the bar uses the original red / dark track. Change colours only here; both platforms follow
enum { DM_LP_COMM_ALPHA = 140 };   // 页脚字约 55% 不透明（复制反馈「已复制 / 复制失败」画满不透明） | footer text about 55% opaque (the copy feedback is drawn fully opaque)
enum { DM_LP_COL_TITLE = 0xC8A850, DM_LP_COL_BODY = 0xE3D29B, DM_LP_COL_SUB = 0xA9A49A, DM_LP_COL_PERCENT = 0xE3D29B,
       DM_LP_COL_BAR_FILL = 0xB8401A, DM_LP_COL_BAR_TRACK = 0x2A1A12, DM_LP_COL_BAR_EDGE = 0x5A4020, DM_LP_COL_SHADOW = 0x000000 };

// 多语取串钩子：缺省 = dm_ui()（dm_i18n.c）。安卓导入前还没装宿主库，单独的小库里由 JNI 按系统语言换成 dm_ui_group(key, group)
// i18n hook: default = dm_ui() (dm_i18n.c). On Android, before the host library is loaded, the small standalone library has JNI swap in dm_ui_group(key, group) by system language
const char *dm_loadpage_ui(const char *key);
void dm_loadpage_ui_set_group(int group);   // 独立库用：DM_LG_*；-1 = 回到 dm_ui() | for the standalone library: DM_LG_*; -1 = back to dm_ui()
