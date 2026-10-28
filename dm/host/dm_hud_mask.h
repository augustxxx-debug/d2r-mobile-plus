// 触屏覆盖层在手柄 / 触屏模式下画什么（纯函数，两端共用：iOS DmTouchOverlay.m、Android TouchOverlay 经 pad_android.c；单测 tests/t_hud_mask.c）。
// 触屏模式：触屏控件 + 底部 UI v4 底栏 + 经验行文字全画。手柄模式：原版手柄界面自己有底栏与生命 / 法力数字，只补经验行文字「Lv · % · FPS」
// （原版手柄底栏没有这一行；用户 2026-10-28：进手柄模式后 XP 数字和帧数没了）。覆盖层本身不再因手柄模式整层隐藏。
// What the touch overlay draws in controller / touch mode (pure function shared by both platforms: iOS DmTouchOverlay.m, Android TouchOverlay through pad_android.c; unit test tests/t_hud_mask.c).
// Touch mode: touch controls + the bottom UI v4 bar + the experience line text, all drawn. Controller mode: the original controller UI has its own bar and life / mana numbers,
// so only the experience line text "Lv · % · FPS" is added (the original controller bar lacks it; user 2026-10-28: the XP digits and the frame rate vanished in controller mode).
// The overlay itself is no longer hidden as a whole in controller mode.
#pragma once
enum { DM_HUD_CONTROLS = 1, DM_HUD_BAR = 2, DM_HUD_EXPLINE = 4 };
static inline int dm_hud_draw_mask(int pad_mode) { return pad_mode ? DM_HUD_EXPLINE : DM_HUD_CONTROLS | DM_HUD_BAR | DM_HUD_EXPLINE; }
