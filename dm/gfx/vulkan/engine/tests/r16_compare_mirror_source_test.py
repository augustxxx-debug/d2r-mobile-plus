# R16 比较采样修复的接线检查：判断函数在描述符绑定里被调用、非比较路径保留彩色视图、D16 副本随 Image 生命周期释放、
# A/B 开关能强制旧路径。没有设备也能跑，防止后来的重构把这段悄悄拆掉。
# Wiring checks for the R16 compare-sampling fix: the decision function is called from descriptor binding, the
# non-comparison path keeps the colour view, the D16 copy dies with its Image, and the A/B switch can force the old
# path. Runs without a device, so later refactors cannot quietly unwire it.
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from texture_cache_regression import balanced

root = Path(__file__).resolve().parent.parent
source = (root / 'xbg_game.cpp').read_text(encoding='utf-8')

# 1. 判断只用头文件里的纯函数 | the decision lives in the pure header
assert '#include "xbg_compare_view.h"' in source, 'xbg_game.cpp must include the compare-view policy'
assert (root / 'xbg_compare_view.h').exists(), 'compare-view policy header is missing'

# 2. 描述符绑定里：先算采样器参数，再按比较开关决定视图 | in descriptor binding: sampler params first, then the view decision
# descriptors() 里有带花括号的格式串，balanced() 会在那里提前收尾，所以按绑定处的两个锚点截取。
# descriptors() holds format strings with braces, where balanced() would stop early, so cut between two anchors of the binding.
bind_start = source.index('}ii.sampler=sampler(parameters,im,im.view_format(')
bind = source[bind_start:source.index('if(b.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){', bind_start)]
for needle in ['xbg_compare_view_plan_for(', 'compare_mirror_sync(', 'VK_FORMAT_D16_UNORM',
               'XBG_COMPARE_VIEW_DEPTH_MIRROR', 'XBG_COMPARE_VIEW_UNAVAILABLE']:
    assert needle in bind, needle
assert 'xbg_compare_view_plan_for(' not in source.replace(bind, ''), 'the decision belongs to the compare binding only'
# 旧彩色视图仍是缺省（非比较用法与普通 R16 贴图不许被换掉）| the colour view stays the default
assert 'im.view(registered.has_view?&registered.view:nullptr,false,b.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)' in source
# 只有开了比较的绑定才走副本 | only compare-enabled bindings take the copy
assert 'parameters.cmp_mode' in bind

# 3. 副本的格式与设备能力检查 | the copy's format and the device-capability check
assert 'VK_FORMAT_D16_UNORM' in bind, 'the mirror must be sampled as D16_UNORM'
assert 'vkGetPhysicalDeviceFormatProperties2' in source, 'depth-comparison support must be queried, not assumed'
assert 'XBG_COMPARE_VIEW_DEPTH_MIRROR' in bind, 'the descriptor path must act on the mirror decision'

# 4. 生命周期：副本跟着 Image 一起销毁（漏了就是每帧泄漏一张图）| lifetime: the copy dies with its Image
image = balanced(source, source.index('struct Image {'))
assert 'unique_ptr<Mirror> mirror' in image, 'Image must own the D16 mirror'
assert 'vkDestroyImageView(cfg.device,mirror->view' in image or 'vkDestroyImage(cfg.device,mirror->image' in image, \
    'the Image destructor must release the mirror'

# 5. A/B：标记文件/环境变量能强制旧路径 | A/B: a marker file or environment variable forces the old path
assert 'gfx-r16-compare-mirror' in source, 'A/B marker for the old path is missing'
assert 'DM_GFX_R16_COMPARE_MIRROR' in source, 'A/B environment variable for the old path is missing'

# 6. 不能把比较整体关掉（B078 的 exp1 已经证明那会改光照）：代码里不许出现关 compareEnable 的写法
# Do not turn comparison off wholesale (B078's exp1 already showed that changes the lighting): no such code
assert 's.compareEnable=VK_FALSE' not in source, 'comparison must not be disabled to silence the layer'

print('r16_compare_mirror_source_test: ok')
