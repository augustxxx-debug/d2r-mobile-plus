#!/usr/bin/env python3
"""安卓包随附第三方组件的许可证清单（dm/android/THIRD_PARTY_NOTICES.txt）：手机端生成器运行时（gen_runtime.sh）与随包字体里的每一件，
写名称、版本、许可证、在 APK 里的位置、来源，再附许可证原文。原文取自 gen_runtime.sh 下载并核过 sha256 的源码包 / 轮子、.NET NativeAOT 运行时包，
以及几份官方仓库按版本标签取的许可证文件（--extra 目录，文件名见 EXTRA）。改了 gen_runtime.sh 的组件或版本就重跑本脚本并提交结果；
shell_apk.sh 核对 runtime.txt 里的每个版本都出现在清单里，对不上就停。
用法：third_party_notices.py --dl <gen_runtime 工作目录>/dl --spirv <gen_runtime 工作目录>/build/SPIRV-Tools --dotnet-pack <~/.nuget/packages/microsoft.netcore.app.runtime.nativeaot.linux-bionic-arm64/<版本>>
                             --extra <许可证文件目录> --font-license <OFL.txt> --output dm/android/THIRD_PARTY_NOTICES.txt
Licence notices for the third-party components bundled in the Android package (dm/android/THIRD_PARTY_NOTICES.txt): every part of the on-phone generator runtime
(gen_runtime.sh) and the bundled font, with name, version, licence, location in the APK and source, followed by the licence texts. The texts come from the source
archives / wheels gen_runtime.sh downloads and checks by sha256, the .NET NativeAOT runtime pack, and a few licence files taken from the official repositories at the
version tag (--extra directory, file names in EXTRA). Rerun this script and commit the result whenever gen_runtime.sh changes a component or version; shell_apk.sh
checks that every version in runtime.txt appears in the notices and stops otherwise.
"""
import argparse
import io
from pathlib import Path
import tarfile
import zipfile

# --extra 目录里要有的文件（官方仓库按标签取）| files expected in the --extra directory (taken from the official repositories at the tag)
EXTRA = {
    'cpython-LICENSE': 'https://raw.githubusercontent.com/python/cpython/v3.13.16/LICENSE',
    'cpython-license.rst': 'https://raw.githubusercontent.com/python/cpython/v3.13.16/Doc/license.rst',
    'chaquopy-LICENSE.txt': 'https://raw.githubusercontent.com/chaquo/chaquopy/master/LICENSE.txt',
    'openssl-LICENSE.txt': 'https://raw.githubusercontent.com/openssl/openssl/openssl-3.5.0/LICENSE.txt',
    'xz-COPYING': 'https://raw.githubusercontent.com/tukaani-project/xz/v5.4.6/COPYING',
    'bzip2-LICENSE': 'https://sourceware.org/git/?p=bzip2.git;a=blob_plain;f=LICENSE;hb=refs/tags/bzip2-1.0.8',
}

