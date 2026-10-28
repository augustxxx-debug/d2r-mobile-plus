#!/usr/bin/env python3
# d2rm_android.py 在手机上跑（App 自带工具）时的约定：翻译器 / spirv-opt / spirv-val / 查询核由环境变量指过去，不要求 .NET、glslc；进度标记供 App 显示。
# Conventions for d2rm_android.py when it runs on the phone (tools shipped in the App): the translator / spirv-opt / spirv-val / query kernels come from
# environment variables, with no .NET or glslc required; progress markers are for the App to show.
import contextlib, io, os, stat, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d2rm_android


def script(path, body):
    with open(path, 'w') as f: f.write('#!/bin/sh\n' + body + '\n')
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
    return path


class OnDeviceToolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); t = self.tmp.name
        self.game = os.path.join(t, 'game')
        for f in ('ExeFS/main', 'ExeFS/sdk', 'RomFS/data/build_manifest.psv'):
            os.makedirs(os.path.dirname(os.path.join(self.game, f)), exist_ok=True); open(os.path.join(self.game, f), 'wb').close()
        self.bin = os.path.join(t, 'bin'); os.makedirs(self.bin)
        self.opt = script(os.path.join(t, 'opt'), 'echo "  --trim-capabilities"')
        self.val = script(os.path.join(t, 'val'), 'exit 0')
        self.xlate = script(os.path.join(t, 'xlate'), 'exit 0')
        self.queries = os.path.join(t, 'queries'); os.makedirs(self.queries)
        for i in range(4): open(os.path.join(self.queries, '%016x.xgs' % i), 'wb').write(b'XGS1%d' % i)
        open(os.path.join(self.queries, 'notes.txt'), 'w').write('not a bundle')

    def tearDown(self): self.tmp.cleanup()

    def device_env(self, **extra):
        e = {'PATH': self.bin, 'DMSH_XLATE': self.xlate, 'SPIRV_OPT': self.opt, 'SPIRV_VAL': self.val, 'DM_PREBUILT_QUERIES': self.queries}
        e.update(extra); return e

    def test_shipped_tools_need_no_dotnet_or_glslc(self):
        with mock.patch.dict(os.environ, self.device_env(), clear=True):
            self.assertIsNone(d2rm_android.check_env(self.game))   # 没有 dotnet 也不报缺 | no dotnet and nothing reported missing

    def test_missing_shipped_tool_is_reported(self):
        with mock.patch.dict(os.environ, self.device_env(SPIRV_VAL=os.path.join(self.tmp.name, 'nope')), clear=True):
            with self.assertRaises(SystemExit) as cm: d2rm_android.check_env(self.game)
        self.assertIn('spirv-val', str(cm.exception))

    def test_desktop_still_requires_dotnet(self):
        with mock.patch.dict(os.environ, {'PATH': self.bin, 'SPIRV_OPT': self.opt, 'SPIRV_VAL': self.val, 'DM_PREBUILT_QUERIES': self.queries}, clear=True), \
             mock.patch.object(d2rm_android.os.path, 'expanduser', return_value=os.path.join(self.tmp.name, 'no-home')):
            with self.assertRaises(SystemExit) as cm: d2rm_android.check_env(self.game)
        self.assertIn('.NET', str(cm.exception))

    def test_prebuilt_queries_copy_exactly_four_bundles(self):
        sh = os.path.join(self.tmp.name, 'shaders'); os.makedirs(sh)
        with mock.patch.dict(os.environ, self.device_env(), clear=True):
            d2rm_android.add_queries(sh, os.path.join(self.tmp.name, 'stage'))
        self.assertEqual(sorted(os.listdir(sh)), ['%016x.xgs' % i for i in range(4)])
        self.assertEqual(open(os.path.join(sh, '%016x.xgs' % 2), 'rb').read(), b'XGS12')

    def test_prebuilt_queries_must_be_four(self):
        os.remove(os.path.join(self.queries, '%016x.xgs' % 3))
        with mock.patch.dict(os.environ, self.device_env(), clear=True):
            with self.assertRaises(SystemExit): d2rm_android.add_queries(os.path.join(self.tmp.name, 'shaders'), os.path.join(self.tmp.name, 'stage'))

    def test_hud_art_follows_gen_assets_dir(self):
        # 手机上生成目录不在源码树里（DM_GEN_ASSETS），底栏素材要跟着走 | on the phone the generated assets are outside the source tree (DM_GEN_ASSETS); the bottom-bar art must follow
        with mock.patch.dict(os.environ, {'DM_GEN_ASSETS': os.path.join(self.tmp.name, 'gen')}, clear=True):
            self.assertEqual(d2rm_android.hud_dir(), os.path.join(self.tmp.name, 'gen', 'NativeHUD', 'v4', 'hud_v4_ui'))

    def test_progress_markers_only_when_asked(self):
        for env, want in (({}, ''), ({'DM_GEN_PROGRESS': '1'}, '@progress shaders 6931\n')):
            out = io.StringIO()
            with mock.patch.dict(os.environ, env, clear=True), contextlib.redirect_stdout(out): d2rm_android.progress('shaders', 6931)
            self.assertEqual(out.getvalue(), want)


if __name__ == '__main__':
    unittest.main()
