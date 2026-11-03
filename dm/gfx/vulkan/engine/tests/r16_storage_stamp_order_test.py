# storage 图像的写戳时序回归（B131 Kimi P1）：写戳只能在真正录好 draw/dispatch 之后推进。
# storage 图像是「先绑定、后写」：描述符绑定发生在 vkCmdDraw*/vkCmdDispatch 之前。如果在绑定时就
# note_image_write()，那么「storage 先绑定、同图比较后绑定」这一版集合会把写前的副本按新写戳填戳，
# 真正写进去之后不再推进，同一提交下一次比较就复用写前内容（旧 D16 读取）。
# 本回归做两件事：(1) 钉住生产代码里的三处调用位置（绑定只排队、描述符构建开始时清队列、draw/dispatch
# 记录后推进）；(2) 把生产代码里的排队/推进函数原文抽出来，和 xbg_compare_view.h 的真实刷新判据一起
# 编译执行，跑「绑定在前、比较在后、实际写后同提交再比较」的图形与计算两种时序，以及早退/流水线失败
# 不得补戳。
# Ordering regression for the storage-image write stamp (B131 Kimi P1): the stamp may only advance after the
# draw/dispatch has actually been recorded. A storage image is bound before it is written; stamping at binding
# time marks a pre-write copy as current, and nothing advances the stamp after the real write, so the next
# compare in the same submission reuses the old D16 copy. This regression pins the three call sites in the
# production source and then compiles the production queue/stamp functions verbatim against the real refresh
# decision in xbg_compare_view.h, running the graphics and compute orderings plus the early-return cases.
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from texture_cache_regression import balanced

root = Path(__file__).resolve().parent.parent
source = (root / 'xbg_game.cpp').read_text(encoding='utf-8')

# 1. 绑定路径：storage 绑定只排队，不许在绑定时就推进写戳。
# At binding: a storage binding only queues the image; it must not move the write stamp.
# 描述符记忆的排除守卫（descriptors() 里那一条）不是绑定路径，所以按带能力检查的那一段取。
# The descriptor-memo exclusion guard is not the binding path, so take the block that does the capability checks.
storage_at = source.index('if(b.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){VkFormat viewed=')
storage_block = source[storage_at:source.index('image_infos.push_back(ii);write.pImageInfo=&image_infos.back();', storage_at)]
assert 'note_image_write(' not in storage_block, \
    'the storage binding must not advance the write stamp: the draw/dispatch that writes comes later (B131 P1)'
assert 'note_storage_bind(im)' in storage_block, 'the storage binding must queue the image for the recorded write'
assert 'note_image_write(im,__LINE__);}' not in storage_block, 'no bind-time stamp behind the capability checks'

# 2. 描述符构建开始时清队列：早退或流水线失败的那一版永远走不到 draw，队列不能漏到下一次。
# The queue starts empty for every descriptor build: a build that never reaches its draw must not stamp later.
descriptors_at = source.index('void descriptors(Descriptors &out,Commands &commands,bool compute){')
assert source.count('note_storage_begin();') == 1, 'the queue is cleared in exactly one place'
assert source.index('note_storage_begin();', descriptors_at) < \
    source.index('descriptors_build(out,commands,compute);', descriptors_at), \
    'the queue must be cleared before this build appends to it'

# 3. 记录路径：写戳在实际 vkCmdDraw*/vkCmdDispatch 之后推进，且必须在流水线失败的 return 之后。
# The stamp moves after the actual vkCmdDraw*/vkCmdDispatch, and after the pipeline-failure return.
assert source.count('note_storage_recorded();') == 2, 'graphics and compute each record their storage writes once'
draw_flush = source.index('note_storage_recorded();')
assert source.index('if(!pipeline.pipeline)return;') < draw_flush, \
    'a failed graphics pipeline never records a draw, so it must not stamp'
assert source.index('vkCmdDrawIndexed(c.command,count,indexed->instances') < draw_flush, \
    'the graphics stamp must follow the indexed draw'
