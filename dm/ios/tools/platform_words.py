#!/usr/bin/env python3
"""一端专属的词（CLAUDE.md §2.8）：dm_i18n.py --check 查文案、dm/tools/package_audit.py 查包都用这一张表。不随手机端生成器进安卓包。
Words that belong to one platform: dm_i18n.py --check on the text and dm/tools/package_audit.py on the packages share this list. It does not ship with the on-phone generator in the Android package."""
import re

AMBIGUOUS = ('Metal', 'Apple', 'iOS', 'Android')
IOS_WORDS = ('MetalFX', 'Metal', 'iCloud', 'Apple', 'TestFlight', 'Xcode', 'MetricKit', 'Dynamic Island', '灵动岛', 'iPhone', 'iPad', 'iOS')
ANDROID_WORDS = ('Vulkan', 'Turnip', 'Download/S2X', '宝塔', '所有文件访问', 'Android')


def pattern(word):
    """整词。产品名不分大小写（NO_XCODE 这种大写也算，内审 K-01）；AMBIGUOUS 里的词小写是普通词（metal 贴图、apple 模型、ios_base、dm/ios 路径、android:: 符号），只认原样与全大写。 | Whole word. Product names match in any case (an upper-case NO_XCODE counts, internal audit K-01); the AMBIGUOUS words are ordinary words in lower case (metal textures, an apple model, ios_base, the dm/ios path, android:: symbols), so only their own spelling and all capitals count."""
    if not (word.isascii() and word[0].isalpha()): return re.compile(re.escape(word.encode()))
    if word in AMBIGUOUS: return re.compile(rb'(?<![A-Za-z])(%s|%s)(?![A-Za-z])' % (re.escape(word.encode()), re.escape(word.upper().encode())))
    return re.compile(rb'(?<![A-Za-z])' + re.escape(word.encode()) + rb'(?!dress)', re.I)   # 产品名按前缀：iPhones、iPadOS、MetalFXSpatial 也算（内审 L-02），原版的 IPAddressText 不算 | product names as a prefix: iPhones, iPadOS, MetalFXSpatial count too (internal audit L-02), the original's IPAddressText does not
