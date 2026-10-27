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
            self.assertEqual(n, '2'); self.assertRegex(fp, r'^[0-9a-f]{12}$'); self.assertEqual(names, 'a.txt,sub/b.h')   # 仓库相对路径（五审 Q5-04） | repo-relative paths (fifth review Q5-04)
            Path(d, 'a.txt').write_text('3\n')
            self.assertNotEqual(run_in(d).split('|')[1].split(':')[1], fp)   # 内容变了指纹就变 | the fingerprint follows the content


class Wiring(unittest.TestCase):
    def test_identity_in_xcconfig_before_gen_only_exit(self):
        # 写进 Gen/dm.xcconfig 且在 DM_GEN_ONLY 提前退出之前：testflight / adhoc 归档也带身份 | written into Gen/dm.xcconfig before the DM_GEN_ONLY exit, so testflight / adhoc archives carry it
        w = RUN.index("printf 'DM_SOURCE_COMMIT = %s\\nDM_SOURCE_DIRTY = %s\\nDM_BUILD_INPUTS = %s\\n'")
        self.assertIn('>> Gen/dm.xcconfig', RUN[w:w + 200])
        self.assertLess(w, RUN.index('[ "${DM_GEN_ONLY:-}" = 1 ] && {'))
        self.assertNotIn('DM_SOURCE_COMMIT="$SC"', RUN)   # 只有一个来源 | single source of truth

    def test_build_inputs_cover_external_inputs_without_paths(self):
        line = next(l for l in RUN.splitlines() if l.strip().startswith('BINPUTS='))
        for key in ('chain=', 'font=', 'weight=', 'diag=', 'movie=', 'free=', 'm3=', 'family='):
            self.assertIn(key, line)
        self.assertNotIn('DM_UI_FONT}', line.replace('${DM_UI_FONT_WEIGHT', ''))   # 字体只记内容指纹，不记路径 | the font is recorded by content fingerprint only, never its path
        fp = next(l for l in RUN.splitlines() if l.strip().startswith('FONTFP='))
        self.assertIn('shasum -a 256 "$DM_UI_FONT"', fp)

    def test_info_plist_and_defaults(self):
        yml = (IOS / 'project.yml').read_text(encoding='utf-8')
        self.assertIn('DMSourceCommit: $(DM_SOURCE_COMMIT)', yml)
        self.assertIn('DMSourceDirty: $(DM_SOURCE_DIRTY)', yml)
        self.assertIn('DMBuildInputs: $(DM_BUILD_INPUTS)', yml)
        # target 层不得定义这三项：它们会盖住项目层 Gen/dm.xcconfig 里的真实值（2026-10-27 实测） | target settings must not define them: they would mask the real values in the project-level Gen/dm.xcconfig (seen 2026-10-27)
        for key in ('DM_SOURCE_COMMIT', 'DM_SOURCE_DIRTY', 'DM_BUILD_INPUTS'):
            self.assertNotRegex(yml, r'(?m)^\s+' + key + r':\s', key)

    def test_boot_log_prints_identity(self):
        main = (IOS / 'Sources' / 'main.m').read_text(encoding='utf-8')
        boot = main.index('plog("boot: 版本 %s 构建 %s')
        ident = main.index('plog("boot: 源码 %s 工作区 %s 构建输入 %s"')
        self.assertLess(boot, ident); self.assertLess(ident - boot, 800)   # 紧跟日志头 | right after the log header
        self.assertIn('idv(@"DMSourceCommit")', main[ident:ident + 400]); self.assertIn('idv(@"DMBuildInputs")', main[ident:ident + 400])


if __name__ == '__main__':
    unittest.main(verbosity=1)