assert source.index('vkCmdDraw(c.command,count,instances,first,first_instance);') < draw_flush, \
    'the graphics stamp must follow the plain draw'
dispatch_at = source.index('void dispatch(const p_dispatch &p){')
dispatch_flush = source.index('note_storage_recorded();', dispatch_at)
assert source.index('if(!pipeline.pipeline){c.finish();return;}', dispatch_at) < dispatch_flush, \
    'a failed compute pipeline never dispatches, so it must not stamp'
assert source.index('vkCmdDispatch(c.command,p.x,p.y,p.z);', dispatch_at) < dispatch_flush, \
    'the compute stamp must follow vkCmdDispatch'
assert dispatch_flush < source.index('c.finish();++perf.dispatches', dispatch_at), \
    'the compute stamp belongs to the recorded command buffer'

# 4. 把生产代码的三个排队函数和 note_image_write 原文抽出来，和真实判据一起执行。
# Extract the production queue/stamp functions verbatim and execute them against the real decision.
flush_body = balanced(source, source.index('void note_storage_recorded(){'))
assert 'note_image_write(*im,__LINE__)' in flush_body, 'the recorded write advances the stamp through the one helper'
assert 'clear();' in flush_body, 'a recorded write consumes its queue, so one draw stamps once'

production = '\n'.join([
    balanced(source, source.index('void note_image_write(Image &im,int line){')),
    balanced(source, source.index('void note_storage_begin(){')),
    balanced(source, source.index('void note_storage_bind(Image &im){')),
    flush_body,
])
# 生产里的副本判据（compare_mirror_sync）必须就是这里执行的两行，否则模型与生产脱节。
# The production copy decision must be the two lines this model runs, or the model drifts from production.
assert 'xbg_mirror_refresh_needed(stats.submissions,im.last_write,im.mirror->stamp)' in source
assert 'xbg_mirror_stamp_filled(stats.submissions,im.last_write,im.mirror->stamp)' in source

