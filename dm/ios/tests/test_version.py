"""版本号管理（2026-10-27）：唯一来源 dm/tools/version.sh——版本 = 最高的 vX.Y.Z 标签、构建号 = 提交时间戳、显示版本「1.0.0+28 · b346e40」（有未提交代码改动加 *）；
iOS（run.sh → Gen/dm.xcconfig → Info.plist）与 Mac（mac_app.sh）都从它取，选角页、启动日志、诊断显示同一行，uireg 收数按提交与 * 验包。
Version management (2026-10-27): single source dm/tools/version.sh -- version = highest vX.Y.Z tag, build number = commit timestamp, display version "1.0.0+28 · b346e40" (* with uncommitted
code changes); iOS (run.sh → Gen/dm.xcconfig → Info.plist) and Mac (mac_app.sh) both take it, character select, boot log and diagnostics show the same line, uireg checks the package by commit and *."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[2]
VERSION_SH = DM / 'tools' / 'version.sh'
RUN = (DM / 'ios' / 'run.sh').read_text(encoding='utf-8')
YML = (DM / 'ios' / 'project.yml').read_text(encoding='utf-8')


class Repo:
    def __init__(self, d):
        self.d = Path(d); self.n = 0
        self.git('init', '-q'); self.git('config', 'user.name', 't'); self.git('config', 'user.email', 't@example.invalid')
        (self.d / 'dm' / 'tools').mkdir(parents=True); shutil.copy(VERSION_SH, self.d / 'dm' / 'tools' / 'version.sh')
        for f in ('dm/gfx/nvn/dmsh_chain.h', 'dm/gfx/tools/dmsh_chain.lock', 'dm/gfx/nvn/c1_full.tsv', 'dm/host/a.c'):
            (self.d / f).parent.mkdir(parents=True, exist_ok=True); (self.d / f).write_text('0\n')
        self.commit()

    def git(self, *a, env=None):
        return subprocess.run(['git', *a], cwd=self.d, check=True, capture_output=True, text=True, env=env).stdout.strip()

    def commit(self):
        self.n += 1; (self.d / 'n.txt').write_text(str(self.n)); self.git('add', '-A')
        t = f'@{1793000000 + self.n * 100} +0000'
        import os
        env = dict(os.environ, GIT_COMMITTER_DATE=t, GIT_AUTHOR_DATE=t)
        self.git('commit', '-qm', f'c{self.n}', env=env)

    def version(self):
        out = subprocess.run(['bash', '-c', 'eval "$(bash dm/tools/version.sh)"; printf "%s|%s|%s|%s" "$DM_VERSION" "$DM_BUILD" "$DM_VERSION_LABEL" "$DM_VERSION_WARN"'],
                             cwd=self.d, check=True, capture_output=True, text=True).stdout
        return dict(zip(('ver', 'build', 'label', 'warn'), out.split('|')))

    def short(self):
        return self.git('rev-parse', '--short=7', 'HEAD')


class VersionScript(unittest.TestCase):
    def test_no_tag_warns_zero(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); v = r.version()
            self.assertEqual(v['ver'], '0.0.0'); self.assertIn('标签', v['warn']); self.assertEqual(v['label'], f'0.0.0 · {r.short()}')

    def test_on_tag_and_after(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); r.git('tag', 'v1.2.3'); v = r.version()
            self.assertEqual((v['ver'], v['label'], v['warn']), ('1.2.3', f'1.2.3 · {r.short()}', ''))
            r.commit(); r.commit(); v = r.version()
            self.assertEqual(v['ver'], '1.2.3'); self.assertEqual(v['label'], f'1.2.3+2 · {r.short()}')   # 版本号不变，标签后的提交数进显示版本 | version unchanged, commits since the tag go into the label

    def test_highest_reachable_semver_tag(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); r.git('tag', 'v1.9.0'); r.commit(); r.git('tag', 'v1.10.0'); r.git('tag', 'v2.0.0-rc1'); r.git('tag', 'build-7')
            r.git('checkout', '-qb', 'side'); r.commit(); r.git('tag', 'v3.0.0'); r.git('checkout', '-q', '-')   # 别的分支上的标签够不着 | a tag on another branch is unreachable
            self.assertEqual(r.version()['ver'], '1.10.0')

    def test_build_is_commit_timestamp_regardless_of_dirt(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); ct = r.git('log', '-1', '--format=%ct'); self.assertEqual(r.version()['build'], ct)
            (r.d / 'dm/host/a.c').write_text('dirty\n'); self.assertEqual(r.version()['build'], ct)   # 旧规则：脏 → 1 | old rule: dirty → 1
            (r.d / 'dm/host/a.c').write_text('0\n'); before = int(ct); r.commit(); self.assertGreater(int(r.version()['build']), before)

    def test_star_for_code_changes_only(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); r.git('tag', 'v1.0.0')
            for f in ('dm/gfx/nvn/dmsh_chain.h', 'dm/gfx/tools/dmsh_chain.lock', 'dm/gfx/nvn/c1_full.tsv'):
                (r.d / f).write_text('local\n')
            self.assertFalse(r.version()['label'].endswith('*'))   # 本机着色器链元数据不算 | local shader-chain metadata does not count
            (r.d / 'dm/host/a.c').write_text('dirty\n'); self.assertTrue(r.version()['label'].endswith('*'))
            (r.d / 'dm/host/new.c').write_text('x\n'); r.git('add', 'dm/host/new.c'); (r.d / 'dm/host/a.c').write_text('0\n')
            self.assertTrue(r.version()['label'].endswith('*'))   # 暂存的新文件也算 | a staged new file counts too

    def test_not_a_repo(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'tools').mkdir(); shutil.copy(VERSION_SH, Path(d) / 'tools' / 'version.sh')
            out = subprocess.run(['bash', '-c', 'eval "$(bash tools/version.sh)"; printf "%s|%s" "$DM_BUILD" "$DM_VERSION_WARN"'], cwd=d, capture_output=True, text=True,
                                 env={'PATH': '/usr/bin:/bin', 'GIT_CEILING_DIRECTORIES': d, 'HOME': d}).stdout
            build, warn = out.split('|'); self.assertEqual(build, '1'); self.assertIn('git', warn)


class Wiring(unittest.TestCase):
    def test_run_sh_writes_versions_before_gen_only_exit(self):
        w = RUN.index('eval "$(bash ../tools/version.sh)"')
        p = RUN.index("printf 'MARKETING_VERSION = %s\\nDM_BUILD_NUMBER = %s\\nCURRENT_PROJECT_VERSION = $(DM_BUILD_NUMBER)\\nDM_VERSION_LABEL = %s\\n'")
        self.assertLess(w, p); self.assertIn('>> Gen/dm.xcconfig', RUN[p:p + 200])
        self.assertLess(p, RUN.index('[ "${DM_GEN_ONLY:-}" = 1 ] && {'))   # 归档脚本也带上 | archive scripts get them too
        self.assertNotIn('DM_BUILD_NUMBER=$BN', RUN); self.assertNotRegex(RUN, r'(?m)^\s*BN=')   # 旧的「脏 → 1」规则不在了 | the old "dirty → 1" rule is gone

    def test_project_has_no_target_level_version_defaults(self):
        # target 层设置优先于项目层 Gen/dm.xcconfig，给了缺省值就会盖住 run.sh 写的真值（同 DM_SOURCE_*） | target settings beat the project-level xcconfig and would mask run.sh's values
        for key in ('MARKETING_VERSION', 'CURRENT_PROJECT_VERSION', 'DM_BUILD_NUMBER', 'DM_VERSION_LABEL'):
            self.assertNotRegex(YML, r'(?m)^\s+' + key + r':\s', key)
        self.assertIn('DMVersionLabel: $(DM_VERSION_LABEL)', YML)
        self.assertEqual(YML.count('CFBundleShortVersionString: $(MARKETING_VERSION)'), 3)

    def test_mac_app_uses_same_source(self):
        mac = (DM / 'tools' / 'mac_app.sh').read_text(encoding='utf-8')
        self.assertIn('eval "$(bash "$HERE/version.sh")"', mac); self.assertIn('<key>DMVersionLabel</key><string>$LABEL</string>', mac)
        self.assertNotIn('date +%m%d%H%M', mac)

    def test_screens_and_logs_show_label(self):
        upd = (DM / 'host' / 'dm_update.c').read_text(encoding='utf-8')
        self.assertIn('snprintf(t, sizeof t, dm_str(b, "dmUpdBuild"), g_label)', upd)
        self.assertIn('dm_str(b, "dmUpdNew"), ver, note)', upd); self.assertIn('dm_str(b, "dmUpdNewUrgent"), ver, note)', upd)
        tsv = {l.split('\t')[0]: l.split('\t') for l in (DM / 'i18n' / 'ui_text.tsv').read_text(encoding='utf-8').splitlines()}
        for key in ('dmUpdBuild', 'dmUpdNew', 'dmUpdNewUrgent'):
            for cell in tsv[key][1:7]:
                self.assertNotIn('%d', cell, key)
        main = (DM / 'ios' / 'Sources' / 'main.m').read_text(encoding='utf-8')
        self.assertIn('plog("boot: 显示版本 %s", idv(@"DMVersionLabel").UTF8String)', main)
        for src in ('ios/Sources/dm_update.m', 'host/app_mac.m'):
            self.assertIn('DMVersionLabel', (DM / src).read_text(encoding='utf-8'), src)

    def test_layout_measures_version_width(self):
        tl = (DM / 'ios' / 'tools' / 'touch_layout.py').read_text(encoding='utf-8')
        self.assertRegex(tl, r"_UPDATE = \(_upd_text\('TouchBuild', 79, 2046, 420, 30, \(150, 140, 115\), fit='\"fitToText\": true, '\)")
        self.assertIn("_upd_text('TouchBuild', 3161, 60, 600, 30", tl)
        upd = (DM / 'host' / 'dm_update.c').read_text(encoding='utf-8')
        self.assertIn('place(bw, xr - 600 - bx, 60)', upd); self.assertIn('tw = *(int32_t *)(bw + 0x78)', upd)


class UiregPackageCheck(unittest.TestCase):
    """tools/uireg/run_dev.sh 的验包段原样执行 | run the package check block of tools/uireg/run_dev.sh verbatim"""
    SRC = (DM / 'ios' / 'tools' / 'uireg' / 'run_dev.sh').read_text(encoding='utf-8')

    def check(self, repo, boot):
        s = self.SRC; block = s[s.index('EXP=$(git -C "$HERE"'):]; block = block[:block.index('\nfi\n') + 4]
        with tempfile.TemporaryDirectory() as o:
            Path(o, 'boot.txt').write_text(boot, encoding='utf-8')
            subprocess.run(['bash', '-c', block], cwd=o, check=True, env={'PATH': '/usr/bin:/bin', 'HERE': str(repo.d), 'O': o, 'HOME': o})
            return Path(o, 'boot_wrongpkg.txt').exists(), Path(o, 'boot.txt').read_text(encoding='utf-8')

    def boot(self, ct, sha, label):
        return f'[dm] boot: 版本 1.0.0 构建 {ct} 机型 iPhone00,0 系统 00.0 时间 x\n[dm] boot: 显示版本 {label}\n[dm] boot: 源码 {sha} 工作区 clean 构建输入 chain=x\n'

    def test_cases(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); ct = r.git('log', '-1', '--format=%ct'); sha = r.git('rev-parse', 'HEAD')
            self.assertFalse(self.check(r, self.boot(ct, sha, '1.0.0 · ' + sha[:7]))[0])
            bad, text = self.check(r, self.boot(ct, sha, '1.0.0 · ' + sha[:7] + '*'))
            self.assertTrue(bad); self.assertIn('带 *', text)
            bad, text = self.check(r, self.boot(ct, 'f' * 40, '1.0.0 · fffffff'))
            self.assertTrue(bad); self.assertIn('源码', text)
            bad, text = self.check(r, self.boot('1', sha, '1.0.0 · ' + sha[:7]))
            self.assertTrue(bad); self.assertIn('构建号 1', text)


if __name__ == '__main__':
    unittest.main(verbosity=1)
