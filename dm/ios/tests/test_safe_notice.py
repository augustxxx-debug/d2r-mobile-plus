"""崩溃围栏的主菜单游戏内提示框（用户：「不要用原生的弹窗，用游戏内弹窗」）：布局控件齐、按钮发空操作消息、手柄版不带锚点；
六种语言的标题 / 正文按字宽估算不超出文字框（拉丁 / 西里尔字母按 0.62 个字号宽，汉字 / 韩文按 1 个字号宽，偏保守）；宿主处理确定与手柄 A；原生弹框已删。
Crash fence's in-game notice box on the main menu (user: "do not use a native popup, use an in-game one"): layout widgets present, the button sends a no-op message, the controller version has no anchor;
title / body of all six languages fit the text boxes by an estimated glyph width (Latin / Cyrillic 0.62 of the point size, CJK / Hangul 1.0, on the conservative side); the host handles OK and controller A; native popups are gone."""
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
import touch_layout  # noqa: E402
import dm_i18n  # noqa: E402

BOX_W = 1700   # 标题 / 正文文字框宽（touch_layout._safe） | width of the title / body text boxes (touch_layout._safe)


def est_width(line, pt):
    return sum(pt if re.match(r'[ᄀ-ᇿ　-鿿가-힯＀-￯]', ch) else pt * 0.62 for ch in line)


class Layout(unittest.TestCase):
    def test_widgets(self):
        for name, anchored in (('_SAFE', True), ('_SAFE_PAD', False)):
            t = getattr(touch_layout, name)
            for w in ('TouchSafeBg', 'TouchSafeTitle', 'TouchSafeDesc', 'TouchSafeOk'):
                self.assertIn('"name": "%s"' % w, t, (name, w))
            self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchSafeOk"', t)
            self.assertIn('"textString": "@dmConfirm"', t)
            self.assertEqual('"anchor"' in t, anchored, name)
            self.assertNotIn('"x": 9', re.sub(r'"x": 99999', '', t), 'everything starts off screen: ' + name)
        self.assertEqual(touch_layout._SAFE_STUB.count('\n'), 4)

    def test_both_main_menus_carry_it(self):
        fake = '{\n    "type": "Widget", "name": "MainMenuPanel",\n    "children": [\n        { "type": "Widget", "name": "X" },\n    ]\n}'
        self.assertIn('TouchSafeOk', touch_layout.pad_mainmenu(fake))
        src = (DM / 'ios' / 'tools' / 'touch_layout.py').read_text(encoding='utf-8')
        self.assertIn("+ _UPDATE + _SAFE + '    ' + s[i:], n + 2", src)
        self.assertIn('_UPDATE_STUB + _SAFE_STUB + mid[i:]', src)

    def test_update_notice_still_has_no_buttons(self):   # 四审 Q4-04 不受影响 | fourth review Q4-04 unaffected
        for name in ('_UPDATE', '_UPDATE_PAD'):
            self.assertNotIn('ButtonWidget', getattr(touch_layout, name))


class Text(unittest.TestCase):
    def test_six_languages_fit(self):
        rows = {r[0]: r[1:] for r in dm_i18n.rows()}
        langs = ('中文', 'English', 'Español', 'Español MX', 'Русский', '한국어')
        for key, pt in (('dmSafeModeTitle', 48), ('dmSafeModeMsg', 36)):
            texts = rows[key]
            n = texts[1].count('\n')
            for lang, t in zip(langs, texts):
                self.assertEqual(t.count('\n'), n, (key, lang))
                for line in t.split('\n'):
                    self.assertLessEqual(est_width(line, pt), BOX_W, (key, lang, line, est_width(line, pt)))
        self.assertEqual(rows['dmSafeModeMsg'][1].count('\n'), 2, 'three lines (the body box is 300 high)')


class Host(unittest.TestCase):
    def test_host_handles_ok_and_pad_a(self):
        c = (DM / 'host' / 'dm_crashfence.c').read_text(encoding='utf-8')
        self.assertIn('"TouchSafeOk"', c); self.assertIn('dm_padmode_page_keys(DM_BTN_A)', c)
        self.assertIn('前端：提示已显示：', c); self.assertIn('玩家点了确定', c)
        self.assertIn('dm_crashfence_pump()', (DM / 'host' / 'hle_hid.c').read_text(encoding='utf-8'))
        self.assertIn('dm_crashfence_on_uimsg(cat, act, arg)', (DM / 'host' / 'diag.c').read_text(encoding='utf-8'))

    def test_native_popups_gone(self):
        for rel in ('ios/Sources/front_ios.m', 'host/front_mac.m', 'host/dm_crashfence.c', 'host/dm_crashfence.h'):
            self.assertNotIn('front_notice', (DM / rel).read_text(encoding='utf-8'), rel)


if __name__ == '__main__':
    unittest.main(verbosity=1)
