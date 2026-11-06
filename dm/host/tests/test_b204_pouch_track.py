#!/usr/bin/env python3
"""B204 源码断言（补 t_pouch_proof 只测纯规则的缺口，DeepSeek 变异 M5–M7 存活）：
pouch_hook 放出的证据按 where 到对应存档里找；pouch_track 找不到就标 gone；只有直接证明且仍在背包页的才发 0x63。"""
import re
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = (HERE.parent / 'dm_pouch.c').read_text(encoding='utf-8')


def code(s):
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', s, flags=re.S))


def fn(c, name):
    return re.search(r'static (?:void|int) ' + name + r'\(.*?\n}\n', c, re.S).group(0)


class Track(unittest.TestCase):
    def setUp(self):
        self.c = code(SRC)

    def test_hook_searches_by_where(self):   # M5
        h = fn(self.c, 'pouch_hook')
        self.assertIn('if (p->put || !p->where) have = dm_memfind(bl.d, bl.n, p->pb, p->plen) != NULL; else for (int q = 0; q < ns && !have; q++) have = dm_memfind(sb[q].d, sb[q].n, p->pb, p->plen) != NULL;', h)
        self.assertIn('dm_pouch_proven(p->put, have, p->gone, p->gone && p->h0 && live && p->sess == ss && fp != p->h0)', h)   # 终审 P1：本局内、角色档已变 | final review P1
        self.assertIn('fp = hb ? dm_fnv64(bl.d, bl.n) : 0; uint32_t ss = atomic_load(&g_sess); int live = atomic_load(&g_live);', h)
        self.assertNotIn('dm_fs_commit_count() + 2', h)
        self.assertIn('"%s/%s/%s.d2i", "%s/%s.d2i"', h)   # 共享仓库档两处 | the two shared-stash locations
        self.assertIn('need_s |= !g_up[i].put && !g_up[i].gone && g_up[i].where;', h)   # F8：要时才读 | read only when needed
        self.assertIn('if (need_s && !ns) LOG("【红】', h)                            # F5：读不到报红 | red when unreadable

    def test_track_marks_gone(self):   # M6
        t = fn(self.c, 'pouch_track')
        self.assertIn('if (!it) { int st = dm_ser_state(); if (st == 1) continue;', t)   # 在途存盘：等写完再记指纹 | wait for the in-flight save
        self.assertIn('p->gone = 1; p->h0 = h; p->sess = atomic_load(&g_sess);', t)
        self.assertIn('if (held && *(uint32_t *)(held + 8) == p->nid) continue;', t)   # 手上的不判离开 | in hand is not gone
        self.assertIn('p->where = (uint8_t)where; p->inbag = (uint8_t)inbag; p->plen = (uint16_t)len; memcpy(p->pb, buf, (size_t)len);', t)
        self.assertIn('pouch_track(b, G, player);', self.c)

    def test_runs(self):   # 终审 P1：出局后不算本局；跨局遗留的离开条目不等、留在囊里并计数 | final review P1
        self.assertIn('atomic_store(&g_live, 0); lp = 0; return; }', self.c)
        self.assertIn('atomic_fetch_add(&g_sess, 1); atomic_store(&g_live, 1);', self.c)
        self.assertIn('int pend = g_uload && (g_nup - ng || g_udirty);', self.c)
        self.assertIn('atomic_fetch_add(&g_kept, ng)', self.c)
        self.assertIn('if (h) atomic_store(&g_gone_save, 1);', self.c)   # r2 F1：离开就请存盘 | request a save on leaving
        self.assertIn('if (atomic_exchange(&g_gone_save, 0)) LOG(', self.c)

    def test_ready_only_in_bag(self):   # M7
        h = fn(self.c, 'pouch_hook')
        self.assertIn('if (how == 1 && p->inbag && g_nready < 64) g_ready[g_nready++] = p->nid;', h)


if __name__ == '__main__':
    unittest.main()
