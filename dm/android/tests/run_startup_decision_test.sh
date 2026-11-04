#!/usr/bin/env bash
# 编译并运行 StartupDecisionTest：直接调 StartupDecision.decide / contentReady，不依赖 Android SDK。
# 用法：bash dm/android/tests/run_startup_decision_test.sh（JAVA_HOME 指向 JDK 17+ 时自动用它的 javac / java）。
# Compile and run StartupDecisionTest: it calls StartupDecision.decide / contentReady directly and needs no Android SDK.
# Usage: bash dm/android/tests/run_startup_decision_test.sh (with JAVA_HOME pointing at JDK 17+, its javac / java are used).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../app/src/main/java/com/xiaoboluo/d2r"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/dm-startup-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

JAVAC="${JAVAC:-javac}"
JAVA="${JAVA:-java}"
if [[ -n "${JAVA_HOME:-}" ]]; then
    JAVAC="$JAVA_HOME/bin/javac"
    JAVA="$JAVA_HOME/bin/java"
fi

"$JAVAC" -encoding UTF-8 -d "$OUT" \
    "$SRC/StartupDecision.java" \
    "$SRC/PackGenerator.java" \
    "$HERE/StartupDecisionTest.java"
"$JAVA" -cp "$OUT" com.xiaoboluo.d2r.StartupDecisionTest
