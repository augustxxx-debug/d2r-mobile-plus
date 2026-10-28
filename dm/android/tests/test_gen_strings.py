"""安卓启动界面文案：统一表里的六套（中、英、西、拉美西、俄、韩）都出成安卓资源，拉美西语空着时同西语。不读游戏内容。
Android startup text: all six sets in the shared table (Chinese, English, Spanish, Latin American Spanish, Russian, Korean) become Android resources; Latin American Spanish falls back to Spanish when empty. No game content.
"""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
spec = importlib.util.spec_from_file_location('gen_strings', TOOLS / 'gen_strings.py')
strings = importlib.util.module_from_spec(spec); spec.loader.exec_module(strings)
spec = importlib.util.spec_from_file_location('dm_i18n', TOOLS.parents[1] / 'ios/tools/dm_i18n.py')
table = importlib.util.module_from_spec(spec); spec.loader.exec_module(table)

FOLDERS = {'values': 2, 'values-zh': 1, 'values-es': 3, 'values-es-rMX': 4, 'values-ru': 5, 'values-ko': 6}


def read(path):
    return {e.get('name'): (e.text or '').replace('\\n', '\n').replace("\\'", "'").replace('\\"', '"').replace('\\\\', '\\') for e in ET.parse(path).getroot()}


class SixLanguages(unittest.TestCase):
    def test_every_android_row_in_six_locales(self):
        rows = {r[0]: r for r in table.rows() if r[0].startswith('dmAndroid')}
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); strings.generate(root)
            for folder, column in FOLDERS.items():
                got = read(root / folder / 'strings.xml')
                for key in ('dmAndroidGenTitle', 'dmAndroidGenFailed', 'dmAndroidGenNeedFiles', 'dmAndroidGenNoSpace', 'dmAndroidImporting'):
                    self.assertEqual(got[key], rows[key][column], (folder, key))
                self.assertEqual(set(k for k in got if k.startswith('dmAndroid')), set(rows), folder)

    def test_generation_steps_differ_by_language(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); strings.generate(root)
            titles = {folder: read(root / folder / 'strings.xml')['dmAndroidGenTitle'] for folder in FOLDERS}
            self.assertEqual(len({titles[f] for f in ('values', 'values-zh', 'values-es', 'values-ru', 'values-ko')}), 5)


if __name__ == '__main__':
    unittest.main()
