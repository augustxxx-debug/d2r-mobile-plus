"""All shipped Deposit variants must fit the UTF-8 payload, not a byte-count guess."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import dm_i18n
strings={row[0]:row[1:] for row in dm_i18n.rows()}
maximum=0;cases=0
for language in range(6):
    for mask in range(8):
        text=strings['dmStoreHead'][language]; any_items=False
        for bit,key,separator in ((1,'dmStoreVault',False),(2,'dmStoreStash',True),(4,'dmStorePouch',True)):
            if mask & bit:
                sep=strings['dmStoreSep'][language] if any_items else ''
                text+=strings[key][language] % ((sep,2147483647) if separator else 2147483647)
                any_items=True
        text+=(strings['dmStoreLeft'][language] % 2147483647) if any_items else strings['dmStoreNone'][language]
        encoded=text.encode('utf-8'); assert len(encoded)<512,(language,mask,len(encoded))
        assert encoded.decode('utf-8')==text
        maximum=max(maximum,len(encoded));cases+=1
assert len(strings['dmCloudConflict'])==6
print('PASS notification localization: %d variants across six locales; maximum %d UTF-8 bytes fits the 512-byte result buffer' % (cases,maximum))
