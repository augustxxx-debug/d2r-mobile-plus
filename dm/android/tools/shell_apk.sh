#!/bin/bash
# 出安卓的发布包：一个不含任何游戏内容的 APK。玩家用自己的游戏文件在电脑上做导入包（dm/tools/d2rm_android.py），拷进手机，App 打开时导入。
#   shell_apk.sh [release] [gradle 参数…]      例：shell_apk.sh -PdmAppId=org.example.test（换包名并排装）
#   主包缺省带手机端生成器（玩家不用电脑，App 首次打开自己生成导入包；生成器只是工具，仍不含游戏内容），见下面 DM_GEN_RUNTIME
#   缺省出调试包（可调试，测试用：art_compare.sh 要 run-as）；release = 发布用的正式包：release 构建、用正式钥匙签，钥匙与密码在本机
#   ${DM_ANDROID_SIGN:-~/.config/dm-android/release.env}（DM_ANDROID_KEYSTORE / DM_ANDROID_KEY_ALIAS / DM_ANDROID_KEY_PASS），不进仓库
# 产物（$DM_ROOT/build/android_shell/）：D2RMobile-android.apk；gen/ = 编进去的替身表；ref/ = 用本机游戏文件生成的真表（只用来核对，不发布）
# 做法同 iOS 壳（dm/ios/tools/shell_build.sh）：三份「从游戏文件生成、编进代码」的文件（经验表、编年史仓库表、技能图标索引）换成替身，
#   运行时从导入的 files/dm/derived.tsv 读（dm/host/dm_derived_rt.h）；亮度表 dm_bright_levels.inc 照 iOS 壳编进去（每个关卡号一位：随不随昼夜）。
# 游戏程序、着色器、覆盖层、界面素材本来就不进 APK（运行时在 files/dm/）。出包这台机器要有游戏文件：要用真实的生成结果定表的大小。
# 在源码副本里构建（$O/tree，只含仓库跟踪的与未被忽略的文件）：工作树里平时构建留下的真表（dm/host/dm_vault_tab.h 等）会被
#   #include "…" 先于生成目录找到，副本里没有它们。
# 环境：JAVA_HOME（JDK 17+）、ANDROID_HOME（SDK 36 + NDK 29.0.14206865 + CMake 3.31.6）；缺省取 Homebrew 的 openjdk@17 与 ~/Library/Android/sdk
# Build the Android release package: an APK with no game content. Players make the import pack on a computer from their own game files (dm/tools/d2rm_android.py), copy it to the phone, and the App imports it when opened.
#   shell_apk.sh [release] [gradle arguments...]      e.g. shell_apk.sh -PdmAppId=org.example.test (another application id, to install side by side)
#   The main package carries the on-phone generator by default (no computer needed: the App makes the import pack itself on first launch; the generator is only tools, still no game content), see DM_GEN_RUNTIME below
#   Default is a debug package (debuggable, for testing: art_compare.sh needs run-as); release = the package for publishing: a release build signed with the release key, whose key and password live on this machine in
#   ${DM_ANDROID_SIGN:-~/.config/dm-android/release.env} (DM_ANDROID_KEYSTORE / DM_ANDROID_KEY_ALIAS / DM_ANDROID_KEY_PASS), never in the repository
# Products ($DM_ROOT/build/android_shell/): D2RMobile-android.apk; gen/ = the stand-in tables compiled in; ref/ = the real tables generated from this machine's game files (only for checking, never released)
# Same approach as the iOS shell (dm/ios/tools/shell_build.sh): the three files "generated from game files and compiled in" (experience table, chronicle vault table, skill icon index) are replaced by stand-ins
#   that read the imported files/dm/derived.tsv at run time (dm/host/dm_derived_rt.h); the brightness table dm_bright_levels.inc is compiled in as in the iOS shell (one bit per level number: follows day and night or not).
# The game program, shaders, overlay and UI art never go into the APK (they are in files/dm/ at run time). The building machine needs the game files: the real generated results set the table sizes.
# Builds in a copy of the source ($O/tree, only tracked and non-ignored files): the real tables a normal build leaves in the working tree (dm/host/dm_vault_tab.h and others) would be
#   found by #include "..." before the generated directory; the copy does not have them.
# Environment: JAVA_HOME (JDK 17+), ANDROID_HOME (SDK 36 + NDK 29.0.14206865 + CMake 3.31.6); default Homebrew's openjdk@17 and ~/Library/Android/sdk
set -u
KIND=debug; [ "${1:-}" = release ] && { KIND=release; shift; }
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); DM=$(cd "$HERE/../.." && pwd)
ROOT=${DM_ROOT:-$HOME/dm-work}; O=$ROOT/build/android_shell; G=$O/gen
ROMFS=${DM_ROMFS_SRC:-$ROOT/data/switch/3.3.94012/Update/RomFS}
PY=${DM_PYTHON:-$ROOT/build/cache/dm/venv/bin/python}
export JAVA_HOME=${JAVA_HOME:-$(brew --prefix openjdk@17 2> /dev/null)/libexec/openjdk.jdk/Contents/Home}
export ANDROID_HOME=${ANDROID_HOME:-$HOME/Library/Android/sdk}; export ANDROID_SDK_ROOT=$ANDROID_HOME
t() { echo "[$(date +%T)] $*"; }; die() { t "🔴 $*"; exit 1; }
[ -f "$ROMFS/data/build_manifest.psv" ] || die "没找到游戏文件 ${ROMFS}（DM_ROMFS_SRC）"
W=$O/tree
rm -rf "$G" "$O/ref" "$O/D2RMobile-android.apk" && mkdir -p "$G" "$O/ref" "$W" || die "建不了 $O"
# 用 tar 拷（不用 rsync：macOS 自带的 openrsync 没有 --files-from=- 配 -0 / --ignore-missing-args 这几项，Linux 的 rsync 才有；bsdtar 与 GNU tar 都认 --null -T -）
# Copy with tar (not rsync: the openrsync that ships with macOS lacks --files-from=- with -0 / --ignore-missing-args, only Linux's rsync has them; bsdtar and GNU tar both accept --null -T -)
(cd "$DM/.." && git ls-files -z -co --exclude-standard dm | tar -c --null -T - -f - | tar -x -f - -C "$W") || die "拷源码副本失败"
for f in host/dm_vault_tab.h host/dm_hud_exp.h host/dm_bright_levels.inc ios/Sources/dm_touch_native_art.c; do [ ! -e "$W/dm/$f" ] || die "副本里有生成文件 $f"; done
t "用本机游戏文件生成真表"
"$PY" "$DM/ios/tools/gen_vault_table.py" "$ROMFS" > "$G/dm_vault_tab.h" || die "生成 dm_vault_tab.h 失败"
"$PY" "$DM/ios/tools/gen_hud_exp.py" "$ROMFS" > "$G/dm_hud_exp.h" || die "生成 dm_hud_exp.h 失败"
"$PY" "$DM/tools/gen_bright_levels.py" "$ROMFS" > "$G/dm_bright_levels.inc" || die "生成 dm_bright_levels.inc 失败"
"$PY" "$DM/ios/tools/native_art/generate.py" "$ROMFS" --output-dir "$O/ref/native_art" > /dev/null || die "生成技能图标索引失败"
cp "$O/ref/native_art/dm_touch_native_art.c" "$G/" && cp "$G/dm_vault_tab.h" "$G/dm_hud_exp.h" "$G/dm_touch_native_art.c" "$O/ref/" || die "拷真表失败"
"$PY" "$DM/ios/tools/shell_stubs.py" data-gen "$G" "$O/ref/derived.tsv" || die "拆表失败"
"$PY" "$DM/ios/tools/shell_stubs.py" stubs-gen "$G" || die "写替身失败"
# 版本号在 git 检出里算（源码副本没有 git 会退回 0.0.0 / 20000）：整套传给 gradle；version.sh 说不能出包就停
# The version is computed in the git checkout (the source copy has no git and would fall back to 0.0.0 / 20000) and passed to gradle as a set; stop when version.sh says the package must not be built
eval "$(bash "$DM/tools/version.sh")"; [ -z "${DM_VERSION_ERROR:-}" ] || die "version.sh：$DM_VERSION_ERROR"; [ -z "${DM_VERSION_WARN:-}" ] || t "version.sh：$DM_VERSION_WARN"
t "版本 $DM_VERSION 构建号 $DM_BUILD（$DM_BUILD_SOURCE）显示 $DM_VERSION_LABEL"
VER=(-PdmVersionName="$DM_VERSION" -PdmBuildNumber="$DM_BUILD" -PdmVersionLabel="$DM_VERSION_LABEL")
# 更新清单地址（加载页页脚的官方免费渠道，按顺序试：自己的服务器 → GitHub raw）：出包时由 DM_UPDATE_URLS 给（空格分隔，只许 https://），不进源码；没给 = 包里不联网
# Update manifest addresses (official free channels in the loading page footer, tried in order: own server → GitHub raw): given at packaging time by DM_UPDATE_URLS
# (space separated, https:// only), never in the source; none = the package makes no network access
if [ -n "${DM_UPDATE_URLS:-}" ]; then VER+=(-PdmUpdateUrls="$DM_UPDATE_URLS"); t "清单地址 $(echo $DM_UPDATE_URLS | wc -w | tr -d ' ') 个，按顺序：$(for u in $DM_UPDATE_URLS; do printf '%s ' "$(echo "$u" | sed -E 's#^https://([^/]+).*#\1#')"; done)"
else t "没给 DM_UPDATE_URLS：包里不联网，加载页不显示官方免费渠道"; fi
# 手机端生成器缺省编进主包（同一个 App：没有导入包就自己生成）。运行时 = gen_runtime.sh 的输出，脚本与翻译工具源码都没变就复用，否则重编（约 5 分钟，要网）；
#   DM_GEN_RUNTIME=<目录> 指定输出位置，DM_GEN_RUNTIME=none 出不带生成器的包（只认电脑做的导入包）；工作目录 DM_GEN_WORK 要 ASCII 路径
# The on-phone generator goes into the main package by default (one App: it makes the import pack itself when there is none). The runtime is the output of gen_runtime.sh,
#   reused while neither the script nor the translator sources changed, otherwise rebuilt (about 5 minutes, needs the network);
#   DM_GEN_RUNTIME=<dir> sets where it goes, DM_GEN_RUNTIME=none builds a package without the generator (only computer-made import packs); the work directory DM_GEN_WORK must be an ASCII path
GR=${DM_GEN_RUNTIME:-$HOME/dm-android/genrt}; GEN=()
if [ "$GR" != none ]; then
  XS=$(cat "$DM/gfx/tools/dmsh_xlate/Program.cs" "$DM/gfx/tools/dmsh_xlate/dmsh_xlate.csproj" | shasum -a 256 | cut -c1-16); SS=$(shasum -a 256 "$HERE/gen_runtime.sh" | cut -c1-16)
  if grep -qx "xlate_source_sha=$XS" "$GR/assets/dmgen/runtime.txt" 2> /dev/null && grep -qx "script_sha=$SS" "$GR/assets/dmgen/runtime.txt"; then t "手机端生成器运行时：复用 $GR"
  else t "手机端生成器运行时：重编到 $GR"; bash "$HERE/gen_runtime.sh" "${DM_GEN_WORK:-$HOME/dm-android/genrt-work}" "$GR" > "$O/gen_runtime.log" 2>&1 || die "编运行时失败，日志 $O/gen_runtime.log"; fi
  GEN=(-PdmGenRuntime="$GR")
  # 开源楷体随包（手机端生成覆盖层时替换中日文字形）：缺省霞鹜文楷 Medium v1.530（SIL OFL 1.1，许可证同目录 OFL.txt），加粗 0 档（观感对齐原先私有楷体的 3 档）；
  #   DM_ANDROID_FONT=<.ttf> 换字体（同目录要有 OFL.txt 或用 DM_ANDROID_FONT_LICENSE 指定），DM_ANDROID_FONT=none 不带（原版字）；字体文件不进仓库
  # The open-licence Kai font goes into the package (the on-phone overlay step uses it for CJK glyphs): LXGW WenKai Medium v1.530 by default (SIL OFL 1.1, licence OFL.txt
  #   next to it), stroke level 0 (matching the look of the former private Kai font at level 3); DM_ANDROID_FONT=<.ttf> picks another font (OFL.txt next to it, or
  #   DM_ANDROID_FONT_LICENSE), DM_ANDROID_FONT=none ships none (original typeface); font files never enter the repository
  FONT=${DM_ANDROID_FONT:-$HOME/dm-android/fonts/lxgw-wenkai-v1.530/LXGWWenKai-Medium.ttf}
  if [ "$FONT" != none ]; then
    FLIC=${DM_ANDROID_FONT_LICENSE:-$(dirname "$FONT")/OFL.txt}; [ -f "$FONT" ] && [ -f "$FLIC" ] || die "字体或许可证不在：$FONT / $FLIC（DM_ANDROID_FONT=none 不带字体）"
    GEN+=(-PdmFont="$FONT" -PdmFontLicense="$FLIC" -PdmFontWeight="${DM_ANDROID_FONT_WEIGHT:-0}"); t "随包字体：$(basename "$FONT")（sha256 $(shasum -a 256 "$FONT" | cut -c1-16)…），加粗 ${DM_ANDROID_FONT_WEIGHT:-0} 档"
  fi
