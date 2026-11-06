package com.xiaoboluo.d2r;

// 生成失败页 / 熔断页上手柄 A 键的去向（纯判定，javac 就能单测；DmActivity.dispatchKeyEvent 用）。
// 页上可能同时有「分享诊断日志」与「再试一次」两个按钮，手柄十字键照系统焦点在两者间移动；A 只触发当前有焦点的那个。
// 没有任何按钮有焦点时（例如焦点被系统清掉）才按页面缺省：熔断页 = 再试一次，否则 = 分享。触屏点按钮不经过这里。
// Where controller A goes on the generation failure / breaker page (pure decision, unit-testable with plain javac; used by DmActivity.dispatchKeyEvent).
// The page may carry both "Share Diagnostics" and "Try again"; the D-pad moves the system focus between them and A activates only the focused one.
// Only when no button has focus (e.g. the system cleared it) does the page default apply: breaker page = Try again, otherwise = Share. Touch taps never come here.
final class GenScreenKeys {
    private GenScreenKeys() {}

    static final int NONE = 0, SHARE = 1, RETRY = 2;

    // focused：当前有焦点的按钮（NONE / SHARE / RETRY）；shareShown / retryShown：页上有没有这个按钮 | focused: the button holding focus; *Shown: whether the page has that button
    static int aTarget(int focused, boolean shareShown, boolean retryShown) {
        if (focused == SHARE && shareShown) return SHARE;
        if (focused == RETRY && retryShown) return RETRY;
        if (retryShown) return RETRY;
        return shareShown ? SHARE : NONE;
    }

    // 去向 → 动作：RETRY 调 retry、SHARE 调 share、NONE 什么都不做。DmActivity 只经这里分发，接线由 GenScreenKeysTest 与 test_gen_screen_focus.py 锁住（terra T4 P2）
    // target -> action: RETRY runs retry, SHARE runs share, NONE does nothing. DmActivity dispatches only through here; the wiring is pinned by GenScreenKeysTest and test_gen_screen_focus.py (terra T4 P2)
    static void dispatch(int target, Runnable share, Runnable retry) {
        if (target == RETRY) retry.run();
        else if (target == SHARE) share.run();
    }

    // 提示行里 A 键的键帽字：PlayStation（索尼 vendor 0x054c）的下键是 ✕，Xbox / Switch / 掌机自带键位都写 A（同 pad_android.c glyph_of 的分法）
    // the glyph for the A key in the hint line: the PlayStation (Sony vendor 0x054c) bottom button is ✕; Xbox / Switch / built-in controls read A (same split as glyph_of in pad_android.c)
    static final int VENDOR_SONY = 0x054c;
    static String aGlyph(int vendorId) { return vendorId == VENDOR_SONY ? "\u2715" : "A"; }
}
