"""手机端生成器随 APK 带的本仓文件：只带 git 跟踪的生成脚本，不带第三方源码、鸿蒙 / tvOS、本机文件与缓存；同样输入出同样字节。不读游戏内容。
The repo files the on-phone generator ships in the APK: only the git-tracked generator scripts, no third-party sources, HarmonyOS / tvOS, local files or caches; same input, same bytes. No game content.
"""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
spec = importlib.util.spec_from_file_location('pack_gen_assets', TOOLS / 'pack_gen_assets.py')
pga = importlib.util.module_from_spec(spec); spec.loader.exec_module(pga)


def git(root, *args): subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True)


class SourceZip(unittest.TestCase):
    def make_repo(self, root):
        files = {'dm/tools/d2rm_android.py': 'print(1)\n', 'dm/ios/tools/gen_assets.py': 'x = 1\n', 'dm/PRODUCT': '3.3.94012-r44\n',
                 'dm/third_party/ryujinx/a.cs': 'class A {}\n', 'dm/harmony/b.ets': 'b\n', 'dm/tvos/c.m': 'c\n', 'dm/android/app/src/D.java': 'class D {}\n', 'dm/ios/tests/t_x.m': '/Users/alice\n'}
        for name, text in files.items():
            p = root / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(text)
        git(root, 'init', '-q'); git(root, 'add', '.'); git(root, '-c', 'user.email=t@t', '-c', 'user.name=t', 'commit', '-qm', 'x')
        (root / 'dm/gfx/nvn').mkdir(parents=True); (root / 'dm/gfx/nvn/c1_full.tsv').write_text('local only\n')   # 本机文件，不跟踪 | local, untracked
        (root / 'dm/tools/__pycache__').mkdir(); (root / 'dm/tools/__pycache__/x.pyc').write_bytes(b'\0')
        (root / 'dm/tools/d2rm_android.py').write_text('print(2)\n')   # 跟踪文件的未提交改动要带上（开发包用工作树） | uncommitted edits of tracked files are included (dev builds use the work tree)

    def test_selection(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); self.make_repo(root)
            self.assertEqual(pga.source_files(root / 'dm'), ['dm/PRODUCT', 'dm/ios/tools/gen_assets.py', 'dm/tools/d2rm_android.py'])

    def test_copy_without_git_skips_build_output(self):
        # shell_apk.sh 在没有 git 的源码副本里构建：gradle 的缓存与构建目录不能被打进去 | shell_apk.sh builds in a source copy without git: gradle caches and build directories must not be packed
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); self.make_repo(root); shutil.rmtree(root / '.git')
            for junk in ('dm/android/.gradle/9/executionHistory.bin', 'dm/android/app/build/x.apk', 'dm/gfx/build/y.o', 'dm/android/.cxx/z'):
                p = root / junk; p.parent.mkdir(parents=True, exist_ok=True); p.write_text('junk')
            self.assertEqual(pga.source_files(root / 'dm'), ['dm/PRODUCT', 'dm/gfx/nvn/c1_full.tsv', 'dm/ios/tools/gen_assets.py', 'dm/tools/d2rm_android.py'])

    def test_zip_is_reproducible_and_has_work_tree_content(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); self.make_repo(root)
            a, b = root / 'a.zip', root / 'b.zip'
            pga.write_zip(root / 'dm', pga.source_files(root / 'dm'), a)
            (root / 'dm/PRODUCT').touch()   # 只改时间 | only the time changes
            pga.write_zip(root / 'dm', pga.source_files(root / 'dm'), b)
            self.assertEqual(a.read_bytes(), b.read_bytes())
            with zipfile.ZipFile(a) as z: self.assertEqual(z.read('dm/tools/d2rm_android.py'), b'print(2)\n')


if __name__ == '__main__':
    unittest.main()
