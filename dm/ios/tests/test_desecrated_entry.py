"""「进入恐惧区域」入口（方案 C §3.2；落地清单第 6 节的回退方案）：方案 C 把原版「游戏设定」页从页签里拿掉之后，
原版的离线恐惧区域开关（OfflineDesecratedZonesEnabled）在界面上没有入口了。游戏页的「难度」块里、难度行说明下面加一行我们自己的入口：
行名 + 副文「只在创建新游戏时生效」+ 一个「打开」钮；钮发原版设置页的切页消息（SettingsPanelMessage:CheckChanges:OnlineGameOptionsPanel），
由原版自己的开关读写（不写原版控件，守卫 native_write_scan.py）。触屏 / 手柄两份页同一块；手柄焦点链把这一行接在难度行之后、Plus 行之前。
The 「Terror Zones」 entry (plan C section 3.2; the fallback in item 6 of the landing checklist): once plan C drops the original 「Game Settings」 tab,
the original offline Terror Zones switch (OfflineDesecratedZonesEnabled) has no way in. Below the difficulty row's note in the Gameplay page's difficulty
block we add a row of our own: label + the note 「takes effect only in a new game」 + an 「Open」 button; the button sends the original settings page's
switch-tab message (SettingsPanelMessage:CheckChanges:OnlineGameOptionsPanel), and the original switch itself reads / writes the value (no original
widget is written; guard native_write_scan.py). Touch and controller pages share the block; the controller focus chain puts this row right after the
difficulty row and before the Plus rows."""
import json
import os
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
sys.path.insert(0, str(DM / 'ios' / 'tests'))
os.environ.pop('MOVIE_FFMPEG', None)
os.environ.pop('DM_TARGET', None)
import dm_i18n  # noqa: E402
import touch_layout  # noqa: E402
from test_community_entry import est_width  # noqa: E402
from test_difficulty_row import fake_gameplay  # noqa: E402

MSG = 'SettingsPanelMessage:CheckChanges:OnlineGameOptionsPanel'
NOTE_BOTTOM = 460          # TouchDiffNote 的下沿（360 + 100）；入口行从 480 起（20 的间隔） | bottom edge of TouchDiffNote (360 + 100); the entry row starts at 480 (a 20-unit gap)
ENTRY_H = 180


