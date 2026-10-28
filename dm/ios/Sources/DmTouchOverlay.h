#import <UIKit/UIKit.h>
#include "dm_touch_controls.h"
#include "dm_touch_game_state.h"
#include "../../host/dm_hud.h"

// 绘制层不吃触点；真实事件由DmMetalView统一收集并保留dm_touch_set通路。
// The drawing layer does not consume touch points; real events are collected in one place by DmMetalView, keeping the dm_touch_set path.
@interface DmTouchOverlay : UIView
@property(nonatomic, assign) const dm_tc_state *controls;
@property(nonatomic, assign) BOOL navigationMode;
@property(nonatomic, assign) dm_tg_snapshot nativeMapping;
@property(nonatomic, assign) dm_hud_info hud;
@property(nonatomic, assign) BOOL padMode;   // 手柄模式（front_ios.m padMode:）：按 dm/host/dm_hud_mask.h 只画经验行文字，不画触屏控件与底栏（原版手柄界面自带） | controller mode (front_ios.m padMode:): per dm/host/dm_hud_mask.h only the experience line text is drawn, no touch controls or bar (the original controller UI has its own)
@property(nonatomic, assign) int fps;   // 「显示帧数」开着时的实际上屏帧率，接在经验行末尾画；0 = 不画（front_ios.m attach_fps_hud） | actual presented frame rate when "Show frame rate" is on, drawn at the end of the experience line; 0 = not drawn
@property(nonatomic, assign) BOOL dialogOnly;   // 只开着 NPC 对话菜单：底栏照常画 | only the NPC dialog menu is open: the bottom bar is drawn as usual
@property(nonatomic, assign) BOOL hudBlocked;   // 面板栈里有非常驻面板：UIKit 底栏让开（front_ios.m hud_stack_message）   // 宿主每拍发布的 HUD 数字（底部 UI v4：经验条、角标、难度） | the panel stack has a non-persistent panel: the UIKit bottom bar gets out of the way (front_ios.m hud_stack_message)   // HUD numbers published by the host every tick (bottom UI v4: experience bar, badges, difficulty)
- (NSDictionary *)controlMetadata;
@end
