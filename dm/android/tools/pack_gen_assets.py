#!/usr/bin/env python3
"""手机端生成导入包时 App 要带的本仓文件（不读游戏文件，不含游戏内容）：
  dmgen/src.zip      dm/ 下 git 跟踪的文件（工作树内容；去掉第三方源码、鸿蒙、tvOS、安卓 App 自身与各处 tests/），手机上解开后跑 dm/tools/d2rm_android.py；
                     与 libdm 编进去的覆盖层源指纹同一份源码，生成的覆盖层清单才对得上
  dmgen/src.sha256   src.zip 的 sha256（App 据此判断要不要重新解开）
  dmgen/queries/     4 个查询核（dm/gfx/tools/pack_builtin_queries.py，用 NDK 的 glslc 编），手机上没有 glslc
  dmgen/font/        可选：开源楷体（--font，须带 --font-license 的许可证文件，如霞鹜文楷 + OFL.txt）与 font.txt（文件名、加粗档、sha256）；
                     手机端生成覆盖层时用它替换中日文字形（DM_UI_FONT），字体本身是可再分发的开源字体，不是游戏内容
用法：pack_gen_assets.py --output <assets 目录> --glslc <glslc> [--font <.ttf> --font-license <许可证> [--font-weight 0..8]]
The repo files the App carries for making the import pack on the phone (no game files are read, no game content):
  dmgen/src.zip      git-tracked files under dm/ (work-tree content; without third-party sources, HarmonyOS, tvOS, the Android App itself and any tests/), unpacked on the phone to run dm/tools/d2rm_android.py;
                     the same sources as the overlay source fingerprint compiled into libdm, so the generated overlay manifest matches
  dmgen/src.sha256   sha256 of src.zip (the App unpacks again when it changes)
  dmgen/queries/     the 4 query kernels (dm/gfx/tools/pack_builtin_queries.py, compiled with the NDK's glslc); there is no glslc on the phone
  dmgen/font/        optional: an open-licence Kai font (--font, which needs the licence file given by --font-license, e.g. LXGW WenKai + OFL.txt) and font.txt (file name,
                     stroke level, sha256); the on-phone overlay step uses it for the CJK glyphs (DM_UI_FONT); the font is redistributable open-source, not game content
Usage: pack_gen_assets.py --output <assets directory> --glslc <glslc> [--font <.ttf> --font-license <licence> [--font-weight 0..8]]
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

SKIP = ('dm/third_party/', 'dm/harmony/', 'dm/tvos/', 'dm/android/app/')
BUILT = {'build', '.gradle', '.cxx', '.externalNativeBuild', '__pycache__'}   # 构建产物目录（没有 git 的源码副本里构建过会有；仓库里没有跟踪文件在这些名字的目录下） | build output directories (present in a source copy without git once built; no tracked file lives under such a name)


def source_files(dm):
    """dm/ 下要带的文件（相对仓库根，排好序）：有 git 就按跟踪清单（本机文件、缓存都不在里面），源码副本没有 git 就走目录
    The files under dm/ to ship (relative to the repo root, sorted): the tracked list when git is there (local files and caches are not in it); a source copy without git is walked"""
    dm = Path(dm).resolve(); root = dm.parent
    r = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--', 'dm'], capture_output=True)
    if r.returncode == 0 and r.stdout:
        names = [n for n in r.stdout.decode('utf-8').split('\0') if n]
    else:
        names = [str(p.relative_to(root)) for p in dm.rglob('*') if p.is_file() and p.suffix != '.pyc']
    return sorted(n for n in set(names) if not n.startswith(SKIP) and '/tests/' not in n and not BUILT & set(n.split('/')[:-1]) and (root / n).is_file())   # 测试与夹具不带（夹具里有示例路径） | no tests or fixtures (the fixtures hold sample paths)


def write_zip(dm, names, out):
    """固定时间与权限、按名排序：同样内容出同样字节 | fixed times and modes, sorted by name: same content, same bytes"""
    root = Path(dm).resolve().parent
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in names:
            info = zipfile.ZipInfo(n, date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o644 << 16; info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, (root / n).read_bytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--output', type=Path, required=True); ap.add_argument('--glslc', required=True)
    ap.add_argument('--font', type=Path); ap.add_argument('--font-license', type=Path); ap.add_argument('--font-weight', default='0', choices=[str(i) for i in range(9)])
    a = ap.parse_args()
    if a.font and not (a.font_license and a.font_license.is_file()): sys.exit('带字体必须带许可证文件（--font-license）| a font needs its licence file (--font-license)')
    if a.font and (a.font.suffix.lower() != '.ttf' or not a.font.is_file()): sys.exit('字体要是存在的 .ttf：%s' % a.font)
    dm = Path(__file__).resolve().parents[2]
    gen = a.output / 'dmgen'; shutil.rmtree(gen, ignore_errors=True); gen.mkdir(parents=True)
    names = source_files(dm); write_zip(dm, names, gen / 'src.zip')
    (gen / 'src.sha256').write_text(hashlib.sha256((gen / 'src.zip').read_bytes()).hexdigest() + '\n')
    # pack_builtin_queries.py 不许输出在 dm/ 里（构建目录在 dm/android/app/build 下），先编到临时目录再搬
    # pack_builtin_queries.py refuses output inside dm/ (the build directory is under dm/android/app/build), so compile into a temporary directory and move
    with tempfile.TemporaryDirectory() as t:
        q = Path(t) / 'q'
        subprocess.run([sys.executable, str(dm / 'gfx/tools/pack_builtin_queries.py'), '--output', str(q), '--glslc', a.glslc], check=True, stdout=subprocess.DEVNULL)
        got = sorted(f for f in os.listdir(q) if f.endswith('.xgs'))
        if len(got) != 4: sys.exit('查询核不是 4 个：%s' % got)
        (gen / 'queries').mkdir()
        for f in got: shutil.copyfile(q / f, gen / 'queries' / f)
    if a.font:
        f = gen / 'font'; f.mkdir(); shutil.copyfile(a.font, f / a.font.name); shutil.copyfile(a.font_license, f / 'OFL.txt')
        (f / 'font.txt').write_text('file=%s\nweight=%s\nsha256=%s\n' % (a.font.name, a.font_weight, hashlib.sha256(a.font.read_bytes()).hexdigest()))
    print('dmgen：src.zip %d 个文件，查询核 %d 个，字体 %s' % (len(names), len(got), a.font.name if a.font else '无'))


if __name__ == '__main__':
    main()
