#!/usr/bin/env bash
# 编译并运行 CommunityFetchTest：直接调 CommunityFetch，不依赖 Android SDK。
# 用法：bash dm/android/tests/run_community_fetch_test.sh（JAVA_HOME 指向 JDK 17+ 时自动用它的 javac / java）。
# Compile and run CommunityFetchTest: it calls CommunityFetch directly and needs no Android SDK.
# Usage: bash dm/android/tests/run_community_fetch_test.sh (with JAVA_HOME pointing at JDK 17+, its javac / java are used).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../app/src/main/java/com/xiaoboluo/d2r"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/dm-community-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

JAVAC="${JAVAC:-javac}"
JAVA="${JAVA:-java}"
if [[ -n "${JAVA_HOME:-}" ]]; then
    JAVAC="$JAVA_HOME/bin/javac"
    JAVA="$JAVA_HOME/bin/java"
fi

"$JAVAC" -encoding UTF-8 -d "$OUT" \
    "$SRC/CommunityFetch.java" \
    "$HERE/CommunityFetchTest.java"
"$JAVA" -cp "$OUT" com.xiaoboluo.d2r.CommunityFetchTest