fi
t "构建 APK"
if [ $KIND = release ]; then
  SIGN=${DM_ANDROID_SIGN:-$HOME/.config/dm-android/release.env}; [ -f "$SIGN" ] || die "没有正式钥匙配置 $SIGN"
  set -a; . "$SIGN"; set +a; [ -f "${DM_ANDROID_KEYSTORE:-}" ] && [ -n "${DM_ANDROID_KEY_PASS:-}" ] || die "钥匙配置不全：$SIGN"
  # 发布钥匙从此固定（2026-10-28 生成，证书 SHA-256 下面这串）：换钥匙签出来的包，已装的用户不能覆盖升级。钥匙库对不上就不出包，也不另生成新钥匙；口令只走环境变量，不上命令行
  # The release key is fixed from now on (made 2026-10-28, certificate SHA-256 below): a package signed with another key cannot upgrade existing installs. A keystore that does not
  # match stops the build and no new key is ever generated; the password goes through an environment variable, never the command line
  PIN=${DM_ANDROID_CERT_SHA256:-b4af06c165dd8699ab9f2b0dec8f6c2de05cd4d01f510fe63778eac6898a52da}
  have=$("$JAVA_HOME/bin/keytool" -list -v -keystore "$DM_ANDROID_KEYSTORE" -storepass:env DM_ANDROID_KEY_PASS 2>/dev/null | sed -n 's/^.*SHA256: //p' | head -1 | tr -d ':' | tr 'A-F' 'a-f')
  [ "$have" = "$PIN" ] || die "钥匙库不是固定的发布钥匙（钥匙库 ${have:-读不出} / 应 $PIN）：用备份恢复，别换钥匙"
  TASK=:app:assembleRelease; OUT=$W/dm/android/app/build/outputs/apk/release/app-release.apk
