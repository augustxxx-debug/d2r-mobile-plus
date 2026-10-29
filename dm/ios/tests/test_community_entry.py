"""游戏内「社区入口」：暂停菜单「加入社区讨论」行（触屏两份 / 手柄两份 / Mac App 两份）、选项「自定义」页末尾的「关于」组、
两处共用的游戏内提示框（边框 15 片 + 标题 + 三行正文 + 复制 / 确定两颗按钮，全部屏外起步，宿主 dm_community.c 写字摆位）；
六种语言的文字按字宽估算不压边框；宿主 / 外壳 / 分享日志摘要键 / 更新日志都接上。
In-game community entry: the pause menu's "Join the community" row (two touch layouts / two controller layouts / two Mac App layouts), the "About" group at the end of the Custom options page,
the in-game notice box the two share (15 frame tiles + title + three body lines + Copy / OK buttons, all starting off screen; the host dm_community.c writes text and places them);
six languages fit by estimated glyph width; host / shells / share-log summary keys / changelog are wired up."""
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

BOX_W = 1560   # 同 test_safe_notice：边框 1800 减两侧边条与留白 | as test_safe_notice: the 1800 frame minus edge strips and margins
BTN_W = 400    # 原版前端中号按钮 470 宽，字留 400 | the original medium front-end button is 470 wide, 400 for text
PAUSE_BTN_W = 600   # 暂停菜单按钮上的字（「分享诊断日志」同一颗按钮；现有最长的西语「Compartir diagnóstico」按同一估算正好 588） | text on a pause-menu button (the same button as "Share Diagnostics"; the longest existing Spanish "Compartir diagnóstico" estimates to 588 by the same rule)


# 鸣谢标签可用宽：标签起点 = 行缩进 40（DM_ROW_INDENT），名单列在 x 620，中间至少留 40——原先按 620 − 40 = 580 判，西语「Agradecimiento especial」估 580 刚好过线，
# Duo 内屏截图上却压到名单（用户 2026-10-29 反馈「关于」区排版）
# room for a credit label: it starts at the 40-unit row indent and the names column is at x 620, with at least 40 between -- the old 580 limit let the Spanish label (est. 580) through, and it overlapped the names on the Duo
CREDIT_LABEL_W = 620 - 40 - 40
def est_width(line, pt):
    return sum(pt if re.match(r'[ᄀ-ᇿ　-鿿가-힯＀-￯]', ch) else pt * 0.7 for ch in line)


def fake_pause(garden):   # 原版暂停菜单的骨架：只放 _pause_no_help 要找的按钮，位置照原版 | skeleton of the original pause menu: only the buttons _pause_no_help looks for, at the original positions
    def btn(name, y, text, msg, anchor=''):
        return ('        {\n            "type": "ButtonWidget", "name": "%s",\n            "fields": {\n                "rect": { "x": 564, "y": %d },\n%s'
                '                "filename": "Controller/Panel/EscapeMenu/EscapeOptions_Button",\n                "textString": "%s",\n                "onClickMessage": "%s",\n'
                '                "pointSize": "$MediumLargeFontSize",\n            }\n        }' % (name, y, anchor, text, msg))
    if garden:
        rows = [btn('MessageLog', 278, '@minipanelmessage', 'PanelManager:OpenPanel:MessageLogPanel'), btn('Settings', 428, '@OptionsHD', 'PanelManager:OpenPanel:SettingsPanel'),
                btn('Help', 578, '@minipanelHelp', 'PanelManager:OpenPanel:HelpPanel'), btn('Exit', 728, '@SaveAndExit', 'PausePanelMessage:ExitGame'),
                btn('LootFilter', 878, '@LootFilter', 'PanelManager:OpenPanel:LootFilterPanel'), btn('Chronicle', 1028, '@Chronicle', 'PanelManager:OpenPanel:ChroniclePanel')]
        bg = '        { "type": "ImageWidget", "name": "EscapeOptionsBG", "fields": { "rect": { "x": 0, "y": 0 }, "filename": "Controller/Panel/EscapeMenu/EscapeOptionsBG" } }'
    else:
        rows = [btn('MessageLog', 428, '@minipanelmessage', 'PanelManager:OpenPanel:MessageLogPanel'), btn('Settings', 578, '@OptionsHD', 'PanelManager:OpenPanel:SettingsPanel'),
                btn('Help', 728, '@minipanelHelp', 'PanelManager:OpenPanel:HelpPanel'), btn('Exit', 878, '@SaveAndExit', 'PausePanelMessage:ExitGame')]
        bg = '        { "type": "ImageWidget", "name": "EscapeOptionsBGClassic", "fields": { "rect": { "x": 0, "y": 0 }, "filename": "Controller/Panel/EscapeMenu/EscapeOptionsBGClassic" } }'
    return '{\n    "basedOn": "PauseLayoutHD.json",\n    "type": "PausePanel", "name": "PauseLayout%s",\n    "children": [\n%s,\n%s,\n    ]\n}\n' % ('Garden' if garden else '', bg, ',\n'.join(rows))