class Layout(unittest.TestCase):
    def check(self, out, where):
        # 原版设置页仍然是这一行的归属：入口只把玩家送到原版开关那，不复制原版控件的名字 | the original settings page still owns the value: the entry only sends the player to the original switch, it never copies an original widget name
        self.assertNotIn('OfflineDesecratedZonesEnabled', out, where)
        self.assertNotIn('@TerrorZonesEnabled', out, where)
        blk, tab = out.find('"name": "TouchDiffRows"'), out.find('"name": "TouchTableHolder"')
        self.assertTrue(0 <= blk < tab, (where, blk, tab))   # 这一块仍在滚动区、原版表格之前 | the block is still inside the scroll view, before the original table
        # 块高 = 标题 / 难度行 / 说明 + 入口行 | block height = title / difficulty row / note + the entry row
        self.assertEqual(touch_layout.DIFF_H, touch_layout.DESECRATED_Y + ENTRY_H, where)
        self.assertEqual(touch_layout.DESECRATED_Y, NOTE_BOTTOM + 20, where)   # 说明下面留 20 的间隔 | a 20-unit gap below the note
        self.assertTrue(re.search(r'"name": "TouchDiffRows", "fields": \{ "rect": \{ "x": 0, "y": 0, "width": 1730, "height": %d \}' % touch_layout.DIFF_H, out), where)
        # 入口行紧接说明的下沿 | the entry row starts right below the note
        m = re.search(r'"type": "Widget", "name": "TouchDesecratedRow", "fields": \{ "rect": \{ "x": 0, "y": (\d+), "width": 1730, "height": (\d+) \} \}', out)
        self.assertTrue(m, where)
        self.assertEqual((int(m.group(1)), int(m.group(2))), (touch_layout.DESECRATED_Y, ENTRY_H), where)
        # 顺序：说明 → 入口行 → Plus 分组 → 原版表格 | order: note -> entry row -> Plus groups -> original table
        i_note, i_row, i_grp = out.find('"name": "TouchDiffNote"'), out.find('"name": "TouchDesecratedRow"'), out.find('"name": "TouchGrp')
        self.assertTrue(0 <= i_note < i_row < (i_grp if i_grp >= 0 else tab) < tab, (where, i_note, i_row, i_grp, tab))
        # 行名：设置页文字样式；副文：28 号 / 说明灰 / $ReducedSpacing，在行内、不压右端的钮 | label: settings text style; note: 28 pt / description grey / $ReducedSpacing, inside the row, clear of the button on the right
        self.assertTrue(re.search(r'"type": "TextBoxWidget", "name": "TouchDesecratedText", "fields": \{ "rect": \{ "x": 0, "y": 0, "width": 1000, "height": 140 \}, "text": "@dmOptDesecrated", "style": "\$StyleSettingsText" \}', out), where)
        n = re.search(r'"name": "TouchDesecratedNote", "fields": \{ "rect": \{ "x": (\d+), "y": (\d+), "width": (\d+), "height": (\d+) \}, "text": "@dmOptDesecratedNote", "style": \{ "fontColor": "\$FontColorLightGray", "pointSize": 28, "spacing": "\$ReducedSpacing"', out)
        self.assertTrue(n, where)
        x, y, w, h = (int(g) for g in n.groups())
        self.assertTrue(0 <= y and y + h <= ENTRY_H and x + w <= 1000, (where, x, y, w, h))   # 副文在行内、在文字框下面 | inside the row, below the label box
        # 「打开」钮：原版前端中号按钮，消息就是原版设置页的切页消息 | the 「Open」 button: the original front-end medium button, whose message is the original settings page's switch-tab message
        b = re.search(r'"type": "ButtonWidget", "name": "TouchDesecratedOpen",\s*"fields": \{(?P<f>[^}]*\}[^}]*)\}', out)
        self.assertTrue(b, where)
        f = b.group('f')
        self.assertIn('"textString": "@dmOptDesecratedOpen"', f, where)
        self.assertIn('"onClickMessage": "%s"' % MSG, f, where)
        self.assertIn('FrontEnd_ButtonMed', f, where)
        # 生成结果是合法布局（面板读不了会整页不显示且不报错）| the result is a valid layout (a panel that cannot read it shows nothing and reports nothing)
        json.loads(re.sub(r',(\s*[}\]])', r'\1', re.sub(r'(?m)^\s*//[^\n]*', '', re.sub(r'/\*[\s\S]*?\*/', '', out))))
        # 原版表格整体让位到入口行之后 | the original table moves below the entry row
        self.assertTrue(re.search(r'"name": "TouchTableHolder", "fields": \{ "rect": \{ "x": 0, "y": %d,' % (touch_layout.DIFF_H + touch_layout._game_h()), out), where)

    def test_touch_and_controller_pages(self):
        out, _ = touch_layout.PRESENT['gameplayoptionspanelhd.json'](fake_gameplay()); self.check(out, 'touch')
        pad, _ = touch_layout.PAD_ROOT['gameplayoptionspanelhd.json'](fake_gameplay()); self.check(pad, 'pad')

    def test_real_layout_if_present(self):
        romfs = os.environ.get('DM_ROMFS')
        if not romfs or not os.path.isdir(romfs):
            self.skipTest('DM_ROMFS 未设')
        for fn, where in ((touch_layout.PRESENT['gameplayoptionspanelhd.json'], 'touch'), (touch_layout.PAD_ROOT['gameplayoptionspanelhd.json'], 'pad')):
            _, text = touch_layout.read_layout(romfs, 'gameplayoptionspanelhd.json'); out, _ = fn(text); self.check(out, where)


