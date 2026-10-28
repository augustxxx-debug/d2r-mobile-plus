"""「游戏难度」从暂停菜单挪到「选项 → 游戏」页（用户 2026-10-28）：暂停菜单两种模式统一后 7 行、没有难度行、「选项」仍在 y 653（手柄光标初始落点那一行）；
游戏页的难度行是我们自己的一块（原版这页的「离线难度」三行在本版被面板代码按行名无条件藏起、滑块刻度从不设——要用就得改写原版控件可见位，违反守卫）：
滚动区里、原版表格之前放 TouchDiffRows（分节标题 / 行名 + 滑块 TouchDiffSlider / 说明），原版表格下移；原版三行原样不动；触屏版与手柄版两份页都一样；
宿主每帧读滑块、手柄接管 Y（+1 档）并写提示条，变了即走底栏难度格同一条路（dm_hud_request_players）立刻生效；分享日志摘要带「游戏难度：」行。
"Game Difficulty" moved from the pause menu to the Options → Gameplay page (user, 2026-10-28): the unified pause menu has 7 rows, no difficulty row, and Options still sits at y 653
(the row where the controller cursor lands); the Gameplay page's difficulty row is a block of our own (the page's original three "offline difficulty" rows are hidden unconditionally by
row name by the panel code in this build and the slider scale is never set -- using them would mean writing original widgets' visible flags, which the guard forbids):
TouchDiffRows (section title / label + slider TouchDiffSlider / note) inside the scroll view before the original table, which moves down; the original three rows untouched; touch and
controller pages alike; the host reads the slider every frame, takes over Y (+1 level) on the controller and writes the legend, and on change goes the same way as the bottom-bar
difficulty cell (dm_hud_request_players) so it takes effect at once; the share-log summary carries a 「游戏难度：」 line."""
import os
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
os.environ.pop('MOVIE_FFMPEG', None); os.environ.pop('DM_OVERLAY_MAC', None)
import touch_layout  # noqa: E402
import dm_i18n  # noqa: E402
from test_community_entry import fake_pause, rect_y, est_width  # noqa: E402

PAD_ROWS = ('AutoPartyInvite', 'ItemHotkeyTooltipAppender', 'ChatFontSize', 'ChatBackground', 'ShowParty', 'ShowNames', 'ShowRoomName', 'LowVisionMode',
            'ControllerCursorSpeed', 'ControllerWeaponSwapSeparateBindings', 'ControllerRumbleEnabled')
TOUCH_ROWS = PAD_ROWS + ('Controller Options', 'ControllerSwapThumbsticks', 'ColorBlindCalibration')


