"""手柄版选角列表的字号：两份字号档两种模式共用（BIG_FONT 统一 40），原版字节下第三行「等级 职业」压出 184 高的行、被下一行底图和选中金框盖住半截。
手柄版页（Controller/Console 层）给三个文字控件补上完整样式、字号同触屏版（CHARSEL_FONTS = 原主机档 30 / 34 / 30），并进手柄专用清单。
Controller character list font sizes: both font profiles are shared by the two modes (BIG_FONT, uniformly 40); with the original bytes the third line (level and class)
runs out of the 184-unit row and is half covered by the next row's tile and the selection frame. The controller page (Controller/Console layer) gets full styles on the three
text widgets with the touch version's sizes (CHARSEL_FONTS = the original console 30 / 34 / 30) and joins the controller-only list."""
import json
import os
import re
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
os.environ.pop('MOVIE_FFMPEG', None); os.environ.pop('DM_OVERLAY_MAC', None)
import touch_layout  # noqa: E402

ROW_H = 184        # 原版列表行高（CharacterTemplate rect） | original list row height (CharacterTemplate rect)
LEVEL_Y = 120      # 原版「等级 职业」行的 y（noTitleCharacterLevelClassPosition 同值） | original y of the level-and-class line


def text_box(name, size_var):
    alt = ('        "useAltStyleIfDoesntFit": true,\r\n        "altStyle": {\r\n            "fontColor": "$LightButtonTextColor",\r\n            "pointSize": "$SmallFontSize",\r\n'
           '            "spacing": "$MinimumSpacing",\r\n        },\r\n') if name == 'Name' else ''
    return ('{\r\n    "type": "TextBoxWidget", "name": "%s",\r\n    "fields": {\r\n        "rect": { "x": 25, "y": 11, "width": 400 },\r\n'
            '        "style": {\r\n            "fontColor": "$CharacterListDetailsColor",\r\n            "pointSize": "%s",\r\n            "spacing": "$ReducedSpacing",\r\n        },\r\n%s    }\r\n}' % (name, size_var, alt))


BASE = ('{ "children": [ ' + ', '.join(text_box(n, v) for n, v, _ in touch_layout.CHARSEL_FONTS) + ' ] }')
CONSOLE = ('{\r\n    "basedOn": "Controller/CharacterSelectPanelHD.json",\r\n    "children": [\r\n'
           + ',\r\n'.join('        {\r\n            "type": "TextBoxWidget", "name": "%s",\r\n        }' % n for n, _, _ in touch_layout.CHARSEL_FONTS) + '\r\n    ]\r\n}')


def loads(text):   # 原版布局的 JSON 带 // 注释与尾逗号 | the original layout JSON carries // comments and trailing commas
    return json.loads(re.sub(r',(\s*[}\]])', r'\1', re.sub(r'//[^\n]*', '', text)))


