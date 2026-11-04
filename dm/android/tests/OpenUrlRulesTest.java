package com.xiaoboluo.d2r;

// UrlRules 的主机规则：社区网盘打开前再判一次 https 与主机名（安卓壳 DmActivity.openExternalUrl 用它）。
// Host rules of UrlRules: the Android shell's DmActivity.openExternalUrl re-checks https and the host before opening a cloud-drive link.
public final class OpenUrlRulesTest {
    private static int checks, fails;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }

    public static void main(String[] args) {
        check(UrlRules.openable("https://pan.quark.cn/s/abc"), "https provider link passes");
        check(UrlRules.openable("https://www.123pan.com/s/x"), "another provider passes");
        check(UrlRules.openable("HTTPS://pan.baidu.com/s/x"), "uppercase scheme passes");
        check(!UrlRules.openable("http://pan.quark.cn/s/abc"), "http rejected");
        check(!UrlRules.openable("https:///s/abc"), "empty host rejected");
        check(!UrlRules.openable("https://"), "bare scheme rejected");
        check(!UrlRules.openable("https://pan.quark.cn s/abc"), "space rejected");
        check(!UrlRules.openable("not a url"), "garbage rejected");
        check(!UrlRules.openable(""), "empty rejected");
        check(!UrlRules.openable(null), "null rejected");
        check(!UrlRules.openable("file:///tmp/x"), "non-https scheme rejected");
        System.out.println("OpenUrlRulesTest: " + checks + " checks, " + fails + " failed");
        if (fails != 0) System.exit(1);
    }
}
