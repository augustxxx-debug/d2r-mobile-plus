#!/usr/bin/env python3
"""一端专属的词（CLAUDE.md §2.8）：dm_i18n.py --check 查文案、dm/tools/package_audit.py 查包都用这一张表。不随手机端生成器进安卓包。
Words that belong to one platform: dm_i18n.py --check on the text and dm/tools/package_audit.py on the packages share this list. It does not ship with the on-phone generator in the Android package."""
IOS_WORDS = ('MetalFX', 'Metal', 'iCloud', 'Apple', 'TestFlight', 'Xcode', 'MetricKit', 'Dynamic Island', '灵动岛', 'iPhone', 'iPad', 'iOS')
ANDROID_WORDS = ('Vulkan', 'Turnip', 'Download/S2X', '宝塔', '所有文件访问', 'Android')
