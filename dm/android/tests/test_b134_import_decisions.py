"""B134 返工：启动前「启动 / 导入 / 生成」判定的测试（P0-1、P1-1、P1-2、P2-1、P2-4）。
B134 rework: tests for the pre-start "boot / import / generate" decision (P0-1, P1-1, P1-2, P2-1, P2-4).

主测试是 Java 单测 StartupDecisionTest.java（run_startup_decision_test.sh）：直接执行 StartupDecision 的真实代码。
本文件保留两份补充：
- 用 Python 按同一套规则建临时目录跑行为用例（没有 JDK 的机器上也能挡回退）；
- 源码契约：两边的规则表（六件程序、判定顺序）必须相同，另外钉住 DmActivity / PackGenerator 里无法 javac 单测的行。
The primary test is the Java unit test StartupDecisionTest.java (run_startup_decision_test.sh), which executes the real StartupDecision code.
This file keeps two supplements:
- a Python port of the same rules builds temporary directories and runs the states (catches regressions on machines without a JDK);
- source contracts: the rule table (six program files, decision order) must be identical on both sides, and the lines in DmActivity / PackGenerator that
  cannot be unit-tested with plain javac are pinned.
"""
import inspect
import os
import re
import tempfile
import unittest
from pathlib import Path

ANDROID = Path(__file__).resolve().parents[1]                       # dm/android
DECISION = ANDROID / 'app/src/main/java/com/xiaoboluo/d2r/StartupDecision.java'
ACTIVITY = ANDROID / 'app/src/main/java/com/xiaoboluo/d2r/DmActivity.java'

MODULES = ['main.patched.bin', 'main.fixups.tsv', 'main.gates.tsv',
           'sdk.patched.bin', 'sdk.fixups.tsv', 'sdk.gates.tsv']


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def method_body(code, signature):
    start = code.index(signature)
    open_brace = code.index('{', start)
    depth = 0
    for i in range(open_brace, len(code)):
        if code[i] == '{':
            depth += 1
        elif code[i] == '}':
            depth -= 1
            if depth == 0:
                return code[open_brace:i + 1]
    raise AssertionError('unbalanced braces after ' + signature)


# ---- Python 侧的同一套判定（行为用例用；与 StartupDecision.java 一一对应） | the same decision in Python (for the behavioural cases; one-to-one with StartupDecision.java) ----
def read_text(path):
    try:
        return Path(path).read_bytes().decode('utf-8')
    except (OSError, UnicodeDecodeError):
        return None


def pack_product(info):
    if info is not None:
        for line in info.split('\n'):
            if line.startswith('PRODUCT='):
                return line[8:].strip()
    return None


def install_pending(runtime):
    return (Path(runtime) / 'install.pending').exists() or (Path(runtime) / 'import.part').exists()


def content_ready(runtime, product):
    if runtime is None:
        return False
    runtime = Path(runtime)
    if not runtime.is_dir() or install_pending(runtime):
        return False
    out = runtime / 'out'
    for module in MODULES:
        p = out / module
        if not p.is_file() or not os.access(str(p), os.R_OK):
            return False
    names = [p.name for p in (runtime / 'shaders').iterdir()] if (runtime / 'shaders').is_dir() else []
    if not any(n.endswith('.xgs') for n in names):
        return False
    if not (runtime / 'overlay').is_dir():
        return False
    derived = runtime / 'derived.tsv'
    if not derived.is_file() or not os.access(str(derived), os.R_OK):
        return False
    info = read_text(runtime / 'pack_info.txt')
    return info is not None and product is not None and pack_product(info) == product


