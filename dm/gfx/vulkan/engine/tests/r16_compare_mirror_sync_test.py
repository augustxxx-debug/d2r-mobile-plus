# R16 比较采样副本的同步回归：终审（Kimi）给出的三个 P1 与两个 P2 在这里逐条变成断言。
# 没有设备也能跑：检查生产源码里「描述符记忆命中会不会漏刷副本」「所有 GPU 写源图是否推进写戳」
# 「副本最终屏障是否覆盖 compute 与非片元阶段」「mutable 重建是否让填戳失效」「旧驱动能力查询是否有门控」。
# Regression for the R16 compare-mirror sync: one assertion per finding of the final review.
# No device needed: checks that a descriptor-memo hit cannot skip the refresh, that every GPU write to the
# source advances the write stamp, that the copy's final barrier covers compute and non-fragment stages, that a
# mutable rebuild invalidates the fill stamp, and that the capability query is gated for pre-1.3 drivers.
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from texture_cache_regression import balanced

root = Path(__file__).resolve().parent.parent
source = (root / 'xbg_game.cpp').read_text(encoding='utf-8')
header = (root / 'xbg_compare_view.h').read_text(encoding='utf-8')

# 1. 副本的刷新判据是头文件里的纯函数，生产代码必须真的用它（否则这套回归又是「只测到一个副本」）。
# The refresh decision lives in the pure header and the production path must use it.
for needle in ('xbg_mirror_refresh_needed(', 'xbg_mirror_stamp_filled(', 'xbg_mirror_stamp_clear(',
               'xbg_mirror_never_filled('):
    assert needle in header, 'the refresh decision belongs to the pure header: ' + needle

sync = balanced(source, source.index('VkImageView compare_mirror_sync(Image &im,VkCommandBuffer cmd)'))
assert 'xbg_mirror_refresh_needed(' in sync, 'compare_mirror_sync must use the shared refresh decision'
assert 'xbg_mirror_stamp_filled(' in sync, 'compare_mirror_sync must record the fill stamp'
assert 'filled_submission==stats.submissions' not in sync, 'the refresh must not be decided inline again'
assert 'xbg_mirror_never_filled(' in sync, 'the first fill must be recognised by the shared predicate'

# 2. P1-1：描述符记忆命中（含跨提交 persistent 项）也必须刷新它绑定过的副本。
# P1-1: a descriptor-memo hit (persistent ones survive submissions) must refresh the copies it bound.
hit = source[source.index('if(hit!=desc_memo.end()){DescMemo &m=hit->second;'):]
hit = hit[:hit.index('++desc_memo_why[3];')]
assert 'compare_mirror_sync(' in hit, 'a memo hit must refresh the D16 copy (Kimi P1-1)'
assert 'm.mirrored' in hit, 'the memo must record which of its images bound the copy'
assert 'prepare_read(' in hit, 'a memo hit still needs the sampled-image barrier'
# 记下来的绑定集合必须由绑定路径填写，而且只给真正换了副本的图 | the recorded set comes from the binding path
assert 'desc_memo_mirrored' in source, 'the binding path must record which images bound the copy'
bind_tail = source[source.index('VkImageView mirror=DESC_CALL(ImageView,compare_mirror_sync(im,commands.command));'):]
bind_tail = bind_tail[:bind_tail.index('if(b.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){')]
assert 'desc_memo_mirrored' in bind_tail, 'only a binding that took the copy may be recorded as mirrored'
# memo 条目保存这一份标记 | the entry keeps that set
assert 'std::vector<uint8_t> mirrored;' in source, 'DescMemo must keep the per-image mirror flags'
assert 'dm.mirrored' in source, 'the new entry must copy the mirror flags'

# 3. P1-2：任何把内容写进源图的 GPU 命令都推进写戳，且只有一个推进点。
# P1-2: every GPU command that writes the source advances the write stamp, from one single place.
assert source.count('last_write=++write_seq') == 1, 'the write stamp must have exactly one bump site'
stamp = balanced(source, source.index('void note_image_write(Image &im,int line){'))
assert 'last_write=++write_seq' in stamp, 'the write-stamp helper bumps the stamp'
assert 'gpu_written=true' in stamp and 'provenance_write(' in stamp, 'the helper also marks the image written'
# 其余地方不许再手写 gpu_written/provenance 组合，否则下一次加写路径还会漏
# No other place may hand-roll the pair, or the next write path will leak again
assert source.count('gpu_written=true') == 1, 'image write marks come from note_image_write only'
assert 'gpu_written=true' in stamp, 'the helper is the one place that marks the image written'