class PadCharselect(unittest.TestCase):
    def test_sizes_are_the_original_console_profile(self):
        self.assertEqual([s for _, _, s in touch_layout.CHARSEL_FONTS], [30, 34, 30])
        # 40 号（BIG_FONT）的汉字字身约 1.15 个字号高：120 + 46 = 166 字脚已贴行底，再加投影与下沉就出界；30 号到 155 | at 40 (BIG_FONT) a CJK glyph body is about 1.15 em: 120 + 46 = 166 ...
        self.assertLessEqual(LEVEL_Y + 1.15 * touch_layout.CHARSEL_FONTS[2][2], ROW_H - 25)

    def test_full_style_with_numeric_size(self):
        out = loads(touch_layout.pad_charselect(CONSOLE, BASE))
        self.assertEqual(out['basedOn'], 'Controller/CharacterSelectPanelHD.json')
        got = {c['name']: c['fields']['style'] for c in out['children']}
        for name, _, size in touch_layout.CHARSEL_FONTS:
            self.assertEqual(got[name]['pointSize'], size, name)
            self.assertEqual(got[name]['fontColor'], '$CharacterListDetailsColor', name)   # 整块抄，不只是字号 | copied whole, not only the size
            self.assertEqual(got[name]['spacing'], '$ReducedSpacing', name)
        # 中文名字按 34 判放不下会换 altStyle：它也得是 34，否则中文名字仍是 40（英文 34） | a Chinese name judged "does not fit" at 34 takes altStyle: it must be 34 too
        alt = next(c['fields']['altStyle'] for c in out['children'] if c['name'] == 'Name')
        self.assertEqual(alt['pointSize'], 34)
        self.assertEqual(alt['spacing'], '$MinimumSpacing')
        self.assertTrue(all('altStyle' not in c['fields'] for c in out['children'] if c['name'] != 'Name'))

    def test_changed_original_fails_loudly(self):
        with self.assertRaises(SystemExit):
            touch_layout.pad_charselect(CONSOLE, BASE.replace('$XMediumLargeFontSize', '$LargeFontSize'))
        with self.assertRaises(SystemExit):
            touch_layout.pad_charselect(CONSOLE.replace('"name": "Name"', '"name": "PlayerName"'), BASE)

    def test_front_end_legend_size(self):
        menu = ('{\r\n    "children": [\r\n        {\r\n            "type": "ButtonLegendWidget", "name": "MainMenuLegend",\r\n            "fields": {\r\n                "fitToParent": true,\r\n'
                '                "text": {\r\n                    "style": "$StyleButtonLegend",\r\n                    "style": {\r\n                        "fontColor": "$FontColorLightGold",\r\n'
                '                        "spacing": { "leading": 1.4 }\r\n                    }\r\n                },\r\n            }\r\n        }\r\n    ]\r\n}')
        out = touch_layout.pad_legend_size(menu)
        self.assertEqual(touch_layout.PAD_LEGEND_PT, 28)   # 原主机档 _profileConsoleHD 的 $MediumFontSize | the original console profile's $MediumFontSize
        self.assertEqual(loads(out)['children'][0]['fields']['text']['style']['pointSize'], 28)   # 同名键后者生效：字面量那块带字号 | the later duplicate key wins
        self.assertIn('"$StyleButtonLegend"', out)
        with self.assertRaises(SystemExit):   # 原版那块已带字号 = 布局变了 | a size already there = the original changed
            touch_layout.pad_legend_size(out)

    def test_info_box_sizes(self):
        def tb(name, var):
            return ('                {\r\n                    "type": "TextBoxWidget", "name": "%s",\r\n                    "fields": {\r\n'
                    '                        "style": {\r\n                            "pointSize": "%s",\r\n                        },\r\n                    }\r\n                }' % (name, var))
        kids = [tb('Req%d' % k, '$MediumFontSize') for k in range(7)] + [tb('GameType', '$MediumFontSize'), tb('Title', '$XMediumFontSize'), tb('Name', '$LargeFontSize'), tb('Level&Class', '$XMediumFontSize')]
        menu = ('{\r\n    "children": [\r\n        {\r\n            "type": "RectangleWidget", "name": "PlayerInfoContainer",\r\n            "children": [\r\n' + ',\r\n'.join(kids)
                + '\r\n            ]\r\n        },\r\n' + tb('Outside', '$MediumFontSize') + '\r\n    ]\r\n}')
        out = loads(touch_layout.pad_info_sizes(menu))
        got = {c['name']: c['fields']['style']['pointSize'] for c in out['children'][0]['children']}
        self.assertEqual((got['Name'], got['Title'], got['Level&Class'], got['GameType'], got['Req0']), (38, 30, 30, 28, 28))   # 原主机档 | original console profile
        self.assertEqual(out['children'][1]['fields']['style']['pointSize'], '$MediumFontSize')   # 框外不动 | outside the box untouched
        with self.assertRaises(SystemExit):   # 个数对不上 = 原版变了 | count mismatch = the original changed
            touch_layout.pad_info_sizes(menu.replace('"$LargeFontSize"', '"$XMediumFontSize"'))

    def test_overlay_wiring(self):
        src = (DM / 'ios' / 'tools' / 'gen_romfs_overlay.py').read_text(encoding='utf-8')
        self.assertIn("touch_layout.pad_charselect(lt,base)", src)
        self.assertIn("touch_layout.pad_info_sizes(touch_layout.pad_legend_size(touch_layout.pad_mainmenu(lt)))", src)
        pad_pages = re.search(r"PAD_PAGES=\((.*?)\)\n", src, re.S).group(1)
        self.assertIn("'controller/console/characterselectpanelhd.json'", pad_pages)


if __name__ == '__main__':
    unittest.main()
