#!/usr/bin/env python3
"""B218：着色器翻译断点续做。生成中途被杀 / 切后台后下一轮：做完的不重译，只做剩下的；半截文件、清单外文件、大小或哈希不符的重做。
B218: shader translation resume. After a kill / backgrounding mid-way the next round never re-translates finished shaders and only does the rest;
half-written files, files not on the done list and size / hash mismatches are redone."""
import hashlib, os, pathlib, shutil, sys, tempfile, unittest
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'gfx' / 'tools'))
import dmsh_resume as R

KEYS = ['%016x' % i for i in range(200)]
class Killed(Exception): pass

def body(k): return hashlib.sha256(k.encode()).digest() * 8

class Resume(unittest.TestCase):
    def setUp(self):
        self.out = tempfile.mkdtemp(); self.addCleanup(shutil.rmtree, self.out); self.translated = []
    def round(self, kill_after=None):
        """一轮生成：先读清单，只做没做完的；kill_after = 翻译了这么多块后「被杀」| one generation round; kill_after = blocks before the "kill" """
        done = R.load_done(self.out); todo = [k for k in KEYS if k not in done]; log = R.DoneLog(self.out); blocks = []
        def translate(b):
            if kill_after is not None and len(blocks) >= kill_after: raise Killed()
            blocks.append(b); self.translated.extend(b); return {}
        def finish(k):
            R.write_output(self.out, k + '.xgs', body(k)); log.add(k, k + '.xgs', body(k)); return k, 'ok', ''
        try: return done, todo, R.run_chunks(todo, 16, 1, translate, finish)
        finally: log.close()
    def test_killed_round_resumes_without_retranslating(self):
        with self.assertRaises(Killed): self.round(kill_after=5)            # 5 块（80 个）后被杀 | killed after 5 blocks (80)
        first = list(self.translated); self.translated.clear()
        done, todo, res = self.round()
        self.assertEqual(len(done), 80); self.assertEqual(sorted(done), sorted(first))
        self.assertFalse(set(self.translated) & set(first), '做完的不许重译 | finished shaders must not be retranslated')
        self.assertEqual(len(self.translated), 120)                       # 只做剩下的 | only the rest
        self.assertEqual(len(R.load_done(self.out)), 200)
        for k in KEYS: self.assertEqual((pathlib.Path(self.out) / (k + '.xgs')).read_bytes(), body(k))
    def test_untrusted_files_are_redone(self):
        self.round(); self.translated.clear(); o = pathlib.Path(self.out)
        (o / (KEYS[0] + '.xgs')).write_bytes(body(KEYS[0])[:-1])         # 半截（大小不符）| truncated
        b = bytearray(body(KEYS[1])); b[0] ^= 1; (o / (KEYS[1] + '.xgs')).write_bytes(bytes(b))   # 同大小但内容变了 | same size, altered
        (o / (KEYS[2] + '.xgs')).unlink()                                # 清单有、文件没了 | listed but missing
        (o / 'ffffffffffffffff.xgs').write_bytes(b'stray')               # 清单外 | not on the list
        (o / ('.%s.xgs.part' % KEYS[3])).write_bytes(b'half')            # 被杀时的临时文件 | a temp file from the kill
        with open(o / R.DONE, 'a') as f: f.write('garbage line\n')
        done, todo, res = self.round()
        self.assertEqual(sorted(todo), sorted(KEYS[:3])); self.assertEqual(sorted(self.translated), sorted(KEYS[:3]))
        self.assertFalse((o / 'ffffffffffffffff.xgs').exists())
        self.assertEqual(len(R.load_done(self.out)), 200)
    def test_translation_errors_are_not_recorded(self):
        log = R.DoneLog(self.out)
        res = R.run_chunks(KEYS[:4], 2, 1, lambda b: {b[0]: 'bad'}, lambda k: (R.write_output(self.out, k + '.xgs', body(k)), log.add(k, k + '.xgs', body(k)), (k, 'ok', ''))[2])
        log.close()
        self.assertEqual([r[1] for r in res], ['翻译失败', 'ok', '翻译失败', 'ok'])
        self.assertEqual(sorted(R.load_done(self.out)), sorted([KEYS[1], KEYS[3]]))

