"""版本号管理（2026-10-27）：唯一来源 dm/tools/version.sh——版本 = 最高的 vX.Y.Z 标签、构建号 = 沿提交历史严格递增的序号（通常就是提交时间戳，四审 Q4-01）、显示版本「1.0.0+28 · b346e40」（有未提交代码改动加 *）；
iOS（run.sh → Gen/dm.xcconfig → Info.plist）与 Mac（mac_app.sh）都从它取，选角页、启动日志、诊断显示同一行，uireg 收数按构建号、提交、显示版本与工作区验包（四审 Q4-02）。
Version management (2026-10-27): single source dm/tools/version.sh -- version = highest vX.Y.Z tag, build number = a sequence strictly increasing along the commit history (usually the commit timestamp, fourth review Q4-01), display version "1.0.0+28 · b346e40" (* with uncommitted
code changes); iOS (run.sh → Gen/dm.xcconfig → Info.plist) and Mac (mac_app.sh) both take it, character select, boot log and diagnostics show the same line, uireg checks the package by build, commit, display version and working tree (fourth review Q4-02)."""
import re
import shutil
import subprocess
import sys
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

    def commit(self, at=None):
        self.n += 1; (self.d / 'n.txt').write_text(str(self.n)); self.git('add', '-A')
        t = f'@{at if at is not None else 1793000000 + self.n * 100} +0000'
        import os
        env = dict(os.environ, GIT_COMMITTER_DATE=t, GIT_AUTHOR_DATE=t)
        self.git('commit', '-qm', f'c{self.n}', env=env)

    def version(self, rev='', **env):
        import os
        out = subprocess.run(['bash', '-c', 'eval "$(bash dm/tools/version.sh ' + rev + ')"; printf "%s|%s|%s|%s|%s|%s" "$DM_VERSION" "$DM_BUILD" "$DM_VERSION_LABEL" "$DM_VERSION_WARN" "$DM_BUILD_SOURCE" "$DM_VERSION_ERROR"'],
                             cwd=self.d, check=True, capture_output=True, text=True, env=dict(os.environ, **env)).stdout
        return dict(zip(('ver', 'build', 'label', 'warn', 'src', 'err'), out.split('|')))

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

    def test_same_second_and_clock_back_still_increase(self):
        # 四审 Q4-01：同一秒的父子提交、时钟回拨的提交，构建号也必须比所有祖先大，否则旧包判成「没有新版本」
        # Fourth review Q4-01: a child in the same second as its parent, or after a clock went back, must still be above every ancestor, or old packages see "no new version"
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); r.commit(at=1793001000); first = int(r.version()['build']); self.assertEqual(first, 1793001000)
            r.commit(at=1793001000); second = int(r.version()['build']); self.assertGreater(second, first)
            r.commit(at=1793000950); third = int(r.version()['build']); self.assertGreater(third, second)
            r.commit(at=1793009999); self.assertEqual(r.version()['build'], '1793009999')   # 时间正常往前走时就是提交时间戳 | equals the commit timestamp while time moves forward
            self.assertEqual(r.version()['src'], 'history')

    def test_merge_is_above_both_parents(self):
        with tempfile.TemporaryDirectory() as d:
            import os
            def at(t): return dict(os.environ, GIT_COMMITTER_DATE=f'@{t} +0000', GIT_AUTHOR_DATE=f'@{t} +0000')
            r = Repo(d); r.git('checkout', '-qb', 'side'); (r.d / 'side.txt').write_text('s\n'); r.git('add', '-A'); r.git('commit', '-qm', 's', env=at(1793005000))
            side = int(r.version()['build']); r.git('checkout', '-q', '-'); (r.d / 'main.txt').write_text('m\n'); r.git('add', '-A'); r.git('commit', '-qm', 'm', env=at(1793000300))
            r.git('merge', '-q', '--no-ff', '-m', 'merge', 'side', env=at(1793000301))
            self.assertGreater(int(r.version()['build']), side)

    def test_rev_argument(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); old = r.git('rev-parse', 'HEAD'); old_build = r.version()['build']; r.commit()
            (r.d / 'dm/host/a.c').write_text('dirty\n')
            v = r.version(old); self.assertEqual(v['build'], old_build); self.assertEqual(v['label'], '0.0.0 · ' + old[:7])   # 不看工作区 | the working tree is ignored

    def test_override_and_release_floor(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); b = int(r.version()['build'])
            v = r.version(DM_BUILD_NUMBER='1800000000'); self.assertEqual((v['build'], v['src'], v['err']), ('1800000000', 'override', ''))
            for bad in ('abc', '012', '-5'):
                self.assertIn('DM_BUILD_NUMBER', r.version(DM_BUILD_NUMBER=bad)['err'], bad)
            self.assertIn('不大于上次发布', r.version(DM_BUILD_FLOOR=str(b))['err'])
            self.assertIn('不大于上次发布', r.version(DM_BUILD_FLOOR=str(b + 10))['err'])
            self.assertEqual(r.version(DM_BUILD_FLOOR=str(b - 1))['err'], '')
            self.assertIn('不大于上次发布', r.version(DM_BUILD_NUMBER='5', DM_BUILD_FLOOR=str(b))['err'])   # 覆盖值同样过闸门 | an override goes through the floor too
            self.assertIn('不是整数', r.version(DM_BUILD_FLOOR='x')['err'])

    def test_shallow_clone_warns_about_history(self):
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as c:
            r = Repo(d); r.git('tag', 'v1.0.0'); r.commit(); r.commit()
            subprocess.run(['git', 'clone', '-q', '--depth', '1', 'file://' + d, c], check=True, capture_output=True)
            v = Repo.__new__(Repo); v.d = Path(c); out = v.version()
            self.assertEqual(out['ver'], '0.0.0'); self.assertIn('浅克隆缺历史', out['warn']); self.assertIn('构建号只按看得见的历史', out['warn'])

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
    def test_packaging_refuses_on_version_error(self):
        self.assertIn('[ -n "${DM_VERSION_ERROR:-}" ] && { t "🔴 ${DM_VERSION_ERROR}"; exit 1; }', RUN)
        mac = (DM / 'tools' / 'mac_app.sh').read_text(encoding='utf-8')
        self.assertIn('[ -n "$DM_VERSION_ERROR" ] && { echo "🔴 $DM_VERSION_ERROR"; exit 2; }', mac)
        self.assertIn('DM_BUILD_NUMBER=${DM_MAC_BUILD:-${DM_BUILD_NUMBER:-}} bash "$HERE/version.sh"', mac)

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
        self.assertIn('bash "$HERE/version.sh")"', mac); self.assertIn('<key>DMVersionLabel</key><string>$LABEL</string>', mac)
        self.assertNotIn('date +%m%d%H%M', mac)

    def test_screens_and_logs_show_label(self):
        upd = (DM / 'host' / 'dm_update.c').read_text(encoding='utf-8')
        self.assertIn('snprintf(t, sizeof t, dm_str(b, "dmUpdBuild"), g_label)', upd)
        self.assertIn('dm_str(b, "dmUpdNew"), ver)', upd); self.assertIn('dm_str(b, "dmUpdNewUrgent"), ver, note)', upd)
        tsv = {l.split('\t')[0]: l.split('\t') for l in (DM / 'i18n' / 'ui_text.tsv').read_text(encoding='utf-8').splitlines()}
        for key in ('dmUpdBuild', 'dmUpdNew', 'dmUpdNewUrgent'):
            for cell in tsv[key][1:7]:
                self.assertNotIn('%d', cell, key)
        main = (DM / 'ios' / 'Sources' / 'main.m').read_text(encoding='utf-8')
        self.assertIn('plog("boot: 显示版本 %s", idv(@"DMVersionLabel").UTF8String)', main)
        for src in ('ios/Sources/dm_update.m', 'host/app_mac.m'):
            self.assertIn('DMVersionLabel', (DM / src).read_text(encoding='utf-8'), src)

    def test_short_language_neutral_texts(self):
        # 2026-10-27 用户：多语言越简单越好，普通档只要一行新版本提示，不要说明和按钮（iPad 等窄屏会挤到「进入游戏」）
        # user, 2026-10-27: keep every language as short as possible; the ordinary tier is one new-version line, no note, no button (narrow screens such as iPad would crowd Play)
        tsv = {l.split('\t')[0]: l.split('\t') for l in (DM / 'i18n' / 'ui_text.tsv').read_text(encoding='utf-8').splitlines()}
        self.assertEqual({c for c in tsv['dmUpdBuild'][1:7] if c}, {'v%s'})
        for cell in (c for c in tsv['dmUpdNew'][1:7] if c):
            self.assertEqual(cell.count('%'), 1, cell); self.assertNotIn('·', cell); self.assertLessEqual(len(cell), 12, cell)
        upd = (DM / 'host' / 'dm_update.c').read_text(encoding='utf-8')
        self.assertIn('else place(nt, nx, 2046+bottom);', upd)

    def test_layout_measures_version_width(self):
        tl = (DM / 'ios' / 'tools' / 'touch_layout.py').read_text(encoding='utf-8')
        self.assertRegex(tl, r"_UPDATE = \(_upd_text\('TouchBuild', 79, 2046, 420, 30, \(150, 140, 115\), fit='\"fitToText\": true, '\)")
        self.assertIn("_upd_text('TouchBuild', 3161, 60, 600, 30", tl)
        upd = (DM / 'host' / 'dm_update.c').read_text(encoding='utf-8')
        self.assertIn('place(bw, xr - 600 - bx, 60)', upd); self.assertIn('tw = *(int32_t *)(bw + 0x78)', upd)



