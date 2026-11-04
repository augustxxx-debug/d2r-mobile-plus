package com.xiaoboluo.d2r;

import java.net.URI;

// 外部链接的打开规则（社区网盘卡）：只接受 https 且主机名非空；解析不了的串一律拒。纯 Java，可在主机 JVM 上单测。
// 与 iOS 的 NSURL scheme / host 判定、宿主 dm_pan_open_allowed 同一条口径（宿主那头另查网盘前缀）。
// Rules for opening an external link (the cloud-drive cards): https with a non-empty host only; anything unparseable is rejected. Pure Java, unit-testable on a host JVM.
// Same rule as iOS's NSURL scheme / host check and the host's dm_pan_open_allowed (which additionally checks the provider prefix).
final class UrlRules {
    private UrlRules() {}

    static boolean openable(String url) {
        if (url == null || url.isEmpty()) return false;
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
