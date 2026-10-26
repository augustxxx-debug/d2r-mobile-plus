"""安装包的源码身份（审计 2026-10-27）：run.sh 把提交号与工作区差异指纹传给 xcodebuild，写进 Info.plist，启动日志首行打出。
Source identity of the installed app (audit 2026-10-27): run.sh passes the commit and a working-tree diff fingerprint to xcodebuild, they go into Info.plist,
and the first boot log line prints them."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

IOS = Path(__file__).resolve().parents[1]
RUN = (IOS / 'run.sh').read_text(encoding='utf-8')


def identity_snippet():
    """从 run.sh 取出计算 SC / SDIRTY 的那两行，原样执行 | Take the two lines that compute SC / SDIRTY from run.sh and run them verbatim"""
    lines = [l.strip() for l in RUN.splitlines() if re.match(r'\s*(SC=\$\(git rev-parse|if \[ -z "\$DF" \]; then SDIRTY=)', l)]
    assert len(lines) == 2, lines
    return '\n'.join(lines) + '\necho "$SC|$SDIRTY"\n'


def run_in(repo):
    return subprocess.run(['bash', '-c', identity_snippet()], cwd=repo, capture_output=True, text=True, check=True).stdout.strip()


class Fingerprint(unittest.TestCase):
    def test_clean_and_dirty(self):
        with tempfile.TemporaryDirectory() as d:
            g = lambda *a: subprocess.run(['git', *a], cwd=d, check=True, capture_output=True)
            g('init', '-q'); g('config', 'user.name', 't'); g('config', 'user.email', 't@example.invalid')
            Path(d, 'a.txt').write_text('1\n'); Path(d, 'sub').mkdir(); Path(d, 'sub', 'b.h').write_text('x\n')
            g('add', '.'); g('commit', '-qm', 'c')
            head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=d, capture_output=True, text=True).stdout.strip()
            self.assertEqual(run_in(d), head + '|clean')
            Path(d, 'sub', 'b.h').write_text('y\n'); Path(d, 'a.txt').write_text('2\n')
            sc, dirty = run_in(d).split('|')
            self.assertEqual(sc, head)
            n, fp, names = dirty.split(':')
            self.assertEqual(n, '2'); self.assertRegex(fp, r'^[0-9a-f]{12}$'); self.assertEqual(names, 'a.txt,b.h')
            Path(d, 'a.txt').write_text('3\n')
            self.assertNotEqual(run_in(d).split('|')[1].split(':')[1], fp)   # 内容变了指纹就变 | the fingerprint follows the content


class Wiring(unittest.TestCase):
    def test_xcodebuild_receives_identity(self):
        self.assertIn('xcodebuild -project DmIOS.xcodeproj', RUN)
        self.assertRegex(RUN, r'DM_BUILD_NUMBER=\$BN DM_SOURCE_COMMIT="\$SC" DM_SOURCE_DIRTY="\$SDIRTY" .* build >')   # xcodebuild 的续行 | continuation line of the xcodebuild command

    def test_info_plist_and_defaults(self):
        yml = (IOS / 'project.yml').read_text(encoding='utf-8')
        self.assertIn('DMSourceCommit: $(DM_SOURCE_COMMIT)', yml)
        self.assertIn('DMSourceDirty: $(DM_SOURCE_DIRTY)', yml)
        self.assertIn('DM_SOURCE_COMMIT: "unknown"', yml)

    def test_boot_log_prints_identity(self):
        main = (IOS / 'Sources' / 'main.m').read_text(encoding='utf-8')
        boot = main.index('plog("boot: 版本 %s 构建 %s')
        ident = main.index('plog("boot: 源码 %s 工作区 %s"')
        self.assertLess(boot, ident); self.assertLess(ident - boot, 800)   # 紧跟日志头 | right after the log header
        self.assertIn('inf[@"DMSourceCommit"]', main[ident:ident + 300])


if __name__ == '__main__':
    unittest.main(verbosity=1)