harness = r'''
#include <stdint.h>
#include <vector>
#include <cstdio>
#include "xbg_compare_view.h"
struct Image{uint64_t last_write=0;bool gpu_written=false;uint64_t provenance_writer=0;unsigned provenance_write_site=0;};
uint64_t write_seq=1,provenance_seq=1;
void provenance_write(Image &im,unsigned site){im.provenance_writer=++provenance_seq;im.provenance_write_site=site;}
std::vector<Image*> pending_storage_writes;
''' + production + r'''
// compare_mirror_sync 的这一段：写戳不一致就重填，并把填戳记成当前写戳（上面断言钉住了生产原文）。
// compare_mirror_sync's core: a moved write stamp refills the copy and stamps it with the current write.
struct Mirror{xbg_mirror_stamp stamp;unsigned fills=0;};
bool mirror_sync(Image &im,Mirror &m,uint64_t submission){
    if(!xbg_mirror_refresh_needed(submission,im.last_write,m.stamp))return false;
    xbg_mirror_stamp_filled(submission,im.last_write,m.stamp);++m.fills;return true;
}
#define CHECK(x) do{if(!(x))return __LINE__;}while(false)
static int run(){
    const uint64_t submission=7;
    {   // A. 图形：同提交早先的比较填了写前副本 → storage 绑定排在比较之前 → 记录 draw 后必须重填
        // Graphics: an earlier compare in this submission filled the pre-write copy; storage binds before the
        // compare, and after the draw is recorded that copy must be stale.
        Image im;im.last_write=5;Mirror m;xbg_mirror_stamp_clear(m.stamp);
        CHECK(mirror_sync(im,m,submission));CHECK(m.fills==1);
        CHECK(!mirror_sync(im,m,submission));CHECK(m.fills==1);
        const uint64_t before=im.last_write;
        note_storage_begin();note_storage_bind(im);
        CHECK(im.last_write==before);            // 绑定不是写 | binding is not a write
        CHECK(!mirror_sync(im,m,submission));    // 写前的副本仍是当前内容 | still current before the write
        note_storage_recorded();                 // vkCmdDraw 已记录
        CHECK(im.last_write!=before);            // 只有记录之后才推进
        CHECK(mirror_sync(im,m,submission));     // 写前的副本必须重填 | the pre-write copy is stale now
        CHECK(m.fills==2);
        CHECK(!mirror_sync(im,m,submission));    // 重填后同提交内可复用
        CHECK(mirror_sync(im,m,submission+1));   // 下一提交照样重填
    }
    {   // B. 计算：比较排在 storage 绑定之前，dispatch 记录后同样必须重填
        // Compute: the compare binds before the storage binding; the dispatch still invalidates the copy.
        Image im;im.last_write=9;Mirror m;xbg_mirror_stamp_clear(m.stamp);
        const uint64_t before=im.last_write;
        note_storage_begin();
        CHECK(mirror_sync(im,m,submission));CHECK(m.fills==1);
        note_storage_bind(im);CHECK(im.last_write==before);
        CHECK(!mirror_sync(im,m,submission));
        note_storage_recorded();CHECK(im.last_write!=before);
        CHECK(mirror_sync(im,m,submission));CHECK(m.fills==2);
    }
    {   // C. 早退/流水线失败：绑定了但没记录 draw，下一次构建清空后不许补戳
        // Early return / pipeline failure: bound but no draw recorded; the next build drops the entry.
        Image im,other;im.last_write=3;other.last_write=1;Mirror m;xbg_mirror_stamp_clear(m.stamp);
        const uint64_t im_before=im.last_write,other_before=other.last_write;
        note_storage_begin();note_storage_bind(im);
        note_storage_begin();note_storage_bind(other);
        note_storage_recorded();
        CHECK(im.last_write==im_before);         // 没写就不推进 | no write, no stamp
        CHECK(other.last_write!=other_before);
        CHECK(mirror_sync(im,m,submission));     // 副本照旧有效，只需首次填充
        CHECK(!mirror_sync(im,m,submission));
    }
    {   // D. 一次记录只推进一次；同一张图绑两次是两次写
        // One recorded draw stamps once; the same image bound twice is two writes.
        Image im;im.last_write=4;const uint64_t before=im.last_write;
        note_storage_begin();note_storage_bind(im);note_storage_bind(im);note_storage_recorded();
        CHECK(im.last_write==before+2);
        note_storage_recorded();CHECK(im.last_write==before+2);
    }
    return 0;
}
int main(){int n=run();if(n){printf("FAIL: storage-stamp ordering harness line %d\n",n);return 1;}
    printf("r16_storage_stamp_order_test: ok\n");return 0;}
'''

with tempfile.TemporaryDirectory(prefix='xbg-storage-stamp-') as temp:
    cpp, exe = Path(temp) / 'storage_stamp.cpp', Path(temp) / 'storage_stamp'
    cpp.write_text(harness, encoding='utf-8')
    build = subprocess.run(['c++', '-O1', '-Wall', '-Wextra', '-std=c++17', '-I', str(root),
                            str(cpp), '-o', str(exe)], capture_output=True, text=True)
    if build.returncode:
        sys.stderr.write(build.stderr)
        raise SystemExit('RED: the extracted storage-stamp helpers do not compile against the production source')
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    if run.returncode:
        sys.stderr.write(run.stderr)
        # 失败时把 harness 那一行原文打出来（CHECK 返回的是行号）。
        # On failure print the harness line the CHECK returned, so the assertion is readable.
        failed = [l for l in run.stdout.splitlines() if l.startswith('FAIL:')]
        line_no = int(failed[0].rsplit(' ', 1)[1]) if failed else 0
        lines = harness.splitlines()
        for n in range(max(0, line_no - 3), min(len(lines), line_no + 1)):
            sys.stderr.write('harness %d: %s\n' % (n + 1, lines[n]))
        raise SystemExit('RED: ' + run.stdout.strip() + ' — a storage write recorded after the compare binding did '
                         'not invalidate the pre-write D16 copy (B131 Kimi P1)')
