#!/bin/bash
# 手机上生成导入包要用的运行时（App 首次打开时用玩家自己的游戏文件跑 dm/tools/d2rm_android.py，不用电脑）。这里只编工具，不碰任何游戏文件，产物不含游戏内容。
#   Python 3.13（Chaquopy 发布的安卓 CPython）+ numpy / lz4（Chaquopy 预编轮子）+ capstone 5.0.7、Pillow 13.0.0（FreeType 2.14.3 链 HarfBuzz 14.5.0、zlib-ng 2.3.3，
#   与电脑上官方轮子同版本同配置，手机生成的界面 PNG 才与电脑逐字节一致）+ fontTools 4.67.0；着色器翻译工具 dmsh_xlate 用 NativeAOT 编成安卓可执行文件；
#   SPIRV-Tools 与电脑同一提交用 NDK 编 spirv-opt / spirv-val。
# 用法：gen_runtime.sh <工作目录（ASCII 路径，下载与中间产物）> <输出目录>
# 输出：<输出>/jniLibs/arm64-v8a/libdmgen_{py,xlate,spvopt,spvval}.so —— 其实是可执行文件，起成 lib*.so 才会被安装到 nativeLibraryDir（安卓 10 起 App 只能 exec 那里的文件）；
#       <输出>/assets/dmgen/runtime.zip（lib/ 共享库、py/ 标准库与第三方包）+ runtime.txt（各件版本与 sha256、翻译工具源码指纹）。
# 之后：gradle assembleDebug -PdmGenRuntime=<输出目录>（app/build.gradle 把这两样打进 APK，另把本仓的生成脚本与 4 个查询核现打进 assets/dmgen/）。
# 需要：ANDROID_HOME 下的 NDK 29.0.14206865 与 cmake 3.31.6、.NET SDK 10（dotnet）、python3、git、curl。
# The runtime used to make the import pack on the phone (when first opened, the App runs dm/tools/d2rm_android.py on the player's own game files, without a computer).
#   Only tools are built here, no game file is touched, and the output contains no game content.
#   Python 3.13 (the Android CPython published by Chaquopy) + numpy / lz4 (Chaquopy prebuilt wheels) + capstone 5.0.7, Pillow 13.0.0 (FreeType 2.14.3 linked with HarfBuzz 14.5.0, zlib-ng 2.3.3,
#   the same versions and configuration as the official wheels used on the computer, so that UI PNGs made on the phone are byte-identical to the computer's) + fontTools 4.67.0;
#   the shader translator dmsh_xlate is built into an Android executable with NativeAOT; spirv-opt / spirv-val are built with the NDK from the same SPIRV-Tools commit as the computer.
# Usage: gen_runtime.sh <work directory (ASCII path, downloads and intermediates)> <output directory>
# Output: <output>/jniLibs/arm64-v8a/libdmgen_{py,xlate,spvopt,spvval}.so -- really executables, named lib*.so so they are installed into nativeLibraryDir (since Android 10 an App may only exec files there);
#         <output>/assets/dmgen/runtime.zip (lib/ shared libraries, py/ the standard library and third-party packages) + runtime.txt (versions and sha256 of each part, the translator source fingerprint).
# Then: gradle assembleDebug -PdmGenRuntime=<output directory> (app/build.gradle packs both into the APK, and also packs this repo's generator scripts and the 4 query kernels into assets/dmgen/).
# Needs: NDK 29.0.14206865 and cmake 3.31.6 under ANDROID_HOME, the .NET SDK 10 (dotnet), python3, git, curl.
set -euo pipefail
[ $# -eq 2 ] || { echo "用法：$0 <工作目录> <输出目录>"; exit 2; }
DM=$(cd "$(dirname "$0")/../.." && pwd); mkdir -p "$1" "$2"; W=$(cd "$1" && pwd); OUT=$(cd "$2" && pwd)
if printf %s "$W" | LC_ALL=C grep -q '[^ -~]'; then echo "工作目录要 ASCII 路径（NDK 链接器与 NativeAOT 在非 ASCII 路径下出错）：$W"; exit 2; fi
NDK=${ANDROID_NDK:-$ANDROID_HOME/ndk/29.0.14206865}; HOSTTAG=$(ls "$NDK/toolchains/llvm/prebuilt" | head -1); BIN=$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin
CC=$BIN/aarch64-linux-android29-clang; CXX=$BIN/aarch64-linux-android29-clang++; STRIP=$BIN/llvm-strip
export PATH="$ANDROID_HOME/cmake/3.31.6/bin:$PATH"
MAP="-ffile-prefix-map=$W=/w"   # 产物里不留本机路径（shell_apk.sh 逐文件查） | no local paths in the output (shell_apk.sh checks every file)
TC=(-G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON "-DCMAKE_C_FLAGS=$MAP" "-DCMAKE_CXX_FLAGS=$MAP")
t() { echo "[$(date +%T)] $*"; }
DL=$W/dl; B=$W/build; PFX=$W/prefix; RT=$W/rt; mkdir -p "$DL" "$B"

# 下载并核 sha256 | download and check sha256
fetch() {   # <url> <sha256> [文件名]
  local f=$DL/${3:-$(basename "$1")}
  [ -f "$f" ] && [ "$(shasum -a 256 "$f" | cut -d' ' -f1)" = "$2" ] && return
  curl -fsSL --retry 3 -o "$f.part" "$1"; mv "$f.part" "$f"
  [ "$(shasum -a 256 "$f" | cut -d' ' -f1)" = "$2" ] || { echo "sha256 不对：$f"; exit 1; }
}
MAVEN=https://repo.maven.apache.org/maven2/com/chaquo/python/target/3.13.16-0; CHQ=https://chaquo.com/pypi-13.1
fetch $MAVEN/target-3.13.16-0-arm64-v8a.zip fae8cfd4d3910b6f978b22279fe820c0c6ba49d0ea20f7b106aa6130f1c8f355
fetch $MAVEN/target-3.13.16-0-stdlib.zip 68e3f71e6e2f2a2febb22f2d3575657787b3dd94f07d2c6f3780461babbcbf58
fetch $CHQ/numpy/numpy-1.26.2-0-cp313-cp313-android_24_arm64_v8a.whl 05db9adc14aa49f075a5ea5c59559c361e54efd926f321eddd3b37e2fd05b721
fetch $CHQ/lz4/lz4-4.3.3-0-cp313-cp313-android_24_arm64_v8a.whl 915b9c461bd7a266e202b11360955f8b981ff9e16ad2a65cb7ea4cd2bd3b0d71
fetch $CHQ/chaquopy-openblas/chaquopy_openblas-0.2.20-5-py3-none-android_21_arm64_v8a.whl 1e8e67a4f9e2fcde384890678bafbf7ffefe7b2cb4887e5c2996ba7607f4324c
fetch $CHQ/chaquopy-libgfortran/chaquopy_libgfortran-4.9-0-py3-none-android_21_arm64_v8a.whl 0b4caed1147f2a19707d4ba730afea57f7b8f2d8c046d8a48cf49377741c4604
fetch $CHQ/chaquopy-libcxx/chaquopy_libcxx-180000-0-py3-none-android_24_arm64_v8a.whl 811547210b01eefb0fb850fd35bd27b53a00a76b9466b8c03e049b7e8175004b
fetch https://files.pythonhosted.org/packages/py3/f/fonttools/fonttools-4.67.0-py3-none-any.whl 4304f03ed7f4ba000a8dcc941ad854bfa52e2f3b6112b8f099b6f431cf98e701
fetch https://files.pythonhosted.org/packages/source/c/capstone/capstone-5.0.7.tar.gz 796bdd69b05fa124fc2aa2e74b9a0b3d4c4e7f3e02add5e583cf2f3bca282ede
fetch https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.3.3.tar.gz f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1 zlib-ng-2.3.3.tar.gz
fetch https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.gz e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1
fetch https://github.com/harfbuzz/harfbuzz/releases/download/14.5.0/harfbuzz-14.5.0.tar.xz b7132e148358a45185c9feafd049dbaf243649d3c44414b3534d9c95d18592b9
fetch https://files.pythonhosted.org/packages/source/p/pillow/pillow-13.0.0.tar.gz cdcd1ba258178f7356d74170c9a81ba4eab6a5d55748bc402a5a2a6fe6be3510
fetch https://zlib.net/fossils/zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
fetch https://raw.githubusercontent.com/python/cpython/v3.13.16/Modules/zlibmodule.c d73bf051e646f4eb5f77194a1ded56c42fff31f7d1dc72541dd776b8d292a0d2
fetch https://raw.githubusercontent.com/python/cpython/v3.13.16/Modules/clinic/zlibmodule.c.h af2289823b2d9edba268430d2029d727d59d4a0bbb1d0eeb0d697e83f199c6a0
SPVT=28f99f74a2a922ff85fc8fd966d6b3d6a45be795; SPVH=86f980c731e62ae4eaf383d320449d71687936bf   # 与电脑上 d2rm_android.py 用的 spirv-opt 同一提交（v2026.4-83） | same commit as the spirv-opt d2rm_android.py uses on the computer (v2026.4-83)

# 一、Python 与预编轮子 | 1. Python and prebuilt wheels
t "Python 3.13 与 numpy / lz4"
rm -rf "$RT"; mkdir -p "$RT/lib" "$RT/py/lib/python3.13/lib-dynload" "$RT/py/lib/python3.13/site-packages"; SP=$RT/py/lib/python3.13/site-packages
rm -rf "$B/target"; unzip -q "$DL/target-3.13.16-0-arm64-v8a.zip" -d "$B/target"
cp "$B/target/jniLibs/arm64-v8a/"lib{python3.13,crypto_python,ssl_python,sqlite3_python}.so "$RT/lib/"
cp "$B/target/lib-dynload/arm64-v8a/"*.so "$RT/py/lib/python3.13/lib-dynload/"
unzip -q "$DL/target-3.13.16-0-stdlib.zip" -d "$RT/py/lib/python3.13"
for w in numpy-1.26.2 lz4-4.3.3 chaquopy_openblas chaquopy_libgfortran chaquopy_libcxx fonttools-4.67.0; do unzip -q -o "$DL/$w"*.whl -d "$SP"; done
mv "$SP/chaquopy/lib/"* "$RT/lib/"; rm -rf "$SP/chaquopy" "$SP"/chaquopy_*.dist-info
# 标准库里的构建配置记着上游构建机的路径：只留 arm64 那份，路径换成占位 | the build configuration in the standard library records the upstream build machine's paths: keep only the arm64 one, paths replaced by a placeholder
rm -f "$RT/py/lib/python3.13/_sysconfigdata__android_x86_64-linux-android.py"
sed -i.bak -E 's#/Users/[A-Za-z0-9._-]+#/build#g' "$RT/py/lib/python3.13/_sysconfigdata__android_aarch64-linux-android.py"; rm -f "$RT/py/lib/python3.13/"*.bak

# Python 的 zlib 模块改为静态链标准 zlib 1.3.1：系统 libz（安卓 13 的 1.2.11 等）在有 ARM CRC32 的真机上级别 9 压出的字节与电脑不同
#   （解压一样，但覆盖层 CASC 档就不逐字节一致了，奥丁实测）；标准 zlib 的输出与 Mac 上 Python 的一致
# Python's zlib module is rebuilt statically linked with stock zlib 1.3.1: the system libz (Android 13's 1.2.11 and others) compresses at level 9 to different bytes
#   than the computer on real devices with ARM CRC32 (same when decompressed, but the overlay CASC archive is then not byte-identical; measured on the Thor);
#   stock zlib gives the same output as Python on the Mac
t "Python zlib 模块（标准 zlib 1.3.1 静态链）"
rm -rf "$B/zlib-1.3.1" "$B/zmod"; tar -xzf "$DL/zlib-1.3.1.tar.gz" -C "$B"; mkdir -p "$B/zmod/clinic"; cp "$DL/zlibmodule.c" "$B/zmod/"; cp "$DL/zlibmodule.c.h" "$B/zmod/clinic/"
Z=$B/zlib-1.3.1
$CC -O2 -fPIC "$MAP" -w -shared -DPy_BUILD_CORE_MODULE -I"$B/target/include/python3.13" -I"$B/target/include/python3.13/internal" -I"$Z" "$B/zmod/zlibmodule.c" \
  $(for f in adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil; do echo "$Z/$f.c"; done) \
  -Wl,--exclude-libs,ALL -Wl,-Bsymbolic -L"$RT/lib" -lpython3.13 -Wl,-z,max-page-size=16384 -o "$RT/py/lib/python3.13/lib-dynload/zlib.cpython-313-aarch64-linux-android.so"

# 二、capstone（ctypes 加载包里 lib/libcapstone.so） | 2. capstone (ctypes loads lib/libcapstone.so inside the package)
t "capstone 5.0.7"
rm -rf "$B/capstone-5.0.7"; tar -xzf "$DL/capstone-5.0.7.tar.gz" -C "$B"
cmake -S "$B/capstone-5.0.7/src" -B "$B/capstone-5.0.7/b" "${TC[@]}" -DCAPSTONE_BUILD_SHARED=ON -DCAPSTONE_BUILD_STATIC=OFF -DCAPSTONE_BUILD_TESTS=OFF -DCAPSTONE_BUILD_CSTOOL=OFF -DBUILD_SHARED_LIBS=ON > "$B/capstone.log" 2>&1
ninja -C "$B/capstone-5.0.7/b" >> "$B/capstone.log" 2>&1
cp -R "$B/capstone-5.0.7/capstone" "$SP/capstone"; rm -rf "$SP/capstone/lib" "$SP/capstone/include"; mkdir -p "$SP/capstone/lib"; cp "$B/capstone-5.0.7/b/libcapstone.so" "$SP/capstone/lib/"

# 三、Pillow 13.0.0 + 依赖（同官方轮子：FreeType 先不带 HarfBuzz 编一次，编 HarfBuzz，再带 HarfBuzz 编 FreeType） | 3. Pillow 13.0.0 + dependencies (as the official wheels: FreeType without HarfBuzz, then HarfBuzz, then FreeType again with HarfBuzz)
t "zlib-ng / FreeType / HarfBuzz / Pillow 13.0.0"
rm -rf "$PFX" "$B/zlib-ng-2.3.3" "$B/freetype-2.14.3" "$B/harfbuzz-14.5.0" "$B/pillow-13.0.0"
for f in zlib-ng-2.3.3.tar.gz freetype-2.14.3.tar.gz harfbuzz-14.5.0.tar.xz pillow-13.0.0.tar.gz; do tar -xf "$DL/$f" -C "$B"; done
cmake -S "$B/zlib-ng-2.3.3" -B "$B/zlib-ng-2.3.3/b" "${TC[@]}" -DCMAKE_INSTALL_PREFIX=$PFX -DZLIB_COMPAT=ON -DBUILD_SHARED_LIBS=OFF -DZLIB_ENABLE_TESTS=OFF -DZLIBNG_ENABLE_TESTS=OFF -DWITH_GTEST=OFF > "$B/pil.log" 2>&1
ninja -C "$B/zlib-ng-2.3.3/b" install >> "$B/pil.log" 2>&1
FT=(-DCMAKE_INSTALL_PREFIX=$PFX -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_REQUIRE_ZLIB=ON -DZLIB_INCLUDE_DIR=$PFX/include -DZLIB_LIBRARY=$PFX/lib/libz.a)
cmake -S "$B/freetype-2.14.3" -B "$B/freetype-2.14.3/b1" "${TC[@]}" "${FT[@]}" -DFT_DISABLE_HARFBUZZ=ON >> "$B/pil.log" 2>&1; ninja -C "$B/freetype-2.14.3/b1" install >> "$B/pil.log" 2>&1
python3 -m venv "$B/venv" && "$B/venv/bin/pip" -q install meson==1.11.2 ninja pkgconf
cat > "$B/cross-android.txt" <<X
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$BIN/llvm-ar'
strip = '$STRIP'
pkg-config = '$B/venv/bin/pkg-config'
[built-in options]
c_args = ['-fPIC', '$MAP']
cpp_args = ['-fPIC', '$MAP']
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
X
(cd "$B/harfbuzz-14.5.0" && PKG_CONFIG_LIBDIR=$PFX/lib/pkgconfig "$B/venv/bin/meson" setup b --cross-file "$B/cross-android.txt" --prefix=$PFX --libdir=lib --buildtype=minsize --default-library=static \
   -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dicu=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled -Dintrospection=disabled >> "$B/pil.log" 2>&1 \
 && PATH="$B/venv/bin:$PATH" "$B/venv/bin/meson" install -C b >> "$B/pil.log" 2>&1)
cmake -S "$B/freetype-2.14.3" -B "$B/freetype-2.14.3/b2" "${TC[@]}" "${FT[@]}" -DFT_REQUIRE_HARFBUZZ=ON -DFT_DYNAMIC_HARFBUZZ=OFF -DHarfBuzz_INCLUDE_DIR=$PFX/include/harfbuzz -DHarfBuzz_LIBRARY=$PFX/lib/libharfbuzz.a >> "$B/pil.log" 2>&1
ninja -C "$B/freetype-2.14.3/b2" install >> "$B/pil.log" 2>&1
# Pillow 的 C 扩展：源文件表取自它的 setup.py（_IMAGING、_LIB_IMAGING，Mode.c 是每个扩展都链的 pil_imaging_mode）；静态库符号不导出，免得被进程里系统的 libz 抢绑定
# Pillow's C extensions: the source lists come from its setup.py (_IMAGING, _LIB_IMAGING; Mode.c is pil_imaging_mode, linked into every extension); static library symbols are not exported, so the system libz in the process cannot take over the bindings
P=$B/pillow-13.0.0; PO=$B/pillow-out; rm -rf "$PO"; mkdir -p "$PO/PIL" "$PO/obj"
CFL=(-O3 -fPIC "$MAP" -DNDEBUG -fno-strict-overflow -Wno-unused-result -w -I"$B/target/include/python3.13" -I"$PFX/include" -I"$PFX/include/freetype2" -I"$P/src/libImaging" -DHAVE_LIBZ '-DPILLOW_VERSION="13.0.0"')
LDF=(-shared -Wl,--exclude-libs,ALL -Wl,-Bsymbolic -L"$RT/lib" -lpython3.13 -lm -Wl,-z,max-page-size=16384)
LIBI=$(python3 -c "import re,sys; s=open(sys.argv[1]).read(); a=s.index('_LIB_IMAGING = ('); print(' '.join(re.findall(r'\"(\w+)\"', s[a:s.index(')', a)])))" "$P/setup.py")
$CC "${CFL[@]}" -c "$P/src/libImaging/Mode.c" -o "$PO/obj/mode.o"; objs=("$PO/obj/mode.o")
for f in _imaging decode encode map display outline path; do $CC "${CFL[@]}" -c "$P/src/$f.c" -o "$PO/obj/$f.o"; objs+=("$PO/obj/$f.o"); done
for f in $LIBI; do $CC "${CFL[@]}" -c "$P/src/libImaging/$f.c" -o "$PO/obj/li_$f.o"; objs+=("$PO/obj/li_$f.o"); done
EXT=cpython-313-aarch64-linux-android.so
$CC "${objs[@]}" "$PFX/lib/libz.a" "${LDF[@]}" -o "$PO/PIL/_imaging.$EXT"
$CC "${CFL[@]}" -c "$P/src/_imagingft.c" -o "$PO/obj/ft.o"
$CXX "$PO/obj/ft.o" "$PO/obj/mode.o" "$PFX/lib/libfreetype.a" "$PFX/lib/libharfbuzz.a" "$PFX/lib/libfreetype.a" "$PFX/lib/libz.a" "${LDF[@]}" -static-libstdc++ -o "$PO/PIL/_imagingft.$EXT"
$CC "${CFL[@]}" "$P/src/_imagingmath.c" "$PO/obj/mode.o" "${LDF[@]}" -o "$PO/PIL/_imagingmath.$EXT"
$CC "${CFL[@]}" "$P/src/_imagingmorph.c" "$PO/obj/mode.o" "${LDF[@]}" -o "$PO/PIL/_imagingmorph.$EXT"
cp "$P/src/PIL/"*.py "$PO/PIL/"; cp -R "$PO/PIL" "$SP/PIL"

# 四、SPIRV-Tools（上游对安卓只出库，放开可执行文件） | 4. SPIRV-Tools (upstream builds only libraries for Android; executables enabled here)
t "SPIRV-Tools $SPVT"
if [ ! -d "$B/SPIRV-Tools/.git" ]; then git clone -q https://github.com/KhronosGroup/SPIRV-Tools.git "$B/SPIRV-Tools"; git clone -q https://github.com/KhronosGroup/SPIRV-Headers.git "$B/SPIRV-Tools/external/spirv-headers"; fi
git -C "$B/SPIRV-Tools" checkout -q -f $SPVT; git -C "$B/SPIRV-Tools/external/spirv-headers" fetch -q origin; git -C "$B/SPIRV-Tools/external/spirv-headers" checkout -q -f $SPVH
sed -i.bak 's/^if (IOS OR ANDROID)$/if (IOS)/' "$B/SPIRV-Tools/CMakeLists.txt"
cmake -S "$B/SPIRV-Tools" -B "$B/SPIRV-Tools/b" "${TC[@]}" -DANDROID_STL=c++_static -DSPIRV_SKIP_TESTS=ON -DSPIRV_WERROR=OFF -DSPIRV_BUILD_FUZZER=OFF > "$B/spvt.log" 2>&1
ninja -C "$B/SPIRV-Tools/b" spirv-opt spirv-val >> "$B/spvt.log" 2>&1

# 五、着色器翻译工具：NativeAOT 出 linux-bionic-arm64（源码复制到 ASCII 路径，NDK clang 当链接器） | 5. The shader translator: NativeAOT for linux-bionic-arm64 (sources copied to an ASCII path, NDK clang as the linker)
t "dmsh_xlate（NativeAOT linux-bionic-arm64）"
XS=$B/xlate-src; rm -rf "$XS"; mkdir -p "$XS"; cp -R "$DM/gfx/tools/dmsh_xlate" "$XS/"; cp -R "$DM/third_party/ryujinx" "$XS/"
XSHA=$(cat "$DM/gfx/tools/dmsh_xlate/Program.cs" "$DM/gfx/tools/dmsh_xlate/dmsh_xlate.csproj" | shasum -a 256 | cut -c1-16)   # 同 dmsh_xlate --source-sha 的口径 | same convention as dmsh_xlate --source-sha
dotnet publish "$XS/dmsh_xlate" -c Release -r linux-bionic-arm64 -p:RyuSrc="$XS/ryujinx" -p:PublishAot=true -p:DisableUnsupportedError=true -p:PublishAotUsingRuntimePack=true \
  -p:CppCompilerAndLinker="$CC" -p:ObjCopyName="$BIN/llvm-objcopy" -o "$B/xlate-out" > "$B/xlate.log" 2>&1

# 六、Python 启动器（Py_BytesMain） | 6. The Python launcher (Py_BytesMain)
t "启动器"
printf 'extern int Py_BytesMain(int argc, char **argv);\nint main(int argc, char **argv) { return Py_BytesMain(argc, argv); }\n' > "$B/dmpy.c"
J=$OUT/jniLibs/arm64-v8a; rm -rf "$OUT/jniLibs" "$OUT/assets/dmgen"; mkdir -p "$J" "$OUT/assets/dmgen"
$CC -O2 "$MAP" "$B/dmpy.c" -L"$RT/lib" -lpython3.13 -Wl,-z,max-page-size=16384 -o "$J/libdmgen_py.so"
cp "$B/xlate-out/dmsh_xlate" "$J/libdmgen_xlate.so"
$STRIP -o "$J/libdmgen_spvopt.so" "$B/SPIRV-Tools/b/tools/spirv-opt"; $STRIP -o "$J/libdmgen_spvval.so" "$B/SPIRV-Tools/b/tools/spirv-val"

# 七、精简（测试、IDLE、Tk 等用不到）并打包 | 7. Trim (tests, IDLE, Tk and the like are unused) and pack
t "打包"
L=$RT/py/lib/python3.13
rm -rf "$L"/{test,idlelib,tkinter,turtledemo,ensurepip,pydoc_data,lib2to3} "$SP"/numpy/*/tests "$SP"/numpy/tests "$SP"/fontTools/*/tests
find "$RT" -name __pycache__ -prune -exec rm -rf {} +
find "$RT" -name '*.so' -o -name '*.so.*' | while read -r f; do $STRIP "$f" 2>/dev/null || true; done
leak=$(LC_ALL=C grep -raoE -- "/Users/[A-Za-z0-9._-]+|$HOME" "$RT" "$J" | head -3 || true); [ -z "$leak" ] || { echo "运行时里带着本机路径：$leak"; exit 1; }
(cd "$RT" && find . -type f | LC_ALL=C sort | sed 's#^\./##' | COPYFILE_DISABLE=1 zip -q -X -@ "$OUT/assets/dmgen/runtime.zip")
{ echo "python=3.13.16-0 zlib(python)=1.3.1 numpy=1.26.2 lz4=4.3.3 capstone=5.0.7 pillow=13.0.0 freetype=2.14.3 harfbuzz=14.5.0 zlib-ng=2.3.3 fonttools=4.67.0"
  echo "spirv-tools=$SPVT spirv-headers=$SPVH"
  echo "xlate_source_sha=$XSHA"
  echo "script_sha=$(shasum -a 256 "$0" | cut -c1-16)"   # shell_apk.sh 据此与 xlate_source_sha 判断能否复用这份运行时 | shell_apk.sh reuses this runtime only when this and xlate_source_sha still match
  (cd "$OUT" && shasum -a 256 assets/dmgen/runtime.zip jniLibs/arm64-v8a/*.so)
} > "$OUT/assets/dmgen/runtime.txt"
cat "$OUT/assets/dmgen/runtime.txt"; du -sh "$OUT/assets/dmgen/runtime.zip" "$J"
t "完成：$OUT"