# (名称, 版本, 许可证, 在 APK 里, 来源, 原文来源) | (name, version, licence, where in the APK, source, text source)
# 原文来源：('tar', 包, 成员) / ('whl', 轮子前缀, 成员) / ('extra', 文件) / ('file', 路径键) / ('note', 文字)
COMPONENTS = [
    ('CPython (Android build by Chaquopy)', 'python=3.13.16-0', 'PSF-2.0 (Python Software Foundation License)', 'runtime.zip lib/libpython3.13.so, py/lib/python3.13; lib/arm64-v8a/libdmgen_py.so',
     'https://repo.maven.apache.org/maven2/com/chaquo/python/target/3.13.16-0/', ('extra', 'cpython-LICENSE')),
    ('Software incorporated in CPython 3.13 (expat 2.8.5, libffi, libmpdec 2.5.1, HACL*, Modules/_decimal, hashlib and others)', '3.13.16', 'see the CPython "Licenses and Acknowledgements for Incorporated Software" below',
     'runtime.zip py/lib/python3.13/lib-dynload/*.so', 'https://github.com/python/cpython/blob/v3.13.16/Doc/license.rst', ('extra', 'cpython-license.rst')),
    ('Chaquopy (Android CPython build and wheels)', 'chaquopy target 3.13.16-0, pypi-13.1', 'MIT', 'runtime.zip (Python build, numpy / lz4 / OpenBLAS / libgfortran / libc++ wheels)',
     'https://github.com/chaquo/chaquopy', ('extra', 'chaquopy-LICENSE.txt')),
    ('OpenSSL', '3.5.9', 'Apache-2.0 (text from the openssl-3.5.0 tag; the same in every 3.5.x)', 'runtime.zip lib/libcrypto_python.so, lib/libssl_python.so', 'https://www.openssl.org', ('extra', 'openssl-LICENSE.txt')),
    ('SQLite', '3 (2026-07-24 build)', 'Public domain', 'runtime.zip lib/libsqlite3_python.so', 'https://sqlite.org',
     ('note', 'The SQLite source code is in the public domain: "The author disclaims copyright to this source code. In place of a legal notice, here is a blessing: '
              'May you do good and not evil. May you find forgiveness for yourself and forgive others. May you share freely, never taking more than you give." '
              'See https://sqlite.org/copyright.html')),
    ('XZ Utils (liblzma)', '5.4.6', '0BSD / public domain (liblzma)', 'runtime.zip py/lib/python3.13/lib-dynload/_lzma*.so', 'https://tukaani.org/xz/', ('extra', 'xz-COPYING')),
    ('bzip2 (libbzip2)', '1.0.8', 'bzip2-1.0.6 (BSD-style)', 'runtime.zip py/lib/python3.13/lib-dynload/_bz2*.so', 'https://sourceware.org/bzip2/', ('extra', 'bzip2-LICENSE')),
    ('zlib (Python zlib module, statically linked)', 'zlib(python)=1.3.1', 'Zlib', 'runtime.zip py/lib/python3.13/lib-dynload/zlib*.so', 'https://zlib.net', ('tar', 'zlib-1.3.1.tar.gz', 'zlib-1.3.1/LICENSE')),
    ('NumPy', 'numpy=1.26.2', 'BSD-3-Clause (with bundled components listed in its notice)', 'runtime.zip py/lib/python3.13/site-packages/numpy', 'https://numpy.org',
     ('whl', 'numpy-1.26.2', ['numpy-1.26.2.dist-info/LICENSE.txt', 'numpy-1.26.2.dist-info/LICENSES_bundled.txt'])),
    ('OpenBLAS', '0.2.20', 'BSD-3-Clause', 'runtime.zip lib/libopenblas.so', 'https://www.openblas.net', ('whl', 'chaquopy_openblas-0.2.20', ['chaquopy_openblas-0.2.20.dist-info/LICENSE'])),
    ('libgfortran (GCC runtime)', '4.9', 'GPL-3.0 WITH GCC-exception-3.1', 'runtime.zip lib/libgfortran.so.3', 'https://gcc.gnu.org', ('whl', 'chaquopy_libgfortran-4.9', ['chaquopy_libgfortran-4.9.dist-info/LICENSE.txt'])),
    ('LLVM libc++ (shared from Chaquopy; also statically linked from the Android NDK into Pillow\'s _imagingft and spirv-opt / spirv-val)', '18 (Chaquopy 180000); NDK 29.0.14206865',
     'Apache-2.0 WITH LLVM-exception', 'runtime.zip lib/libc++_shared.so; lib/arm64-v8a/libdmgen_spvopt.so, libdmgen_spvval.so', 'https://libcxx.llvm.org',
     ('whl', 'chaquopy_libcxx-180000', ['chaquopy_libcxx-180000.dist-info/LICENSE.TXT'])),
    ('python-lz4 (with LZ4)', 'lz4=4.3.3', 'BSD-3-Clause (python-lz4); BSD-2-Clause (LZ4 library)', 'runtime.zip py/lib/python3.13/site-packages/lz4', 'https://github.com/python-lz4/python-lz4',
     ('whl', 'lz4-4.3.3', ['lz4-4.3.3.dist-info/LICENSE'])),
    ('fontTools', 'fonttools=4.67.0', 'MIT', 'runtime.zip py/lib/python3.13/site-packages/fontTools', 'https://github.com/fonttools/fonttools',
     ('whl', 'fonttools-4.67.0', ['fonttools-4.67.0.dist-info/licenses/LICENSE', 'fonttools-4.67.0.dist-info/licenses/LICENSE.external'])),
    ('Capstone', 'capstone=5.0.7', 'BSD-3-Clause', 'runtime.zip py/lib/python3.13/site-packages/capstone', 'https://www.capstone-engine.org', ('tar', 'capstone-5.0.7.tar.gz', 'capstone-5.0.7/LICENSE.TXT')),
    ('Pillow', 'pillow=13.0.0', 'MIT-CMU (HPND)', 'runtime.zip py/lib/python3.13/site-packages/PIL', 'https://python-pillow.org', ('tar', 'pillow-13.0.0.tar.gz', 'pillow-13.0.0/LICENSE')),
    ('FreeType (statically linked into Pillow)', 'freetype=2.14.3', 'FTL (FreeType License; used under the FTL, not the GPLv2 option)', 'runtime.zip PIL/_imagingft*.so', 'https://freetype.org',
     ('tar', 'freetype-2.14.3.tar.gz', ['freetype-2.14.3/LICENSE.TXT', 'freetype-2.14.3/docs/FTL.TXT'])),
    ('HarfBuzz (statically linked into Pillow)', 'harfbuzz=14.5.0', 'MIT (Old MIT)', 'runtime.zip PIL/_imagingft*.so', 'https://harfbuzz.github.io', ('tar', 'harfbuzz-14.5.0.tar.xz', 'harfbuzz-14.5.0/COPYING')),
    ('zlib-ng (statically linked into Pillow)', 'zlib-ng=2.3.3', 'Zlib', 'runtime.zip PIL/_imaging*.so, _imagingft*.so', 'https://github.com/zlib-ng/zlib-ng', ('tar', 'zlib-ng-2.3.3.tar.gz', 'zlib-ng-2.3.3/LICENSE.md')),
    ('SPIRV-Tools (spirv-opt, spirv-val)', 'spirv-tools=28f99f74a2a922ff85fc8fd966d6b3d6a45be795', 'Apache-2.0', 'lib/arm64-v8a/libdmgen_spvopt.so, libdmgen_spvval.so', 'https://github.com/KhronosGroup/SPIRV-Tools',
     ('file', 'spirv-tools')),
    ('SPIRV-Headers', 'spirv-headers=86f980c731e62ae4eaf383d320449d71687936bf', 'MIT (Khronos)', 'lib/arm64-v8a/libdmgen_spvopt.so, libdmgen_spvval.so (compiled in)', 'https://github.com/KhronosGroup/SPIRV-Headers',
     ('file', 'spirv-headers')),
    ('.NET runtime (NativeAOT, linux-bionic-arm64)', 'Microsoft.NETCore.App.Runtime.NativeAOT 10.0.13', 'MIT (third-party notices below)', 'lib/arm64-v8a/libdmgen_xlate.so', 'https://github.com/dotnet/runtime',
     ('file', 'dotnet')),
    ('Ryujinx shader translator (compiled into the translator)', 'Ryujinx 1.3.2 (vendored unmodified in dm/third_party/ryujinx)', 'MIT', 'lib/arm64-v8a/libdmgen_xlate.so', 'https://github.com/Ryubing',
     ('file', 'ryujinx')),
    ('LXGW WenKai (霞鹜文楷) Medium', 'v1.530', 'SIL Open Font License 1.1 (shipped unmodified, licence also at assets/dmgen/font/OFL.txt)', 'assets/dmgen/font/LXGWWenKai-Medium.ttf',
     'https://github.com/lxgw/LxgwWenKai', ('file', 'ofl')),
]