def decide(source, runtime, ext_runtime, product, generating=False):
    if generating:
        return 'GENERATE'
    want = read_text(Path(source) / 'pack_info.txt') if source is not None else None
    have = read_text(Path(runtime) / 'pack_info.txt') if runtime is not None else None
    if want is not None and want != have and product is not None and product == pack_product(want):
        return 'IMPORT'
    if runtime is not None and install_pending(runtime):
        # P2-1：只有 import.part 残留、没有 install.pending，且下载目录还有本版配套包 → 重走导入（importPack 开头清 part）
        # P2-1: only import.part is left with no install.pending, and the download folder still has this build's pack -> redo the import (importPack clears part)
        part_only = (not (Path(runtime) / 'install.pending').exists()) and (Path(runtime) / 'import.part').exists()
        if part_only and want is not None and product is not None and product == pack_product(want):
            return 'IMPORT'
        return 'GENERATE'
    internal = content_ready(runtime, product)
    external = (runtime is not None
                and not (Path(runtime) / 'out/main.patched.bin').is_file()
                and content_ready(ext_runtime, product))
    return 'BOOT' if internal or external else 'GENERATE'


def write(path, text='x'):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


def make_runtime(root, product='3.3.94012-r44', modules=MODULES, xgs=True, overlay=True,
                 derived=True, record=True, record_product=None, pending=False, part=False):
    root = Path(root)
    for module in modules:
        write(root / 'out' / module)
    if xgs:
        write(root / 'shaders' / 'a.xgs')
    if overlay:
        (root / 'overlay').mkdir(parents=True, exist_ok=True)
    if derived:
        write(root / 'derived.tsv', 'table\n')
    if record:
        write(root / 'pack_info.txt', 'PRODUCT=%s\n' % (record_product or product))
    if pending:
        write(root / 'install.pending', '1')
    if part:
        write(root / 'import.part/pack_info.txt', 'PRODUCT=x\n')
    return root


def make_source(root, product, extra=''):
    root = Path(root)
    write(root / 'pack_info.txt', 'PRODUCT=%s\nMADE=old\n%s' % (product, extra))
    return root


