#!/usr/bin/env bash
# 编译并运行 GenScreenKeysTest（不依赖 Android SDK）。JAVA_HOME 指向 JDK 17+ 时用它的 javac / java。
# Compile and run GenScreenKeysTest (no Android SDK needed). With JAVA_HOME pointing at JDK 17+, its javac / java are used.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../app/src/main/java/com/xiaoboluo/d2r"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/dm-genkeys-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
JAVAC="${JAVAC:-javac}"; JAVA="${JAVA:-java}"
if [[ -n "${JAVA_HOME:-}" ]]; then JAVAC="$JAVA_HOME/bin/javac"; JAVA="$JAVA_HOME/bin/java"; fi
"$JAVAC" -encoding UTF-8 -d "$OUT" "$SRC/GenScreenKeys.java" "$HERE/GenScreenKeysTest.java"
"$JAVA" -cp "$OUT" com.xiaoboluo.d2r.GenScreenKeysTest
