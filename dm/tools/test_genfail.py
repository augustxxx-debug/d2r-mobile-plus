#!/usr/bin/env python3
# 生成失败日志（genfail.py、d2rm_android 的 files_check / after_failure，缺陷台账 B026）：嵌套回溯挑最里层、报告带足末尾行、四种游戏文件问题各说成一句话、
# 手机上的 @summary / @fail 标记。
# Generation failure logs (genfail.py, d2rm_android's files_check / after_failure, defect ledger B026): the innermost error out of nested tracebacks, enough tail lines
# in the report, each of four game-file problems stated as one sentence, the @summary / @fail markers on the phone.
import contextlib, hashlib, io, os, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import genfail


def put(f, body):
    with open(f, 'wb') as h: h.write(body)

NESTED = '\n'.join(['[12:00:01] 生成覆盖层', 'romfs_overlay: 前端布局 12 个'] + ['casc: 写 %d' % i for i in range(150)] + [
    'Traceback (most recent call last):',
    '  File "casc_overlay.py", line 88, in <module>',
    '    main()',
    '  File "casc_overlay.py", line 40, in read_index',
    "KeyError: 'data.005'",
    'Traceback (most recent call last):',
    '  File "gen_romfs_overlay.py", line 259, in emit',
    '    subprocess.run(cmd, check=True)',
    "subprocess.CalledProcessError: Command '['python3', 'run.py', 'casc_overlay.py']' returned non-zero exit status 1."])


class RootCauseTest(unittest.TestCase):
    def test_innermost_first_wrapper_last(self):
        c = genfail.root_causes(NESTED)
        self.assertEqual(c[0], "KeyError: 'data.005'")
        self.assertTrue(c[-1].startswith('subprocess.CalledProcessError'))

    def test_marks_and_plain_error_lines(self):
        c = genfail.root_causes('写到一半\n[x] 写覆盖层（casc_overlay.py）退出码 1，它的输出见上\nOSError: [Errno 28] No space left on device\n')
        self.assertEqual(c[0], '[x] 写覆盖层（casc_overlay.py）退出码 1，它的输出见上')
        self.assertIn('OSError: [Errno 28] No space left on device', c)

    def test_report_carries_at_least_80_tail_lines(self):
        r = genfail.fail_report('生成覆盖层', 1, NESTED, '/x/gen_overlay.log').splitlines()
        self.assertEqual(r[0], '生成覆盖层 没成功（退出码 1）')
        self.assertEqual(r[2], "  KeyError: 'data.005'")   # 根因在最前面 | the cause comes first
        a = next(i for i, l in enumerate(r) if l.startswith('----- 「生成覆盖层」输出的最后')); b = r.index('----- 以上 -----')
        self.assertGreaterEqual(b - a - 1, 80)
        self.assertIn('完整输出 /x/gen_overlay.log', r[a])

    def test_no_error_line_still_says_so(self):
        r = genfail.fail_report('x', 9, 'just\nnoise\n')
        self.assertIn('输出里没有找到报错行', r)


class FilesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.game = self.tmp.name; self.rows = []
        for p, body in (('ExeFS/main', b'M' * 64), ('ExeFS/sdk', b'S' * 32), ('ExeFS/subsdk0', b'O'), ('RomFS/data/.build.config', b'cfg'),
                        ('RomFS/data/05-00000001.data', b'D' * 4096), ('RomFS/data/build_manifest.psv', b'B' * 100)):
            f = os.path.join(self.game, p); os.makedirs(os.path.dirname(f), exist_ok=True); put(f, body)
            self.rows.append((p, len(body), hashlib.sha256(body).hexdigest()))

    def tearDown(self): self.tmp.cleanup()

    def test_all_good(self):
        r = genfail.check_files(self.game, self.rows)
        self.assertEqual(genfail.kind_of(r), ''); self.assertEqual(genfail.describe(r, 6), ['游戏文件 6 个都在，大小都对'])
        self.assertEqual(genfail.verify_hashes(self.game, self.rows)[0], [])

    def test_missing_build_config(self):
        os.remove(os.path.join(self.game, 'RomFS/data/.build.config'))
        r = genfail.check_files(self.game, self.rows)
        self.assertEqual(genfail.kind_of(r), 'files'); self.assertIn('缺 data/.build.config（隐藏文件，要一起拷）', genfail.describe(r, 6))

    def test_truncated_data(self):
        put(os.path.join(self.game, 'RomFS/data/05-00000001.data'), b'D' * 1000)
        r = genfail.check_files(self.game, self.rows)
        self.assertEqual(genfail.kind_of(r), 'files')
        self.assertIn('data/05-00000001.data 大小不对：应为 4096 字节，实为 1000 字节（可能没拷完）', genfail.describe(r, 6))

    def test_corrupt_same_size_found_by_hash(self):
        put(os.path.join(self.game, 'RomFS/data/05-00000001.data'), b'D' * 4095 + b'X')
        r = genfail.check_files(self.game, self.rows); self.assertEqual(genfail.kind_of(r), '')   # 大小对，开头查不出 | right size: the start cannot tell
        bad, n, total, secs = genfail.verify_hashes(self.game, self.rows, only=set(r['ok']))
        self.assertEqual(bad, ['RomFS/data/05-00000001.data']); self.assertEqual(n, 6)
        d = genfail.describe_hashes(bad, n, total, secs)
        self.assertIn('1 个内容不对', d[0]); self.assertIn('data/05-00000001.data 内容不对（可能拷坏了，重新拷一次）', d)

    def test_optional_missing_is_not_incomplete(self):
        os.remove(os.path.join(self.game, 'ExeFS/subsdk0'))
        r = genfail.check_files(self.game, self.rows); self.assertEqual(genfail.kind_of(r), '')
        self.assertIn('（ExeFS/subsdk0 没有也不影响）', genfail.describe(r, 6))

    def test_only_manifest_size_differs_is_version(self):
        put(os.path.join(self.game, 'RomFS/data/build_manifest.psv'), b'B' * 120)
        self.assertEqual(genfail.kind_of(genfail.check_files(self.game, self.rows)), 'version')

    def test_manifest_file_shipped_matches_49(self):
        rows = genfail.load_manifest(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'game_files_3.3.94012.tsv'))
        self.assertEqual(len(rows), 49); self.assertIn('RomFS/data/.build.config', [p for p, _, _ in rows])


class PhoneMarkersTest(unittest.TestCase):
    """d2rm_android 在手机上（DM_GEN_PROGRESS=1）打的 @summary / @fail | the @summary / @fail lines d2rm_android prints on the phone (DM_GEN_PROGRESS=1)"""
    def setUp(self):
        import d2rm_android; self.m = d2rm_android; self.m.GAME_CHECK.clear(); self.m.FAILED.clear()
        self.env = mock.patch.dict(os.environ, {'DM_GEN_PROGRESS': '1'}); self.env.start()

    def tearDown(self): self.env.stop()

    def lines(self, fn, *a):
        o = io.StringIO()
        with contextlib.redirect_stdout(o):
            try: fn(*a)
            except SystemExit: pass
        return o.getvalue().splitlines()

    def test_space(self):
        out = self.lines(self.m.after_failure, '[x] 翻译着色器 没成功（退出码 1）\n根因（最里层的报错）：\n  OSError: [Errno 28] No space left on device\n')
        self.assertIn('@summary 失败：翻译着色器 没成功（退出码 1）', out); self.assertIn('@fail space', out)
        self.assertTrue(any(l.startswith('@summary 剩余空间：') for l in out))

    def test_internal_when_files_fine(self):
        out = self.lines(self.m.after_failure, NESTED)
        self.assertIn("@summary 根因：KeyError: 'data.005'", out); self.assertEqual(out[-1], '@fail internal')

    def test_files_check_dies_with_sentence(self):
        with tempfile.TemporaryDirectory() as g:
            os.makedirs(os.path.join(g, 'ExeFS')); put(os.path.join(g, 'ExeFS', 'main'), b'x')
            o = io.StringIO()
            with contextlib.redirect_stdout(o), self.assertRaises(SystemExit) as e: self.m.files_check(g)
            out = o.getvalue().splitlines()
        self.assertIn('@fail files', out)
        self.assertTrue(str(e.exception.code).startswith('[x] 游戏文件不完整：'))
        self.assertIn('ExeFS/main 大小不对：应为 18099792 字节，实为 1 字节（可能没拷完）', str(e.exception.code))
        self.assertIn('@summary 游戏文件核对：缺 data/.build.config（隐藏文件，要一起拷）', out)
        self.m.after_failure(str(e.exception.code))   # 已定类：收尾不再改判 | already classified: the wrap-up does not reclassify
        self.assertEqual(self.m.FAILED, ['files'])


if __name__ == '__main__': unittest.main()