class PromptOnly(unittest.TestCase):
    """四审 Q4-04：更新信息只提示、不提供更新动作——普通 / 紧急、触屏 / 手柄 / Mac 都没有能点或能按的更新入口。
    Fourth review Q4-04: update information is a notice only - normal / urgent, touch / controller / Mac all lack any tappable or pressable update entry."""
    def test_layouts_have_no_update_buttons(self):
        sys.path.insert(0, str(DM / 'ios' / 'tools')); import touch_layout
        for name in ('_UPDATE', '_UPDATE_PAD'):
            text = getattr(touch_layout, name)
            self.assertNotIn('ButtonWidget', text, name); self.assertNotIn('onClickMessage', text, name); self.assertNotIn('TouchUpdate', text, name)
            self.assertNotIn('@dmUpdButton', text, name)

    def test_host_core_has_no_action_paths(self):
        upd = (DM / 'host' / 'dm_update.c').read_text(encoding='utf-8')
        for gone in ('TouchUpdate', 'on_click', 'g_click', 'dm_padmode_page_keys', 'dm_update_click_at', 'TouchUpdBig', 'TouchUpdSmall', 'dmUpdButton'):
            self.assertNotIn(gone, upd, gone)
        self.assertIn('void dm_update_init(int own_build, const char *label, void (*on_select)(void))', upd)

    def test_shells_never_open_install_or_download_page(self):
        ios = (DM / 'ios' / 'Sources' / 'dm_update.m').read_text(encoding='utf-8')
        self.assertNotIn('openURL', ios); self.assertNotIn('install_url"]', ios); self.assertNotIn('on_click', ios)
        mac = (DM / 'host' / 'app_mac.m').read_text(encoding='utf-8')
        i = mac.index('// ---- 更新检查 ----'); j = mac.index('\n}\n', mac.index('static void upd_start(void)')); upd = mac[i:j]
        self.assertNotIn('openURL', upd); self.assertNotIn('upd_click', upd)
        self.assertNotIn('dm_update_click_at', (DM / 'host' / 'touch_mac.c').read_text(encoding='utf-8'))

    def test_no_update_button_string(self):
        tsv = {l.split('\t')[0] for l in (DM / 'i18n' / 'ui_text.tsv').read_text(encoding='utf-8').splitlines()}
        self.assertNotIn('dmUpdButton', tsv)


