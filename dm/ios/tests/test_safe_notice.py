"""崩溃围栏的主菜单游戏内提示框（用户：「不要用原生的弹窗，用游戏内弹窗」）：布局控件齐、按钮发空操作消息、手柄版不带锚点；
六种语言的标题 / 正文按字宽估算不压边框（拉丁 / 西里尔字母按 0.7 个字号宽，汉字 / 韩文按 1 个字号宽；可用宽 1560 = 边框 1800 减两侧边条与留白）；宿主处理确定与手柄 A；原生弹框已删。
Crash fence's in-game notice box on the main menu (user: "do not use a native popup, use an in-game one"): layout widgets present, the button sends a no-op message, the controller version has no anchor;
title / body of all six languages stay clear of the frame by an estimated glyph width (Latin / Cyrillic 0.7 of the point size, CJK / Hangul 1.0; usable width 1560 = the 1800 frame minus both edge strips and margins); the host handles OK and controller A; native popups are gone."""
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
import touch_layout  # noqa: E402
import dm_i18n  # noqa: E402

# 可用宽：文字框 1700（touch_layout._safe）比边框内沿还宽，按边框内沿再留白算。Mac 实测 0.62 估算放过了一行西语，左端压到边框（字体小型大写字母实宽约 0.67 个字号）
# Usable width: the 1700 text box (touch_layout._safe) is wider than the frame's inner edge, so this counts from the inner edge with a margin. On the Mac a 0.62 estimate let a Spanish line through whose left end ran into the frame (the small-caps font is about 0.67 of the point size wide)
BOX_W = 1560


def est_width(line, pt):
    return sum(pt if re.match(r'[ᄀ-ᇿ　-鿿가-힯＀-￯]', ch) else pt * 0.7 for ch in line)


class Layout(unittest.TestCase):
    def test_widgets(self):
        for name, anchored in (('_SAFE', True), ('_SAFE_PAD', False)):
            t = getattr(touch_layout, name)
            for w in ['TouchSafeTile%d%d' % (r, c) for r in range(5) for c in range(3)] + ['TouchSafeTitle', 'TouchSafeDesc0', 'TouchSafeDesc1', 'TouchSafeDesc2', 'TouchSafeOk']:
                self.assertIn('"name": "%s"' % w, t, (name, w))
            self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchSafeOk"', t)
            self.assertIn('"filename": "Ztouch_safe/tile42"', t)   # 边框 = 我们通知框同一套美术、自己一套切片 | frame = same art as our notification box, own slices
            self.assertNotIn('RectangleWidget', t)   # 不另叠底板：按钮图中间本身是不透明的石纹 | no separate backing: the button image's middle is opaque stone
            self.assertIn('"textString": "@dmConfirm"', t)
            self.assertEqual('"anchor"' in t, anchored, name)
            self.assertNotIn('"x": 9', re.sub(r'"x": 99999', '', t), 'everything starts off screen: ' + name)
        self.assertEqual(touch_layout._SAFE_STUB.count('\n'), 20)
        ov = (DM / 'ios' / 'tools' / 'gen_romfs_overlay.py').read_text(encoding='utf-8')
        self.assertIn("build_notice_slices(romfs,t,nw,folder='ztouch_safe',prefix='safe_')", ov)

    def test_both_main_menus_carry_it(self):
        fake = '{\n    "type": "Widget", "name": "MainMenuPanel",\n    "children": [\n        { "type": "Widget", "name": "X" },\n    ]\n}'
        self.assertIn('TouchSafeOk', touch_layout.pad_ribbon(fake, movies=True))   # 手柄版挂在左侧按钮栏面板下（画在主菜单之上） | controller: under the button-column panel (drawn above the main menu)
        self.assertNotIn('TouchSafeOk', touch_layout.pad_mainmenu(fake))
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
        for key, pt in (('dmSafeModeTitle', 48), ('dmSafeModeMsgCrash', 36), ('dmSafeModeMsgHang', 36), ('dmSafeModeMsgMem', 36), ('dmSafeModeMsgOther', 36)):
            texts = rows[key]
            n = texts[1].count('\n')
            for lang, t in zip(langs, texts):
                self.assertEqual(t.count('\n'), n, (key, lang))
                for line in t.split('\n'):
                    self.assertLessEqual(est_width(line, pt), BOX_W, (key, lang, line, est_width(line, pt)))
        for k in ('Crash', 'Hang', 'Mem', 'Other'):   # 按原因分场景（用户：「应该分场景」） | split by cause (user: "it should be split by scenario")
            self.assertEqual(rows['dmSafeModeMsg' + k][1].count('\n'), 2, 'three lines (one text box each: TouchSafeDesc0..2)')


class Host(unittest.TestCase):
    def test_host_handles_ok_and_pad_a(self):
        c = (DM / 'host' / 'dm_crashfence.c').read_text(encoding='utf-8')
        self.assertIn('"TouchSafeOk"', c); self.assertIn('dm_padmode_page_keys(DM_BTN_A)', c)
        self.assertIn('前端：提示已显示：', c); self.assertIn('玩家点了确定', c)
        # 显示前要写标题和按原因选的正文（曾在挪位置时误删，Mac 截图只剩空框） | title and the cause-picked body must be written before showing (once lost while moving the box: the Mac shot showed an empty frame)
        self.assertIn('cf_text(b, ti, title);', c); self.assertIn('dm_str(b, k_cf_msg[g_msg])', c); self.assertIn('cf_text(b, de[k], ln)', c)
        self.assertIn('dm_crashfence_pump()', (DM / 'host' / 'hle_hid.c').read_text(encoding='utf-8'))
        self.assertIn('dm_crashfence_on_uimsg(cat, act, arg)', (DM / 'host' / 'diag.c').read_text(encoding='utf-8'))

    def test_native_popups_gone(self):
        for rel in ('ios/Sources/front_ios.m', 'host/front_mac.m', 'host/dm_crashfence.c', 'host/dm_crashfence.h'):
            self.assertNotIn('front_notice', (DM / rel).read_text(encoding='utf-8'), rel)


if __name__ == '__main__':
    unittest.main(verbosity=1)
