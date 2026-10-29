package com.xiaoboluo.d2r;

// 加载页手柄来源判定的矩阵（外审 EXT6-04）：键盘 / 音量键 / 触屏 / 鼠标 / 手柄 / 摇杆 / 方向键 / 手柄键名兜底。
// Source matrix of the loading page's controller test (external review EXT6-04): keyboard / volume keys / touch / mouse / gamepad / joystick / D-pad / gamepad key code fallback.
public class InputSourceTest {
    private static int cases;
    private static void equal(boolean want, boolean actual, String what) { cases++; if (want != actual) throw new AssertionError(what + ": want " + want); }
    public static void main(String[] a) {
        final int TOUCHSCREEN = 0x00001002, MOUSE = 0x00002002, STYLUS = 0x00004002, TRACKBALL = 0x00010004, CLASS_BUTTON = 0x00000001;
        equal(false, InputSource.isPad(InputSource.SOURCE_KEYBOARD, false), "键盘（含音量键） | keyboard (volume keys too)");
        equal(false, InputSource.isPad(CLASS_BUTTON, false), "只有按键类位 | button class bit alone");
        equal(false, InputSource.isPad(TOUCHSCREEN, false), "触屏 | touchscreen");
        equal(false, InputSource.isPad(MOUSE, false), "鼠标 | mouse");
        equal(false, InputSource.isPad(STYLUS, false), "手写笔 | stylus");
        equal(false, InputSource.isPad(TRACKBALL, false), "轨迹球 | trackball");
        equal(false, InputSource.isPad(0, false), "未知来源 | unknown source");
        equal(true, InputSource.isPad(InputSource.SOURCE_GAMEPAD, false), "手柄 | gamepad");
        equal(true, InputSource.isPad(InputSource.SOURCE_JOYSTICK, false), "摇杆 | joystick");
        equal(true, InputSource.isPad(InputSource.SOURCE_DPAD, false), "方向键 | D-pad");
        equal(true, InputSource.isPad(InputSource.SOURCE_GAMEPAD | InputSource.SOURCE_KEYBOARD, false), "手柄 + 键盘合报（奥丁内置手柄） | gamepad + keyboard (Thor's built-in pad)");
        equal(true, InputSource.isPad(InputSource.SOURCE_KEYBOARD, true), "键盘来源但键名是手柄键（adb input gamepad 有时这样） | keyboard source with a gamepad key code");
        equal(true, InputSource.isPad(0, true), "只有键名 | key code only");
        System.out.println("InputSourceTest: " + cases + " 例全过");
    }
}
