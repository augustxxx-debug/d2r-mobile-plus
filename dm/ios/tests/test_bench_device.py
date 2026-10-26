"""功耗基准的设备互斥（审计 P1，2026-10-27）：进程按安装目录识别；脚本里所有设备访问都在设备锁之后，启动前锁内复查，不强制结束已在运行的进程。
Benchmark device mutual exclusion (audit P1, 2026-10-27): processes are matched by install directory; every device access in the script comes after the
device lock, the launch is re-checked inside the lock, and no running process is force-terminated."""
import re
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import bench_device as bd  # noqa: E402

APP = {'bundleIdentifier': 'org.example.d2r', 'url': 'file:///private/var/containers/Bundle/Application/AAAA/DmIOS.app/'}


class Identity(unittest.TestCase):
    def test_folder_normalized(self):
        self.assertEqual(bd.app_folder([APP], 'org.example.d2r'), '/var/containers/Bundle/Application/AAAA/DmIOS.app')

    def test_not_installed(self):
        self.assertIsNone(bd.app_folder([], 'org.example.d2r'))

    def test_ambiguous_or_wrong_bundle_refused(self):
        with self.assertRaises(bd.DeviceError):
            bd.app_folder([APP, APP], 'org.example.d2r')
        with self.assertRaises(bd.DeviceError):
            bd.app_folder([dict(APP, bundleIdentifier='org.other')], 'org.example.d2r')

    def test_only_target_process_matches(self):
        folder = bd.app_folder([APP], 'org.example.d2r')
        procs = [
            {'processIdentifier': 11, 'executable': 'file:///private/var/containers/Bundle/Application/BBBB/DmIOS.app/DmIOS'},   # 另一个移植，同名可执行 | another port, same executable name
            {'processIdentifier': 22, 'executable': 'file:///private/var/containers/Bundle/Application/AAAA/DmIOS.app/DmIOS'},
            {'processIdentifier': 33, 'executable': 'file:///private/var/containers/Bundle/Application/AAAA/DmIOS.appx/DmIOS'},  # 前缀相同但不是同一目录 | same prefix, different directory
        ]
        self.assertEqual(bd.target_pids(folder, procs), [22])
        self.assertEqual(bd.target_pids(folder, procs[:1]), [])

    def test_savetest_detection(self):
        self.assertTrue(bd.has_savetest([{'name': 'save.test', 'resources': {'isDirectory': True}}]))
        self.assertFalse(bd.has_savetest([{'name': 'save.test', 'resources': {'isDirectory': False}}, {'name': 'save', 'resources': {'isDirectory': True}}]))

    def test_device_error_is_not_absence(self):
        orig = bd.device_json
        def boom(*a):
            raise bd.DeviceError('devicectl 失败')
        bd.device_json = boom
        try:
            self.assertEqual(bd.main(['x', 'state', 'dev', 'org.example.d2r']), 2)
            self.assertEqual(bd.main(['x', 'pid', 'dev', 'org.example.d2r']), 2)
        finally:
            bd.device_json = orig


class Queries(unittest.TestCase):
    def test_mtime_picks_named_file(self):
        files = [{'name': 'thrcpu.tsv', 'resources': {'isDirectory': False}, 'metadata': {'lastModDate': 'A'}},
                 {'name': 'heat.tsv', 'resources': {'isDirectory': False}, 'metadata': {'lastModDate': '2026-10-27T10:00:05.000Z'}}]
        self.assertEqual(bd.file_mtime(files, 'heat.tsv'), '2026-10-27T10:00:05.000Z')
        self.assertIsNone(bd.file_mtime(files[:1], 'heat.tsv'))
        self.assertIsNone(bd.file_mtime([{'name': 'heat.tsv', 'resources': {'isDirectory': True}}], 'heat.tsv'))

    def test_ready_and_mtime_report_device_errors(self):
        orig = bd.device_json
        def boom(*a):
            raise bd.DeviceError('设备不可用')
        bd.device_json = boom
        try:
            self.assertEqual(bd.main(['x', 'ready', 'dev', 'org.example.d2r']), 2)
            self.assertEqual(bd.main(['x', 'mtime', 'dev', 'org.example.d2r', 'Documents/bench_x', 'heat.tsv']), 2)
        finally:
            bd.device_json = orig
        bd.device_json = lambda *a: {'apps': []}
        try:
            self.assertEqual(bd.main(['x', 'ready', 'dev', 'org.example.d2r']), 0)
        finally:
            bd.device_json = orig


class ScriptOrder(unittest.TestCase):
    src = (TOOLS / 'power_bench.sh').read_text(encoding='utf-8')
    code = '\n'.join(l for l in src.splitlines() if not l.lstrip().startswith('#'))

    def first(self, pattern):
        m = re.search(pattern, self.code)
        self.assertIsNotNone(m, pattern)
        return m.start()

    def test_lock_before_any_device_access(self):
        lock = self.first(r'\ndevlock_acquire ')
        for pat in (r'present \|\|', r'"\$\{BD\[@\]\}" state', r'device copy to', r'process launch'):
            self.assertLess(lock, self.first(pat), pat)
        # 函数定义里的 devicectl 不算访问；锁之前只能有定义 | devicectl inside function definitions is not an access; only definitions may precede the lock
        before = self.code[:lock]
        for line in before.splitlines():
            if 'devicectl' in line or 'bench_device' in line:
                self.assertTrue(re.match(r'\s*(present\(\)|BD=)', line), line)

    def test_no_forced_termination_of_running_app(self):
        self.assertNotIn('--terminate-existing', self.code)

    def test_recheck_inside_lock_right_before_launch(self):
        recheck = self.first(r'PID=\$\("\$\{BD\[@\]\}" pid')
        self.assertLess(self.first(r'\ndevlock_acquire '), recheck)
        self.assertLess(recheck, self.first(r'process launch'))
        self.assertIn('|| { t "🔴 读不到设备状态，不启动"; exit 1; }', self.code)

    def test_lock_limit_covers_reconnect_wait(self):
        self.assertIn('RECONNECT=${BENCH_RECONNECT_S:-43200}', self.code)
        self.assertIn('+ RECONNECT +', self.code[self.first(r'\ndevlock_acquire '):self.first(r'trap devlock_release')])

    def test_ready_is_a_real_query_not_the_device_list(self):
        # 二审 R02：设备“在清单里”不等于“连得上” | second review R02: listed is not reachable
        self.assertNotIn('list devices', self.code)
        self.assertIn('present(){ "${BD[@]}" ready', self.code)

    def test_ownership_window_matches_app_lifetime(self):
        # 二审：寿命窗口与 DM_WATCH 同一个值，不再多给 60 秒 | second review: the lifetime window is the same value as DM_WATCH, no extra 60 s
        self.assertIn('WATCH=$((TOTAL + 240))', self.code); self.assertIn('DEVICECTL_CHILD_DM_WATCH=$WATCH', self.code)
        self.assertNotIn('TOTAL + 300', self.code)
        tail = self.code[self.first(r'等设备重新接上'):]
        self.assertIn('-gt "$WATCH"', tail); self.assertIn('-le "$WATCH"', tail); self.assertIn('heat_mtime', tail)

    def test_end_only_terminates_target(self):
        tail = self.code[self.first(r'等设备重新接上'):]
        self.assertNotIn('DmIOS.app/DmIOS', tail)
        self.assertIn('"${BD[@]}" pids', tail)


if __name__ == '__main__':
    unittest.main(verbosity=1)
