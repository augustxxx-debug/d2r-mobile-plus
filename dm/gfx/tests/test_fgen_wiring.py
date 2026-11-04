#!/usr/bin/env python3
# 帧生成 P1 调用点接线的源码形状闸门。
# 注入钩子（t_fgen 第 10 节）已经真的走 present_now / 出图线程，能抓住这些调用点的变异；只有显示节拍回调的 !submitted 分支
# 需要真的 CAMetalDisplayLink 与运行循环，单测驱动不了，所以在这里用源码形状把它锁住。
# 两处 !submitted 的处理已抽成共享函数 pq_real_end_unsubmitted：出图线程那一处由 t_fgen 第 10c 节的行为测试覆盖，
# 任何进到共享函数里的变异（例如改成 pq_finish）同样会被 t_fgen 抓住；本闸门只检查两处分支真的调用这个共享函数、
# 且共享函数只调 pq_real_end_locked，不直接推进 g_pq_done / 不调 pq_finish。匹配前先去掉注释和字符串字面量，
# 把原调用留在注释里或用别名绕过的写法都不能骗过闸门。
# Source-shape gate for the frame-generation P1 call-site wiring.
# The injection hooks (section 10 of t_fgen) really go through present_now / the present thread and catch mutations of those call sites; only the
# display-link callback's !submitted branch needs a real CAMetalDisplayLink and run loop, which the unit test cannot drive, so it is locked here by shape.
# Both !submitted branches now share pq_real_end_unsubmitted: the present-thread branch is covered behaviorally by section 10c of t_fgen,
# so any mutation inside the shared function (e.g. switching to pq_finish) is caught by t_fgen too; this gate checks that both branches really call
# the shared function and that the shared function calls only pq_real_end_locked, never advancing g_pq_done / calling pq_finish. Comments and string
# literals are stripped before matching, so "leave the original call in a comment" or an alias cannot fool the gate.
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "nvn" / "nvn_mtl.m"
raw = SRC.read_text(encoding="utf-8", errors="replace")
fail = 0
n = 0

SHARED = "pq_real_end_unsubmitted"


def strip_c(src):
    """把注释、字符串和字符字面量换成空白（保留换行），免得注释 / 字符串里的假调用骗过匹配。"""
    out = []
    i, size = 0, len(src)
    while i < size:
        c = src[i]
        if c == "/" and src[i + 1:i + 2] == "*":
            j = src.find("*/", i + 2)
            j = size if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in src[i:j]))
            i = j
        elif c == "/" and src[i + 1:i + 2] == "/":
            j = src.find("\n", i)
            j = size if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == '"' or c == "'":
            q = c
            j = i + 1
            while j < size:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == q:
                    j += 1
                    break
                j += 1
            out.append("".join(ch if ch == "\n" else " " for ch in src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def brace_block(s, open_idx):
    """返回 s[open_idx] 起的配对花括号块（s[open_idx] 必须是 '{'）。"""
    depth = 0
    for i in range(open_idx, len(s)):
        if s[i] == "{":
            depth += 1
        elif s[i] == "}":
            depth -= 1
            if depth == 0:
                return s[open_idx:i + 1]
    return ""


def squeeze(s):
    return re.sub(r"\s+", " ", s).strip()


code = strip_c(raw)


def check(name, ok, detail=""):
    global fail, n
    n += 1
    print(("%s  %s" % ("OK  " if ok else "FAIL", name)) + (("：" + detail) if detail and not ok else ""))
    if not ok:
        fail += 1


# 1. 两处 !submitted 分支（显示节拍回调 / 经典出图线程）都必须只调用共享函数 pq_real_end_unsubmitted(ticket)
branches = list(re.finditer(r"if \(!submitted\) \{", code))
check("!submitted 分支恰好两处", len(branches) == 2, "实际 %d 处" % len(branches))
for i, m in enumerate(branches):
    inner = squeeze(brace_block(code, m.end() - 1)[1:-1])
    check("第 %d 处 !submitted 只调用 %s(ticket)" % (i + 1, SHARED),
          inner == SHARED + "(ticket);", "实际 %r" % inner)

# 2. 共享函数必须存在，且只登记真帧了结（pq_real_end_locked），不得直接 pq_finish / 推进 g_pq_done / 换成别名
DECL = re.search(r"static void %s\(uint64_t ticket\) \{" % SHARED, code)
check("共享函数 %s 存在" % SHARED, bool(DECL))
if DECL:
    body = squeeze(brace_block(code, DECL.end() - 1)[1:-1])
    expect = ("pthread_mutex_lock(&g_pq_mu); pq_real_end_locked(ticket); "
              "pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);")
    check("%s 只调 pq_real_end_locked(ticket)" % SHARED, body == expect, "实际 %r" % body)
    check("%s 不调 pq_finish / 不碰 g_pq_done" % SHARED,
          "pq_finish" not in body and "g_pq_done" not in body)

# 3. present_now 里每一个开了生成阶段之后的提前 return 0 都必须先 pq_gen_close
check("present_now 真帧取不到 drawable 时关生成阶段",
      "if (!dr) { if (fg_gen_open) pq_gen_close(ticket); return 0; }" in code)
check("present_now 真帧编码失败时关生成阶段",
      bool(re.search(r"if \(!present_encode_m4\([^\n]*\|\| FGEN_REAL_ENCODE_FAILED\(\)\) \{[^\n]*if \(fg_gen_open\) pq_gen_close\(ticket\);", code)))
# 4. 外推末尾（真帧提交之后）必须关生成阶段
check("present_now 外推末尾关生成阶段",
      "if (fg_gen_open) { pq_gen_close(ticket); fg_gen_open = 0; }" in code)
# 5. 防御：队列深度必须小于水位环长
check("_Static_assert(PQ < NVN_PQ_RING)", bool(re.search(r"_Static_assert\(PQ < NVN_PQ_RING", code)))
# 6. 旧票号带在途状态时要报红
check("pq_gen_slot_locked 旧票号告警", bool(re.search(r"if \(s->open \|\| s->out\) RED_ONCE\(", code)))

print("== %d 项，失败 %d ==" % (n, fail))
sys.exit(1 if fail else 0)