class Text(unittest.TestCase):
    def test_six_languages(self):
        rows = {r[0]: r[1:] for r in dm_i18n.rows()}
        langs = ('中文', 'English', 'Español', 'Español MX', 'Русский', '한국어')
        for key, pt, limit in (('dmOptDesecrated', 36, 1000), ('dmOptDesecratedNote', 28, 1000), ('dmOptDesecratedOpen', 36, 400)):
            self.assertIn(key, rows, key)
            texts = rows[key]
            self.assertEqual(len(texts), 6, key)
            for lang, t in zip(langs, texts):
                self.assertTrue(t.strip(), (key, lang))
                self.assertEqual(t.count('\n'), 0, (key, lang))
                self.assertEqual(t.count('%'), 0, (key, lang))
                self.assertLessEqual(est_width(t, pt), limit, (key, lang, t, est_width(t, pt)))
        self.assertIn('恐惧', rows['dmOptDesecrated'][0])
        self.assertIn('Terror', rows['dmOptDesecrated'][1])

    def test_no_new_platform_word(self):
        # 两端都有这一行：不得出现某一端专属的用词（CLAUDE.md §2.8）| the row exists on both platforms: no platform-only wording may appear
        self.assertEqual(dm_i18n.platform_of('dmOptDesecrated'), '')
        self.assertEqual(dm_i18n.platform_of('dmOptDesecratedNote'), '')
        self.assertEqual(dm_i18n.platform_of('dmOptDesecratedOpen'), '')


class Host(unittest.TestCase):
    def test_controller_chain(self):
        pm = (DM / 'host' / 'dm_padmode.c').read_text(encoding='utf-8')
        # 焦点链停靠点：难度滑块之后就是入口钮，Plus 行再往后 | focus stops: the difficulty slider, then the entry button, then the Plus rows
        self.assertIn('st[0] = w;', pm)
        self.assertTrue(re.search(r'st\[1\] = pm_find\(gp, "TouchDesecratedOpen", 0\);', pm), 'entry button is the second stop')
        self.assertTrue(re.search(r'int ns = 2 \+ dm_custom_game_stops\(', pm), 'the custom rows start after the entry button')
        # A 在入口钮上 = 发原版设置页的切页消息（原版的开关自己读写设置；不替它改控件） | A on the entry button = send the original settings page's switch-tab message (the original switch reads and writes the setting itself; no widget is written for it)
        self.assertTrue(re.search(r'dm_ui_send\("SettingsPanelMessage", "CheckChanges", "OnlineGameOptionsPanel"\)', pm))
        self.assertTrue(re.search(r'void dm_ui_send\(const char \*, const char \*, const char \*\);', pm))
        self.assertTrue(re.search(r'si == 1 && \(ev & DM_BTN_A\)', pm), 'the entry row handles A itself')
        self.assertTrue(re.search(r'si > 1 && \(ev & DM_BTN_A\)\) dm_custom_game_press', pm), 'the Plus rows keep their A handling')
        # 滚动到入口行时把这一行整行滚进视口 | when focused, scroll the whole entry row into view
        self.assertIn('TouchDesecratedOpen', pm.split('dm_custom_scroll_step')[0])
        # 不再碰原版控件 | never writes the original widget
        self.assertNotIn('"OfflineDesecratedZonesEnabled"', pm)

    def test_host_stack_starts_below_the_block(self):
        # 宿主把 Plus 分组从难度块下沿往下摆（dm_custom.c 的 game_ui_pump）：这个数必须与布局的 DIFF_H 一致，否则入口行被第一组整个盖住（本切片第一次 Mac 截图就是这样：
        # 分组仍从 480 起摆，入口行只剩行名看得见、钮被压在下面，触屏点不到）
        # The host stacks the Plus groups from the bottom of the difficulty block (game_ui_pump in dm_custom.c): it must equal the layout's DIFF_H, otherwise the entry row is covered
        # by the first group entirely (that is how the first Mac screenshot of this slice looked: the groups still started at 480, only the label showed and the button was underneath, untappable)
        custom = (DM / 'host' / 'dm_custom.c').read_text(encoding='utf-8')
        m = re.search(r'#define GAME_ROWS_Y (\d+)', custom)
        self.assertTrue(m, 'GAME_ROWS_Y missing')
        self.assertEqual(int(m.group(1)), touch_layout.DIFF_H)
        self.assertIn('int y = GAME_ROWS_Y;', custom)
        self.assertNotIn('int y = 480;', custom)

    def test_suite_registration(self):
        mk = (DM / 'ios' / 'tests' / 'Makefile').read_text(encoding='utf-8')
        self.assertIn('python3 test_desecrated_entry.py', mk)


if __name__ == '__main__':
    unittest.main()
