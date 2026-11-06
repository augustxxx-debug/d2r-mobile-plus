// 加载页「这个按键算不算手柄」的判定（外审 EXT6-04）：按完整来源位分别认 GAMEPAD / JOYSTICK / DPAD，isGamepadButton 兜底。
// 来源常数照 android.view.InputDevice 抄成字面量，类本身不依赖 android.*，桌面 JVM 上单测（tests/InputSourceTest.java）。
// SOURCE_GAMEPAD（0x401）与键盘 SOURCE_KEYBOARD（0x101）共用 SOURCE_CLASS_BUTTON 这一位，只做按位与非零会把音量键、键盘当成手柄。
// Whether a key counts as a controller on the loading page (external review EXT6-04): the full source bits of GAMEPAD / JOYSTICK / DPAD each, with isGamepadButton as the fallback.
// The source constants are copied from android.view.InputDevice as literals so the class does not depend on android.* and is unit tested on a desktop JVM (tests/InputSourceTest.java).
// SOURCE_GAMEPAD (0x401) shares the SOURCE_CLASS_BUTTON bit with SOURCE_KEYBOARD (0x101); a plain non-zero AND treats volume keys and keyboards as a controller.
package com.xiaoboluo.d2r;

final class InputSource {
    static final int SOURCE_KEYBOARD = 0x00000101, SOURCE_DPAD = 0x00000201, SOURCE_GAMEPAD = 0x00000401, SOURCE_JOYSTICK = 0x01000010;
    private static boolean has(int source, int mask) { return (source & mask) == mask; }
    static boolean isPad(int source, boolean gamepadButton) {
        return has(source, SOURCE_GAMEPAD) || has(source, SOURCE_JOYSTICK) || has(source, SOURCE_DPAD) || gamepadButton;
    }
    private InputSource() {}
}
