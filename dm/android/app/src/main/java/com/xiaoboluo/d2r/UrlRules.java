package com.xiaoboluo.d2r;

import java.net.URI;

// 外部链接的打开规则（社区网盘卡）：只接受 https 且主机名非空；解析不了的串一律拒。纯 Java，可在主机 JVM 上单测。
// 与 iOS 的 NSURL scheme / host 判定、宿主 dm_pan_open_allowed 同一条口径（宿主那头另查网盘前缀）。
// Rules for opening an external link (the cloud-drive cards): https with a non-empty host only; anything unparseable is rejected. Pure Java, unit-testable on a host JVM.
// Same rule as iOS's NSURL scheme / host check and the host's dm_pan_open_allowed (which additionally checks the provider prefix).
final class UrlRules {
    private UrlRules() {}

    // 四家网盘的分享前缀，与 dm/host/dm_pan.h 的 dm_pan_url_ok 同一张白名单（test_community_entry.py 核两边一致）；mirror / github / site / http / javascript: 都不在里面
    // the four drives' share prefixes, the same whitelist as dm_pan_url_ok in dm/host/dm_pan.h (test_community_entry.py checks both agree); mirror / github / site / http / javascript: are not in it
    static final String[] PAN_PREFIXES = {"https://pan.quark.cn/s/", "https://pan.baidu.com/s/", "https://www.alipan.com/s/", "https://www.aliyundrive.com/s/", "https://www.123pan.com/s/"};
    static boolean openable(String url) {
        if (url == null || url.isEmpty()) return false;
        boolean listed = false; for (String p : PAN_PREFIXES) if (url.startsWith(p) && url.length() > p.length()) { listed = true; break; }
        if (!listed) return false;
        try {
            URI u = new URI(url);
            if (!"https".equalsIgnoreCase(u.getScheme())) return false;
            String host = u.getHost();
            return host != null && !host.isEmpty();
        } catch (java.net.URISyntaxException e) {
            return false;
        }
    }
}