class Helpers(unittest.TestCase):   # DeepSeek 变异 M4 / M5 补测 | added for audit mutations M4 / M5
    def test_pending_skips_done(self):
        self.assertEqual(R.pending(['a', 'b', 'c'], {'b': 'b.xgs'}), ['a', 'c'])
        self.assertEqual(R.pending(['a'], {}), ['a'])
    def test_commit_writes_before_recording(self):
        out = tempfile.mkdtemp(); self.addCleanup(shutil.rmtree, out); log = R.DoneLog(out)
        real = R.write_output
        def boom(*a): raise OSError('disk full')
        R.write_output = boom
        try:
            with self.assertRaises(OSError): R.commit(out, log, 'k1', 'k1.xgs', b'data')
        finally: R.write_output = real
        log.close()
        self.assertEqual(open(os.path.join(out, R.DONE)).read(), '')     # 没写成就不记 | not written, not recorded
        log = R.DoneLog(out); R.commit(out, log, 'k2', 'k2.xgs', b'data'); log.close()
        self.assertEqual(R.load_done(out), {'k2': 'k2.xgs'})

class Inputs(unittest.TestCase):   # B218 终审 P1（terra 指纹探针 / 复用探针）：输入一变，以前的成品必须作废 | final review P1: any input change invalidates earlier bundles
    def setUp(self):
        self.t = pathlib.Path(tempfile.mkdtemp()); self.addCleanup(shutil.rmtree, self.t)
        for n in ('vk.py', 'facts.py', 'xlate', 'main', 'opt', 'val'): (self.t / n).write_bytes(b'v1-' + n.encode())
        (self.t / 'xl').mkdir(); (self.t / 'xl' / 'a.dll').write_bytes(b'asm1'); (self.t / 'packs').mkdir(); (self.t / 'packs' / 'p.bin').write_bytes(b'AAAA')
    def fp(self, opts=None, files=None):
        t = self.t; return R.input_fingerprint('x', files or [str(t / n) for n in ('vk.py', 'facts.py', 'xlate', 'main', 'opt', 'val')], [str(t / 'xl')], str(t / 'packs'),
                                               opts or {'XLATE_REDUCED': '1', 'XLATE_DEBUG': '1'})
    def test_every_input_changes_the_fingerprint(self):
        base = self.fp(); self.assertEqual(base, self.fp())
        for what, path, data in [('同长度改包内容 | same-length pack change', 'packs/p.bin', b'BBBB'), ('facts 解析器 | facts parser', 'facts.py', b'v2-facts.py'),
                                 ('spirv-opt', 'opt', b'v2-opt'), ('spirv-val', 'val', b'v2-val'), ('翻译器目录里的程序集 | translator assembly', 'xl/a.dll', b'asm2')]:
            old = (self.t / path).read_bytes(); (self.t / path).write_bytes(data)
            self.assertNotEqual(self.fp(), base, what); (self.t / path).write_bytes(old); self.assertEqual(self.fp(), base)
        self.assertNotEqual(self.fp({'XLATE_REDUCED': '0', 'XLATE_DEBUG': '1'}), base, '翻译选项 | translation option')
        self.assertNotEqual(self.fp(files=[str(self.t / n) for n in ('vk.py', 'facts.py', 'xlate', 'main', 'opt', 'nope')]), base, '工具缺失 | missing tool')
    def test_check_inputs_wipes_on_mismatch(self):
        out = str(self.t / 'out'); self.assertFalse(R.check_inputs(out, 'a'))
        log = R.DoneLog(out); R.commit(out, log, 'k', 'k.xgs', b'old'); log.close(); open(os.path.join(out, 'keep.txt'), 'w').write('x')
        self.assertTrue(R.check_inputs(out, 'a')); self.assertEqual(R.load_done(out), {'k': 'k.xgs'})
        self.assertFalse(R.check_inputs(out, 'b')); self.assertFalse(os.path.exists(os.path.join(out, R.DONE)), '作废要删完成清单 | the done list goes too')   # 审计 M9
        self.assertEqual(R.load_done(out), {}); self.assertFalse(os.path.exists(os.path.join(out, 'k.xgs'))); self.assertTrue(os.path.exists(os.path.join(out, 'keep.txt')))
    def test_real_entry_invalidates_after_same_length_pack_change(self):
        """真入口（terra 复用探针）：真 dmsh_vk.py + 同一缓存目录；同长度改包后旧成品不许再被跳过 | real dmsh_vk.py: after a same-length pack change the old bundle must not be reused"""
        import subprocess
        gt = HERE.parent / 'gfx' / 'tools'; rows = [l.split('\t') for l in (gt.parent / 'nvn' / 'shader_map.tsv').read_text(encoding='utf-8').splitlines()]
        name, key = next((r[0], r[3]) for r in rows if len(r) > 4 and r[0].endswith('.bin'))
        packs, out = self.t / 'dmsh', self.t / 'cache'; (packs / name).parent.mkdir(parents=True, exist_ok=True); (packs / name).write_bytes(b'AAAA')
        def run():
            e = dict(os.environ, WORK=str(self.t), XLATE='/no/translator', MAIN=str(self.t / 'main'), PYTHONDONTWRITEBYTECODE='1')
            return subprocess.run([sys.executable, str(gt / 'dmsh_vk.py'), str(packs), str(out), key], capture_output=True, text=True, env=e).stdout
        run()                                                              # 第一次：记下输入指纹（翻译会失败，不要紧）| first round records the fingerprint
        log = R.DoneLog(str(out)); R.commit(str(out), log, key, key + '.xgs', b'prior bundle'); log.close()
        self.assertIn('已完成 1 个', run()); self.assertEqual((out / (key + '.xgs')).read_bytes(), b'prior bundle')   # 输入没变：续做 | unchanged: resumed
        (packs / name).write_bytes(b'BBBB')
        o = run(); self.assertIn('输入变了', o); self.assertIn('已完成 0 个', o); self.assertFalse((out / (key + '.xgs')).exists())
    def xlate_case(self, setup, change):
        """真 dmsh_vk + 同一缓存：setup 返回 (XLATE 环境值, PATH 前缀)；记下指纹、塞一个旧成品，change() 后必须作废 | real-entry translator identity case"""
        import subprocess
        gt = HERE.parent / 'gfx' / 'tools'; rows = [l.split('\t') for l in (gt.parent / 'nvn' / 'shader_map.tsv').read_text(encoding='utf-8').splitlines()]
        name, key = next((r[0], r[3]) for r in rows if len(r) > 4 and r[0].endswith('.bin'))
        packs, out = self.t / 'dmsh', self.t / 'cache'; (packs / name).parent.mkdir(parents=True, exist_ok=True); (packs / name).write_bytes(b'AAAA')
        xl, pre = setup()
        def run():
            e = dict(os.environ, WORK=str(self.t), XLATE=xl, MAIN=str(self.t / 'main'), PATH=pre + os.pathsep + os.environ.get('PATH', ''), PYTHONDONTWRITEBYTECODE='1')
            return subprocess.run([sys.executable, str(gt / 'dmsh_vk.py'), str(packs), str(out), key], capture_output=True, text=True, env=e).stdout
        run(); log = R.DoneLog(str(out)); R.commit(str(out), log, key, key + '.xgs', b'prior bundle'); log.close()
        self.assertIn('已完成 1 个', run())
        change(); o = run(); self.assertIn('输入变了', o); self.assertFalse((out / (key + '.xgs')).exists())
    def exe(self, p, body):
        p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(body); p.chmod(0o755); return p
    def test_translator_on_path_changes(self):   # terra 第二轮 XLATE 探针 | round-2 XLATE probe
        b = self.t / 'xbin'; x = self.exe(b / 'fake-xlate', b'#!/bin/sh\nexit 1\n')
        self.xlate_case(lambda: ('fake-xlate', str(b)), lambda: x.write_bytes(b'#!/bin/sh\nexit 2\n'))
    def test_dotnet_assembly_beside_path_launcher_changes(self):   # PATH 上的 .NET 启动器：旁边程序集换了 | .NET launcher on PATH, assembly changed
        b = self.t / 'pub'; self.exe(b / 'fake-xlate', b'#!/bin/sh\nexit 1\n'); (b / 'fake-xlate.dll').write_bytes(b'asm1')
        self.xlate_case(lambda: ('fake-xlate', str(b)), lambda: (b / 'fake-xlate.dll').write_bytes(b'asm2'))
    def test_translator_symlink_on_path(self):   # PATH 里是符号链接：按指向的文件与它所在的发布目录算 | symlink on PATH: the target and its publish directory count
        pub = self.t / 'pub2'; tgt = self.exe(pub / 'fake-xlate', b'#!/bin/sh\nexit 1\n'); (pub / 'fake-xlate.dll').write_bytes(b'asm1')
        b = self.t / 'lnk'; b.mkdir(); os.symlink(tgt, b / 'fake-xlate')
        self.xlate_case(lambda: ('fake-xlate', str(b)), lambda: (pub / 'fake-xlate.dll').write_bytes(b'asm2'))
    def test_translator_absolute_path_changes(self):   # 手机上的写法：绝对路径、单文件（NativeAOT）| the phone form: absolute path, single file
        x = self.exe(self.t / 'aot' / 'libdmgen_xlate.so', b'#!/bin/sh\nexit 1\n')
        self.xlate_case(lambda: (str(x), str(self.t)), lambda: x.write_bytes(b'#!/bin/sh\nexit 3\n'))
    def test_real_entry_tool_on_path_changes(self):
        """工具只给名字、从 PATH 找到时，按找到的那个文件的内容算（审计 M8）| a tool given by name is fingerprinted by the file found on PATH"""
        import subprocess
        gt = HERE.parent / 'gfx' / 'tools'; rows = [l.split('\t') for l in (gt.parent / 'nvn' / 'shader_map.tsv').read_text(encoding='utf-8').splitlines()]
        name, key = next((r[0], r[3]) for r in rows if len(r) > 4 and r[0].endswith('.bin'))
        packs, out, bindir = self.t / 'dmsh', self.t / 'cache', self.t / 'bin'; (packs / name).parent.mkdir(parents=True, exist_ok=True); (packs / name).write_bytes(b'AAAA')
        bindir.mkdir(); tool = bindir / 'dmsh-test-opt'; tool.write_bytes(b'#!/bin/sh\nexit 1\n'); tool.chmod(0o755)
        def run():
            e = dict(os.environ, WORK=str(self.t), XLATE='/no/translator', MAIN=str(self.t / 'main'), SPIRV_OPT='dmsh-test-opt',
                     PATH=str(bindir) + os.pathsep + os.environ.get('PATH', ''), PYTHONDONTWRITEBYTECODE='1')
            return subprocess.run([sys.executable, str(gt / 'dmsh_vk.py'), str(packs), str(out), key], capture_output=True, text=True, env=e).stdout
        run(); log = R.DoneLog(str(out)); R.commit(str(out), log, key, key + '.xgs', b'prior bundle'); log.close()
        self.assertIn('已完成 1 个', run())
        tool.write_bytes(b'#!/bin/sh\nexit 2\n')                              # 同名工具换了内容 | same name, new content
        self.assertIn('输入变了', run())

