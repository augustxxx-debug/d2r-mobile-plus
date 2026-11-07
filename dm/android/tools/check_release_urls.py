#!/usr/bin/env python3
"""正式包更新清单地址闸门（B246）：最终 APK 的 BuildConfig.DM_UPDATE_URLS 必须非空。

1.1.0 / 1.1.1 正式包出包时没给 DM_UPDATE_URLS（出包日志「没给 DM_UPDATE_URLS：包里不联网」），包里没有清单地址：
加载页左下角的官方免费渠道页脚（官网 / 群号）不显示，诊断上传 / 统计取清单也关着（CommunityFetch、CrashReportBridge、TelemetryBridge），而且出包只记一行提示、不失败。
这里直接查最终 APK 的 dex：找 BuildConfig 写进去的「空格分隔的 https:// 地址串」。给了 --expect 时必须逐字一致。
Release update-manifest gate (B246): BuildConfig.DM_UPDATE_URLS in the final APK must not be empty. The 1.1.0 / 1.1.1 releases
were built without DM_UPDATE_URLS, so the loading-page footer (official channels) and the diagnostic-upload / telemetry manifest
stayed off, and packaging only printed a note. This reads the final APK's dex for the space-separated https:// list
BuildConfig wrote; with --expect it must match exactly.

用法 | usage: check_release_urls.py <apk> [--expect "<DM_UPDATE_URLS>"]   退出码 0 = 通过，1 = 不通过，2 = 读不了
"""
import re
import sys
import zipfile

# 库自带的文档链接（Kotlin / AndroidX 的报错文本），不是我们的清单 | documentation links shipped by libraries, not our manifest
LIB_HOSTS = ('youtrack.jetbrains.com', 'kotlinlang.org', 'developer.android.com', 'issuetracker.google.com', 'github.com/Kotlin', 'goo.gle')
URL_LIST = re.compile(rb'https://[!-~]+(?: https://[!-~]+)*')


def candidates(dex_blobs):
    out = []
    for blob in dex_blobs:
        for m in URL_LIST.finditer(blob):
            s = m.group(0).decode('ascii', 'replace')
            urls = s.split(' ')
            if any(u in ('https://',) or len(u) <= len('https://x') for u in urls):
                continue
            if any(h in u for u in urls for h in LIB_HOSTS):
                continue
            if any(c in s for c in '[]()+?*\\'):   # 正则字面量（如校验 https 的模式）| regex literals
                continue
            out.append(s)
    return sorted(set(out))


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 2
    apk = argv[1]; expect = None
    if len(argv) >= 4 and argv[2] == '--expect':
        expect = ' '.join(argv[3].split())
    try:
        with zipfile.ZipFile(apk) as z:
            blobs = [z.read(n) for n in z.namelist() if re.fullmatch(r'classes\d*\.dex', n)]
    except (OSError, zipfile.BadZipFile) as e:
        print('读不了 APK | cannot read the APK: %s' % e); return 2
    if not blobs:
        print('APK 里没有 classes.dex | no classes.dex in the APK'); return 2
    found = candidates(blobs)
    hosts = sorted({re.sub(r'^https://([^/]+).*$', r'\1', u) for s in found for u in s.split(' ')})
    if not found:
        print('不通过：包里没有更新清单地址（出包时没给 DM_UPDATE_URLS）——加载页官方渠道页脚、诊断上传 / 统计清单都会关着'
              ' | FAIL: no update manifest address in the package (DM_UPDATE_URLS not given at packaging)')
        return 1
    if expect is not None and expect not in found:
        print('不通过：包里的清单地址与出包给的不一致（主机 %s）| FAIL: the manifest addresses differ from the ones given (hosts %s)' % (hosts, hosts))
        return 1
    print('通过：清单地址 %d 组，主机 %s | PASS: manifest address list present, hosts %s' % (len(found), ' '.join(hosts), ' '.join(hosts)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
