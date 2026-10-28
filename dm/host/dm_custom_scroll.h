// 自定义页手柄模式的滚动决策（纯函数，dm_custom.c 用；单测 tests/t_custom_scroll.c）。
// 手柄选到列表框外的行时给原版滚动区发滚轮（每 4 帧一格）直到整行在框内。原先只看「行在不在框内」：最后一行的下沿（加 10 单位边距）比内容
// 还低、滚动区滚到头也露不全，于是每 4 帧发一格、一直发——每格都放原版的点击音，玩家听见「一直在响」（奥丁手柄，2026-10-28 用户）。
// 现在再看「上一格滚轮有没有真的滚动」：同方向、滚动量没变 = 已到头，不再发；换行重新来。
// Scroll decision of the Custom page in controller mode (pure function, used by dm_custom.c; unit test tests/t_custom_scroll.c).
// When the controller selects a row outside the list box the original scroll area is sent wheel notches (one every 4 frames) until the whole row is inside.
// Previously only "is the row inside" was checked: the last row's bottom edge (plus a 10-unit margin) lies below the content, the scroll area cannot show it
// even at its end, so a notch was sent every 4 frames forever -- each one plays the original click sound and the player hears it "ringing nonstop"
// (Thor controller, user 2026-10-28). Now "did the previous notch actually scroll" is checked too: same direction with an unchanged offset = at the end, stop; a new row starts over.
#pragma once
// top / bot：行的上下沿（内容坐标）；off：已滚量；vh：框高；last_dir / last_off：上一格滚轮的方向与发出时的已滚量（没发过：0 / -1）。返回 +1 向上一格、-1 向下一格、0 不发
// top / bot: the row's edges (content coordinates); off: amount scrolled; vh: box height; last_dir / last_off: the previous notch's direction and the amount scrolled when it was sent (none: 0 / -1). Returns +1 one notch up, -1 one notch down, 0 none
static inline int dm_custom_scroll_step(int top, int bot, int off, int vh, int last_dir, int last_off) {
    int want = top < off ? 1 : bot > off + vh ? -1 : 0;
    if (want && want == last_dir && off == last_off) return 0;   // 上一格没滚动：到头了 | the previous notch did not move: at the end
    return want;
}