def rect_y(text, name):
    m = re.search(r'"name": "%s",\s*"fields": \{\s*"rect": \{ "x": 564, "y": (\d+)' % re.escape(name), text)
    return int(m.group(1)) if m else None


BOX = (['TouchCommTile%d%d' % (r, c) for r in range(5) for c in range(3)] + ['TouchCommTitle']
       + ['TouchComm%s%d' % (w, k) for k in range(2) for w in ('Label', 'Value', 'Copy', 'CopyText')] + ['TouchCommCard%d%s' % (k, e) for k in range(2) for e in 'TBLR']
       + ['TouchCommFree', 'TouchCommDivider', 'TouchCommFoot0', 'TouchCommFoot0R', 'TouchCommFoot1', 'TouchCommOk'] + ['TouchCommFocus%s' % e for e in 'TBLR'])
CO_LABEL_W, CO_BTN_W, CO_CARD_W = 260, 293, 1480   # 同 dm_community.c | as dm_community.c


def box_ok(tc, out, where):   # 社区 / 关于弹窗（RC1.1 重构）：42 个控件屏外起步、不带锚点；§2.6 三级——标题 48 GoldYellow / 值 36 LightGold / 标签、按钮字、声明、页脚 28 | RC1.1 box: 42 widgets off screen, no anchor; three levels per §2.6
    for w in BOX: tc.assertIn('"name": "%s"' % w, out, (where, w))
    for k in range(2): tc.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchCommCopy%d"' % k, out)
    tc.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchCommOk"', out)
    tc.assertNotIn('TouchCommCopyQq', out); tc.assertNotIn('TouchCommDesc', out)   # 旧的大「复制」与正文行没了 | the old big Copy and body lines are gone
    tc.assertIn('"type": "ImageWidget", "name": "TouchCommDivider", "fields": { "rect": { "x": 99999, "y": 99999 }, "filename": "PauseMenu\\\\Divider" } }', out, where)
    body = out[out.index('TouchCommTile00'):]
    tc.assertNotIn('"x": 9', re.sub(r'"x": 99999', '', body), 'the box starts off screen: ' + where)
    tc.assertNotIn('"anchor"', body, 'the box carries no anchor: the host places it from the panel rect: ' + where)
    def style(name, pt, col, align):
        tc.assertRegex(out, r'"name": "%s", "fields": \{ "rect": \{ "x": 99999, "y": 99999, "width": \d+, "height": \d+ \}, "text": "", "style": \{ "pointSize": %d, "fontColor": "%s", "alignment": \{ "h": "%s"' % (name, pt, re.escape(col), align), (where, name))
    style('TouchCommTitle', 48, '$FontColorGoldYellow', 'center'); style('TouchCommFree', 28, '$FontColorGoldYellow', 'center')
    for k in range(2):
        style('TouchCommLabel%d' % k, 28, '$FontColorLightGray', 'left'); style('TouchCommValue%d' % k, 36, '$FontColorLightGold', 'left'); style('TouchCommCopyText%d' % k, 28, '$LightButtonTextColor', 'center')
        tc.assertIn('"name": "TouchCommCopy%d", "fields": { "rect": { "x": 99999, "y": 99999, "scale": 0.8 }, "filename": "FrontEnd\\\\HD\\\\Final\\\\FrontEnd_ButtonSmall"' % k, out)
        for e in 'TBLR': tc.assertIn('"name": "TouchCommCard%d%s", "fields": { "rect": { "x": 99999, "y": 99999, "width": 4, "height": 4 }, "filename": "Ztouch_comm/rule_%s" }' % (k, e, 'h' if e in 'TB' else 'v'), out)
    for e in 'TBLR': tc.assertIn('"name": "TouchCommFocus%s", "fields": { "rect": { "x": 99999, "y": 99999, "width": 4, "height": 4 }, "filename": "Ztouch_comm/focus_%s" }' % (e, 'h' if e in 'TB' else 'v'), out)
    for name in ('TouchCommFoot0', 'TouchCommFoot1'): style(name, 28, '$FontColorLightGray', 'left')
    style('TouchCommFoot0R', 28, '$FontColorLightGray', 'right')


def fake_ribbon():   # 手柄版主菜单左列骨架：选项 / 电影动画 / 制作人员（原版位置与焦点链） | skeleton of the controller main-menu column: Options / Cinematics / Credits (original positions and focus links)
    return ('{\n    "basedOn": "Controller/MainMenuButtonRibbonHD.json",\n    "type": "MainMenuButtonRibbon", "name": "MainMenuButtonRibbon",\n    "children": [\n'
            '        { "type": "ButtonWidget", "name": "Settings", "fields": { "rect": { "x": 164, "y": 1025 }, "navigation": { "up": { "name": "QuickPlayButton" }, "down": { "name": "Cinematics" }, "right": { "panelBoundary": true }, } } },\n'
            '        { "type": "ButtonWidget", "name": "Cinematics", "fields": { "rect": { "x": 164, "y": 1175 }, "navigation": { "up": { "name": "Settings" }, "down": { "name": "Credits" }, "right": { "panelBoundary": true }, } } },\n'
            '        { "type": "ButtonWidget", "name": "Credits", "fields": { "rect": { "x": 164, "y": 1325 }, "navigation": { "up": { "name": "Cinematics" }, "down": { "name": "MotDScrollBar" }, "right": { "panelBoundary": true }, } } },\n'
            '    ]\n}\n')