class Wiring(unittest.TestCase):   # 生成器接线：缓存目录跨轮保留、按指纹换目录，进度看缓存目录 | generator wiring
    def test_generator_uses_persistent_cache(self):
        src = (HERE / 'd2rm_android.py').read_text(encoding='utf-8')
        self.assertIn("progress('shaders', shc)", src); self.assertIn("if ln.startswith('着色器续做：'): say(ln.rstrip())", src)
        self.assertNotIn("'.done.tsv'), encoding='utf-8'))", src)   # 不再报清单原始行数 | no raw done-list count (round-2 P2)
        self.assertIn("run([PY, os.path.join(GT, 'dmsh_vk.py'), packs, shc]", src)
        self.assertIn("shutil.copytree(shc, sh, ignore=shutil.ignore_patterns('.*'))", src)
        vk = (HERE.parent / 'gfx' / 'tools' / 'dmsh_vk.py').read_text(encoding='utf-8')
        self.assertIn('dmsh_resume.load_done(out)', vk); self.assertIn('todo = dmsh_resume.pending(keys, done)', vk)
        self.assertIn('dmsh_resume.commit(out, log, key,', vk)
        self.assertNotIn("open(os.path.join(out, k2 + '.xgs'), 'wb').write(data)", vk)   # 不再直接写成品 | no direct writes
        self.assertLess(vk.index('dmsh_resume.check_inputs(out, FP)'), vk.index('dmsh_resume.load_done(out)'))   # 先核输入再读清单 | inputs first
        self.assertIn('**xopts}', src)                                        # 选项定下来传给 dmsh_vk | options passed explicitly
    def test_sources_cover_local_imports(self):
        import ast
        gt = HERE.parent / 'gfx' / 'tools'; tree = ast.parse((gt / 'dmsh_vk.py').read_text(encoding='utf-8'))
        mods = {a.name for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names}
        local = {m for m in mods if (gt / (m + '.py')).exists() or (gt / '../../verify' / (m + '.py')).exists()}
        self.assertEqual(local, {os.path.basename(s)[:-3] for s in R.SOURCES} - {'dmsh_vk'})
    def test_module_ships_with_generator(self):
        self.assertIn('dm/gfx/tools/dmsh_resume.py', (HERE.parent / 'android' / 'tools' / 'pack_gen_assets.py').read_text(encoding='utf-8'))

if __name__ == '__main__':
    unittest.main()
