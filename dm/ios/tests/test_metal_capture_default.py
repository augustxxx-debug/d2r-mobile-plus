"""Metal 抓帧层缺省不带（光追内存 P1，2026-10-28）：真机 a8161cb 的 dev 包因 run.sh 缺省带 MetalCaptureEnabled，关着光追也每 300 帧常驻涨约 190 MB，长测十几秒被系统按内存结束。
run.sh 的抓帧开关块：不设 DM_METAL_CAPTURE → NO；DM_METAL_CAPTURE=1 → YES（抓帧包，打一行警告）；DM_GEN_ONLY=1（正式包归档）→ 一律 NO；别的值 → NO。
project.yml 的「Metal 抓帧开关」构建步骤：只有 DM_METAL_CAPTURE = YES 才写 MetalCaptureEnabled，其余删掉（Darwin 上用 PlistBuddy 真跑一遍）。
Metal capture layer off by default (ray-tracing memory P1, 2026-10-28): the a8161cb dev package on the device carried MetalCaptureEnabled because run.sh added it by default; even with ray tracing off resident memory grew about 190 MB per 300 frames, and the long test was ended by the system for memory after a dozen seconds.
The capture-switch block of run.sh: DM_METAL_CAPTURE unset → NO; DM_METAL_CAPTURE=1 → YES (capture package, one warning line); DM_GEN_ONLY=1 (release archive) → always NO; any other value → NO.
The "Metal capture switch" build step of project.yml writes MetalCaptureEnabled only for DM_METAL_CAPTURE = YES and removes it otherwise (run for real with PlistBuddy on Darwin)."""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

IOS = Path(__file__).resolve().parents[1]
RUN = (IOS / 'run.sh').read_text(encoding='utf-8')
YML = (IOS / 'project.yml').read_text(encoding='utf-8')


def mcap_block():
    i = RUN.index('\n  MCAP=NO') + 1
    j = RUN.index('\n  BINPUTS=', i)
    return RUN[i:j]


def mcap(env):
    script = 't(){ echo "T $*"; }\n' + mcap_block() + '\necho "MCAP=$MCAP"\n'
    e = {k: v for k, v in os.environ.items() if k not in ('DM_METAL_CAPTURE', 'DM_GEN_ONLY')}; e.update(env)
    out = subprocess.run(['bash', '-u', '-c', script], env=e, capture_output=True, text=True, check=True).stdout
    return re.search(r'MCAP=(\w+)', out).group(1), 'T ' in out


class RunSh(unittest.TestCase):
    def test_default_no(self):
        self.assertEqual(mcap({}), ('NO', False))

    def test_explicit_yes_warns(self):
        self.assertEqual(mcap({'DM_METAL_CAPTURE': '1'}), ('YES', True))

    def test_release_archive_never(self):
        self.assertEqual(mcap({'DM_METAL_CAPTURE': '1', 'DM_GEN_ONLY': '1'}), ('NO', False))
        self.assertEqual(mcap({'DM_GEN_ONLY': '1'}), ('NO', False))

    def test_other_values_no(self):
        for v in ('YES', '0', 'true', ''):
            self.assertEqual(mcap({'DM_METAL_CAPTURE': v}), ('NO', False), v)

    def test_identity_carries_flag(self):
        self.assertIn('capture=${MCAP}', RUN)
        self.assertIn("printf 'DM_METAL_CAPTURE = %s\\n' \"$MCAP\"", RUN)


@unittest.skipUnless(sys.platform == 'darwin' and os.path.exists('/usr/libexec/PlistBuddy'), 'PlistBuddy only on macOS')
class ProjectYml(unittest.TestCase):
    def step(self):
        i = YML.index('- name: Metal 抓帧开关'); i = YML.index('script: |', i)
        lines = []
        for ln in YML[i:].splitlines()[1:]:
            if not ln.startswith('          '):
                break
            lines.append(ln[10:])
        return '\n'.join(lines)

    def run_step(self, val):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'Info.plist'
            p.write_text('<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>MetalCaptureEnabled</key><true/></dict></plist>\n')
            env = dict(os.environ, TARGET_BUILD_DIR=d, INFOPLIST_PATH='Info.plist', DM_METAL_CAPTURE=val)
            subprocess.run(['bash', '-c', self.step()], env=env, check=True, capture_output=True)
            return 'MetalCaptureEnabled' in p.read_text()

    def test_only_yes_writes_key(self):
        self.assertTrue(self.run_step('YES'))
        self.assertFalse(self.run_step('NO'))
        self.assertFalse(self.run_step(''))


if __name__ == '__main__':
    r = unittest.main(exit=False, verbosity=1).result
    sys.exit(1 if (not r.testsRun or r.failures or r.errors) else 0)