class FrontEnd(unittest.TestCase):
    """前端主菜单（用户 2026-10-28 再定）：只留「加入社区讨论」一行（开含「关于」段的同一个社区框，同暂停菜单），「关于」行撤掉；手柄左列放在原「电影动画」那一格，
    制作人员原位保留；触屏首页「进入游戏」旁一颗钮，三种版式（缺省 = 正下方居中）；触屏首页继承层要列出所有新加的控件名，否则不建。
    Front-end main menu (user, revised): only "Join the community" (the same community box with an About section, as the pause menu), the About row withdrawn; the controller column puts it in the
    original Cinematics slot, Credits stays; the touch front page has one button beside Play in three layouts (default = right under, centred); the touch front page's inheriting layer must list every added widget, or it is not built."""
    def test_pad_ribbon_one_row(self):
        out = touch_layout.pad_ribbon(fake_ribbon())
        self.assertIn('"name": "TouchCommunityFe", "fields": { "rect": { "x": 164, "y": 1175 }', out); self.assertIn('"textString": "@dmCommunityFe"', out)
        self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchCommunityFe"', out)
        self.assertNotIn('TouchAboutFe', out); self.assertNotIn('"PanelManager:ClosePanel:TouchAbout"', out); self.assertNotIn('@dmCommunityAbout', out)
        self.assertIn('"name": "Cinematics", "fields": { "rect": { "x": 99999, "y": 99999 }', out)   # 电影动画挪走，那一格给社区行 | Cinematics away, its slot to the row
        self.assertIn('"name": "Credits", "fields": { "rect": { "x": 164, "y": 1325 }', out)       # 制作人员原位 | Credits at its original place
        self.assertIn('"name": "Settings", "fields": { "rect": { "x": 164, "y": 1025 }, "navigation": { "up": { "name": "QuickPlayButton" }, "down": { "name": "TouchCommunityFe" }', out)
        self.assertIn('"navigation": { "up": { "name": "Settings" }, "down": { "name": "Credits" }', out)
        self.assertIn('"name": "Credits", "fields": { "rect": { "x": 164, "y": 1325 }, "navigation": { "up": { "name": "TouchCommunityFe" }', out)
        self.assertEqual(out.count('FrontEnd_ButtonMed", "textString"'), 1)   # 前端左列只加了一行 | only one row added to the column
        self.assertIn('"name": "TouchSafeOk"', out)
        box_ok(self, out, 'pad ribbon')
        movies = touch_layout.pad_ribbon(fake_ribbon(), movies=True)   # 编进过场的包：列已满，不放行，框照放 | movie builds: column full, no row, the box still hangs
        self.assertNotIn('"name": "TouchCommunityFe"', movies); box_ok(self, movies, 'pad ribbon movies')

    FAKE_MAIN = ('{\n    "type": "MainMenuPanel", "name": "MainMenuPanel",\n    "children": [\n'
                 '        { "type": "ButtonWidget", "name": "PlayTab", "fields": { "rect": { "x": -1010, "y": 1865 }, "textString": "@PlayTab", } },\n'
                 '        { "type": "ButtonWidget", "name": "OnlinePlaySettingsButton", "fields": { "rect": { "x": 1, "y": 1 } } },\n'
                 '        { "type": "ImageWidget", "name": "Divider", "fields": { "rect": { "x": 1, "y": 1 } } },\n'
                 '        { "type": "ButtonWidget", "name": "SecondaryContextButton", "fields": { "rect": { "x": 1, "y": 1 } } },\n    ]\n}\n')

    def rect(self, out, name):
        m = re.search(r'"name": "%s",\s*"fields": \{\s*(?:"anchor": \{"x": 0.5\},\s*)?"rect": \{ "x": (-?\d+), "y": (\d+)' % name, out)
        return (int(m.group(1)), int(m.group(2))) if m else None

    def test_touch_main_menu_layouts(self):
        PW, PH, MW, MH, GAP = 606, 141, 470, 115, 80   # 大号 / 中号前端按钮图（Mac 截图量）与并排间隔 | large / medium front-end button images (measured) and the side gap
        self.assertEqual((touch_layout.FE_PLAY_W, touch_layout.FE_PLAY_H, touch_layout.FE_MED_W, touch_layout.FE_MED_H, touch_layout.FE_GAP), (PW, PH, MW, MH, GAP))
        self.assertEqual(touch_layout.FE_TOUCH_ROW if not os.environ.get('DM_TEST_FE_ROW') else 'below', 'below')   # 缺省 = 正下方居中（用户最新一条） | default = right under, centred
        old = touch_layout.FE_TOUCH_ROW
        try:
            for mode, play, join in (('below', (-303, 1865 - MH - 20), (-235, 1865 - MH - 20 + PH + 20)),   # 不叠：中号顶 = 大号底 + 20；整体底 = 原「进入游戏」底附近 | no overlap; pair bottom near Play's original bottom
                                     ('row_right', (-303, 1865), (303 + GAP, 1865 + PH - MH)),                # 同行：「进入游戏」原位，次级钮靠右，底边对齐 | same row: Play in place, secondary to the right, bottoms aligned
                                     ('row_center', (-(PW + GAP + MW) // 2, 1865), (-(PW + GAP + MW) // 2 + PW + GAP, 1865 + PH - MH))):   # 同行整体居中 | same row centred
                touch_layout.FE_TOUCH_ROW = mode
                out, n = touch_layout._pc_mainmenu(self.FAKE_MAIN)
                self.assertEqual(self.rect(out, 'PlayTab'), play, mode); self.assertEqual(self.rect(out, 'TouchCommunityFe'), join, mode)
                self.assertEqual(out.count('"name": "TouchCommunityFe"'), 1, mode); self.assertNotIn('TouchAboutFe', out, mode); self.assertNotIn('ClosePanel:TouchAbout"', out, mode)
                self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchCommunityFe"', out); self.assertIn('"textString": "@dmCommunityFe"', out)
                self.assertIn('"filename": "FrontEnd\\\\HD\\\\Final\\\\FrontEnd_ButtonMed"', out)   # 次级钮 = 中号（小一号） | the secondary is medium (one size smaller)
                (px, py), (jx, jy) = play, join
                self.assertTrue(jy >= py + PH or jx >= px + PW or jx + MW <= px, (mode, 'overlap'))   # 两颗不叠 | the two never overlap
                self.assertLessEqual(max(py + PH, jy + MH), 2030, mode)   # 底边不低于原版「进入游戏」底（2006）太多：参考高 2160，下方留安全区 | bottoms stay near the original (2006): reference height 2160, safe area below
                self.assertLessEqual(max(px + PW, jx + MW), 2592 // 2 - 62, mode)   # 窄屏（4:3 / 内屏，参考宽 ≥ 2592）右侧留安全区 62 | narrow screens: right safe area 62
                self.assertGreaterEqual(min(px, jx), -(2592 // 2) + 62 + 420, mode)   # 左边让开版本标签（x 79 起宽 420） | clears the version label on the left
                self.assertIn('"name": "TouchSafeOk"', out); self.assertIn('"name": "TouchBuild"', out); box_ok(self, out, 'touch main menu ' + mode)
        finally:
            touch_layout.FE_TOUCH_ROW = old

    def test_touch_main_menu_inherit_layer_lists_every_widget(self):
        # 触屏首页走继承层（Controller/MainMenuPanelHD basedOn 1touchbase_lay），继承层只建按名字列出的子控件：_pc_mainmenu 加的每个 Touch 控件都要列上
        # （先前漏了社区钮与提示框：Mac 实测触屏首页看不到钮，宿主报「开着的面板里没有提示框控件」） | the inheriting layer builds only listed children: every Touch widget _pc_mainmenu adds must be listed
        out, _ = touch_layout._pc_mainmenu(self.FAKE_MAIN)
        added = set(re.findall(r'"name": "(Touch\w+)"', out))
        listed = set(re.findall(r'"name": "(Touch\w+)"', touch_layout._PCMAIN_STUB))
        self.assertEqual(added - listed, set()); self.assertIn('TouchCommunityFe', listed); self.assertIn('TouchCommTitle', listed)
        self.assertIn('_PCMAIN_STUB + mid[i:]', (DM / 'ios' / 'tools' / 'touch_layout.py').read_text(encoding='utf-8'))

    def test_host_moves_join_with_play(self):   # 手机上宿主把「进入游戏」按底部安全区往上抬，社区钮要一起抬 | on phones the host lifts Play for the bottom safe area; Join must move with it
        c = (DM / 'host' / 'dm_cursor.c').read_text(encoding='utf-8')
        self.assertIn('{"MainMenuPanel","TouchCommunityFe"}', c); self.assertIn('!strcmp(nm,"TouchCommunityFe")', c)


class PauseMenu(unittest.TestCase):
    def test_touch_and_pad_pause_are_the_same(self):   # 两种模式同一份行表、同一顺序、同一位置（用户 2026-10-28 统一；上游基线分两套） | one row table, order and positions for both modes
        for garden, rel in ((False, 'controller/pauselayouthd.json'), (True, 'controller/pauselayoutgardenhd.json')):
            touch, _ = touch_layout.PRESENT[rel](fake_pause(garden)); pad = touch_layout.pad_pause(fake_pause(garden), garden, share=True)
            self.assertEqual(touch, pad, rel)
            rows = {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'Chronicle': 778, 'MessageLog': 903, 'TouchCommunity': 1028} if garden else \
                   {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'MessageLog': 778, 'TouchCommunity': 903}
            for name, y in rows.items(): self.assertEqual(rect_y(touch, name), y, (rel, name))   # 可达：每行都在屏内、位置一致 | reachable: every row on screen at the same position
            self.assertEqual(rect_y(touch, 'Help'), None); self.assertNotIn('TouchPadDiff', touch)   # 难度行挪去「选项 → 游戏」页（test_difficulty_row） | difficulty moved to Options → Gameplay (test_difficulty_row)

    def check_rows(self, text, want):   # want：{按钮名: y}；相邻行中心距 ≥ 118（按钮图高 120 的手柄版最密排法），每行都在底图范围内 | want: {name: y}; adjacent centres >= 118 apart, every row within the background
        ys = {}
        for name, y in want.items():
            self.assertEqual(rect_y(text, name), y, name); ys[name] = y
        order = sorted(ys.values())
        for a, b in zip(order, order[1:]): self.assertGreaterEqual(b - a, 118, (a, b))
        self.assertGreaterEqual(order[0], 278); self.assertLessEqual(order[-1], 1028)

    def test_touch_pause_has_community_row_and_box(self):
        for garden, rel in ((False, 'controller/pauselayouthd.json'), (True, 'controller/pauselayoutgardenhd.json')):
            out, _ = touch_layout.PRESENT[rel](fake_pause(garden))
            self.assertIn('"name": "TouchCommunity"', out, rel); self.assertIn('"textString": "@dmCommunityJoin"', out)
            self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchCommunity"', out)
            self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchShareLog"', out)   # 分享诊断日志仍在 | Share Diagnostics still there
            box_ok(self, out, rel)
            if garden: self.check_rows(out, {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'Chronicle': 778, 'MessageLog': 903, 'TouchCommunity': 1028})
            else: self.check_rows(out, {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'MessageLog': 778, 'TouchCommunity': 903})
            self.assertEqual(rect_y(out, 'Help'), None)   # 帮助挪走 | Help moved away

    def test_pad_pause_rows(self):
        # 两种模式同一份：退出 / 重开 / 存放 / 选项（固定 y 653，屏幕正中那行只许是按了没后果的）/ [编年史] / 分享诊断日志 / 加入社区讨论；难度行挪去「选项 → 游戏」页（用户 2026-10-28）
        # Both modes: exit / restart / deposit / options (fixed at y 653: the row at the screen centre must be one whose press has no consequence) / [chronicle] / share diagnostics / join the community; difficulty moved to Options → Gameplay (user, 2026-10-28)
        out = touch_layout.pad_pause(fake_pause(False), False, share=True)
        self.check_rows(out, {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'MessageLog': 778, 'TouchCommunity': 903})
        out = touch_layout.pad_pause(fake_pause(True), True, share=True)
        self.check_rows(out, {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'Chronicle': 778, 'MessageLog': 903, 'TouchCommunity': 1028})
        # Mac App（没有系统分享面板）：少「分享诊断日志」，社区行接在它的位置上 | Mac App (no system share sheet): no Share Diagnostics; the community row takes its place
        out = touch_layout.pad_pause(fake_pause(False), False, share=False)
        self.check_rows(out, {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'TouchCommunity': 778}); self.assertIsNone(rect_y(out, 'MessageLog'))   # 分享诊断日志那颗在屏外 | the Share Diagnostics button is off screen
        out = touch_layout.pad_pause(fake_pause(True), True, share=False)
        self.check_rows(out, {'Exit': 278, 'TouchRestart': 403, 'TouchPadStore': 528, 'Settings': 653, 'Chronicle': 778, 'TouchCommunity': 903})
        box_ok(self, out, 'mac garden')


class OptionsPage(unittest.TestCase):
    def setUp(self):
        out, _ = touch_layout._custom_page('"name": "VideoOptionsPanel", "name": "OptionsTable"')
        self.out = out

    def test_about_group_and_box(self):
        out = self.out
        for w in ('TouchAboutRow', 'TouchAboutTitle', 'TouchAboutCommunityRow', 'TouchAboutCommunityText', 'TouchCommunityOpt') + tuple(BOX): self.assertIn('"name": "%s"' % w, out, w)
        # 十二审 Q12-01：社区行子容器名字以 Row 结尾，dm_custom.c indent_rows 才往里找一层把标签右移 40；标签载入 x = 0（缩进只动载入 0 的叶子） | Q12-01: the sub-container ends in Row so indent_rows reaches the label; the label loads at x = 0
        self.assertNotIn('"name": "TouchAboutCommunity"', out)
        self.assertIn('"name": "TouchAboutCommunityRow", "fields": { "rect": { "x": 0, "y": 180, "width": 1730, "height": 180 } }, "children": [\n                { "type": "TextBoxWidget", "name": "TouchAboutCommunityText", "fields": { "rect": { "x": 0, "y": 0,', out)
        self.assertIn('"text": "@dmCommunityAbout"', out); self.assertIn('"text": "@dmCommunityRow"', out); self.assertIn('"textString": "@dmCommunityOpen"', out)
        # 鸣谢子块：子标题（标题级）+ 2 个标签框（36 LightGold）+ 6 个名单行框（36 LightGray），字空着由宿主写，不是按钮、不发消息，在社区行之后 | credits sub-block: sub-title + 2 label boxes + 6 name-line boxes, text written by the host, no buttons / messages, after the community row
        for w in ('TouchCreditsTitle', 'TouchCreditLabel0', 'TouchCreditLabel1') + tuple('TouchCreditLine%d' % k for k in range(6)): self.assertIn('"name": "%s"' % w, out, w)
        block = out[out.index('"name": "TouchCreditsTitle"'):out.index('"name": "TouchCommTile00"')]
        self.assertIn('"text": "@dmCreditsTitle", "style": "$StyleSettingsTitle"', block); self.assertNotIn('onClickMessage', block); self.assertNotIn('ButtonWidget', block)
        self.assertEqual(block.count('"fontColor": "$FontColorLightGold"'), 3); self.assertEqual(block.count('"fontColor": "$FontColorLightGray"'), 6); self.assertEqual(block.count('"pointSize": 36'), 9); self.assertIn('"name": "TouchCreditLabel2"', block)   # 「制作」标签（用户 2026-10-29） | the maker label
        self.assertGreater(out.index('"name": "TouchCreditsTitle"'), out.index('"name": "TouchCommunityOpt"'))
        self.assertEqual(touch_layout._ABOUT_H, 3 * touch_layout._CUSTOM_LINE_H + 2 * touch_layout._CREDIT_LINE_H)   # 缺省两行：制作 + 特别鸣谢 | two default lines: maker + special
        # 「鸣谢」与「关于」同一种分节标题（用户 2026-10-29）：同样式、同 x 0、同样带原版分隔线子控件（宿主 indent_rows 不缩进带子控件的标题）
        # "Credits" is the same kind of section title as "About" (user, 2026-10-29): same style, x 0, and the same original divider child (indent_rows leaves titles with children unindented)
        div = '"type": "ImageWidget", "name": "%s", "fields": { "rect": "$OptionsDividerRect", "filename": "PauseMenu\\\\Divider" }'
        self.assertIn(div % 'TouchAboutDivider', out); self.assertIn(div % 'TouchCreditsDivider', out)
        for t, d in (('TouchAboutTitle', 'TouchAboutDivider'), ('TouchCreditsTitle', 'TouchCreditsDivider')):
            m = re.search(r'"name": "%s", "fields": \{ "rect": \{ "x": (\d+), "y": \d+, "width": (\d+), "height": (\d+) \}, "text": "@\w+", "style": "\$StyleSettingsTitle" \}, "children": \[\s*\{ [^\n]*"name": "%s"' % (t, d), out)
            self.assertIsNotNone(m, t); self.assertEqual(m.groups(), ('0', '1730', str(touch_layout._CUSTOM_LINE_H)), t)
        self.assertIn('"onClickMessage": "PanelManager:ClosePanel:TouchCommunityOpt"', out)
        rows = out.index('"name": "TouchCustomRows"'); about = out.index('"name": "TouchAboutTitle"'); box = out.index('"name": "TouchCommTile00"'); scroll_end = out.index('"name": "TouchCustomSel"')
        self.assertLess(rows, about); self.assertLess(about, scroll_end, 'the About group scrolls with the rows')
        self.assertGreater(box, scroll_end, 'the box is outside the scroll view, drawn above it')
        # 关于组接在最后一行（含测试假行）之后，容器高度把它算进去 | the About group follows the last row (including test dummy rows) and the container height includes it
        h = int(re.search(r'"name": "TouchCustomRows", "fields": \{ "rect": \{ "x": 0, "y": 0, "width": 1500, "height": (\d+)', out).group(1))
        y_about = int(re.search(r'"name": "TouchAboutRow", "fields": \{ "rect": \{ "x": 0, "y": (\d+)', out).group(1))
        self.assertEqual(y_about, touch_layout._CUSTOM_H); self.assertEqual(h, touch_layout._CUSTOM_H + touch_layout._ABOUT_H)
        self.assertEqual(touch_layout.pad_custom_page('"name": "VideoOptionsPanel", "name": "OptionsTable"'), out)


class Text(unittest.TestCase):
    def test_six_languages_fit(self):
        rows = {r[0]: r[1:] for r in dm_i18n.rows()}
        langs = ('中文', 'English', 'Español', 'Español MX', 'Русский', '한국어')
        sample = {'dmAboutVersion': '1.0.0-rc1+1793254654 · a2bb5c3'}
        # 弹窗（RC1.1 重构）按列宽核：标签列 260、小「复制」钮 293（字左右各留 10）、声明与标题框内 1560、页脚左 880（版本）/ 右 560（制作 + 名字）/ 第二行 1480（特别鸣谢 + 名字 + 说明）
        # the redesigned box by column: label 260, small Copy 293 (10 margin each side), notice and title 1560, footer left 880 (version) / right 560 (maker + name) / second line 1480 (special + name + note)
        for key, pt, limit, tail in (('dmCommunityTitle', 48, BOX_W, ''), ('dmCommunityAbout', 48, BOX_W, ''), ('dmCommunityLabelQq', 28, CO_LABEL_W, ''), ('dmCommunityLabelSite', 28, CO_LABEL_W, ''), ('dmCommunityLabelGithub', 28, CO_LABEL_W, ''),
                               ('dmCommunityCopy', 28, CO_BTN_W - 20, ''), ('dmCommunityCopied', 28, CO_BTN_W - 20, ''), ('dmCommunityCopyFailShort', 28, CO_BTN_W - 20, ''), ('dmLoadCommFree', 28, BOX_W, ''),
                               ('dmAboutVersion', 28, 880, ''), ('dmCreditsMaker', 28, 560, ' Augustus'), ('dmCreditsSpecial', 28, CO_CARD_W, ' jhaizhou-ops'),
                               ('dmCommunityAbout', 40, BTN_W, ''), ('dmCommunityFe', 40, BTN_W, ''), ('dmCommunityOpen', 40, BTN_W, ''),
                               ('dmCommunityJoin', 40, PAUSE_BTN_W, ''), ('dmCommunityRow', 40, 1000, ''), ('dmCommunityAbout', 40, 1000, ''), ('dmCreditsTitle', 40, 1000, ''), ('dmCreditsSpecial', 36, CREDIT_LABEL_W, ''), ('dmCreditsMaker', 36, CREDIT_LABEL_W, ''), ('dmCreditsThanks', 36, CREDIT_LABEL_W, ''), ('dmCreditsUpstreamNote', 36, 1110 - 13 * 25, '')):   # 括号说明接在 jhaizhou-ops（13 字）之后同一行 | the note follows jhaizhou-ops on the same line
            texts = rows[key]
            for lang, t in zip(langs, texts):
                self.assertEqual(t.count('\n'), 0, (key, lang))
                line = t.replace('%s', sample.get(key, '')) + tail
                if key == 'dmCreditsSpecial' and pt == 28: line += rows['dmCreditsUpstreamNote'][langs.index(lang)]
                self.assertLessEqual(est_width(line, pt), limit, (key, lang, line, est_width(line, pt)))
        self.assertEqual([t.count('%s') for t in rows['dmAboutVersion']], [1] * 6)
        self.assertEqual(rows['dmLoadCommFree'][0], '本软件免费，付费购买均为倒卖')   # 与加载页分支同键同文 | same key and text as the loading-page branch


class Maker(unittest.TestCase):
    """署名「制作：Augustus」（用户 2026-10-29）：只在鸣谢区（提示框「关于」段与自定义页鸣谢子块），排在「特别鸣谢」前、同排版同字号；六套语言只翻标签，名字不翻；
    暂停菜单 / 主菜单 / 加载页 / 分享日志摘要都不加；fork README 作者行。
    Maker credit (user, 2026-10-29): only in the credits area, before Special thanks, same layout and size; only the label is translated; nowhere else; the fork README's author line."""
    def test_label_six_languages(self):
        rows = {r[0]: r[1:] for r in dm_i18n.rows()}
        self.assertEqual(rows['dmCreditsMaker'], ('制作', 'Made by', 'Hecho por', 'Hecho por', 'Автор', '제작'))
        for t in rows['dmCreditsMaker']: self.assertNotIn('Augustus', t)   # 名字不进文案表 | the name is not in the text table

    def test_only_in_credits(self):
        c = (DM / 'host' / 'dm_community.c').read_text(encoding='utf-8')
        self.assertEqual(c.count('credits_all(k_maker_default'), 2)   # 提示框与自定义页两处 | the box and the Custom page
        self.assertEqual(c.count('"Augustus"'), 1)
        for f in ('ios/tools/touch_layout.py', 'i18n/ui_text.tsv', 'ios/Sources/dm_sharelog.m', 'host/front_mac.m', 'host/dm_update.c', 'host/diag.c'):
            self.assertNotIn('Augustus', (DM / f).read_text(encoding='utf-8'), f)   # 别处不加 | nowhere else

    def test_readme_author_line(self):
        root = DM.parent
        self.assertIn('Maintained by Augustus', (root / 'README.md').read_text(encoding='utf-8'))
        self.assertIn('制作：Augustus', (root / 'README.zh-CN.md').read_text(encoding='utf-8'))


class Host(unittest.TestCase):
    def test_host_wiring(self):
        c = (DM / 'host' / 'dm_community.c').read_text(encoding='utf-8')
        for s in ('"TouchCommunity"', '"TouchCommunityOpt"', '"TouchCommunityFe"', '"TouchAbout"', '"TouchCommCopy"', '"TouchCommOk"', '"TouchCommTitle"', 'dm_padmode_page_keys(DM_BTN_A | DM_BTN_B | DM_BTN_UP | DM_BTN_DOWN)', 'dm_update_label()', '"dmAboutVersion"',
                  '社区入口：已显示', '"主菜单"', '"暂停菜单"', '"自定义页"', '社区入口：复制 %s %s', '"群号" : ch[row] == DM_CH_SITE ? "官网" : "GitHub"', '社区入口：清单有字段', '社区入口：清单无字段', '"PauseLayout"', '"PauseLayoutGarden"', '"VideoOptionsPanel"', '"MainMenuButtonRibbon"', '"MainMenuPanel"',
                  '"TouchAboutCommunityRow"', 'TouchCreditLine%d', '"dmCreditsSpecial"', '"dmCreditsMaker"', 'k_maker_default[] = { "Augustus" }', 'TouchCreditLabel2', 'co_place(l0, ys >= 0 ? DM_ROW_INDENT : 99999', 'dm_sel_frame.h', '"dmCreditsThanks"', '"dmCreditsUpstreamNote"', '"dmCreditsSep"', '"jhaizhou-ops"'):
            self.assertIn(s, c, s)
        self.assertIn('dm_community_pump()', (DM / 'host' / 'hle_hid.c').read_text(encoding='utf-8'))
        self.assertIn('"TouchCommDivider"', c); self.assertIn('co_divider(', c)   # 分隔线按原图宽度居中摆，收起时挪出屏 | divider centred by its native width, moved away on dismiss
        self.assertNotIn('co_text(b, de[2], dm_str(b, "dmCommunityAbout"))', c)   # 社区框不再写「关于」子标题 | the community box no longer writes the About sub-title
        self.assertEqual(touch_layout._COMM_STUB.count('\n'), 42)
        self.assertIn('dm_community_on_uimsg(cat, act, arg)', (DM / 'host' / 'diag.c').read_text(encoding='utf-8'))
        self.assertIn('dm_community.c', (DM / 'host' / 'Makefile').read_text(encoding='utf-8'))
        self.assertIn('t_community', (DM / 'host' / 'tests' / 'Makefile').read_text(encoding='utf-8'))
        custom = (DM / 'host' / 'dm_custom.c').read_text(encoding='utf-8')
        self.assertIn('dm_community_available()', custom); self.assertIn('"TouchAboutCommunityRow"', custom)   # 手柄十字键能选到关于行、A 打开 | the D-pad reaches the About row and A opens it
        self.assertIn('const char *dm_update_label(void) { return g_label; }', (DM / 'host' / 'dm_update.c').read_text(encoding='utf-8'))

    def test_shells_hand_over_manifest_and_clipboard(self):
        ios = (DM / 'ios' / 'Sources' / 'dm_update.m').read_text(encoding='utf-8')
        self.assertIn('dm_community_manifest(', ios); self.assertIn('UIPasteboard', ios); self.assertIn('dm_community_copy_init(', ios)
        self.assertNotIn('openURL', ios)   # 四审 Q4-04：仍不打开任何页面 | fourth review Q4-04: still opens no page
        mac = (DM / 'host' / 'app_mac.m').read_text(encoding='utf-8')
        self.assertIn('dm_community_manifest(', mac); self.assertIn('NSPasteboard', mac); self.assertIn('dm_community_copy_init(', mac)
        main = (DM / 'host' / 'main.c').read_text(encoding='utf-8'); self.assertIn('dm_community_load()', main)
        self.assertIn('dm_community_load()', (DM / 'ios' / 'Sources' / 'main.m').read_text(encoding='utf-8'))

    def test_share_log_summary_keys(self):
        share = (DM / 'ios' / 'Sources' / 'dm_sharelog.m').read_text(encoding='utf-8')
        keys = re.search(r'static const char \*keys\[\] = \{([^}]*)\}', share).group(1)
        mac = (DM / 'host' / 'front_mac.m').read_text(encoding='utf-8')
        for k in ('社区入口：清单', '社区入口：已显示', '社区入口：复制'):
            self.assertIn('"%s"' % k, keys, k); self.assertIn('@"%s"' % k, mac, k)

    def test_docs(self):
        for rel in ('CHANGELOG.md', 'CHANGELOG.zh-CN.md', 'README.md', 'README.zh-CN.md'):
            self.assertIn('community', (DM.parent / rel).read_text(encoding='utf-8').lower(), rel)
        sample = (DM.parent / 'docs' / 'community_manifest.sample.json').read_text(encoding='utf-8')
        self.assertIn('"community"', sample); self.assertIn('"qq_group"', sample); self.assertIn('"github"', sample)


if __name__ == '__main__':
    unittest.main(verbosity=1)
