"""功耗基准脚本端到端（复审 F01 / F02 / F07，2026-10-27）：用假 xcrun / sleep 驱动完整的 power_bench.sh，真实设备锁逻辑（临时锁目录），不碰真机。
End-to-end benchmark script tests (re-audit F01 / F02 / F07, 2026-10-27): the full power_bench.sh driven by fake xcrun / sleep with the real lock logic (temporary
lock directory); no device is touched.
- F01：macOS 自带 Bash 3.2 在 C.UTF-8 / en_US.UTF-8 / C 下都能跑完，不因变量紧邻中文标点报 unbound variable
- F02：收尾只结束本次启动的进程；用户中途重开同一个 App（新进程号）时不结束它
- F07：拉回关键文件失败或报告生成失败时，整个脚本以非零退出，不打印“数据已拉回”的成功行"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

IOS = Path(__file__).resolve().parents[1]
BASH = '/bin/bash'
DEV, BUNDLE = 'FAKE-DEVICE-0001', 'org.example.benchtest'
APPDIR = '/private/var/containers/Bundle/Application/AAAA/DmIOS.app'

FAKE_XCRUN = r'''#!/usr/bin/env python3
import json, os, sys
st_path = os.environ['FAKE_STATE']; st = json.load(open(st_path))
a = sys.argv[1:]
open(os.environ['FAKE_CALLS'], 'a').write(' '.join(a) + '\n')
def save(): json.dump(st, open(st_path, 'w'))
def out_json(result):
    doc = {'info': {'outcome': 'success'}, 'result': result}
    if '--json-output' in a:
        dst = a[a.index('--json-output') + 1]
        if dst == '-': print(json.dumps(doc))
        else: json.dump(doc, open(dst, 'w'))
    sys.exit(0)
def proc(pid): return {'processIdentifier': pid, 'executable': 'file://' + st['appdir'] + '/DmIOS'}
if a[:3] == ['devicectl', 'list', 'devices']:
    print('Name   Hostname   Identifier   State'); print('iPhone x.local ' + st['device'] + ' available'); sys.exit(0)
if a[:2] != ['devicectl', 'device']: sys.exit(2)
cmd = a[2:4]
if cmd == ['info', 'apps']:
    out_json({'apps': [{'bundleIdentifier': st['bundle'], 'url': 'file://' + st['appdir'] + '/', 'name': 'D2R'}]})
if cmd == ['info', 'processes']:
    pids = st['after'] if st.get('launched') else st['before']
    out_json({'runningProcesses': [proc(p) for p in pids]})
if cmd == ['info', 'files']:
    out_json({'files': [{'name': 'save.test', 'resources': {'isDirectory': True}}]})
if cmd == ['process', 'launch']:
    st['launched'] = True; save()
    out_json({'process': {'processIdentifier': st['launch_pid']}})
if cmd == ['process', 'terminate']:
    sys.exit(0)
if cmd == ['copy', 'from']:
    src = a[a.index('--source') + 1]; dst = a[a.index('--destination') + 1]; name = os.path.basename(src)
    if name in st.get('copy_fail', []): sys.exit(1)
    if name == 'heat.tsv':
        rows = ['\t'.join(['h'] * 14)]
        for i in range(30):
            lv = 1 if i < 13 else 3
            heat = 'x' if st.get('heat_garbage') else str(i * 5)
            rows.append('\t'.join([heat, '1', '100', '60', '10', '11', '300', '0.5', '120', '60', '未充电', '0', str(lv), '2']))
        open(dst, 'w').write('\n'.join(rows) + '\n')
    elif name == 'boot.txt':
        open(dst, 'w').write('boot: 源码 test 工作区 clean\n')
    else:
        open(dst, 'w').write('x\n')
    sys.exit(0)
if cmd == ['copy', 'to']:
    sys.exit(0)
sys.exit(3)
'''

FAKE_SLEEP = '#!/bin/bash\n# 长等待瞬时返回；看门狗的短轮询照常短睡，免得空转 | long waits return at once; short watchdog polls still sleep briefly\ncase "$1" in [0-9]|[0-9].*) exec /bin/sleep 0.02;; *) exit 0;; esac\n'


class Bench(unittest.TestCase):
    def run_bench(self, locale='en_US.UTF-8', **scenario):
        tmp = Path(tempfile.mkdtemp(prefix='bench-e2e-')); self.addCleanup(shutil.rmtree, tmp, True)
        bin_ = tmp / 'bin'; bin_.mkdir()
        (bin_ / 'xcrun').write_text(FAKE_XCRUN); (bin_ / 'sleep').write_text(FAKE_SLEEP)
        os.chmod(bin_ / 'xcrun', 0o755); os.chmod(bin_ / 'sleep', 0o755)
        state = {'device': DEV, 'bundle': BUNDLE, 'appdir': APPDIR, 'before': [], 'after': [111], 'launch_pid': 111}
        state.update(scenario)
        (tmp / 'state.json').write_text(json.dumps(state))
        (tmp / 'local.env').write_text(f'DM_DEVICE={DEV}\nDM_BUNDLE_ID={BUNDLE}\n')
        env = {'PATH': f'{bin_}:/usr/bin:/bin:/usr/sbin:/sbin', 'HOME': str(tmp), 'LC_ALL': locale, 'LANG': locale,
               'FAKE_STATE': str(tmp / 'state.json'), 'FAKE_CALLS': str(tmp / 'calls.log'), 'DM_LOCAL_ENV': str(tmp / 'local.env'),
               'DM_ROOT': str(tmp / 'root'), 'DM_RUN_DIR': str(tmp / 'run'), 'DM_DEVLOCK_DIR': str(tmp / 'lock' / 'device.lock'),
               'DM_DEVLOCK_HIST': str(tmp / 'lock' / 'hist.tsv'), 'DM_DEVLOCK_WD_LOG': str(tmp / 'lock' / 'wd.log')}
        r = subprocess.run([BASH, str(IOS / 'tools' / 'power_bench.sh'), '1', '1,3'], env=env, capture_output=True, text=True, errors='replace', timeout=120)
        calls = (tmp / 'calls.log').read_text() if (tmp / 'calls.log').exists() else ''
        out = r.stdout + r.stderr
        runs = sorted((tmp / 'run' / 'bench').glob('*')) if (tmp / 'run' / 'bench').exists() else []
        return r.returncode, out, calls, runs, tmp

    def test_f01_locales_complete(self):
        for loc in ('C.UTF-8', 'en_US.UTF-8', 'C'):
            with self.subTest(locale=loc):
                rc, out, calls, runs, _ = self.run_bench(locale=loc)
                self.assertNotIn('unbound variable', out)
                self.assertEqual(rc, 0, out[-1500:])
                self.assertTrue(runs and (runs[-1] / 'report.md').exists() and (runs[-1] / 'report.md').stat().st_size > 0, out[-800:])

    def test_f02_terminates_only_the_launched_instance(self):
        rc, out, calls, _, _ = self.run_bench(after=[111])
        self.assertEqual(rc, 0, out[-1500:]); self.assertIn('terminate', calls); self.assertIn('--pid 111', calls)

    def test_f02_user_relaunch_is_not_terminated(self):
        rc, out, calls, _, _ = self.run_bench(after=[999])
        self.assertNotIn('--pid 999', calls)
        self.assertNotIn('process terminate', calls)
        self.assertIn('999', out)   # 报告里说明跳过了谁 | the log says which process was left alone

    def test_f02_launch_never_terminates_existing(self):
        _, _, calls, _, _ = self.run_bench()
        launch = [l for l in calls.splitlines() if 'process launch' in l]
        self.assertTrue(launch); self.assertNotIn('--terminate-existing', launch[0])

    def test_f07_pull_failure_fails_the_run(self):
        rc, out, _, _, _ = self.run_bench(copy_fail=['heat.tsv'])
        self.assertNotEqual(rc, 0); self.assertNotIn('数据已拉回', out); self.assertIn('关键数据没拉全', out)

    def test_f07_report_failure_fails_the_run(self):
        rc, out, _, _, _ = self.run_bench(heat_garbage=True)
        self.assertNotEqual(rc, 0, out[-1500:]); self.assertIn('报告生成失败', out)


class ShellVarBoundary(unittest.TestCase):
    """F01 的同类问题：变量名后紧跟非 ASCII 字符一律要写 ${名}（Bash 3.2 在 UTF-8 区域会把字节并进变量名）| same class as F01: a variable followed by a non-ASCII char must be braced"""
    def test_no_unbraced_variable_before_non_ascii(self):
        import re
        pat = re.compile(r'\$([A-Za-z_][A-Za-z0-9_]*)(?=[^\x00-\x7f])')
        bad = []
        for f in sorted((IOS / 'tools').glob('*.sh')) + [IOS / 'run.sh']:
            for i, line in enumerate(f.read_text(encoding='utf-8').splitlines(), 1):
                code = re.split(r'(?:^|\s)#', line, maxsplit=1)[0]   # 去掉注释 | drop comments
                bad += [f'{f.name}:{i}: ${m.group(1)}' for m in pat.finditer(code)]
        self.assertEqual(bad, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
