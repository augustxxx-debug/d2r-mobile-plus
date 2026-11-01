"""手机端生成器随 APK 带的本仓文件：只带白名单里的生成脚本与数据，不带各端的壳、测试、文档；文案表去掉 iOS 专属行；同样输入出同样字节。不读游戏内容。
The repo files the on-phone generator ships in the APK: only the whitelisted generator scripts and data, no platform shells, tests or docs; the string table without iOS-only rows; same input, same bytes. No game content.
"""
import hashlib
import importlib.util
from pathlib import Path
import random
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
        # 只有词两边都不是文字才算偶然（B113，内审 N11-01 的样例 + 长度前缀 + 无 NUL 长块）| chance only when neither side is text (B113)
        for chance in (b'\0\x93\x01IOS\0', b'\x01iOS\x01', b'\xda\x01iOS\xff\0'):
            at = chance.lower().find(b'ios'); self.assertTrue(self.pa.is_chance_bytes(chance, at, at + 3), chance)
        for text in (b'\0Runs on iOS only\0', b'\0log iOS\n\0', b'\0key\tRuns on iOS\tzh\n\0', b'\0a\r\nRuns on iOS\r\n\0', b'\0\x1b[31mRuns on iOS\x1b[0m\0',
                     b'\0\x0fRuns on iOS now\0', b'\0\x93\x01Runs on iOS only\0', b'\x7fELF\x02\x01\0\x03' + b'A' * 5000 + b' Runs on iOS only\0', b'\0Runs on iOS only' + b'B' * 5000,
                     b'Runs on iOS only\0', b'\x7fELF\x02\x01Runs on iOS\0', b'\0\x01\0' + ('\u652f\u6301\u6587\u672c' * 1500).encode() + ' iOS \u4e13\u5c5e \n'.encode() + b'\0',
                     b'\x01\0' + ('\u4e2d' * 2000).encode() + b'\xe4\xb8 iOS\0', b'\0caf\xe9 iOS\0', b'\0\xe2\x80\x8bRuns on iOS\0', b'\0\x7fRuns on iOS\0',
                     b'\0iOS\0', b'iOS\0', b'\0\x03iOS\0', b'\xda\x03iOS\xff', b'\x01\xe4\xb8\xadiOS\0', b'\x01iOS\xe4\xb8\xad\0', b'\x01iOS 17\0'):
            at = text.find(b'iOS'); self.assertFalse(self.pa.is_chance_bytes(text, at, at + 3), text[:40])
        one = b'\0MetalFX\0'; self.assertFalse(self.pa.is_symbol(one, 1, 8))   # 单词文字不算（内审 L-01） | a single-word text is no symbol
        data = b'\0dm_icloud_supported\0iCloud sync failed\0'
        spots = [m.span() for m in self.w.pattern('iCloud').finditer(data)]
        self.assertEqual([self.pa.is_symbol(data, *s) for s in spots], [True, False])

    def test_game_image_only(self):
        seg = struct.pack('<II16sQQQQ', 0x19, 72, b'__DMsdkW', 0, 0, 100, 50) + b'\0' * 16
        head = struct.pack('<I', 0xfeedfacf) + b'\0' * 12 + struct.pack('<I', 1) + b'\0' * 12
        self.assertEqual(self.pa.game_image_ranges(head + seg), [(100, 150)])
        self.assertEqual(self.pa.game_image_ranges(b'not a mach-o'), [])

    def test_split(self):
        data = b'junk' + zlib.compress(b'iPhone text') + b'more' + zlib.compress(b'second')
        self.assertEqual(self.pa.split_archive(data), (b'iPhone text\0second', b'junk\0more', []))

    def audit_one(self, data, platform='ios'):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / 'x.zip'
            with zipfile.ZipFile(p, 'w') as z: z.writestr('overlay.data', data)
            return self.pa.audit(platform, p)[0]

    # 外审 X-03 的三个夹具：原样 JSON、有效 BLTE N 块、压缩后超过 16 MiB 的有效流，都要报出来 | the three fixtures of external review X-03: plain JSON, a valid BLTE N chunk, a valid stream over 16 MiB compressed; all must be reported
    def test_raw_entry(self):
        self.assertEqual(self.audit_one(b'{"text":"Android-only option"}'), [('overlay.data', 'Android', 1)])

    def test_blte_n_chunk(self):
        raw = b'{"text":"Android-only option"}'; chunk = b'N' + raw
        blte = b'BLTE' + struct.pack('>I', 36) + b'\x0f\0\0\1' + struct.pack('>II', len(chunk), len(raw)) + hashlib.md5(chunk).digest() + chunk
        self.assertEqual(self.audit_one(blte), [('overlay.data!zlib', 'Android', 1)])

    def test_large_stream(self):
        data = zlib.compress(random.Random(20261031).randbytes(17 * 1024 * 1024) + b'\0Vulkan only\0' * 10000)
        self.assertGreater(len(data), 16 << 20)
        self.assertEqual(self.audit_one(data), [('overlay.data!zlib', 'Vulkan', 10000)])

    # 解不完必须失败并写明原因 | what cannot be inflated must fail, with the reason
    def incomplete(self, data):
        bad = self.audit_one(data); self.assertTrue(bad); return ' '.join(b[1] for b in bad)

    def test_incomplete(self):
        rng = random.Random(1); c = zlib.compress(bytes(rng.choice(b'abcdefghij klmnop') for _ in range(400000)))
        self.assertIn('被截断', self.incomplete(c[:len(c) // 2]))
        self.assertIn('数据错', self.incomplete(c[:len(c) // 2] + b'\xff' * 64 + c[len(c) // 2 + 64:]))
        short = zlib.compress(bytes(rng.choice(b'abcdefghij klmnop') for _ in range(3000))); self.assertLess(len(short), 4096); self.assertIn('数据错', self.incomplete(short[:600] + b'\xff' * 64 + short[664:]))   # 不足一次 4 KiB 也要报 | shorter than 4 KiB is reported too
        stored = zlib.compress(random.Random(1).randbytes(300000)); self.assertIn('数据错', self.incomplete(stored[:-1] + bytes([stored[-1] ^ 1])))

    def test_limit(self):
        inflated, plain, notes = self.pa.split_archive(zlib.compress(b'x' * 100000), limit=1000)
        self.assertEqual(inflated, b''); self.assertEqual(len(notes), 1); self.assertIn('上限', notes[0])

    def test_chance_header_is_not_a_stream(self):
        self.assertEqual(self.audit_one(b'x^ junk, not a stream ' * 20), [])

    # 外审 X-04：低窗口 zlib 要解开扫；解不开的编码、不是文本的残留都要失败并写明，不能当明文扫完放过 | external review X-04: low-window zlib is inflated and scanned; encodings that cannot be decoded and non-text remains fail with a statement, not pass after a plain scan
    def blte(self, chunk, usize):
        return b'BLTE' + struct.pack('>I', 36) + b'\x0f\0\0\1' + struct.pack('>II', len(chunk), usize) + hashlib.md5(chunk).digest() + chunk

    def test_low_window_zlib(self):
        for wbits in (9, 10, 12, 14, 15):
            c = zlib.compressobj(9, wbits=wbits); data = c.compress(b'Vulkan only ' * 100) + c.flush()
            self.assertEqual(self.audit_one(data), [('overlay.data!zlib', 'Vulkan', 100)], wbits)

    def test_blte_unknown_modes(self):
        last = b'ulkan only .'; lz4 = b'\x30Vul\x03\x00' + bytes([len(last) << 4]) + last   # 解开是 VulVulVulkan only .，压缩字节里没有这个词 | decodes to VulVulVulkan only .; the word is not in the encoded bytes
        for mode, body in ((b'4', (19).to_bytes(4, 'little') + lz4), (b'E', b'\x11' * 40), (b'F', b'\x22' * 40), (b'?', b'\x33' * 40)):
            self.assertIn('不支持', self.incomplete(self.blte(mode + body, 19)), mode)

    def test_blte_broken(self):
        good = self.blte(b'Ntext', 4)
        self.assertIn('MD5', self.incomplete(good[:-1] + b'x'))
        self.assertIn('越界', self.incomplete(good[:-1]))
        self.assertIn('头表', self.incomplete(b'BLTE' + b'\xff' * 20))
        self.assertIn('解出', self.incomplete(self.blte(b'Ntext', 9)))
        z = zlib.compress(b'text'); self.assertIn('多余', self.incomplete(self.blte(b'Z' + z + b'tail', 4)))

    def test_blte_two_chunks_join(self):   # 词跨块也要能扫到 | a word across chunks is still found
        a, b = b'NVul', b'Zx' ; b = b'Z' + zlib.compress(b'kan only')
        head = b'BLTE' + struct.pack('>I', 60) + b'\x0f\0\0\2' + struct.pack('>II', len(a), 3) + hashlib.md5(a).digest() + struct.pack('>II', len(b), 8) + hashlib.md5(b).digest()
        self.assertEqual(self.audit_one(head + a + b), [('overlay.data!zlib', 'Vulkan', 1)])

    def test_real_shape_is_clean(self):   # 实包的样子：容器后面没有残留 | the shape of a real package: nothing left over
        data = self.blte(b'Z' + zlib.compress(b'plain game text'), 15) + zlib.compress(b'more') + b'\0' * 16
        self.assertEqual(self.pa.split_archive(data), (b'plain game text\0more', b'', []))

    def test_not_text_fails(self):
        last = b'ulkan only .'; lz4 = b'\x30Vul\x03\x00' + bytes([len(last) << 4]) + last
        for data in (lz4, b'\x78\x9c' + b'junk' * 20, zlib.compress(b'Vulkan only ' * 100)[:-1], zlib.compress(b'short')[:-1], b'\xff\xfe\x01binary'):
            self.assertIn('不是文本', self.incomplete(data), data)

    # 内审 N-01：N 块开头的词、原样段的拼接边界 | internal audit N-01: a word opening an N chunk, and the seam between plain segments
    def test_word_opening_n_chunk(self):
        raw = b'Android first'; self.assertEqual(self.audit_one(self.blte(b'N' + raw, len(raw))), [('overlay.data!zlib', 'Android', 1)])

    def test_no_word_across_seams(self):
        self.assertEqual(self.audit_one(b'Vulk' + zlib.compress(b'x') + b'an'), [])
        self.assertEqual(self.audit_one(zlib.compress(b'Vulk') + zlib.compress(b'an')), [])

    def test_made_up_identifier(self):
        for name in (b'VK_STRUCTURE_TYPE_METALFX_HINT', b'mtl_metalfx_show_hint', b'front_metal_layer'):
            data = b'\0' + name + b'\0'; self.assertFalse(self.pa.is_symbol(data, 1, 1 + len(name)), name)
        for name in (b'VK_STRUCTURE_TYPE_EXPORT_METAL_BUFFER_INFO_EXT', b'VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK', b'mtl_metalfx_scale_hint', b'dm_icloud_supported', b'icloud'):
            data = b'\0' + name + b'\0'; self.assertTrue(self.pa.is_symbol(data, 1, 1 + len(name)), name)


if __name__ == '__main__':
    unittest.main()
