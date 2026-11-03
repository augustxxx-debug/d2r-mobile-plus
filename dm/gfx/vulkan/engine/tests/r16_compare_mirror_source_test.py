# R16 比较采样修复的接线检查：判断函数在描述符绑定里被调用、非比较路径保留彩色视图、D16 副本随 Image 生命周期释放、
# A/B 开关能强制旧路径；以及立方体（Fold8 的 serial 44）按同型视图建副本、每一层都拷。
# 没有设备也能跑，防止后来的重构把这段悄悄拆掉。
# Wiring checks for the R16 compare-sampling fix: the decision function is called from descriptor binding, the
# non-comparison path keeps the colour view, the D16 copy dies with its Image, and the A/B switch can force the old
# path. Plus the cube case (Fold8's serial 44): the copy gets the matching view type and every layer is copied.
# Runs without a device, so later refactors cannot quietly unwire it.
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

# 7. 立方体：形状/层数随绑定传入，副本按同型视图建，每一层都拷，建不出来就保持彩色视图。
#    Cube: shape and layer count come from the binding, the copy gets the matching view type, every layer is copied, and
#    a failure keeps the colour view.
assert 'cv.two_dimensional=xbg_compare_2d_target(im.source.d.target)' in bind, 'the binding must use the shared shape test'
assert 'cv.cube=xbg_compare_cube_target(im.source.d.target)' in bind, 'the binding must say whether it is a cube'
assert 'cv.layers=im.layers' in bind, 'the binding must pass the layer count'
assert 'cv.data_bytes=xbg_compare_mirror_bytes(im.width,im.height,im.levels,im.layers)' in bind, \
    'the copy size must count every layer (a cube is six)'
# 视图类型由同一份纯函数决定（不是就地 if(layers>1)），绑定与创建不会漂移 | one shared pure decision, no drift
assert 'xbg_compare_mirror_view_kind(cube,im.layers)' in source, 'the view type must come from the policy'
assert 'im.layers>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D' not in source, \
    'a layer count alone cannot pick the view type: a cube is CUBE, not a 2D array of a single layer'

create = source[source.index('bool compare_mirror_create(Image &im){'):source.index('VkImageView compare_mirror_sync(')]
for needle in ['VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT', 'VK_IMAGE_VIEW_TYPE_CUBE', 'VK_IMAGE_VIEW_TYPE_CUBE_ARRAY',
               'XBG_MIRROR_VIEW_NONE', 'fail("no-legal-view")']:
    assert needle in create, needle
assert 'ci.arrayLayers=im.layers' in create, 'the copy must allocate every layer'
assert 'ci.flags' in create and 'vkGetPhysicalDeviceImageFormatProperties(cfg.physical,ci.format' in create, \
    'cube-compatible images must be checked against the device, not assumed'

sync = source[source.index('VkImageView compare_mirror_sync('):source.index('void provenance_image(')]
for needle in ['xbg_compare_mirror_regions(im.levels,im.layers)', 'offsets)!=xbg_compare_mirror_bytes(im.width,im.height,im.levels,im.layers)',
               'for(uint32_t y=0;y<im.layers;y++)', 'return VK_NULL_HANDLE;']:
    assert needle in sync, needle
assert 'regions.reserve(size_t(im.levels)*im.layers)' not in sync, 'the region count must come from the policy'

# 副本建不出来/拷贝不完整时，绑定必须留着彩色视图（那一帧的结果照旧未定义，但不许读别的层、不许崩）
# When the copy cannot be built or is short, the binding must keep the colour view (that frame stays undefined, but no
# other layer is read and nothing crashes).
assert 'if(mirror){ii.imageView=mirror;' in bind, 'a missing copy must fall back to the colour view'

print('r16_compare_mirror_source_test: ok')