def fake_gameplay():   # 原版游戏页的骨架：生成器要删的行各一行，加原版「离线」三行与隐私行，结构照原版 | skeleton of the original Gameplay page: one row per row the generator removes, plus the original 'Offline' three rows and the privacy row
    def row(name, inner):
        return '                        {\n                            "type": "TableRowWidget", "name": "Row %s",\n                            "children": [\n%s\n                            ]\n                        }' % (name, inner)
    def text(name, key, style='$StyleSettingsText'):
        return '                                { "type": "TextBoxWidget", "name": "%s", "fields": { "text": "%s", "style": "%s" } }' % (name, key, style)
    rows = [row(n, text(n + 'Text', '@' + n)) for n in TOUCH_ROWS]
    rows.append(row('Offline', '                                { "type": "TextBoxWidget", "name": "Offline", "fields": { "text": "@OfflineHeader", "style": "$StyleSettingsTitle" }, "children": [\n'
                               '                                    { "type": "ImageWidget", "name": "Divider", "fields": { "rect": "$OptionsDividerRect", "filename": "PauseMenu\\\\Divider" } } ] }'))
    rows.append(row('OfflineDifficultyScaling', text('OfflineDifficultyScalingText', '@OptionsMenuOfflineDifficultyScalingSetting') + ',\n'
                    '                                {\n                                    "type": "SettingsSliderWidget", "name": "OfflineDifficultyScaling",\n                                    "fields": {\n'
                    '                                        "rect": "$OptionsSliderRect",\n                                        "isHorizontal": true,\n                                        "increment": 1,\n'
                    '                                        "onUpdateMessage": "OptionsPanelMessage:SetOfflineDifficultyScaling",\n                                        "valueText": "$SettingsSliderValueFields",\n'
                    '                                        "tooltipString": "@OptionsMenuOfflineDifficultyScalingTooltip",\n                                    }\n                                }'))
    rows.append(row('OfflineSettingsEffectNote', '                                {\n                                        "type": "TextBoxWidget", "name": "OfflineSettingsEffectNote",\n                                        "fields": {\n'
                    '                                                "rect": { "x": 0, "y": 0, "width": 1730 },\n                                                "anchor": { "x": 0.0, "y": 1.0 },\n'
                    '                                                "text": "@OfflineSettingsEffectNote",\n                                                "style": "$StyleSettingsText"\n                                        }\n                                }'))
    rows.append(row('DataPrivacy', text('DataPrivacy', '@DataPrivacyHeader', '$StyleSettingsTitle')))
    return ('{\n    "type": "GameplayOptionsPanel", "name": "GameplayOptionsPanel",\n    "children": [\n        { "type": "ScrollViewWidget", "name": "ScrollView", "fields": { "fitToParent": true }, "children": [\n'
            '        {\n            "type": "TableWidget", "name": "OptionsTable",\n            "fields": { "rowHeight": "$OptionsTableRowHeight" }, "children": [\n%s\n        ] }\n        ] }\n    ]\n}\n' % ',\n'.join(rows))


class PauseMenu(unittest.TestCase):
    # 7 行（带编年史、带分享）：保存并退出 / 一键重开 / 存放 / 选项 / 编年史 / 分享诊断日志 / 加入社区讨论，125 距 278–1028；「选项」在第四行 y 653。
    # 用户给的顺序是「保存并退出 / 一键重开 / 选项 / 存放 / …」，但 7 行里 y 653 是正中第四行，「选项」在第三行就得整组下移一行（顶上空一行）；所以「存放」与「选项」对调，其余顺序不变。
    # 7 rows (with Chronicle and share): Save and Exit / Quick Restart / Deposit / Options / Chronicle / Share Diagnostics / Join the community, 125 apart from 278 to 1028; Options is the fourth row at y 653.
    # The user's order was 'Save and Exit / Quick Restart / Options / Deposit / …', but among 7 rows y 653 is the centre (fourth) row, and Options on the third row would push the whole group down a row (blank row at the top); so Deposit and Options swap places, the rest unchanged.
    WANT = {(True, True): {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'Chronicle': 778, 'MessageLog': 903, 'TouchCommunity': 1028},
            (True, False): {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'Chronicle': 778, 'TouchCommunity': 903},
            (False, True): {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'MessageLog': 778, 'TouchCommunity': 903},
            (False, False): {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'TouchCommunity': 778}}

    def check(self, out, want, where):
        for name, y in want.items(): self.assertEqual(rect_y(out, name), y, (where, name))
        ys = sorted(want.values())
        for a, b in zip(ys, ys[1:]): self.assertEqual(b - a, 125, where)   # 原版行距，整组从原版首行 278 起 | the original spacing, the group starts at the original first row 278
        self.assertEqual(want['Settings'], 653, where); self.assertEqual(sum(1 for y in ys if y < 653), 3, where)   # 「选项」= 第四行 | Options = the fourth row
        for gone in ('TouchPadDiff', 'TouchPadDiffTxt', 'Help', 'LootFilter'): self.assertIsNone(rect_y(out, gone), (where, gone))   # 难度行没有；帮助 / 掉落筛选照旧挪出屏 | no difficulty row; Help / loot filter stay off screen
        self.assertTrue('TouchPadDiff' not in out, where)
        self.assertNotIn('@OptionsMenuGameDifficulty', out, where)

    def test_pause_rows_both_modes(self):
        for garden, rel in ((False, 'controller/pauselayouthd.json'), (True, 'controller/pauselayoutgardenhd.json')):
            touch, _ = touch_layout.PRESENT[rel](fake_pause(garden))
            for share in (True, False):
                pad = touch_layout.pad_pause(fake_pause(garden), garden, share=share)
                self.check(pad, self.WANT[(garden, share)], (rel, share))
                if share: self.assertEqual(touch, pad, rel)   # 触屏 = 手柄同一份 | touch = the same text as the controller version
        self.assertEqual(touch_layout.pad_pause(fake_pause(True), True, share=True).count('"name": "TouchCommunity"'), 1)


