#!/usr/bin/env bash
# 编译并运行 S2xZipTest：直接调 S2xZip.find / plan / extract / spaceNeeded，不依赖 Android SDK。
# 用法：bash dm/android/tests/run_s2x_zip_test.sh（JAVA_HOME 指向 JDK 17+ 时自动用它的 javac / java）。
# Compile and run S2xZipTest: it calls S2xZip.find / plan / extract / spaceNeeded directly and needs no Android SDK.
# Usage: bash dm/android/tests/run_s2x_zip_test.sh (with JAVA_HOME pointing at JDK 17+, its javac / java are used).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../app/src/main/java/com/xiaoboluo/d2r"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/dm-s2x-zip-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

JAVAC="${JAVAC:-javac}"
JAVA="${JAVA:-java}"
if [[ -n "${JAVA_HOME:-}" ]]; then
    JAVAC="$JAVA_HOME/bin/javac"
    JAVA="$JAVA_HOME/bin/java"
fi

"$JAVAC" -encoding UTF-8 -d "$OUT" \
    "$SRC/S2xZip.java" "$SRC/PackGenerator.java" \
    "$HERE/S2xZipTest.java"
# 测试也在 TMPDIR 里造包 | the test builds its packs under TMPDIR too
"$JAVA" -Djava.io.tmpdir="${TMPDIR:-/tmp}" -cp "$OUT" com.xiaoboluo.d2r.S2xZipTest
