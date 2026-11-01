"""手柄版建角模式页「极乐世界」左边的键帽（缺陷台账 B019，iOS 同源 B018）：原来只有一张写死的 Switch「Y」（ButtonLeft），接 Xbox / PS 手柄时玩家照字面按印 Y 的上键
（= Switch X，这一页收掉）没反应。布局在同位再放 Xbox / PS 两张（覆盖层的 switchx* / switchp* 两套，按游戏键重排：左键 = Xbox 的 X / PS 的 □），缺省在屏外，宿主按当前键帽样式对调
（dm_padmode.c 的名字表与这里的控件名一一对应）。
The key cap left of "Elysium" on the controller character-creation mode page (defect ledger B019, iOS twin B018): it used to be a single hard-coded Switch "Y" (ButtonLeft),
so with an Xbox / PS controller players pressing the printed Y (top = Switch X, swallowed on this page) got nothing. The layout now carries Xbox / PS versions at the same
spot (the overlay's switchx* / switchp* sets, reordered by game key: left = Xbox X / PS square), off screen by default, and the host swaps by the current key-cap style
(the name table in dm_padmode.c matches the widget names here one to one)."""
import os
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
os.environ.pop('MOVIE_FFMPEG', None); os.environ.pop('DM_OVERLAY_MAC', None)
import touch_layout  # noqa: E402

# 原版手柄版建角模式页的最小骨架：天梯四个控件、提示条（含「X 经典版」那一条） | minimal skeleton of the original controller mode page: the four ladder widgets, the legend (with the "X Classic" entry)
PAGE = ('{ "children": [\n'
        + ''.join('        { "type": "Widget", "name": "%s", "fields": { "rect": { "x": 0, "y": 0 } } },\n' % n for n in ('LadderFlag', 'LadderToggle', 'NonLadderToggle', 'LadderCenter'))
        # 标准 / 专家那一行：导航照原版（两张牌都指向中间的骷髅旗帜） | the Standard / Hardcore row: navigation as in the original (both plaques point at the skull banner)
        + '        { "type": "ToggleButtonWidget", "name": "StandardToggle", "fields": { "navigation": { "right": {"name": "HardcoreCenter"}, "up": {"name": "LadderCenter"}, "down": {"name": "ExpansionCenter"} } } },\n'
        + '        { "type": "ToggleButtonWidget", "name": "HardcoreToggle", "fields": { "navigation": { "left": {"name": "HardcoreCenter"}, "up": {"name": "LadderCenter"}, "down": {"name": "ExpansionCenter"} } } },\n'
        + '        { "type": "ButtonWidget", "name": "HardcoreCenter", "fields": { "navigation": { "left": {"name": "StandardToggle"}, "right": {"name": "HardcoreToggle"}, "up": {"name": "LadderCenter"}, "down": {"name": "ExpansionCenter"} } } },\n'
        + '        { "type": "ButtonLegendWidget", "name": "CharacterOptionsLegend", "fields": { "entries": [ { "action": "option2", "description": "@LegacyGameVersionSwitch" }, ] } },\n'
        '] }')


def keys(s):
    return {m.group(1): (int(m.group(2)), m.group(3)) for m in re.finditer(
        r'"name": "(TouchBlissKey[A-Z]?)", "fields": \{ "rect": \{ "x": (-?\d+), "y": 1330, "scale": 1\.5 \}, "filename": "([^"]+)"', s)}


class BlissKeyGlyphs(unittest.TestCase):
    def test_switch_xbox_ps_at_the_same_spot(self):
        k = keys(touch_layout.pad_bliss(PAGE))
        self.assertEqual(k['TouchBlissKey'], (-1665, 'Controller/ControllerButtons/Switch/ButtonLeft'))   # 缺省（Switch 键帽）在原位 | the default (Switch caps) in place
        self.assertEqual(k['TouchBlissKeyX'], (99999, 'Ztouch_controller_buttons_ui/switchXButtonLeft'))
        self.assertEqual(k['TouchBlissKeyP'], (99999, 'Ztouch_controller_buttons_ui/switchPButtonLeft'))
        self.assertNotIn('TouchBlissKeyK', k)   # 键盘那张只在 Mac 覆盖层 | the keyboard one only in the Mac overlay

    def test_plaques_point_at_each_other(self):
        # B072：专家牌按一次左就回到标准（不再先落到中间旗帜、选中项还是专家）；旗帜进页落点与左右照旧 | B072: one Left from Hardcore reaches Standard; the banner keeps its entry landing and Left / Right
        import json
        page = json.loads(re.sub(r',(\s*[}\]])', r'\1', touch_layout.pad_bliss(PAGE)))
        nav = {c['name']: c['fields']['navigation'] for c in page['children'] if c['name'] in ('StandardToggle', 'HardcoreToggle', 'HardcoreCenter')}
        self.assertEqual(nav['StandardToggle']['right'], {'name': 'HardcoreToggle'})
        self.assertEqual(nav['HardcoreToggle']['left'], {'name': 'StandardToggle'})
        self.assertEqual((nav['HardcoreCenter']['left'], nav['HardcoreCenter']['right']), ({'name': 'StandardToggle'}, {'name': 'HardcoreToggle'}))
        self.assertEqual(nav['StandardToggle']['up'], {'name': 'LadderCenter'})   # 上下不动（宿主在这一页收掉上 / 下） | up / down untouched (the host swallows them on this page)

    def test_mac_adds_keyboard(self):
        os.environ['DM_OVERLAY_MAC'] = '1'
        try: k = keys(touch_layout.pad_bliss(PAGE))
        finally: os.environ.pop('DM_OVERLAY_MAC', None)
        self.assertEqual(k['TouchBlissKeyK'][0], 99999); self.assertEqual(len(k), 4)

    def test_glyph_files_are_in_the_overlay_sets(self):
        # 覆盖层按「switch + 套别字母 + 原版名」写图（gen_romfs_overlay.py），游戏按小写认路径 | the overlay writes "switch + set letter + original name"; the game matches paths in lower case
        src = (DM / 'ios' / 'tools' / 'gen_romfs_overlay.py').read_text(encoding='utf-8')
        self.assertIn("ztouch_controller_buttons_ui/switchx'+nm.lower()", src)
        self.assertIn("ztouch_controller_buttons_ui/switchp'+nm", src)

    def test_host_name_table_matches_glyph_styles(self):
        # dm_padmode.c 的名字表按 DM_GLYPH_PAD / KBD / XBOX / PS 的次序 | the dm_padmode.c name table follows the order DM_GLYPH_PAD / KBD / XBOX / PS
        enum = re.search(r'enum \{ (DM_GLYPH_[A-Z_, ]+) \}', (DM / 'host' / 'dm_input.h').read_text(encoding='utf-8')).group(1)
        self.assertEqual([x.strip() for x in enum.split(',')], ['DM_GLYPH_PAD', 'DM_GLYPH_KBD', 'DM_GLYPH_XBOX', 'DM_GLYPH_PS', 'DM_GLYPH_N'])
        host = (DM / 'host' / 'dm_padmode.c').read_text(encoding='utf-8')
        self.assertIn('{ "TouchBlissKey", "TouchBlissKeyK", "TouchBlissKeyX", "TouchBlissKeyP" }', host)


if __name__ == '__main__':
    unittest.main()
