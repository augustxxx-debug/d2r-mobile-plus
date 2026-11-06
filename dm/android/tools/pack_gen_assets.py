#!/usr/bin/env python3
"""手机端生成导入包时 App 要带的本仓文件（不读游戏文件，不含游戏内容）：
  dmgen/src.zip      手机端生成器真正用到的文件（白名单 generator_files，缺一个就出包失败），手机上解开后跑 dm/tools/d2rm_android.py；各端的壳、测试、文档都不进（B089）。
                     ui_text.tsv 进 zip 前去掉 iOS 专属行（dm_i18n.filter_table，与编进 libdm 的过滤同一张表）；覆盖层源指纹按过滤后的样子算（overlay_contract），生成的覆盖层清单才对得上
  dmgen/src.sha256   src.zip 的 sha256（App 据此判断要不要重新解开）
  dmgen/queries/     4 个查询核（dm/gfx/tools/pack_builtin_queries.py，用 NDK 的 glslc 编），手机上没有 glslc
  dmgen/font/        可选：开源楷体（--font，须带 --font-license 的许可证文件，如霞鹜文楷 + OFL.txt）与 font.txt（文件名、加粗档、sha256）；
                     手机端生成覆盖层时用它替换中日文字形（DM_UI_FONT），字体本身是可再分发的开源字体，不是游戏内容
  dmgen/THIRD_PARTY_NOTICES.txt  随包第三方组件（运行时、翻译工具、字体）的许可证清单与原文，取自 dm/android/THIRD_PARTY_NOTICES.txt（tools/third_party_notices.py 生成）
用法：pack_gen_assets.py --output <assets 目录> --glslc <glslc> [--font <.ttf> --font-license <许可证> [--font-weight 0..8]]
The repo files the App carries for making the import pack on the phone (no game files are read, no game content):
  dmgen/src.zip      the files the on-phone generator really uses (the generator_files whitelist; one missing fails the build), unpacked on the phone to run dm/tools/d2rm_android.py; no platform shells, tests or docs (B089).
                     ui_text.tsv loses its iOS-only rows on the way in (dm_i18n.filter_table, the same table as the filter compiled into libdm); the overlay source fingerprint is taken over the filtered form (overlay_contract), so the generated overlay manifest matches
  dmgen/src.sha256   sha256 of src.zip (the App unpacks again when it changes)
  dmgen/queries/     the 4 query kernels (dm/gfx/tools/pack_builtin_queries.py, compiled with the NDK's glslc); there is no glslc on the phone
  dmgen/font/        optional: an open-licence Kai font (--font, which needs the licence file given by --font-license, e.g. LXGW WenKai + OFL.txt) and font.txt (file name,
                     stroke level, sha256); the on-phone overlay step uses it for the CJK glyphs (DM_UI_FONT); the font is redistributable open-source, not game content
  dmgen/THIRD_PARTY_NOTICES.txt  licences of the bundled third-party components (runtime, translator, font) with their texts, from dm/android/THIRD_PARTY_NOTICES.txt (made by tools/third_party_notices.py)
Usage: pack_gen_assets.py --output <assets directory> --glslc <glslc> [--font <.ttf> --font-license <licence> [--font-weight 0..8]]
"""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

# 手机端生成器除覆盖层源指纹输入（overlay_contract.SOURCE_INPUTS）之外还要的文件。清单来自在 Mac 上模拟手机环境跑一遍时实际打开的文件；生成器多用一个文件就要加在这里，否则手机上首启会失败
# What the on-phone generator needs beyond the overlay source fingerprint inputs (overlay_contract.SOURCE_INPUTS). The list comes from the files actually opened in a phone-like run on a Mac; a generator that starts using another file must add it here, or the first start on a phone fails
GENERATOR_EXTRA = (
    'dm/PRODUCT', 'dm/ios/Sources/ControlArt/dm_mobile_socket.png', 'dm/ios/tools/shell_stubs.py',
    'dm/tools/d2rm_android.py', 'dm/tools/gen_common.py', 'dm/tools/genfail.py', 'dm/tools/patch_nso.py',
    'dm/gfx/nvn/shader_facts.tsv', 'dm/gfx/nvn/shader_map.tsv', 'dm/gfx/tools/dmsh_facts.py', 'dm/ios/NativeHUD/touch/close.png', 'dm/verify/nsoimg.py',
    'dm/ios/tools/font_override.py', 'dm/ios/tools/edge_indicators.py', 'dm/ios/tools/gen_notification_frame.py',
    'dm/ios/tools/gen_vault_table.py', 'dm/ios/tools/gen_hud_exp.py', 'dm/gfx/tools/dmsh_vk.py', 'dm/gfx/tools/dmsh_resume.py', 'dm/gfx/tools/pack_builtin_queries.py', 'dm/gfx/tools/pack_game_shaders.py',
)
TABLE = 'dm/i18n/ui_text.tsv'


def tool_module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).resolve().parents[2] / 'ios/tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def generator_files(dm=None):
    """进 src.zip 的文件（相对仓库根，排好序）。游戏文件清单按版本取，都带 | the files in src.zip (relative to the repo root, sorted); the game file lists go by version, all of them ship"""
    dm = Path(dm or Path(__file__).resolve().parents[2]).resolve()
    lists = [str(p.relative_to(dm.parent)) for p in (dm / 'data').glob('game_files_*.tsv')]
    return sorted(set(tool_module('overlay_contract').SOURCE_INPUTS) | set(GENERATOR_EXTRA) | set(lists))


def source_files(dm):
    """白名单里的文件，缺一个就停：出包时发现，好过玩家首启失败 | the whitelist; stop on a missing file: better found at build time than at a player's first start"""
    dm = Path(dm).resolve(); names = generator_files(dm)
    missing = [n for n in names if not (dm.parent / n).is_file()]
    if missing: sys.exit('手机端生成器要的文件不在：%s' % missing)
    return names


def write_zip(dm, names, out):
    """固定时间与权限、按名排序：同样内容出同样字节 | fixed times and modes, sorted by name: same content, same bytes"""
    root = Path(dm).resolve().parent
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in names:
            info = zipfile.ZipInfo(n, date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o644 << 16; info.compress_type = zipfile.ZIP_DEFLATED
            data = (root / n).read_bytes()
            if n == TABLE: data = tool_module('dm_i18n').filter_table(data.decode('utf-8'), 'ios').encode('utf-8')
            z.writestr(info, data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--output', type=Path, required=True); ap.add_argument('--glslc', required=True)
    ap.add_argument('--font', type=Path); ap.add_argument('--font-license', type=Path); ap.add_argument('--font-weight', default='0', choices=[str(i) for i in range(9)])
    a = ap.parse_args()
    if a.font and not (a.font_license and a.font_license.is_file()): sys.exit('带字体必须带许可证文件（--font-license）| a font needs its licence file (--font-license)')
    if a.font and (a.font.suffix.lower() != '.ttf' or not a.font.is_file()): sys.exit('字体要是存在的 .ttf：%s' % a.font)
    dm = Path(__file__).resolve().parents[2]
    gen = a.output / 'dmgen'; shutil.rmtree(gen, ignore_errors=True); gen.mkdir(parents=True)
    notices = dm / 'android/THIRD_PARTY_NOTICES.txt'   # 随包组件的许可证必须随包（四审 A4-01）| the bundled components' licences must ship with them (fourth audit A4-01)
    if not notices.is_file(): sys.exit('缺 %s（tools/third_party_notices.py 生成）' % notices)
    shutil.copyfile(notices, gen / 'THIRD_PARTY_NOTICES.txt')
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
