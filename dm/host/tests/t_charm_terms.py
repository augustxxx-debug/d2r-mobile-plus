#!/usr/bin/env python3
# 护身符页文案用游戏自己的「护身符」叫法（11-04 整机验收：西语写成 Amuletos、俄语写成 амулет / Талисман，都是「项链 / 护符」那一类，不是 charm）。
# 词根取自原版 item-names.json 的 cm1 / cm3：esES「Dije」、esMX「Talismán」、ruRU「оберег」、koKR「부적」；amu（项链）在各语言里的词不许出现。
# Charm-page texts use the game's own word for charm (11-04 Mac acceptance: Spanish said Amuletos and Russian амулет / Талисман, which is the
# amulet family, not charm). Roots from the original item-names.json cm1 / cm3; the amulet word of each language must not appear.
import os, sys
T = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'i18n', 'ui_text.tsv')
rows = {l.split('\t')[0]: l.rstrip('\n').split('\t') for l in open(T, encoding='utf-8') if '\t' in l}
hdr = rows['键']; col = {n: hdr.index(n) for n in ('English', 'Español', 'Español MX', 'Русский', '한국어')}
KEYS = ['dmBagPageCharm', 'dmCharmFull', 'dmCharmRestore', 'dmCharmPickFull', 'dmCharmUnidFull']   # dmCharmTradeHint 随 B148 撤了；两条 B146 / B148 新提示一起核 | dmCharmTradeHint went with B148; the two new B146 / B148 notices are checked too
WANT = {'English': 'charm', 'Español': 'dij', 'Español MX': 'talism', 'Русский': 'оберег', '한국어': '부적'}
BAD = {'English': 'amulet', 'Español': 'amulet', 'Español MX': 'amulet', 'Русский': 'амулет', '한국어': '목걸이'}
checks = fails = 0
for k in KEYS:
    for lang, c in col.items():
        s = rows[k][c].lower() if c < len(rows[k]) else ''
        checks += 1
        if not s or WANT[lang] not in s or BAD[lang] in s or 'талисман' in s: fails += 1; print(f'FAIL {k} {lang}: {s!r}')
print(f't_charm_terms: {checks} checks, {fails} failures'); sys.exit(fails != 0)