class Behaviour(unittest.TestCase):
    """临时目录里的各种状态 → 启动 / 导入 / 生成 | every state in temporary directories -> boot / import / generate."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.product = '3.3.94012-r44'

    def tearDown(self):
        self.temp.cleanup()

    def runtime(self, name='files/dm', **kw):
        return make_runtime(self.root / name, product=self.product, **kw)

    def source(self, product):
        return make_source(self.root / 'Download/S2X/D2R Mobile/android', product)

    # P0-1：下载目录留着不配套的旧电脑导入包，运行目录已是本版生成的内容 → 必须启动，不得再生成
    # P0-1: an incompatible old computer pack in the download folder, runtime already holds this build's content -> boot, never generate
    def test_p0_1_old_pack_is_ignored_when_runtime_is_ready(self):
        runtime = self.runtime()
        self.assertEqual('BOOT', decide(self.source('3.3.94012-r43'), runtime, None, self.product))
        self.assertEqual('BOOT', decide(self.source('stale-overlay'), runtime, None, self.product))
        # 反复判定结果一样：不会因为生成过一轮就换答案 | repeated decisions stay the same: no loop after a generation round
        self.assertEqual('BOOT', decide(self.source('3.3.94012-r43'), runtime, None, self.product))

    def test_p0_1_old_pack_with_empty_runtime_generates_once(self):
        self.assertEqual('GENERATE', decide(self.source('3.3.94012-r43'), self.runtime(modules=[], xgs=False, overlay=False, derived=False, record=False), None, self.product))

    # 与本版配套、和已导入不同的电脑导入包：仍然走导入 | a computer pack for this build that differs from the imported one: still imported
    def test_matching_import_pack_is_imported(self):
        self.assertEqual('IMPORT', decide(self.source(self.product), self.runtime(), None, self.product))

    # P1-2 / M3：六件程序缺任何一件都不能启动 | M3: missing any of the six program files must not boot
    def test_missing_each_module_generates(self):
        for missing in MODULES:
            with self.subTest(missing=missing):
                modules = [m for m in MODULES if m != missing]
                self.assertEqual('GENERATE', decide(None, self.runtime(name='files/dm-' + missing, modules=modules), None, self.product))

    # M5：没有 .xgs 不能启动 | M5: no .xgs bundle must not boot
    def test_missing_shader_bundle_generates(self):
        self.assertEqual('GENERATE', decide(None, self.runtime(xgs=False), None, self.product))

    def test_missing_overlay_generates(self):
        self.assertEqual('GENERATE', decide(None, self.runtime(overlay=False), None, self.product))

    # P1-1：缺 derived.tsv 是半份内容，不能直接启动 | P1-1: missing derived.tsv is half a pack, must not boot
    def test_missing_derived_generates(self):
        self.assertEqual('GENERATE', decide(None, self.runtime(derived=False), None, self.product))

    # P1-1：缺版本记录 / 版本不对，不能当成齐全 | P1-1: no version record / wrong version is not complete
    def test_missing_or_wrong_version_record_generates(self):
        self.assertEqual('GENERATE', decide(None, self.runtime(record=False), None, self.product))
        self.assertEqual('GENERATE', decide(None, self.runtime(record_product='3.3.94012-r43'), None, self.product))

    # P1-1（四审 A4-04）：换上中途被杀，install.pending 或 import.part 残留 → 重新生成
    # P1-1 (fourth audit A4-04): killed during a swap, install.pending or import.part left over -> generate again
    def test_unfinished_install_generates(self):
        self.assertEqual('GENERATE', decide(None, self.runtime(pending=True), None, self.product))
        self.assertEqual('GENERATE', decide(None, self.runtime(part=True), None, self.product))

    # P2-1：导入在删 import.part 之前被杀（没有 install.pending），下载目录还有本版配套包 → 重走导入即可恢复，不卡在「不含生成器」页
    # P2-1: the import was killed before deleting import.part (no install.pending) and the download folder still has this build's pack -> redo the import
    def test_import_killed_before_part_cleanup_recovers(self):
        source = self.source(self.product)
        runtime = self.runtime(name='files/dm-part')
        # importPack 拷过去的就是源包这份 pack_info.txt，所以 have == want | the import copies the source pack_info.txt verbatim, so have == want
        write(runtime / 'pack_info.txt', 'PRODUCT=%s\nMADE=old\n' % self.product)
        write(runtime / 'import.part/pack_info.txt', 'PRODUCT=%s\n' % self.product)
        self.assertEqual('IMPORT', decide(source, runtime, None, self.product))
        # 有 install.pending（生成换上的标记）时 importPack 清不掉它 → 只能重新生成 | with install.pending importPack would leave it behind -> generate
        both = self.runtime(name='files/dm-part-pending')
        write(both / 'pack_info.txt', 'PRODUCT=%s\nMADE=old\n' % self.product)
        write(both / 'import.part/x', 'x')
        write(both / 'install.pending', '1')
        self.assertEqual('GENERATE', decide(source, both, None, self.product))
        # 没有配套导入包就照旧生成 | without a matching import pack, generate as before
        none = self.runtime(name='files/dm-part-none')
        write(none / 'import.part/x', 'x')
        self.assertEqual('GENERATE', decide(None, none, None, self.product))
        old = make_source(self.root / 'Download/S2X/D2R Mobile/old-android', '3.3.94012-r43')
        self.assertEqual('GENERATE', decide(old, none, None, self.product))

    # 内部没有主程序时看外部私有目录；内部有主程序但不全时不能被外部盖过去
    # External private storage counts when internal has no main module; an incomplete internal with main must not be masked by external
    def test_external_runtime_rule(self):
        external = make_runtime(self.root / 'ext/dm', product=self.product)
        internal_empty = make_runtime(self.root / 'files/dm', modules=[], xgs=False, overlay=False, derived=False, record=False)
        self.assertEqual('BOOT', decide(None, internal_empty, external, self.product))
        internal_main = make_runtime(self.root / 'files2/dm', modules=['main.patched.bin'], xgs=False, overlay=False, derived=False, record=False)
        self.assertEqual('GENERATE', decide(None, internal_main, external, self.product))
        # 内部目录上次换上没做完时，不能拿外部旧内容顶上 | when the last swap into internal is unfinished, old external content must not stand in
        internal_pending = make_runtime(self.root / 'files3/dm', modules=[], xgs=False, overlay=False, derived=False, record=False, pending=True)
        self.assertEqual('GENERATE', decide(None, internal_pending, external, self.product))

    def test_generating_always_keeps_the_loading_page(self):
        self.assertEqual('GENERATE', decide(None, self.runtime(), None, self.product, generating=True))


class SourceContract(unittest.TestCase):
    """钉住 Java 里真正做判定的行；行为主测是 Java 单测，这里的 Python 用例作无 JDK 时的补充。
    Pin the Java lines that really decide; the Java unit test is the primary behaviour test and the Python cases here supplement it without a JDK."""

    # 判定顺序（两边必须一致）：生成中 → 读 want/have → 配套导入 → 换到一半 → 内部齐全 → 外部齐全 → 启动/生成
    # Decision order (must match on both sides): generating -> want/have -> matching import -> unfinished swap -> internal ready -> external ready -> boot/generate
    JAVA_ORDER = [
        'if (generating) return Action.GENERATE;',
        'String want =',
        'if (want != null && !want.equals(have)',
        'PackGenerator.installPending(runtime)) {',
        'boolean internal = contentReady',
        'boolean external =',
        'return internal || external ? Action.BOOT',
    ]
    PYTHON_ORDER = [
        'if generating:',
        'want = read_text',
        'if want is not None and want != have',
        'if runtime is not None and install_pending(runtime):',
        'internal = content_ready(runtime, product)',
        'external = (runtime is not None',
        "return 'BOOT' if internal or external else 'GENERATE'",
    ]

    def setUp(self):
        self.decision = strip_comments(DECISION.read_text(encoding='utf-8'))
        self.activity = strip_comments(ACTIVITY.read_text(encoding='utf-8'))

    def test_module_list_matches(self):
        # Python 侧与 Java 侧的六件程序是同一份清单 | the six program files are the same list on the Python and Java sides
        declared = re.search(r'MODULES\s*=\s*\{([^}]*)\}', self.decision).group(1)
        self.assertEqual(MODULES, re.findall(r'"([^"]+)"', declared))

    def test_decision_order_matches_python(self):
        # 源码契约：Java 与 Python 同规则实现的判定顺序逐条相同；P2-1 的恢复分支两边都在换到一半的分支里
        # Source contract: the Java and Python implementations decide in the same order; the P2-1 recovery branch sits in the unfinished-swap branch on both sides
        java = method_body(self.decision, 'static Action decide(File source, File runtime, File extRuntime, String product, boolean generating)')
        py = inspect.getsource(decide)
        self.assertEqual(len(self.JAVA_ORDER), len(self.PYTHON_ORDER))
        for side, body, anchors in (('java', java, self.JAVA_ORDER), ('python', py, self.PYTHON_ORDER)):
            at = [body.index(a) for a in anchors]
            self.assertEqual(sorted(at), at, '%s decide order: %s' % (side, at))
        self.assertIn('importOnly', java)
        self.assertIn('part_only', py)

    def test_content_ready_checks_every_dimension(self):
        body = method_body(self.decision, 'static boolean contentReady(File runtime, String product)')
        self.assertIn('PackGenerator.installPending(runtime)', body)                 # P1-1 A4-04
        self.assertIn('if (!new File(out, m).canRead()) return false;', body)        # M3
        self.assertIn('if (n.endsWith(".xgs")) { bundle = true; break; }', body)     # M5
        self.assertIn('if (!bundle || !new File(runtime, "overlay").isDirectory()) return false;', body)
        self.assertIn('if (!new File(runtime, "derived.tsv").canRead()) return false;', body)   # P1-1
        self.assertIn('return info != null && product != null && product.equals(packProduct(info));', body)   # 版本记录 | version record

    # P0-1 / M4：不配套的旧包只当没有；运行目录不全时必须落到 GENERATE
    # P0-1 / M4: an incompatible old pack is merely absent; incomplete runtime content must fall through to GENERATE
    def test_decide_ignores_incompatible_pack_and_generates_when_not_ready(self):
        body = method_body(self.decision, 'static Action decide(File source, File runtime, File extRuntime, String product, boolean generating)')
        self.assertIn('if (want != null && !want.equals(have) && product != null && product.equals(packProduct(want))) return Action.IMPORT;', body)
        self.assertNotIn('!product.equals(packProduct(want))', body)                # 不配套不再单独生成 | incompatible no longer generates on its own
        self.assertNotIn('generate(', body)
        # P1-1：换上没做完就生成；P2-1：只有 import.part 残留、且有本版配套包时先重走导入 | P1-1: an unfinished swap generates;
        # P2-1: when only import.part is left and this build's pack is present, redo the import first
        self.assertIn('if (runtime != null && PackGenerator.installPending(runtime)) {', body)
        self.assertIn('boolean importOnly = !new File(runtime, PackGenerator.PENDING).exists() && new File(runtime, "import.part").exists();', body)
        self.assertIn('if (importOnly && want != null && product != null && product.equals(packProduct(want))) return Action.IMPORT;', body)
        self.assertIn('return Action.GENERATE;', body)
        self.assertIn('boolean internal = contentReady(runtime, product);', body)
        self.assertIn('!new File(runtime, "out/main.patched.bin").canRead() && contentReady(extRuntime, product)', body)
        self.assertIn('return internal || external ? Action.BOOT : Action.GENERATE;', body)   # M4

    # M6：-2 / -5 不进重开分支，-3 不许显示 dmAndroidMissingData；对应文案直接指路
    # M6: -2 / -5 do not relaunch, and -3 must not show dmAndroidMissingData; the corresponding texts point the way
    def test_missing_data_and_config_do_not_relaunch(self):
        missing = method_body(self.activity, 'static boolean contentMissing(int code)')
        self.assertIn('code == -3', missing)
        self.assertIn('code == -4', missing)
        self.assertIn('code == -9', missing)
        self.assertIn('code == -10', missing)
        self.assertNotIn('code == -2', missing)
        self.assertNotIn('code == -5', missing)
        start = method_body(self.activity, 'private void startHost()')
        self.assertIn('if (result == -2 || result == -5)', start)
        self.assertIn('result == -2 ? R.string.dmAndroidMissingData : R.string.dmAndroidMissingConfig', start)
        # 这两条文案只出现在 -2 / -5 的指路分支里：-3 等生成类返回码不许改用它
        # These two texts appear only in the -2 / -5 guidance branch: generation-fixable codes such as -3 must not use them
        self.assertEqual(1, start.count('R.string.dmAndroidMissingData'), start)
        self.assertEqual(1, start.count('R.string.dmAndroidMissingConfig'), start)

    # M7：content_regen_for 写的是本版版本号，先比后写，不会每次都重开
    # M7: content_regen_for stores this build's version code, compared before written, so it never relaunches every time
    def test_content_regen_marker_uses_version_code(self):
        relaunch = method_body(self.activity, 'private boolean relaunchForMissingContent()')
        self.assertIn('getLong("content_regen_for", 0) == BuildConfig.VERSION_CODE) return false', relaunch)
        self.assertIn('putLong("content_regen_for", BuildConfig.VERSION_CODE)', relaunch)
        self.assertLess(relaunch.index('getLong("content_regen_for"'), relaunch.index('putLong("content_regen_for"'))

    # P2-2：不带生成器的包在开头就分流，显示专门文案，不再说「重新打开会接着生成」
    # P2-2: a package without the generator branches at the top, shows the dedicated text and never says "reopen to continue"
    def test_without_generator_has_dedicated_failure(self):
        gen = method_body(self.activity, 'private void generate(File base, File runtime)')
        at = gen.index('if (!generatorAvailable())')
        branch = gen[at:gen.index('showLoadPage()', at)]
        self.assertIn('R.string.dmAndroidGenNoGenerator', branch)
        self.assertNotIn('dmAndroidGenInterrupted', branch)


if __name__ == '__main__':
    unittest.main()
