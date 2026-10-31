"""手机端生成器随 APK 带的本仓文件：只带白名单里的生成脚本与数据，不带各端的壳、测试、文档；文案表去掉 iOS 专属行；同样输入出同样字节。不读游戏内容。
The repo files the on-phone generator ships in the APK: only the whitelisted generator scripts and data, no platform shells, tests or docs; the string table without iOS-only rows; same input, same bytes. No game content.
"""
import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import unittest
import zipfile
import zlib

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
spec = importlib.util.spec_from_file_location('pack_gen_assets', TOOLS / 'pack_gen_assets.py')
pga = importlib.util.module_from_spec(spec); spec.loader.exec_module(pga)


DM = Path(__file__).resolve().parents[2]


class SourceZip(unittest.TestCase):
    def test_whitelist_has_no_shells_or_tests(self):
        names = pga.source_files(DM)
        self.assertLess(len(names), 100)
        self.assertIn('dm/tools/d2rm_android.py', names)
        allowed = ('dm/ios/tools/', 'dm/ios/docs/native_art_v1_geometry.json', 'dm/ios/docs/touch_layout_spec_native_binding_v5.json', 'dm/ios/Sources/TouchArt/native_art_manifest.json', 'dm/ios/Sources/ControlArt/dm_mobile_socket.png', 'dm/ios/NativeHUD/touch/close.png')
        for n in names:
            self.assertFalse(n.startswith(('dm/third_party/', 'dm/harmony/', 'dm/tvos/', 'dm/android/app/')) or '/tests/' in n, n)
            if n.startswith('dm/ios/'): self.assertTrue(n.startswith(allowed), n)   # dm/ios 下只有生成器与它读的数据 | under dm/ios only the generator and the data it reads
        self.assertNotIn('dm/tools/d2rm_pack.py', names); self.assertNotIn('dm/ios/tools/platform_words.py', names)

    def test_fingerprint_inputs_ship(self):
        self.assertLessEqual(set(pga.tool_module('overlay_contract').SOURCE_INPUTS), set(pga.source_files(DM)))

    def test_missing_file_stops_the_build(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / 'dm/data').mkdir(parents=True)
            with self.assertRaises(SystemExit): pga.source_files(root / 'dm')

    def test_imports_of_shipped_python_are_shipped(self):
        names = set(pga.source_files(DM)); stems = {Path(n).stem for n in names}
        ours = {p.stem for p in DM.rglob('*.py') if '/tests/' not in str(p) and 'third_party' not in str(p)} - {'setup'}
        lazy = {'platform_words', 'pack_gen_assets', 'package_audit'}
        for n in names:
            if not n.endswith('.py'): continue
            for line in (DM.parent / n).read_text(encoding='utf-8').splitlines():
                m = re.match(r'\s*(?:import|from)\s+([\w.]+)', line)
                if m and m.group(1).split('.')[0] in ours - lazy: self.assertIn(m.group(1).split('.')[0], stems, '%s: %s' % (n, line))

    def test_zip_filters_table_and_is_reproducible(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / 'dm/i18n').mkdir(parents=True); (root / 'dm/PRODUCT').write_text('3.3.94012-r44\n')
            (root / 'dm/i18n/ui_text.tsv').write_text('键\t中文\ndmUpdNew\t新版本\ndmCloudTitle\tiCloud\ndmOldQuit\t退出\n', encoding='utf-8')
            a, b, names = root / 'a.zip', root / 'b.zip', ['dm/PRODUCT', 'dm/i18n/ui_text.tsv']
            pga.write_zip(root / 'dm', names, a)
            (root / 'dm/PRODUCT').touch()   # 只改时间 | only the time changes
            pga.write_zip(root / 'dm', names, b)
            self.assertEqual(a.read_bytes(), b.read_bytes())
            with zipfile.ZipFile(a) as z: self.assertEqual(z.read('dm/i18n/ui_text.tsv').decode('utf-8'), '键\t中文\ndmUpdNew\t新版本\n')


class PackageAudit(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('package_audit', DM / 'tools/package_audit.py')
        self.pa = importlib.util.module_from_spec(spec); spec.loader.exec_module(self.pa)
        self.w = self.pa.platform_words

    def test_words(self):
        hit = lambda word, text: bool(self.w.pattern(word).search(text))
        self.assertTrue(hit('iPhone', b'iPhones')); self.assertTrue(hit('iPad', b'iPadOS')); self.assertTrue(hit('MetalFX', b'MetalFXSpatial'))
        self.assertTrue(hit('Xcode', b'docs/NO_XCODE')); self.assertTrue(hit('iPhone', b'IPHONE')); self.assertTrue(hit('iOS', b'on iOS')); self.assertTrue(hit('iOS', b'NO_IOS'))
        for word, text in (('Metal', b'Metallic'), ('Metal', b'metal_roughness'), ('iOS', b'BIOS'), ('iOS', b'ios_base'), ('Android', b'android::String8'), ('Apple', b'apple_lod3'), ('iPad', b'IPAddressText')): self.assertFalse(hit(word, text), text)

    def test_symbol_and_text(self):
        one = b'\0MetalFX\0'; self.assertFalse(self.pa.is_symbol(one, 1, 8))   # 单词文字不算（内审 L-01） | a single-word text is no symbol
        data = b'\0dm_icloud_supported\0iCloud sync failed\0'
        spots = [m.span() for m in self.w.pattern('iCloud').finditer(data)]
        self.assertEqual([self.pa.is_symbol(data, *s) for s in spots], [True, False])

    def test_game_image_only(self):
        seg = struct.pack('<II16sQQQQ', 0x19, 72, b'__DMsdkW', 0, 0, 100, 50) + b'\0' * 16
        head = struct.pack('<I', 0xfeedfacf) + b'\0' * 12 + struct.pack('<I', 1) + b'\0' * 12
        self.assertEqual(self.pa.game_image_ranges(head + seg), [(100, 150)])
        self.assertEqual(self.pa.game_image_ranges(b'not a mach-o'), [])

    def test_inflate(self):
        data = b'junk' + zlib.compress(b'iPhone text') + b'more' + zlib.compress(b'second')
        self.assertEqual(self.pa.inflate_all(data), b'iPhone text\0second')


if __name__ == '__main__':
    unittest.main()
