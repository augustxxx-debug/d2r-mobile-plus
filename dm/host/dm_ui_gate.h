/* 界面判类里两道「这一下不点」闸门的先后（B143，纯函数，宿主测试 t_ui_gate.c 直接编）。
   modal_open = 最上层开着模态框（名字以 Modal 结尾，见 dm_cursor.c modal_top）；in_modal = 点在框里；settings_cover = 点在被设置页盖住的暂停菜单按钮上。
   模态框优先：框开着时只看点没点在框里——框外 = 9（模态框外，只挡这一下），框里 = 往下照常判，不再看设置页那道。
   原先设置页那道排在前面：设置改了没应用、切页签弹出的确认框盖在设置页上，「应用 / 放弃」正好压在暂停菜单按钮上方（B070 加高下移后），
   点它们被判成 11（被盖住，不点），框外又是 9 —— 整屏点不动，只能杀游戏。返回 -1 = 两道都不拦，接着判。
   Order of the two "do not press this tap" gates in UI classification (B143; pure, compiled directly by the host test t_ui_gate.c).
   modal_open = a modal is on top (name ends in Modal, see modal_top in dm_cursor.c); in_modal = the point is inside it; settings_cover = the point is on a pause menu button covered by the settings page.
   The modal comes first: while it is open only "inside or not" matters -- outside = 9 (outside the modal, only this tap is blocked), inside = carry on classifying, the settings gate is not consulted.
   The settings gate used to run first: the confirmation box shown when switching tabs with unapplied settings sits over the settings page, and its Apply / Discard lie right over pause menu buttons
   (after B070 made it taller and moved them down), so they were classified 11 (covered, no click) and outside the box is 9 -- nothing on screen responded and the game had to be killed. -1 = neither gate blocks, carry on. */
#pragma once
static inline int dm_ui_gate(int modal_open, int in_modal, int settings_cover) {
    if (modal_open) return in_modal ? -1 : 9;
    return settings_cover ? 11 : -1;
}