class GameplayPage(unittest.TestCase):
    def has(self, out, pat, where):   # 真布局是 \r\n 加多行字段：按正则找，失败只报短消息 | the real layout has \r\n and multi-line fields: regex search, short failure message only
        self.assertTrue(re.search(pat, out), (where, pat))

    def check(self, out, where):
        sv, blk, tab = out.find('"name": "ScrollView"'), out.find('"name": "TouchDiffRows"'), out.find('"name": "OptionsTable"')
        self.assertTrue(0 <= sv < blk < tab, (where, sv, blk, tab))   # 我们的一块在滚动区里、原版表格之前 | our block is inside the scroll view, before the original table
        self.has(out, r'"name": "TouchDiffRows", "fields": \{ "rect": \{ "x": 0, "y": 0, "width": 1730, "height": %d \}' % touch_layout.DIFF_H, where)
        self.has(out, r'"name": "OptionsTable",\s*"fields": \{ "rect": \{ "x": 0, "y": %d \},' % touch_layout.DIFF_H, where)   # 原版表格整体下移 | the original table moves down
        block = out[blk:tab]
        self.has(block, r'"name": "TouchDiffTitle", "fields": \{ "rect": \{ "x": 0, "y": 0, "width": 1730, "height": 180 \}, "text": "@dmDiffSection", "style": "\$StyleSettingsTitle"', where)
        self.has(block, r'"name": "Divider"', where)
        self.has(block, r'"name": "TouchDiffText", "fields": \{ "rect": \{ "x": 0, "y": 180, "width": 1000, "height": 180 \}, "text": "@dmDiffRow", "style": "\$StyleSettingsText"', where)
        self.has(block, r'"type": "SettingsSliderWidget", "name": "TouchDiffSlider", "fields": \{ "rect": \{ "x": 1110, "y": 235, "width": 480, "height": 72 \}', where)   # 同图形页滑块的摆法 | placed like the Graphics page sliders
        self.has(block, r'"name": "TouchDiffNote", "fields": \{ "rect": \{ "x": 0, "y": 360, "width": 1670, "height": 100 \}, "text": "@dmDiffRowNote", "style": \{ "fontColor": "\$FontColorLightGray", "pointSize": 28, "spacing": "\$ReducedSpacing"', where)   # §2.6 副文 | secondary text
        self.assertTrue('onClickMessage' not in block and 'tooltipString' not in block, where)
        for keep in ('"name": "Row Offline"', '"name": "Row OfflineDifficultyScaling"', '"name": "Row OfflineSettingsEffectNote"', '@OfflineHeader', '@OfflineSettingsEffectNote'):
            self.assertTrue(keep in out, (where, keep))   # 原版三行原样（原版自己藏） | the original three rows untouched (the game hides them itself)

    def test_touch_and_pad_pages(self):
        out, n = touch_layout.PRESENT['gameplayoptionspanelhd.json'](fake_gameplay()); self.check(out, 'touch')
        self.assertEqual(len(re.findall(r'"name": "Row ', out)), 4)   # 其余行都删了 | all other rows removed
        self.assertEqual(out.count('"name": "TouchDiffRows"'), 1)
        pad, _ = touch_layout.PAD_ROOT['gameplayoptionspanelhd.json'](fake_gameplay()); self.check(pad, 'pad')
        self.assertEqual(len(re.findall(r'"name": "Row ', pad)), 4 + len(TOUCH_ROWS) - len(PAD_ROWS))

    def test_real_layout_if_present(self):   # 真 romfs（DM_ROMFS 指到时）：原版页上同样成立 | real romfs (when DM_ROMFS points at it): the same holds on the original page
        romfs = os.environ.get('DM_ROMFS')
        if not romfs or not os.path.isdir(romfs): self.skipTest('DM_ROMFS 未设')
        for fn, where in ((touch_layout.PRESENT['gameplayoptionspanelhd.json'], 'touch'), (touch_layout.PAD_ROOT['gameplayoptionspanelhd.json'], 'pad')):
            _, text = touch_layout.read_layout(romfs, 'gameplayoptionspanelhd.json'); out, _ = fn(text); self.check(out, where)