for name in ('void copy_texture_buffer(', 'void copy_texture(', 'void partial_clear_texture(',
             'void clear_texture(', 'void resolve_texture(', 'Image &get_image_slow(const tex_t *t,bool upload){',
             'void after_dispatch(Commands &c){'):
    body = balanced(source, source.index(name))
    assert 'note_image_write(' in body, name + ' writes the source image without advancing the stamp'

# 附件的 pass 也是写路径：没有 draw 的 pass 仍会 clear（loadOp=CLEAR / vkCmdClearAttachments）
# An attachment pass is a write path too: a pass without draws still clears
pass_body = source[source.index('for(unsigned slot=0;slot<9;slot++)if(targets[slot]){'):]
pass_body = pass_body[:pass_body.index('p.color_slots=colors.size();')]
assert 'note_image_write(' in pass_body, 'a pass that writes an attachment must advance its stamp'

# 4. P1-3：副本最终屏障的目标阶段必须覆盖 compute 与顶点/细分/几何采样。
# P1-3: the copy's final barrier must cover compute and vertex/tessellation/geometry sampling.
assert 'xbg_compare_mirror_dst_stage(' in sync, 'the final barrier must use the shared consumer stage'
assert 'VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT' not in sync, 'a fragment-only barrier leaves compute unsynchronised'
assert 'xbg_compare_mirror_dst_stage(' in source, 'the barrier stage must come from the tested decision'
assert 'static_assert(XBG_COMPARE_MIRROR_DST_STAGE==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT' in source, \
    'the header constant must be pinned to VK_PIPELINE_STAGE_ALL_COMMANDS_BIT at compile time'
# compute 的绑定也走同一段比较逻辑（dispatch 前没有别的全阶段屏障）
# compute bindings take the same path and dispatch records right after it
assert 'descriptors(d,c,true)' in source, 'compute descriptors share the compare path'

# 5. P2：mutable 重建换了源图的 VkImage/内存，副本填戳必须作废。
# P2: a mutable rebuild swaps the source's VkImage/memory, so the copy's fill stamp must be invalidated.
upgrade = balanced(source, source.index('void upgrade_mutable(Image &im){'))
assert 'xbg_mirror_stamp_clear(' in upgrade, 'a mutable rebuild must invalidate the copy fill stamp'

# 6. P2：VkFormatProperties3 必须由 format_feature_flags2 门控（没有 1.3/扩展时不能挂 pNext）。
# P2: VkFormatProperties3 must be gated by format_feature_flags2 (no pNext on pre-1.3 drivers).
assert 'VkFormatProperties3 extra' in source, 'the depth-comparison bit needs FormatProperties3'
assert 'if(format_feature_flags2)all.pNext=&extra;' in source, 'pNext must only be chained when the driver can answer'
assert 'r16_d16_compare=format_feature_flags2 && ' in source, 'without the query the copy must stay off (old path)'

# 7. P2：副本建不起来要记原因，并且同一张图不许在热路径里反复重试。
# P2: a failed copy creation records why and is not retried on the hot path.
create = balanced(source, source.index('bool compare_mirror_create(Image &im)'))
assert 'mirror_fail' in create, 'a failed creation must be remembered on the image'
assert 'if(im.mirror_fail)return false;' in create, 'a remembered failure must short-circuit the next attempt'
assert create.index('if(im.mirror_fail)return false;') < create.index('vkCreateImage'), \
    'the negative cache is checked before any Vulkan call'
assert create.index('mirror_fail') < create.index('vkCreateImage'), 'the negative cache is checked before creating'
assert 'xbg_logf(' in create, 'a failure must be logged with its reason'
for reason in ('extent', 'create', 'memory', 'view'):
    assert '"' + reason in create, 'each failure step needs its own reason: ' + reason

print('r16_compare_mirror_sync_test: ok')