class UiregPackageCheck(unittest.TestCase):
    """tools/uireg/run_dev.sh 的验包段原样执行；缺字段、空字段、未知标签、脏源码、错构建号都要拒（四审 Q4-02）
    Run the package check block of tools/uireg/run_dev.sh verbatim; missing / empty fields, unknown labels, dirty source and wrong builds are all refused (fourth review Q4-02)"""
    SRC = (DM / 'ios' / 'tools' / 'uireg' / 'run_dev.sh').read_text(encoding='utf-8')

    def check(self, repo, boot, **env):
        s = self.SRC; block = s[s.index('# ---- 验包 | package check ----'):s.index('# ---- 验包完 | end of package check ----')]
        with tempfile.TemporaryDirectory() as o:
            if boot is not None: Path(o, 'boot.txt').write_text(boot, encoding='utf-8')
            subprocess.run(['bash', '-c', block], cwd=o, check=True, env=dict({'PATH': '/usr/bin:/bin', 'HERE': str(repo.d), 'O': o, 'HOME': o}, **env))
            return Path(o, 'boot_wrongpkg.txt').exists(), Path(o, 'boot.txt').read_text(encoding='utf-8') if Path(o, 'boot.txt').exists() else ''

    def boot(self, build, sha, label, dirty='clean'):
        lines = [f'[dm] boot: 版本 1.0.0 构建 {build} 机型 iPhone00,0 系统 00.0 时间 x']
        if label is not None: lines.append(f'[dm] boot: 显示版本 {label}'.rstrip())
        lines.append(f'[dm] boot: 源码 {sha} 工作区 {dirty} 构建输入 chain=x')
        return '\n'.join(lines) + '\n'

    def test_cases(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); r.git('tag', 'v1.0.0'); r.commit(); v = r.version(); b, lbl = v['build'], v['label']; sha = r.git('rev-parse', 'HEAD')
            self.assertEqual(lbl, '1.0.0+1 · ' + sha[:7])
            ok = lambda **k: self.assertFalse(self.check(r, self.boot(**dict(dict(build=b, sha=sha, label=lbl), **k)))[0], k)
            bad = lambda why, env={}, **k: self.assertIn(why, self.check(r, self.boot(**dict(dict(build=b, sha=sha, label=lbl), **k)), **env)[1], k)
            ok(); ok(dirty='3:abcdef012345:' + ','.join(self.META))   # 只有本机元数据 | local metadata only
            ok(dirty='1:0123456789ab:dm/gfx/nvn/dmsh_chain.h')
            bad('带 *', label=lbl + '*')
            bad('源码', sha='f' * 40, label='1.0.0+1 · fffffff')
            bad('构建号 1，', build='1')
            bad('显示版本读不到', label=None)                     # 缺行 | line missing
            bad('显示版本读不到', label='')                       # 空 | empty
            bad('期望「' + lbl + '」', label='1.0.0+1 · 未知')    # 未知 | unknown
            bad('期望「' + lbl + '」', label='0.9.0 · ' + sha[:7])
            bad('没提交的代码改动 dm/host/a.c', dirty='2:abcdef012345:dm/host/a.c,dm/gfx/nvn/c1_full.tsv')
            bad('工作区标记读不到', dirty='')
            bad('期望 1800000000', env={'UIREG_EXPECT_BUILD': '1800000000'})
            self.assertFalse(self.check(r, self.boot(build='1800000000', sha=sha, label=lbl), UIREG_EXPECT_BUILD='1800000000')[0])   # 显式覆盖值 | explicit override

    META = ('dm/gfx/nvn/c1_full.tsv', 'dm/gfx/nvn/dmsh_chain.h', 'dm/gfx/tools/dmsh_chain.lock')

    def test_dirty_marker_strict(self):
        # 五审 Q5-04：工作区标记 = clean 或「条数:12 位小写十六进制指纹:逗号分隔的仓库相对路径」，条数与列表一致、路径只许三份本机元数据；其余一律拒
        # Fifth review Q5-04: the working-tree marker is clean or "count:12 lowercase hex:comma-separated repo-relative paths", the count must match the list and only the three local metadata paths are allowed; everything else is refused
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); r.git('tag', 'v1.0.0'); r.commit(); v = r.version(); b, lbl = v['build'], v['label']; sha = r.git('rev-parse', 'HEAD')
            run = lambda dirty: self.check(r, self.boot(b, sha, lbl, dirty))
            self.assertEqual(run('clean'), (False, self.boot(b, sha, lbl, 'clean')))
            for dirty, why in [
                ('1:abcdef012345:', '格式不对'),                              # 空列表 | empty list
                ('unknown:', '格式不对'),                                     # 未知格式 | unknown format
                ('unknown', '格式不对'),
                ('abcdef', '格式不对'),                                       # 无冒号 | no colon
                ('1:abcdef012345', '格式不对'),                               # 缺列表段 | list field missing
                ('1:xyz:dm/gfx/nvn/c1_full.tsv', '格式不对'),                 # 错指纹 | bad fingerprint
                ('1:ABCDEF012345:dm/gfx/nvn/c1_full.tsv', '格式不对'),        # 大写 | uppercase
                ('1:abcdef0123456:dm/gfx/nvn/c1_full.tsv', '格式不对'),       # 13 位 | 13 digits
                ('0:abcdef012345:dm/gfx/nvn/c1_full.tsv', '格式不对'),        # 零条 | zero count
                ('01:abcdef012345:dm/gfx/nvn/c1_full.tsv', '格式不对'),       # 前导零 | leading zero
                ('2:abcdef012345:dm/gfx/nvn/c1_full.tsv', '条数'),            # 条数不一致 | count mismatch
                ('1:abcdef012345:dm/gfx/nvn/c1_full.tsv,dm/gfx/nvn/dmsh_chain.h', '条数'),
                ('2:abcdef012345:dm/gfx/nvn/c1_full.tsv,', '格式不对'),       # 空项 | empty entry
                ('2:abcdef012345:dm/gfx/nvn/c1_full.tsv,dm/gfx/nvn/c1_full.tsv', '重复'),
                ('3:abcdef012345:c1_full.tsv,dmsh_chain.h,dmsh_chain.lock', '没提交的代码改动 c1_full.tsv'),   # 旧格式只有文件名 | old basename-only format
                ('1:abcdef012345:dm/ios/c1_full.tsv', '没提交的代码改动 dm/ios/c1_full.tsv'),                 # 同名不同目录 | same name, other directory
                ('1:abcdef012345:x/dm/gfx/nvn/c1_full.tsv', '没提交的代码改动 x/dm/gfx/nvn/c1_full.tsv'),
            ]:
                rejected, log = run(dirty)
                self.assertTrue(rejected, dirty); self.assertIn(why, log, dirty)

    def test_expect_other_commit(self):
        with tempfile.TemporaryDirectory() as d:
            r = Repo(d); old = r.git('rev-parse', 'HEAD'); v = r.version(); r.commit()
            self.assertFalse(self.check(r, self.boot(v['build'], old, v['label']), UIREG_EXPECT_COMMIT=old)[0])
            self.assertTrue(self.check(r, self.boot(v['build'], old, v['label']))[0])   # 缺省按 HEAD | HEAD by default


if __name__ == '__main__':
    unittest.main(verbosity=1)