class Text(unittest.TestCase):
    def test_six_languages(self):
        rows = {r[0]: r[1:] for r in dm_i18n.rows()}
        langs = ('中文', 'English', 'Español', 'Español MX', 'Русский', '한국어')
        self.assertTrue('dmPadDiffLine' not in rows)   # 暂停菜单那行的字不再用 | the pause-row text is gone
        for key, pt, limit in (('dmDiffSection', 40, 1000), ('dmDiffRow', 36, 1000), ('dmDiffRowNote', 28, 1670), ('dmDiffOutOfRun', 28, 1670), ('dmDiffLegend', 36, 700)):
            self.assertTrue(key in rows, key); texts = rows[key]; self.assertEqual(len(texts), 6, key)
            for lang, t in zip(langs, texts):
                self.assertTrue(t.strip(), (key, lang)); self.assertEqual(t.count('\n'), 0, (key, lang)); self.assertEqual(t.count('%d'), 1 if key == 'dmDiffLegend' else 0, (key, lang))
                line = t.replace('%d', '8'); self.assertLessEqual(est_width(line, pt), limit, (key, lang, line, est_width(line, pt)))
        self.assertEqual(rows['dmDiffRow'][0], '游戏难度')


class Host(unittest.TestCase):
    def test_wiring(self):
        pm = (DM / 'host' / 'dm_padmode.c').read_text(encoding='utf-8')
        self.assertTrue('TouchPadDiff' not in pm and 'dmPadDiffLine' not in pm)   # 暂停菜单难度行的宿主代码删掉 | host code of the pause-menu difficulty row removed
        for s in ('"GameplayOptionsPanel"', '"TouchDiffSlider"', '"TouchDiffNote"', 'DM_VT_SLIDER', 'dm_hud_request_players(', 'dm_hud_slider_players(', 'dm_hud_slider_value(', '游戏难度：',
                  'dm_padmode_page_keys(DM_BTN_Y)', 'dm_padmode_legend(1,', '"dmDiffLegend"', '"dmDiffRowNote"', '"dmDiffOutOfRun"', 'diff_pump(b, frame);'): self.assertTrue(s in pm, s)
        self.assertTrue('"OfflineDifficultyScaling"' not in pm)   # 不碰原版藏着的那颗滑块 | never touches the original hidden slider
        hud = (DM / 'host' / 'dm_hud.h').read_text(encoding='utf-8')
        self.assertTrue('int dm_hud_slider_players(int lo, int hi, int v);' in hud and 'int dm_hud_slider_value(int lo, int hi, int n);' in hud)
        self.assertTrue('游戏难度：当前' in (DM / 'host' / 'diag.c').read_text(encoding='utf-8'))   # 拍线程：档位变了记一行，摘要取最后一行 | tick thread: one line per change, the summary takes the last
        for f in ('ios/Sources/dm_sharelog.m', 'host/front_mac.m'): self.assertTrue('"游戏难度："' in (DM / f).read_text(encoding='utf-8'), f)
        self.assertTrue('t_diffrow' in (DM / 'host' / 'tests' / 'Makefile').read_text(encoding='utf-8'))
        lay = (DM / 'ios' / 'tools' / 'touch_layout.py').read_text(encoding='utf-8')
        self.assertTrue('TouchPadDiff' not in lay and '@OptionsMenuGameDifficulty' not in lay)


if __name__ == '__main__':
    unittest.main()