def member(archive, name):
    with tarfile.open(archive) as t:
        return t.extractfile(name).read().decode('utf-8', 'replace')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--dl', type=Path, required=True); ap.add_argument('--spirv', type=Path, required=True); ap.add_argument('--dotnet-pack', type=Path, required=True)
    ap.add_argument('--extra', type=Path, required=True); ap.add_argument('--font-license', type=Path, required=True); ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    dm = Path(__file__).resolve().parents[2]
    files = {'spirv-tools': [a.spirv / 'LICENSE'], 'spirv-headers': [a.spirv / 'external/spirv-headers/LICENSE'],
             'dotnet': [a.dotnet_pack / 'LICENSE.TXT', a.dotnet_pack / 'THIRD-PARTY-NOTICES.TXT'], 'ryujinx': [dm / 'third_party/ryujinx/LICENSE.txt'], 'ofl': [a.font_license]}
    for f in EXTRA:
        if not (a.extra / f).is_file(): raise SystemExit('缺 %s（%s）' % (a.extra / f, EXTRA[f]))
    out = io.StringIO()
    out.write('THIRD-PARTY SOFTWARE NOTICES — D2R Mobile Plus, Android package\n\n'
              'The Android package bundles the tools below so that the app can build the import pack on the phone from the player\'s own game files\n'
              '(assets/dmgen/runtime.zip, lib/arm64-v8a/libdmgen_*.so) and an open-licence UI font (assets/dmgen/font/). None of them is game content.\n'
              'Each component is listed with its version, licence, location in the APK and source, followed by the full licence texts.\n'
              'This file is generated by dm/android/tools/third_party_notices.py; versions match assets/dmgen/runtime.txt.\n\n')
    for i, c in enumerate(COMPONENTS, 1):
        out.write('%2d. %s\n    version: %s\n    licence: %s\n    in APK:  %s\n    source:  %s\n' % (i, c[0], c[1], c[2], c[3], c[4]))
    for i, c in enumerate(COMPONENTS, 1):
        out.write('\n' + '=' * 78 + '\n%d. %s — %s\n' % (i, c[0], c[2]) + '=' * 78 + '\n\n')
        kind = c[5]
        if kind[0] == 'tar':
            for m in ([kind[2]] if isinstance(kind[2], str) else kind[2]): out.write('[%s]\n%s\n' % (m.split('/', 1)[1], member(a.dl / kind[1], m)))
        elif kind[0] == 'whl':
            whl = next(a.dl.glob(kind[1] + '*.whl'))
            with zipfile.ZipFile(whl) as z:
                for m in kind[2]: out.write('[%s]\n%s\n' % (m.split('/')[-1], z.read(m).decode('utf-8', 'replace')))
        elif kind[0] == 'extra':
            out.write((a.extra / kind[1]).read_text(encoding='utf-8', errors='replace') + '\n')
        elif kind[0] == 'file':
            for p in files[kind[1]]: out.write('[%s]\n%s\n' % (p.name, p.read_text(encoding='utf-8', errors='replace')))
        else:
            out.write(kind[1] + '\n')
    text = out.getvalue().replace('\r\n', '\n')
    with open(a.output, 'w', encoding='utf-8', newline='\n') as f: f.write(text)
    print('%s：%d 个组件，%d 字节' % (a.output, len(COMPONENTS), len(text.encode('utf-8'))))


if __name__ == '__main__':
    main()
