package com.xiaoboluo.d2r;

// 生成失败页 / 熔断页手柄 A 的去向：直接调 GenScreenKeys.aTarget（terra B134 增量终审 P2：焦点在「分享」上按 A 也去重试）。
// Where controller A goes on the failure / breaker page: calls GenScreenKeys.aTarget directly (terra B134 incremental review P2: A on a focused Share still retried).
public class GenScreenKeysTest {
    static int fails, checks;
    static void eq(int want, int got, String what) { checks++; if (want != got) { fails++; System.out.println("  ✗ " + what + "：期望 " + want + "，实际 " + got); } }

    public static void main(String[] a) {
        final int N = GenScreenKeys.NONE, S = GenScreenKeys.SHARE, R = GenScreenKeys.RETRY;
        // 熔断页（两个按钮都在）| breaker page (both buttons)
        eq(S, GenScreenKeys.aTarget(S, true, true), "熔断页焦点在分享：A = 分享");
        eq(R, GenScreenKeys.aTarget(R, true, true), "熔断页焦点在再试一次：A = 再试一次");
        eq(R, GenScreenKeys.aTarget(N, true, true), "熔断页无焦点：缺省 = 再试一次");
        // 普通失败页（只有分享）| plain failure page (Share only)
        eq(S, GenScreenKeys.aTarget(S, true, false), "失败页焦点在分享：A = 分享");
        eq(S, GenScreenKeys.aTarget(N, true, false), "失败页无焦点：缺省 = 分享");
        eq(S, GenScreenKeys.aTarget(R, true, false), "焦点记成不存在的再试一次：按页面缺省 = 分享");
        // 只有再试一次 | Try again only
        eq(R, GenScreenKeys.aTarget(S, false, true), "焦点记成不存在的分享：按页面缺省 = 再试一次");
        eq(N, GenScreenKeys.aTarget(N, false, false), "页上没有按钮：什么都不做");
        System.out.println("GenScreenKeysTest: " + checks + " 项，" + fails + " 失败");
        if (fails > 0) System.exit(1);
    }
}