else unset DM_ANDROID_KEYSTORE; TASK=:app:assembleDebug; OUT=$W/dm/android/app/build/outputs/apk/debug/app-debug.apk; fi
rm -f "$OUT"   # 增量打包会在旧 APK 上追加、留下空洞（实测 25 MB 的内容出了 46 MB），整份重写 | incremental packaging appends to the old APK and leaves holes (25 MB of content came out as 46 MB in a test); rewrite it whole
gradle --no-daemon -q -p "$W/dm/android" $TASK -PdmGenDir="$G" -PdmPython="$PY" "${VER[@]}" "${GEN[@]}" "$@" > "$O/build.log" 2>&1 || { grep -E "error|FAIL" "$O/build.log" | head -8; die "构建失败，日志 $O/build.log"; }
cp "$OUT" "$O/D2RMobile-android.apk" || die "没找到构建出的 APK"
# 核对：APK 里不许有游戏内容——真表里的字符串（编年史仓库的物品键与图标路径，只取源码里本来没有的：多语表等会带物品名）不能出现在原生库里，
#   也不许带游戏程序 / 着色器包 / 覆盖层
# Check: no game content in the APK -- strings from the real tables (the chronicle vault's item keys and icon paths, only those not already in the source: the string table and others carry item names) must not appear in the native library,
#   and no game program / shader bundles / overlay may be packed
A=$O/D2RMobile-android.apk; L=$O/libdm.so; unzip -p "$A" lib/arm64-v8a/libdm.so > "$L" || die "APK 里没有 lib/arm64-v8a/libdm.so"
n=0; hit=0
while IFS= read -r k; do [ ${#k} -ge 6 ] || continue; git -C "$DM" grep -qF -- "$k" -- . && continue
  n=$((n + 1)); LC_ALL=C grep -qaF -- "$k" "$L" && { hit=$((hit + 1)); echo "  原生库里有：$k"; }
done < <(awk -F'\t' '$1=="VTROW"{print $8; print $9}' "$O/ref/derived.tsv" | sort -u)
[ "$n" -ge 100 ] || die "能核的字符串只有 $n 个，核不了"
[ "$hit" = 0 ] || die "原生库里出现了真表里的字符串（$hit 处 / 核了 $n 个）：替身没生效"
# 也不许带本机路径（用户名 / 目录结构）：整个 APK 解开逐个文件查（CMakeLists.txt 用 -ffile-prefix-map 换掉了编译路径）
X=$O/apk_x; rm -rf "$X" && mkdir -p "$X" && (cd "$X" && unzip -q "$A") || die "解不开 APK"
leak=$(LC_ALL=C grep -raoE -- "/Users/[A-Za-z0-9._-]+|$HOME" "$X" | head -3)
[ -z "$leak" ] || die "APK 里带着本机路径：$leak"
# 正式包：签名必须是正式钥匙（证书指纹与钥匙库一致、主体只有项目名），且不可调试
# Release package: the signature must be the release key (certificate fingerprint equal to the keystore's, subject only the project name), and it must not be debuggable
if [ $KIND = release ]; then
  BT=$(ls -d "$ANDROID_HOME"/build-tools/* | tail -1)
  got=$("$BT/apksigner" verify --print-certs "$A" 2>&1 | sed -n 's/.*certificate SHA-256 digest: //p' | head -1)
  want=$("$JAVA_HOME/bin/keytool" -list -v -keystore "$DM_ANDROID_KEYSTORE" -storepass:env DM_ANDROID_KEY_PASS 2>/dev/null | sed -n 's/^.*SHA256: //p' | head -1 | tr -d ':' | tr 'A-F' 'a-f')
  [ -n "$got" ] && [ "$got" = "$want" ] && [ "$got" = "$PIN" ] || die "签名不是固定的发布钥匙（APK $got / 钥匙 ${want} / 应 $PIN）"
  dn=$("$BT/apksigner" verify --print-certs "$A" 2>&1 | sed -n 's/.*certificate DN: //p' | head -1); [ "$dn" = "CN=D2R Mobile, O=D2R Mobile" ] || die "证书主体不对：$dn"
  "$BT/aapt2" dump xmltree --file AndroidManifest.xml "$A" | grep -q 'android:debuggable' && die "正式包是可调试的"
fi
# 带手机端生成器（-PdmGenRuntime）时 assets/dmgen/ 只许有这几样：运行时（工具，gen_runtime.sh）、本仓生成脚本、4 个查询核（项目手写，键取自 nvn_builtin.h）；
#   两个 zip 解开再查一遍本机路径，运行时里只许 lib/ 与 py/
# With the on-phone generator (-PdmGenRuntime), assets/dmgen/ may hold only these: the runtime (tools, gen_runtime.sh), this repo's generator scripts, and the 4 query kernels
#   (written by the project, keys from nvn_builtin.h); both zips are unpacked and checked for local paths again, and the runtime may hold only lib/ and py/
QK=$(sed -n 's/.*{"\([0-9a-f]\{16\}\)", "q_[a-z0-9]*"}.*/\1/p' "$DM/gfx/nvn/nvn_builtin.h" | sort)
GEN=$(unzip -l "$A" | awk '{print $4}' | grep '^assets/dmgen/' | grep -vxE 'assets/dmgen/(runtime\.zip|runtime\.txt|src\.zip|src\.sha256|THIRD_PARTY_NOTICES\.txt)' | grep -vxE "assets/dmgen/queries/($(echo $QK | tr ' ' '|'))\.xgs" \
  | grep -vxE 'assets/dmgen/font/([A-Za-z0-9._-]+\.ttf|OFL\.txt|font\.txt)' | head -3)
[ -z "$GEN" ] || die "assets/dmgen/ 里有不认识的文件：$GEN"
# 带字体就必须带许可证（OFL 要求随字体分发许可证）| a bundled font must ship with its licence (OFL requires it)
if unzip -l "$A" | grep -qE 'assets/dmgen/font/[^/]+\.ttf$'; then unzip -l "$A" | grep -q 'assets/dmgen/font/OFL.txt' || die "带了字体却没有 OFL.txt"; t "随包字体与许可证：$(unzip -l "$A" | awk '{print $4}' | grep '^assets/dmgen/font/' | sed 's|assets/dmgen/font/||' | tr '\n' ' ')"; fi
if unzip -l "$A" | grep -q 'assets/dmgen/runtime.zip'; then
  Z=$O/dmgen_x; rm -rf "$Z" && mkdir -p "$Z/rt" "$Z/src" && unzip -q -o -d "$Z" "$A" 'assets/dmgen/*' && unzip -q "$Z/assets/dmgen/runtime.zip" -d "$Z/rt" && unzip -q "$Z/assets/dmgen/src.zip" -d "$Z/src" || die "解不开 assets/dmgen"
  [ -z "$(ls "$Z/rt" | grep -vxE 'lib|py')" ] || die "运行时里有 lib/ py/ 之外的东西：$(ls "$Z/rt")"
  leak=$(LC_ALL=C grep -raoE -- "/Users/[A-Za-z0-9._-]+|$HOME" "$Z" | head -3); [ -z "$leak" ] || die "assets/dmgen 里带着本机路径：$leak"
  # 随包的第三方运行时、翻译工具、字体都要有许可证随包（四审 A4-01）：清单必须在、与仓库那份相同，runtime.txt 里每个组件版本都写在清单里
  # The bundled third-party runtime, translator and font must ship with their licences (fourth audit A4-01): the notices must be there, equal the repository copy,
  #   and name every component version in runtime.txt
  N=$Z/assets/dmgen/THIRD_PARTY_NOTICES.txt; [ -f "$N" ] || die "带了手机端生成器却没有 THIRD_PARTY_NOTICES.txt（第三方许可证）"
  cmp -s "$N" "$DM/android/THIRD_PARTY_NOTICES.txt" || die "APK 里的 THIRD_PARTY_NOTICES.txt 与 dm/android/ 的不同"
  for kv in $(sed -n '1,2p' "$Z/assets/dmgen/runtime.txt"); do grep -qF "version: $kv" "$N" || die "THIRD_PARTY_NOTICES.txt 没写 $kv（改了运行时组件就重跑 dm/android/tools/third_party_notices.py）"; done
  t "带手机端生成器：assets/dmgen 核过（查询核 $(echo $QK | wc -w | tr -d ' ') 个、运行时只有 lib/ py/、无本机路径；第三方许可证清单在、$(sed -n '1,2p' "$Z/assets/dmgen/runtime.txt" | wc -w | tr -d ' ') 个组件版本对得上）"
fi
# 加载页素材四件（dm/assets/loadpage：背景两张由 App 图标合成，字标图与音乐自制 CC0）是唯一许可的 assets/ 顶层文件 | the four loading-page assets (self-made, CC0, dm/assets/loadpage) are the only allowed top-level assets/ files
bad=$(unzip -l "$A" | awk '{print $4}' | grep -v '^assets/dmgen/' | grep -vxE 'assets/loadpage_(bg_land\.jpg|bg_port\.jpg|logo\.png|loop\.m4a)' | grep -E '\.(xgs|patched\.bin|data)$|^assets/|overlay|derived\.tsv' | head -3)
[ -z "$bad" ] || die "APK 里带了不该有的文件：$bad"
BT2=$(ls -d "$ANDROID_HOME"/build-tools/* | tail -1); vc=$("$BT2/aapt" dump badging "$A" 2>/dev/null | sed -n "s/.*versionCode='\([0-9]*\)'.*versionName='\([^']*\)'.*/\1 \2/p" | head -1); [ "$vc" = "$DM_BUILD $DM_VERSION" ] || die "APK 版本号不对：$vc（应 $DM_BUILD $DM_VERSION）"
t "好了（${KIND}）：${A}（$(($(stat -f %z "$A") / 1048576)) MB）；版本 $DM_VERSION（$DM_BUILD）；核了真表里 $n 个源码里没有的字符串，原生库里 0 处；APK 里没有游戏程序 / 着色器包 / 覆盖层，也没有本机路径"
