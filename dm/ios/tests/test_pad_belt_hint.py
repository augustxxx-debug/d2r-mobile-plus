"""手柄版底栏腰带下的十字键提示上提（缺陷台账 B030，iOS 同源 B017）：只改 HotkeyLabelTemplate 的 y（98 → PAD_BELT_HINT_Y），别的字节不动，且进手柄专用清单。
The D-pad hints under the controller bottom bar's belt are raised (defect ledger B030, iOS twin B017): only HotkeyLabelTemplate's y changes (98 -> PAD_BELT_HINT_Y), nothing
else, and the page is in the controller-only list."""
import os
import sys
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DM / 'ios' / 'tools'))
os.environ.pop('MOVIE_FFMPEG', None); os.environ.pop('DM_OVERLAY_MAC', None)
import touch_layout  # noqa: E402

HUD = ('{\n    "type": "HUDPanelHD", "name": "HUDPanel",\n    "children": [\n        {\n            "type": "BeltWidget", "name": "Belt",\n'
       '            "fields": {\n                "rect": { "x": 1591, "y": 263, },\n            },\n            "children": [\n                {\n'
       '                    "type": "TextBoxWidget", "name": "HotkeyLabelTemplate",\n                    "fields": {\n                        "rect": { "x": 8, "y": 98 },\n'
       '                    }\n                },\n            ]\n        },\n    ]\n}')


class PadBeltHint(unittest.TestCase):
    def test_only_the_label_moves_up(self):
        out = touch_layout.pad_hud(HUD)
        self.assertEqual(out, HUD.replace('"x": 8, "y": 98', '"x": 8, "y": %d' % touch_layout.PAD_BELT_HINT_Y))
        self.assertLess(touch_layout.PAD_BELT_HINT_Y, 98)

    def test_refuses_an_unexpected_layout(self):
        with self.assertRaises(AssertionError): touch_layout.pad_hud(HUD.replace('"y": 98', '"y": 90'))

    def test_in_controller_only_list(self):
        src = (DM / 'ios' / 'tools' / 'gen_romfs_overlay.py').read_text(encoding='utf-8')
        self.assertIn("'controller/hudpanelhd.json'", src[src.index('PAD_PAGES=('):src.index('PAD_PAGES=PAD_PAGES')])
        self.assertIn('touch_layout.pad_hud(lt)', src)


if __name__ == '__main__':
    unittest.main()
