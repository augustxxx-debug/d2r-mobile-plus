// P2 Vulkan 准入微场景（接口 dm_probe.h）：在这台机的驱动上实测引擎依赖的几条语义，只画小图、读回核对，不跑游戏。
//   M1 子组：LocalSize 32 / 64 / 128 的计算着色器记 gl_SubgroupSize / 子组数 / ballot 位数 / 跨 16 道 shuffle（Maxwell 32 道线程束的形状）；再试 requiredSubgroupSize=32。
//   M2 片元四邻：shuffleXor(1) / (2) 取邻居 FragCoord 与 dFdxFine 形式，8×8 读回逐像素核对（引擎 xbg_spirv_quad 改写的两种形状）。
//   M3 ClipDistance：与游戏顶点着色器同形（声明不写、capability 已裁）能否建管线；真写的那种在 shaderClipDistance=0 下怎样。
//   M4 深度比较过滤：D16 / X8D24 画深度梯度，sampler2DShadow 线性过滤采样，看过渡处有没有分数值（PCF）。
//   M5 无格式存储图像写（StorageImageWriteWithoutFormat），读回核对。
//   M6 真实着色器（外部目录 shaders/ 有 .xgs 时）：全部建模块；顶点着色器按包头绑定表 + SPIR-V 输入反射建只有顶点阶段（光栅丢弃）的图形管线；计算着色器建计算管线；记失败与验证层消息。
//   验证层：APK 里带 VK_LAYER_KHRONOS_validation 时启用，经 debug utils 统计错误 / 警告，逐项归到当时的测试。
// P2 Vulkan admission micro-scenes (interface dm_probe.h): measure on this device's driver the semantics the engine relies on; tiny draws read back and checked, no game.
//   M1 subgroups: compute shaders at LocalSize 32 / 64 / 128 record gl_SubgroupSize / subgroup count / ballot bit count / a shuffle across 16 lanes (the shape of Maxwell's 32-lane warps); then requiredSubgroupSize=32.
//   M2 fragment quads: shuffleXor(1) / (2) of the neighbour's FragCoord and the dFdxFine form, an 8x8 draw read back and checked per pixel (the two shapes xbg_spirv_quad rewrites between).
//   M3 ClipDistance: whether a vertex shader shaped like the game's (declared, unwritten, capability trimmed) builds a pipeline; what a really written one does with shaderClipDistance=0.
//   M4 depth compare filtering: D16 / X8D24 depth gradient, sampled through a linear sampler2DShadow, looking for fractional values (PCF) at the transition.
//   M5 formatless storage image write (StorageImageWriteWithoutFormat), read back and checked.
//   M6 real shaders (when the external dir shaders/ holds .xgs): create every module; vertex shaders get a vertex-only (rasterizer discard) graphics pipeline from the bundle's binding table + SPIR-V input reflection; compute shaders get compute pipelines; failures and validation messages recorded.
//   Validation: VK_LAYER_KHRONOS_validation is enabled when the APK carries it; errors / warnings counted through debug utils and attributed to the running test.
#include "dm_probe.h"
#include <dirent.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "probe_shaders.h"

static FILE *o; static int g_fail;
#define OK(...) do { fprintf(o, "OK    " __VA_ARGS__); fputc('\n', o); } while (0)
#define FAIL(...) do { fprintf(o, "FAIL  " __VA_ARGS__); fputc('\n', o); g_fail++; } while (0)
#define INFO(...) do { fprintf(o, "INFO  " __VA_ARGS__); fputc('\n', o); } while (0)
#define KV(k, ...) do { fprintf(o, "KV    vkm.%s=", k); fprintf(o, __VA_ARGS__); fputc('\n', o); } while (0)
static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }

#define VK_FNS(X) \
 X(vkEnumerateInstanceLayerProperties) X(vkCreateInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceMemoryProperties) \
 X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkDestroyInstance) X(vkEnumerateDeviceExtensionProperties) X(vkCreateDebugUtilsMessengerEXT) X(vkDestroyDebugUtilsMessengerEXT) \
 X(vkGetDeviceQueue) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateComputePipelines) \
 X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
 X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
 X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkDeviceWaitIdle) \
 X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch) X(vkCmdPipelineBarrier) X(vkCmdBeginRenderPass) \
 X(vkCmdEndRenderPass) X(vkCmdDraw) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdCopyImageToBuffer) X(vkDestroyDevice) X(vkResetCommandPool)
#define DECL(n) static PFN_##n n;
VK_FNS(DECL)

static VkInstance inst; static VkPhysicalDevice pd; static VkDevice dev; static VkQueue q; static VkCommandPool pool; static VkCommandBuffer cb; static VkPhysicalDeviceMemoryProperties mp;
static VkPhysicalDeviceFeatures feat; static VkPhysicalDeviceSubgroupProperties sgp; static int has_vvl, has_sgsc; static VkDebugUtilsMessengerEXT msgr;
static const char *g_test = "init"; static int v_err, v_warn;
// 驱动崩溃围栏：每个可能让驱动崩的步骤（建管线）之前把步骤名写进 ext/vkm_running.txt，成功后删；启动时看到残留 = 上次在那一步被驱动弄崩，记进 ext/vkm_skip.txt 并跳过，报告里记一行
// Driver crash fence: before every step that may crash the driver (pipeline creation) the step name goes into ext/vkm_running.txt, deleted on success; a leftover at start = the driver crashed there last time, appended to ext/vkm_skip.txt, skipped, and reported
static char g_marker[1100], g_skipfile[1100]; static char g_skip[64][96]; static int g_nskip;
static int skipped(const char *step) { for (int i = 0; i < g_nskip; i++) if (!strcmp(g_skip[i], step)) return 1; return 0; }
static void step_begin(const char *step) { if (!g_marker[0]) return; FILE *f = fopen(g_marker, "w"); if (f) { fputs(step, f); fclose(f); } }
static void step_end(void) { if (g_marker[0]) unlink(g_marker); }
static int risky_begin(const char *step) { if (skipped(step)) { INFO("跳过 %s：上一次运行驱动在这一步崩了（见 vkm.driver_crash）", step); KV("skipped", "%s", step); return 1; } step_begin(step); return 0; }
#define RISKY(step, call) (risky_begin(step) ? VK_ERROR_UNKNOWN : (call))
static char v_first[8][400]; static int v_nfirst;
static VKAPI_ATTR VkBool32 VKAPI_CALL on_msg(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *d, void *ud) {
    (void)type; (void)ud;
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { v_err++; if (v_nfirst < 8) snprintf(v_first[v_nfirst++], sizeof v_first[0], "[%s] %.330s", g_test, d->pMessage ? d->pMessage : ""); }
    else if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) v_warn++;
    return VK_FALSE;
}

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want) { for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i; return UINT32_MAX; }
typedef struct { VkBuffer b; VkDeviceMemory m; void *p; VkDeviceSize size; } buf_t;
static int buf_make(buf_t *x, VkDeviceSize size, VkBufferUsageFlags usage) {
    memset(x, 0, sizeof *x); x->size = size; VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &ci, NULL, &x->b)) return 0; VkMemoryRequirements r; vkGetBufferMemoryRequirements(dev, x->b, &r);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = r.size; ai.memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(dev, &ai, NULL, &x->m) || vkBindBufferMemory(dev, x->b, x->m, 0) || vkMapMemory(dev, x->m, 0, size, 0, &x->p)) return 0;
    memset(x->p, 0, size); return 1;
}
static void buf_free(buf_t *x) { if (x->p) vkUnmapMemory(dev, x->m); if (x->b) vkDestroyBuffer(dev, x->b, NULL); if (x->m) vkFreeMemory(dev, x->m, NULL); memset(x, 0, sizeof *x); }
typedef struct { VkImage i; VkDeviceMemory m; VkImageView v; VkFormat f; uint32_t w, h; VkImageAspectFlags aspect; } img_t;
static int img_make(img_t *x, VkFormat f, uint32_t w, uint32_t h, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    memset(x, 0, sizeof *x); x->f = f; x->w = w; x->h = h; x->aspect = aspect;
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType = VK_IMAGE_TYPE_2D; ci.format = f; ci.extent.width = w; ci.extent.height = h; ci.extent.depth = 1; ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev, &ci, NULL, &x->i)) return 0; VkMemoryRequirements r; vkGetImageMemoryRequirements(dev, x->i, &r);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = r.size; ai.memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(dev, &ai, NULL, &x->m) || vkBindImageMemory(dev, x->i, x->m, 0)) return 0;
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = x->i; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = f; vi.subresourceRange.aspectMask = aspect; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(dev, &vi, NULL, &x->v) == VK_SUCCESS;
}
static void img_free(img_t *x) { if (x->v) vkDestroyImageView(dev, x->v, NULL); if (x->i) vkDestroyImage(dev, x->i, NULL); if (x->m) vkFreeMemory(dev, x->m, NULL); memset(x, 0, sizeof *x); }
static void cb_begin(void) { vkResetCommandPool(dev, pool, 0); VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb, &bi); }
static VkResult cb_run(void) { vkEndCommandBuffer(cb); VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; VkResult r = vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE); if (r == VK_SUCCESS) r = vkQueueWaitIdle(q); return r; }
static void img_barrier(img_t *x, VkImageLayout from, VkImageLayout to, VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.oldLayout = from; b.newLayout = to; b.srcAccessMask = sa; b.dstAccessMask = da; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = x->i; b.subresourceRange.aspectMask = x->aspect; b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, ss, ds, 0, 0, NULL, 0, NULL, 1, &b);
}
static void img_copy_to(img_t *x, buf_t *b) { VkBufferImageCopy c = {0}; c.imageSubresource.aspectMask = x->aspect; c.imageSubresource.layerCount = 1; c.imageExtent.width = x->w; c.imageExtent.height = x->h; c.imageExtent.depth = 1; vkCmdCopyImageToBuffer(cb, x->i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b->b, 1, &c); }
static VkShaderModule mod(const uint32_t *w, uint32_t n) { VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = n * 4; ci.pCode = w; VkShaderModule m = VK_NULL_HANDLE; vkCreateShaderModule(dev, &ci, NULL, &m); return m; }
static VkRenderPass rp_make(VkFormat c0, VkFormat c1, VkFormat d) {   // 至多两个颜色附件 + 一个深度 | up to two colour attachments + one depth
    VkAttachmentDescription a[3]; VkAttachmentReference cr[2], dr; uint32_t n = 0, nc = 0;
    VkFormat cs[2] = {c0, c1}; for (int i = 0; i < 2; i++) if (cs[i]) { memset(&a[n], 0, sizeof a[n]); a[n].format = cs[i]; a[n].samples = VK_SAMPLE_COUNT_1_BIT; a[n].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE; a[n].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; a[n].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; cr[nc].attachment = n; cr[nc].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; n++; nc++; }
    if (d) { memset(&a[n], 0, sizeof a[n]); a[n].format = d; a[n].samples = VK_SAMPLE_COUNT_1_BIT; a[n].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE; a[n].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; a[n].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; dr.attachment = n; dr.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; n++; }
    VkSubpassDescription sp = {0}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = nc; sp.pColorAttachments = cr; sp.pDepthStencilAttachment = d ? &dr : NULL;
    VkRenderPassCreateInfo ci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; ci.attachmentCount = n; ci.pAttachments = a; ci.subpassCount = 1; ci.pSubpasses = &sp; VkRenderPass rp = VK_NULL_HANDLE; vkCreateRenderPass(dev, &ci, NULL, &rp); return rp;
}
// 图形管线：vs（必需）+ fs（可无 = 光栅丢弃），顶点输入由调用者给，颜色附件数 nc，深度写开关 | graphics pipeline: vs (required) + fs (NULL = rasterizer discard), vertex input from the caller, nc colour attachments, depth write switch
static VkResult gp_make(VkShaderModule vs, VkShaderModule fs, VkPipelineLayout pl, VkRenderPass rp, uint32_t nc, int depth, const VkPipelineVertexInputStateCreateInfo *vin, const void *stage_next, VkPipeline *out) {
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}}; st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main"; st[0].pNext = stage_next; st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi0 = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp = {0, 0, 64, 64, 0, 1}; VkRect2D sc = {{0, 0}, {64, 64}}; VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.rasterizerDiscardEnable = fs == VK_NULL_HANDLE; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable = ds.depthWriteEnable = depth != 0; ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState ba[2] = {{0}, {0}}; ba[0].colorWriteMask = ba[1].colorWriteMask = 0xF; VkPipelineColorBlendStateCreateInfo bs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; bs.attachmentCount = nc; bs.pAttachments = ba;
    VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; ci.stageCount = fs ? 2 : 1; ci.pStages = st; ci.pVertexInputState = vin ? vin : &vi0; ci.pInputAssemblyState = &ia; ci.pViewportState = &vps; ci.pRasterizationState = &rs; ci.pMultisampleState = &ms; ci.pDepthStencilState = &ds; ci.pColorBlendState = &bs; ci.layout = pl; ci.renderPass = rp;
    *out = VK_NULL_HANDLE; return vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, out);
}
static VkPipelineLayout pl_make(VkDescriptorSetLayout dsl) { VkPipelineLayoutCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; ci.setLayoutCount = dsl ? 1 : 0; ci.pSetLayouts = &dsl; VkPipelineLayout pl = VK_NULL_HANDLE; vkCreatePipelineLayout(dev, &ci, NULL, &pl); return pl; }
static VkDescriptorSetLayout dsl_make(const VkDescriptorSetLayoutBinding *b, uint32_t n) { VkDescriptorSetLayoutCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; ci.bindingCount = n; ci.pBindings = b; VkDescriptorSetLayout l = VK_NULL_HANDLE; vkCreateDescriptorSetLayout(dev, &ci, NULL, &l); return l; }
static VkDescriptorSet ds_one(VkDescriptorSetLayout l, VkDescriptorType t, VkDescriptorPool *pp) {
    VkDescriptorPoolSize ps = {t, 1}; VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps; vkCreateDescriptorPool(dev, &pi, NULL, pp);
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = *pp; ai.descriptorSetCount = 1; ai.pSetLayouts = &l; VkDescriptorSet s = VK_NULL_HANDLE; vkAllocateDescriptorSets(dev, &ai, &s); return s;
}

static void m1_run(VkDescriptorSetLayout dsl, VkPipelineLayout pl, VkShaderModule m, uint32_t ls, int spec, VkPipelineShaderStageCreateFlags flags, const char *variant) {
    VkSpecializationMapEntry me = {0, 0, 4}; VkSpecializationInfo si = {1, &me, 4, &ls};
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = m; ci.stage.pName = "main"; ci.stage.flags = flags; if (spec) ci.stage.pSpecializationInfo = &si; ci.layout = pl;
    char stepn[48]; snprintf(stepn, sizeof stepn, "M1.%s.ls%u", variant, ls); VkPipeline p = VK_NULL_HANDLE; VkResult r = RISKY(stepn, vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p)); step_end(); if (r) { if (!skipped(stepn)) FAIL("M1 %s LocalSize %u 建计算管线失败 %d", variant, ls, (int)r); return; }
    buf_t out; buf_make(&out, ls * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); VkDescriptorPool dp; VkDescriptorSet s = ds_one(dsl, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &dp);
    VkDescriptorBufferInfo di = {out.b, 0, out.size}; VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &di; vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
    cb_begin(); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &s, 0, NULL); vkCmdDispatch(cb, 1, 1, 1); r = cb_run();
    if (r) FAIL("M1 %s LocalSize %u 执行失败 %d", variant, ls, (int)r);
    else {
        const uint32_t *v = out.p; uint32_t sgsize = v[0] & 0xFF, nsg = v[0] >> 24, maxid = 0, ballot_min = ~0u, ballot_max = 0, sh_ok32 = 0, shx_ok32 = 0, all32 = 0, id_distinct = 0; static uint8_t seen[256];
        memset(seen, 0, sizeof seen);
        int alt = !strcmp(variant, "altlane"); uint32_t lane_distinct = 0; static uint8_t seen2[256]; memset(seen2, 0, sizeof seen2);
        for (uint32_t i = 0; i < ls; i++) { uint32_t id = (v[i * 4] >> 16) & 0xFF; uint32_t dl = (v[i * 4] >> 8) & 0xFF; if (alt) { if (!seen2[dl]) { seen2[dl] = 1; lane_distinct++; } id = dl; } if (id > maxid) maxid = id; if (!seen[id]) { seen[id] = 1; id_distinct++; } uint32_t bc = v[i * 4 + 1]; if (bc < ballot_min) ballot_min = bc; if (bc > ballot_max) ballot_max = bc;
            uint32_t want_sh = 1000u + (i & ~31u) + ((id + 16u) & 31u), want_shx = 1000u + (i ^ 16u);
            if (v[i * 4 + 2] == want_sh) sh_ok32++; if ((v[i * 4 + 3] & 0x7FFFFFFF) == want_shx) shx_ok32++; if (v[i * 4 + 3] & 0x80000000u) all32++; }
        if (alt) { uint32_t raw_id_distinct = 0; static uint8_t seen3[256]; memset(seen3, 0, sizeof seen3); for (uint32_t i = 0; i < ls; i++) { uint32_t rid = (v[i * 4] >> 16) & 0xFF; if (!seen3[rid]) { seen3[rid] = 1; raw_id_distinct++; } } char k2[40]; snprintf(k2, sizeof k2, "m1.altlane.ls%u.ids", ls); KV(k2, "gl_SubgroupInvocationID_distinct=%u derived_lane_distinct=%u", raw_id_distinct, lane_distinct); }
        char k[40]; snprintf(k, sizeof k, "m1.%s.ls%u", variant, ls); KV(k, "subgroup_size=%u num_subgroups=%u max_invocation_id=%u distinct_ids=%u ballot_bits=%u..%u shuffle_as_32lane=%u/%u shufflexor16_as_32lane=%u/%u all32_true=%u raw0=%#x raw1=%#x raw2=%#x raw3=%#x", sgsize, nsg, maxid, id_distinct, ballot_min, ballot_max, sh_ok32, ls, shx_ok32, ls, all32, v[0], v[4], v[8], v[12]);
        if (id_distinct < (ls < sgsize ? ls : sgsize)) FAIL("M1 %s LocalSize %u：gl_SubgroupInvocationID 只有 %u 个不同值（应 %u）——子组内 lane 号不可用，warp32 改写与 5 个计算着色器依赖它", variant, ls, id_distinct, ls < sgsize ? ls : sgsize);
        else if (sgsize >= 32 && ballot_min >= 32 && sh_ok32 == ls && shx_ok32 == ls) OK("M1 %s LocalSize %u：子组 %u 道，ballot / shuffle 按 32 道线程束语义成立", variant, ls, sgsize);
        else if (sgsize >= 32) INFO("M1 %s LocalSize %u：子组 %u 道（%u 个子组），lane 号 %u 个不同值，ballot 位数 %u–%u，跨 16 道 shuffle 按 32 道语义 %u/%u、shuffleXor(16) %u/%u —— 宽于 32 的子组由引擎 warp32 改写处理（64 / 128）", variant, ls, sgsize, nsg, id_distinct, ballot_min, ballot_max, sh_ok32, ls, shx_ok32, ls);
        else { INFO("M1 %s LocalSize %u：子组 %u 道（%u 个子组），ballot 位数 %u–%u，跨 16 道 shuffle 按 32 道语义只对了 %u/%u、shuffleXor(16) 对了 %u/%u → 引擎 xbg_spirv_warp32 不处理 <32 道，这些计算着色器在本机语义不成立", variant, ls, sgsize, nsg, ballot_min, ballot_max, sh_ok32, ls, shx_ok32, ls); if (!strcmp(variant, "spec")) FAIL("M1 子组 %u 道 < 32：5 个 ballot / shuffle 计算着色器需要 16 道模拟或改写（方案 P5 阻断项）", sgsize); }
    }
    vkDestroyDescriptorPool(dev, dp, NULL); buf_free(&out); vkDestroyPipeline(dev, p, NULL);
}
// ---- M1 子组 | subgroups ----
static void m1_subgroup(void) {
    g_test = "M1";
    VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}; VkDescriptorSetLayout dsl = dsl_make(&b, 1); VkPipelineLayout pl = pl_make(dsl); VkShaderModule m = mod(spv_sg_comp, spv_sg_comp_words);
    if (!m) { FAIL("M1 子组计算着色器建模块失败"); return; }
    static const uint32_t sizes[3] = {32, 64, 128};
    for (int k = 0; k < 3; k++) m1_run(dsl, pl, m, sizes[k], 1, 0, "spec");
    // 变体：固定 LocalSize（无特化常量）；特化常量 + REQUIRE_FULL_SUBGROUPS | variants: fixed LocalSize (no specialization constant); specialization constant + REQUIRE_FULL_SUBGROUPS
    { VkShaderModule f32 = mod(spv_sg_fixed32, spv_sg_fixed32_words), f64 = mod(spv_sg_fixed64, spv_sg_fixed64_words), f128 = mod(spv_sg_fixed128, spv_sg_fixed128_words);
      if (f32) m1_run(dsl, pl, f32, 32, 0, 0, "fixed"); if (f64) m1_run(dsl, pl, f64, 64, 0, 0, "fixed"); if (f128) m1_run(dsl, pl, f128, 128, 0, 0, "fixed");
      if (f32) vkDestroyShaderModule(dev, f32, NULL); if (f64) vkDestroyShaderModule(dev, f64, NULL); if (f128) vkDestroyShaderModule(dev, f128, NULL); }
    if (has_sgsc) for (int k = 0; k < 3; k++) m1_run(dsl, pl, m, sizes[k], 1, VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, "full");
    { VkShaderModule ma = mod(spv_sg_alt_comp, spv_sg_alt_comp_words); if (ma) { for (int k = 0; k < 3; k++) m1_run(dsl, pl, ma, sizes[k], 1, 0, "altlane"); vkDestroyShaderModule(dev, ma, NULL); } }   // shuffle 下标用 ballot 推导的 lane | shuffle index from the ballot-derived lane
    { VkShaderModule mn = mod(spv_sg_comp_noopt, spv_sg_comp_noopt_words); if (mn) { for (int k = 0; k < 3; k++) m1_run(dsl, pl, mn, sizes[k], 1, 0, "noopt"); vkDestroyShaderModule(dev, mn, NULL); } }   // glslc 不带 -O | glslc without -O
    // lane 号的替代来路 | alternative lane-id sources
    for (int opt = 0; opt < 2; opt++) { VkShaderModule lm = opt ? mod(spv_sg_lane_comp, spv_sg_lane_comp_words) : mod(spv_sg_lane_comp_noopt, spv_sg_lane_comp_noopt_words); const char *ov = opt ? "" : ".noopt";
      if (lm) { for (int k = 0; k < 2; k++) {
        uint32_t ls = k ? 128 : 64; VkSpecializationMapEntry me = {0, 0, 4}; VkSpecializationInfo si = {1, &me, 4, &ls};
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfo rq = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO}; rq.requiredSubgroupSize = sgp.subgroupSize;
        for (int req = 0; req < (has_sgsc ? 2 : 1); req++) {
          VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = lm; ci.stage.pName = "main"; ci.stage.pSpecializationInfo = &si; ci.layout = pl; if (req) ci.stage.pNext = &rq;
          char stepn[48]; snprintf(stepn, sizeof stepn, "M1.lane%s%s.ls%u", ov, req ? ".req" : "", ls); VkPipeline p = VK_NULL_HANDLE; VkResult r = RISKY(stepn, vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p)); step_end(); if (r) { if (!skipped(stepn)) INFO("M1 lane%s LocalSize %u 建管线失败 %d", req ? "(requiredSubgroupSize)" : "", ls, (int)r); continue; }
          buf_t out; buf_make(&out, ls * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); VkDescriptorPool dp; VkDescriptorSet s = ds_one(dsl, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &dp);
          VkDescriptorBufferInfo di = {out.b, 0, out.size}; VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &di; vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
          cb_begin(); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &s, 0, NULL); vkCmdDispatch(cb, 1, 1, 1); r = cb_run();
          if (!r) { const uint32_t *v = out.p; uint32_t sg_shader = v[3] >> 16, sg = sgp.subgroupSize ? sgp.subgroupSize : 1; uint32_t ok_id = 0, ok_lt = 0, ok_ex = 0, ok_lin = 0, lt_eq_lin = 0;
            for (uint32_t i = 0; i < ls; i++) { uint32_t want = i & (sg - 1); if (v[i * 4] == want) ok_id++; if (v[i * 4 + 1] == want) ok_lt++; if (v[i * 4 + 2] == want) ok_ex++; if ((v[i * 4 + 3] & 0xFFFF) == i) ok_lin++; if (v[i * 4 + 1] == v[i * 4 + 2]) lt_eq_lin++; }
            char k[48]; snprintf(k, sizeof k, "m1.lane%s%s.ls%u", ov, req ? ".req" : "", ls); KV(k, "subgroup_device=%u subgroup_shader=%u id_ok=%u/%u ltmask_ok=%u/%u exclusive_ok=%u/%u localindex_ok=%u/%u ltmask_eq_exclusive=%u/%u raw0=%u,%u,%u raw1=%u,%u,%u raw17=%u,%u,%u", sg, sg_shader, ok_id, ls, ok_lt, ls, ok_ex, ls, ok_lin, ls, lt_eq_lin, ls, v[0], v[1], v[2], v[4], v[5], v[6], v[17 * 4], v[17 * 4 + 1], v[17 * 4 + 2]);
            if (ok_id == ls && sg_shader == sg) OK("M1 lane%s%s LocalSize %u：gl_SubgroupInvocationID 与 gl_SubgroupSize 都正确", ov, req ? "(requiredSubgroupSize)" : "", ls); else if (ok_id == ls) INFO("M1 lane%s%s LocalSize %u：gl_SubgroupInvocationID 正确，但 gl_SubgroupSize 读成 %u（设备 %u）", ov, req ? "(requiredSubgroupSize)" : "", ls, sg_shader, sg);
            else INFO("M1 lane%s%s LocalSize %u：gl_SubgroupInvocationID 对 %u/%u；替代来路 SubgroupLtMask 位数对 %u/%u、ballot exclusiveBitCount 对 %u/%u、LocalInvocationIndex&(size-1) 对 %u/%u", ov, req ? "(requiredSubgroupSize)" : "", ls, ok_id, ls, ok_lt, ls, ok_ex, ls, ok_lin, ls); }
          else FAIL("M1 lane LocalSize %u 执行失败 %d", ls, (int)r);
          vkDestroyDescriptorPool(dev, dp, NULL); buf_free(&out); vkDestroyPipeline(dev, p, NULL); } }
        vkDestroyShaderModule(dev, lm, NULL); } }
    // requiredSubgroupSize = 32 | requiredSubgroupSize = 32
    if (has_sgsc) {
        uint32_t ls = 32; VkSpecializationMapEntry me = {0, 0, 4}; VkSpecializationInfo si = {1, &me, 4, &ls};
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfo rq = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO}; rq.requiredSubgroupSize = 32;
        VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.pNext = &rq; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = m; ci.stage.pName = "main"; ci.stage.pSpecializationInfo = &si; ci.layout = pl;
        int e0 = v_err; VkPipeline p = VK_NULL_HANDLE; VkResult r = RISKY("M1.required32", vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p)); step_end();
        INFO("M1 requiredSubgroupSize=32（设备范围 %u–%u）：vkCreateComputePipelines 返回 %d，验证层错误 +%d", sgp.subgroupSize, sgp.subgroupSize, (int)r, v_err - e0); KV("m1.required32", "result=%d verr=%d", (int)r, v_err - e0);
        if (p) vkDestroyPipeline(dev, p, NULL);
    }
    vkDestroyShaderModule(dev, m, NULL); vkDestroyPipelineLayout(dev, pl, NULL); vkDestroyDescriptorSetLayout(dev, dsl, NULL);
}

// ---- M7 子组逐项（warp32 改写依赖的每个构造）| subgroup items (each construct the warp32 rewrite relies on) ----
// 2026-10-28 奥丁二分：cs_fluid_sim_accumulate_c 里读 SubgroupLtMask 第 1 字就挂 GPU；引擎改写器已改为按 lane 算掩码。本场景逐项记录 gl_SubgroupSize 形状、
// ballot 按 id>>5 选字、LtMask 两个字与选字、本线程束内 shuffle，以及一个照流体着色器形状写的变体（M7.fluid，RISKY 围栏里跑）。
// The Thor bisection of 2026-10-28: reading word 1 of SubgroupLtMask hangs the GPU in cs_fluid_sim_accumulate_c; the engine rewriter now computes masks from the lane id.
// This scene records the gl_SubgroupSize shape, the ballot word picked by id>>5, both LtMask words and the picked one, the in-warp shuffle, and a variant shaped like the
// fluid shader (M7.fluid, inside the RISKY fence).
static void m7_items(void) {
    g_test = "M7";
    VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}; VkDescriptorSetLayout dsl = dsl_make(&b, 1); VkPipelineLayout pl = pl_make(dsl);
    const uint32_t sg = sgp.subgroupSize;
    for (int which = 0; which < 2; which++) {
        VkShaderModule m = which ? mod(spv_m7_fluid_comp, spv_m7_fluid_comp_words) : mod(spv_m7_comp, spv_m7_comp_words); const char *nm = which ? "fluid" : "items";
        if (!m) { FAIL("M7 %s 建模块失败", nm); continue; }
        VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = m; ci.stage.pName = "main"; ci.layout = pl;
        char stepn[48]; snprintf(stepn, sizeof stepn, "M7.%s", nm); VkPipeline p = VK_NULL_HANDLE; VkResult r = RISKY(stepn, vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p));
        if (r) { step_end(); if (!skipped(stepn)) FAIL("M7 %s 建计算管线失败 %d", nm, (int)r); vkDestroyShaderModule(dev, m, NULL); continue; }
        buf_t out; buf_make(&out, 65 * 2 * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); memset(out.p, 0, out.size); VkDescriptorPool dp; VkDescriptorSet s = ds_one(dsl, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &dp);
        VkDescriptorBufferInfo di = {out.b, 0, out.size}; VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &di; vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
        cb_begin(); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &s, 0, NULL); vkCmdDispatch(cb, 1, 1, 1); r = cb_run(); step_end();
        const uint32_t *v = out.p;
        if (r) FAIL("M7 %s 执行失败 %d（DEVICE_LOST = 这个形状把驱动挂了）", nm, (int)r);
        else if (!which) {
            uint32_t size_ok = 0, id_distinct = 0, bw_ok = 0, ltw_ok = 0, lty_ok = 0, ltc_ok = 0, sh_ok = 0, bty_ok = 0; static uint8_t seen[256]; memset(seen, 0, sizeof seen);
            for (uint32_t i = 0; i < 64; i++) {
                const uint32_t *a = v + 8 * i, *c = v + 8 * i + 4; uint32_t id = (a[0] >> 8) & 0xFF, l = id;
                if ((a[0] & 0xFF) == sg) size_ok++; if (!seen[id]) { seen[id] = 1; id_distinct++; }
                if (a[1] == 0x55555555u) bw_ok++;                                                   // ballot(偶数 lane) 本线程束字 | this warp's word of ballot(even lanes)
                if (a[2] == ((l & 31u) ? (1u << (l & 31u)) - 1u : 0u)) ltw_ok++;                     // LtMask 本线程束字 | this warp's LtMask word
                if (c[1] == (l >= 32 ? (l & 31u ? (1u << (l & 31u)) - 1u : 0u) : 0u)) lty_ok++;     // LtMask.y 原样 | raw LtMask.y
                if (c[2] == l) ltc_ok++;                                                             // bitCount(LtMask) == lane | bitCount(LtMask) == lane id
                if (a[3] == 1000u + (i & ~31u) + ((i + 16u) & 31u)) sh_ok++;                        // 本线程束内 shuffle | in-warp shuffle
                if (c[3] == (sg >= 64 ? 0xffffffffu : 0u)) bty_ok++;                                 // ballot(true).y | ballot(true).y
            }
            KV("m7.items", "subgroup_size_ok=%u/64 distinct_ids=%u ballot_word_ok=%u/64 ltmask_word_ok=%u/64 ltmask_y_ok=%u/64 ltmask_bitcount_ok=%u/64 shuffle_inwarp_ok=%u/64 ballot_true_y_ok=%u/64 raw0=%#x raw1=%#x raw2=%#x raw3=%#x raw4=%#x raw5=%#x",
               size_ok, id_distinct, bw_ok, ltw_ok, lty_ok, ltc_ok, sh_ok, bty_ok, v[0], v[1], v[2], v[3], v[4], v[5]);
            if (size_ok == 64 && id_distinct >= (sg < 64 ? sg : 64) && bw_ok == 64 && ltw_ok == 64 && lty_ok == 64 && ltc_ok == 64 && sh_ok == 64 && bty_ok == 64) OK("M7 items：gl_SubgroupSize、lane 号、ballot 选字、LtMask 两字与选字、本线程束 shuffle 全部正确（%u 道）", sg);
            else INFO("M7 items：size %u/64 lane 不同值 %u ballot 选字 %u/64 LtMask 选字 %u/64 LtMask.y %u/64 bitCount(LtMask) %u/64 shuffle %u/64 ballot(true).y %u/64 —— 不全对的项就是 warp32 改写不能依赖的构造", size_ok, id_distinct, bw_ok, ltw_ok, lty_ok, ltc_ok, sh_ok, bty_ok);
        } else {
            uint32_t slots_ok = 0, dup = 0, count = v[64 * 4]; static uint8_t used[256]; memset(used, 0, sizeof used);
            for (uint32_t i = 0; i < 64; i++) { uint32_t slot = v[4 * i]; if ((i & 3u) == 3u) continue; if (slot < 256) { if (used[slot]) dup++; used[slot] = 1; if (slot < 48) slots_ok++; } }
            KV("m7.fluid", "count=%u slots_in_range=%u/48 duplicate_slots=%u raw0=%#x raw1=%#x raw2=%#x raw3=%#x", count, slots_ok, dup, v[0], v[1], v[2], v[3]);
            if (count == 48 && slots_ok == 48 && !dup) OK("M7 fluid：流体形状（LtMask 经 float、线程束聚合追加、读 LtMask.y 不选）跑通且追加结果正确");
            else INFO("M7 fluid：计数 %u（应 48）、槽位在范围 %u/48、重复 %u —— 形状跑通但聚合结果不对，与引擎 warp32 改写前的 64 道语义问题一致", count, slots_ok, dup);
        }
        vkDestroyDescriptorPool(dev, dp, NULL); buf_free(&out); vkDestroyPipeline(dev, p, NULL); vkDestroyShaderModule(dev, m, NULL);
    }
    vkDestroyPipelineLayout(dev, pl, NULL); vkDestroyDescriptorSetLayout(dev, dsl, NULL);
}
// ---- M2 片元四邻 | fragment quads ----
static void m2_quad(void) {
    g_test = "M2";
    img_t c0, c1; img_make(&c0, VK_FORMAT_R32G32B32A32_SFLOAT, 8, 8, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT); img_make(&c1, VK_FORMAT_R32G32B32A32_SFLOAT, 8, 8, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkRenderPass rp = rp_make(c0.f, c1.f, 0); VkImageView vs2[2] = {c0.v, c1.v}; VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass = rp; fi.attachmentCount = 2; fi.pAttachments = vs2; fi.width = fi.height = 8; fi.layers = 1; VkFramebuffer fb; vkCreateFramebuffer(dev, &fi, NULL, &fb);
    VkPipelineLayout pl = pl_make(VK_NULL_HANDLE); VkShaderModule vs = mod(spv_fs_vert, spv_fs_vert_words), fs = mod(spv_quad_frag, spv_quad_frag_words); VkPipeline p = VK_NULL_HANDLE; VkResult r = RISKY("M2", gp_make(vs, fs, pl, rp, 2, 0, NULL, NULL, &p)); step_end();
    if (r) { if (!skipped("M2")) FAIL("M2 四邻片元管线失败 %d", (int)r); return; }
    buf_t b0, b1; buf_make(&b0, 8 * 8 * 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT); buf_make(&b1, 8 * 8 * 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    cb_begin(); VkClearValue cv[2] = {{{{0, 0, 0, 0}}}, {{{0, 0, 0, 0}}}}; VkRenderPassBeginInfo bi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; bi.renderPass = rp; bi.framebuffer = fb; bi.renderArea.extent.width = bi.renderArea.extent.height = 8; bi.clearValueCount = 2; bi.pClearValues = cv;
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE); VkViewport vp = {0, 0, 8, 8, 0, 1}; VkRect2D sc = {{0, 0}, {8, 8}}; vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRenderPass(cb);
    img_copy_to(&c0, &b0); img_copy_to(&c1, &b1); r = cb_run();
    if (r) FAIL("M2 执行失败 %d", (int)r);
    else {
        const float *a = b0.p, *d = b1.p; int okx = 0, oky = 0, okdx = 0, okdy = 0, helpers = 0; float sgsize = a[2];
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { const float *v = a + (y * 8 + x) * 4, *e = d + (y * 8 + x) * 4;
            if (fabsf(v[0] - ((float)(x ^ 1) + 0.5f)) < 1e-3f) okx++; if (fabsf(v[1] - ((float)(y ^ 1) + 0.5f)) < 1e-3f) oky++;   /* 四邻 lane 布局：bit0 = x 奇偶、bit1 = y 奇偶，xor 2 取竖向邻居 = y^1 | quad lane layout: bit0 = x parity, bit1 = y parity, xor 2 reads the vertical neighbour = y^1 */ if (fabsf(e[0] - 1.0f) < 1e-3f) okdx++; if (fabsf(e[1] - 1.0f) < 1e-3f) okdy++; if (e[3] > 0.5f) helpers++; }
        KV("m2", "subgroup_size=%g shufflexor1_ok=%d/64 shufflexor2_ok=%d/64 dfdx_ok=%d/64 dfdy_ok=%d/64 helpers_visible=%d", sgsize, okx, oky, okdx, okdy, helpers);
        { char row[512]; size_t rl = 0; for (int y = 0; y < 2; y++) for (int x = 0; x < 4; x++) { const float *v = a + (y * 8 + x) * 4; rl += (size_t)snprintf(row + rl, sizeof row - rl, "(%d,%d)->x%.1f,y%.1f,id%g ", x, y, v[0], v[1], v[3]); } KV("m2.samples", "%s", row); }
        if (okx == 64 && oky == 64) OK("M2 片元 shuffleXor(1)/(2) 在 %g 道子组上逐像素等于四邻（引擎未改写的 7 个四邻 shuffle 形状可用）", sgsize); else FAIL("M2 片元 shuffleXor 四邻：x 对 %d/64，y 对 %d/64（子组 %g 道）", okx, oky, sgsize);
        if (okdx == 64 && okdy == 64) OK("M2 dFdxFine / dFdyFine 逐像素正确（引擎改写成导数的 271 个形状可用）"); else FAIL("M2 细导数：x 对 %d/64，y 对 %d/64", okdx, okdy);
    }
    buf_free(&b0); buf_free(&b1); vkDestroyPipeline(dev, p, NULL); vkDestroyShaderModule(dev, vs, NULL); vkDestroyShaderModule(dev, fs, NULL); vkDestroyPipelineLayout(dev, pl, NULL); vkDestroyFramebuffer(dev, fb, NULL); vkDestroyRenderPass(dev, rp, NULL); img_free(&c0); img_free(&c1);
}

// ---- M3 ClipDistance ----
static void m3_clip(void) {
    g_test = "M3";
    VkRenderPass rp = rp_make(0, 0, 0); VkPipelineLayout pl = pl_make(VK_NULL_HANDLE);
    VkVertexInputBindingDescription vb = {0, 16, VK_VERTEX_INPUT_RATE_VERTEX}; VkVertexInputAttributeDescription va = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vin = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &vb; vin.vertexAttributeDescriptionCount = 1; vin.pVertexAttributeDescriptions = &va;
    const struct { const char *name; const uint32_t *w; uint32_t n; int game_shape; } cases[2] = {{"声明不写、capability 已裁（游戏顶点着色器的形状）", spv_clipdecl_vert, spv_clipdecl_vert_words, 1}, {"真写 gl_ClipDistance、capability 保留", spv_clipwrite_vert, spv_clipwrite_vert_words, 0}};
    for (int i = 0; i < 2; i++) {
        char stepn[16]; snprintf(stepn, sizeof stepn, "M3.%d", i); int e0 = v_err; VkShaderModule m = mod(cases[i].w, cases[i].n); VkPipeline p = VK_NULL_HANDLE; VkResult r = m ? RISKY(stepn, gp_make(m, VK_NULL_HANDLE, pl, rp, 0, 0, &vin, NULL, &p)) : VK_ERROR_UNKNOWN; step_end();
        if (skipped(stepn)) { if (cases[i].game_shape) FAIL("M3 %s：驱动在建管线时崩溃（上次运行）—— 2,346 个游戏顶点着色器都是这个形状", cases[i].name); else INFO("M3 %s：驱动在建管线时崩溃（上次运行，游戏没有这种着色器）", cases[i].name); if (m) vkDestroyShaderModule(dev, m, NULL); continue; }
        char k[16]; snprintf(k, sizeof k, "m3.%d", i); KV(k, "module=%d pipeline_result=%d verr=%d", m != VK_NULL_HANDLE, (int)r, v_err - e0);
        if (cases[i].game_shape) { if (r == VK_SUCCESS && v_err == e0) OK("M3 %s：建管线成功，验证层 0 错误（shaderClipDistance=%u）", cases[i].name, feat.shaderClipDistance); else if (r == VK_SUCCESS) INFO("M3 %s：建管线成功但验证层报 %d 个错误（见 vkm.verr_first）", cases[i].name, v_err - e0); else FAIL("M3 %s：建管线失败 %d —— 2,346 个游戏顶点着色器都是这个形状", cases[i].name, (int)r); }
        else INFO("M3 %s：建管线返回 %d，验证层错误 +%d（游戏没有这种着色器，只记录）", cases[i].name, (int)r, v_err - e0);
        if (p) vkDestroyPipeline(dev, p, NULL); if (m) vkDestroyShaderModule(dev, m, NULL);
    }
    vkDestroyPipelineLayout(dev, pl, NULL); vkDestroyRenderPass(dev, rp, NULL);
}

// ---- M4 深度比较过滤 | depth compare filtering ----
static PFN_vkGetPhysicalDeviceFormatProperties pfn_fmt;
static void m4_depth(VkFormat df, const char *name) {
    g_test = "M4";
    { VkFormatProperties fp; pfn_fmt(pd, df, &fp); if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) || !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) { INFO("M4 %s：设备不支持该格式的深度附件或线性过滤采样（opt=%#x），跳过", name, fp.optimalTilingFeatures); char k[24]; snprintf(k, sizeof k, "m4.%s", name); KV(k, "unsupported opt=%#x", fp.optimalTilingFeatures); return; } }
    img_t d, c; if (!img_make(&d, df, 64, 64, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)) { FAIL("M4 %s 深度图建不了", name); return; }
    img_make(&c, VK_FORMAT_R32G32B32A32_SFLOAT, 64, 64, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkRenderPass rp1 = rp_make(0, 0, df), rp2 = rp_make(c.f, 0, 0); VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass = rp1; fi.attachmentCount = 1; fi.pAttachments = &d.v; fi.width = fi.height = 64; fi.layers = 1; VkFramebuffer fb1, fb2; vkCreateFramebuffer(dev, &fi, NULL, &fb1); fi.renderPass = rp2; fi.pAttachments = &c.v; vkCreateFramebuffer(dev, &fi, NULL, &fb2);
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.compareEnable = VK_TRUE; si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; VkSampler smp; vkCreateSampler(dev, &si, NULL, &smp);
    VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}; VkDescriptorSetLayout dsl = dsl_make(&b, 1); VkPipelineLayout pl0 = pl_make(VK_NULL_HANDLE), pl1 = pl_make(dsl);
    VkShaderModule v1 = mod(spv_depth_vert, spv_depth_vert_words), v2 = mod(spv_fs_vert, spv_fs_vert_words), f2 = mod(spv_shadow_frag, spv_shadow_frag_words), f0 = mod(spv_empty_frag, spv_empty_frag_words); VkPipeline p1, p2;
    char stepn[24]; snprintf(stepn, sizeof stepn, "M4.%s", name);
    // 深度管线要有片元阶段才光栅化（gp_make 以 fs==NULL 判丢弃）：用空片元，不采样（采样深度图自己会成反馈环）| the depth pipeline needs rasterization (gp_make treats fs==NULL as discard): an empty fragment that samples nothing (sampling the depth image itself would be a feedback loop)
    VkResult r1 = RISKY(stepn, gp_make(v1, f0, pl0, rp1, 0, 1, NULL, NULL, &p1)); step_end(); if (r1) { if (!skipped(stepn)) FAIL("M4 %s 深度管线失败 %d", name, (int)r1); return; }
    if (gp_make(v2, f2, pl1, rp2, 1, 0, NULL, NULL, &p2)) { FAIL("M4 %s 采样管线失败", name); return; }
    VkDescriptorPool dp; VkDescriptorSet s = ds_one(dsl, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dp); VkDescriptorImageInfo ii = {smp, d.v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii; vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
    buf_t rb; buf_make(&rb, 64 * 64 * 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    cb_begin(); VkClearValue cv; cv.depthStencil.depth = 1; cv.depthStencil.stencil = 0; VkRenderPassBeginInfo bi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; bi.renderPass = rp1; bi.framebuffer = fb1; bi.renderArea.extent.width = bi.renderArea.extent.height = 64; bi.clearValueCount = 1; bi.pClearValues = &cv;
    VkViewport vp = {0, 0, 64, 64, 0, 1}; VkRect2D sc = {{0, 0}, {64, 64}};
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE); vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p1); vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRenderPass(cb);
    VkClearValue cc = {{{0, 0, 0, 0}}}; bi.renderPass = rp2; bi.framebuffer = fb2; bi.pClearValues = &cc;
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE); vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p2); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl1, 0, 1, &s, 0, NULL); vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRenderPass(cb);
    img_copy_to(&c, &rb); VkResult r = cb_run();
    if (r) FAIL("M4 %s 执行失败 %d", name, (int)r);
    else {
        const float *a = rb.p; int frac = 0, frac_half = 0, mono = 1; float prev = -1; char row[400] = ""; size_t rl = 0;
        for (int x = 24; x < 40; x++) { const float *v = a + (32 * 64 + x) * 4; if (v[0] > 0.01f && v[0] < 0.99f) frac++; if (v[1] > 0.01f && v[1] < 0.99f) frac_half++; if (v[0] < prev - 1e-4f) mono = 0; prev = v[0]; rl += (size_t)snprintf(row + rl, sizeof row - rl, "%.2f/%.2f ", v[0], v[1]); }
        char k[24]; snprintf(k, sizeof k, "m4.%s", name); KV(k, "fractional_centre=%d fractional_halfpx=%d monotonic=%d row32=%s", frac, frac_half, mono, row);
        if (frac_half > 0 && mono) OK("M4 %s：sampler2DShadow 线性过滤在过渡处给出分数值（半像素偏移处 %d 个），比较过滤（PCF）成立", name, frac_half); else FAIL("M4 %s：没有分数值（中心 %d / 半像素 %d，非降 %d）——线性比较过滤可能退化成 nearest", name, frac, frac_half, mono);
    }
    buf_free(&rb); vkDestroyDescriptorPool(dev, dp, NULL); vkDestroyPipeline(dev, p1, NULL); vkDestroyPipeline(dev, p2, NULL); vkDestroyShaderModule(dev, v1, NULL); vkDestroyShaderModule(dev, v2, NULL); vkDestroyShaderModule(dev, f2, NULL); vkDestroyShaderModule(dev, f0, NULL);
    vkDestroyPipelineLayout(dev, pl0, NULL); vkDestroyPipelineLayout(dev, pl1, NULL); vkDestroyDescriptorSetLayout(dev, dsl, NULL); vkDestroySampler(dev, smp, NULL); vkDestroyFramebuffer(dev, fb1, NULL); vkDestroyFramebuffer(dev, fb2, NULL); vkDestroyRenderPass(dev, rp1, NULL); vkDestroyRenderPass(dev, rp2, NULL); img_free(&d); img_free(&c);
}

// ---- M5 无格式存储图像写 | formatless storage image write ----
static void m5_store(void) {
    g_test = "M5";
    if (!feat.shaderStorageImageWriteWithoutFormat) { FAIL("M5 设备无 shaderStorageImageWriteWithoutFormat（15 个着色器需要）"); return; }
    img_t im; img_make(&im, VK_FORMAT_R8G8B8A8_UNORM, 64, 64, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}; VkDescriptorSetLayout dsl = dsl_make(&b, 1); VkPipelineLayout pl = pl_make(dsl); VkShaderModule m = mod(spv_store_comp, spv_store_comp_words);
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = m; ci.stage.pName = "main"; ci.layout = pl; VkPipeline p = VK_NULL_HANDLE; VkResult r = RISKY("M5", vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p)); step_end();
    if (r) { if (!skipped("M5")) FAIL("M5 无格式写计算管线失败 %d", (int)r); return; }
    VkDescriptorPool dp; VkDescriptorSet s = ds_one(dsl, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dp); VkDescriptorImageInfo ii = {VK_NULL_HANDLE, im.v, VK_IMAGE_LAYOUT_GENERAL}; VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo = &ii; vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
    buf_t rb; buf_make(&rb, 64 * 64 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    cb_begin(); img_barrier(&im, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &s, 0, NULL); vkCmdDispatch(cb, 8, 8, 1);
    img_barrier(&im, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT); img_copy_to(&im, &rb); r = cb_run();
    if (r) FAIL("M5 执行失败 %d", (int)r);
    else { const uint8_t *a = rb.p; int ok = 0; for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { const uint8_t *v = a + (y * 64 + x) * 4; if (abs(v[0] - x * 255 / 64) <= 2 && abs(v[1] - y * 255 / 64) <= 2 && abs(v[2] - 128) <= 2 && v[3] == 255) ok++; }
        KV("m5", "pixels_ok=%d/4096", ok); if (ok == 4096) OK("M5 无格式存储图像写 4096 像素全部正确"); else FAIL("M5 无格式存储图像写：%d/4096 像素正确", ok); }
    buf_free(&rb); vkDestroyDescriptorPool(dev, dp, NULL); vkDestroyPipeline(dev, p, NULL); vkDestroyShaderModule(dev, m, NULL); vkDestroyPipelineLayout(dev, pl, NULL); vkDestroyDescriptorSetLayout(dev, dsl, NULL); img_free(&im);
}

// ---- M6 真实着色器 | real shaders ----
static int has_clip_decl(const uint32_t *w, uint32_t n) {   // OpMemberDecorate … BuiltIn ClipDistance(3) / CullDistance(4) | member decorated ClipDistance / CullDistance
    for (uint32_t i = 5; i < n;) { uint32_t len = w[i] >> 16, op = w[i] & 0xffff; if (!len || i + len > n) break; if (op == 72 && len == 5 && w[i + 3] == 11 && (w[i + 4] == 3 || w[i + 4] == 4)) return 1; if (op == 54 || op == 59) break; i += len; }   // 到函数 / 变量区就停 | stop at functions / variables
    return 0;
}
typedef struct { uint32_t magic, version, stage, binding_count, input_count, c1_bytes, spirv_bytes, reserved; } xgs_hdr;
typedef struct { uint32_t binding, type, count, bytes, source, slot, sampler_slot, flags; } xgs_bind;
// SPIR-V 顶点输入反射：Input 变量的 Location 与类型（浮点 / 整数 / 无符号，列数），给顶点输入描述 | reflect vertex inputs: Location and type (float / int / uint, columns) of Input variables
static int reflect_inputs(const uint32_t *w, uint32_t n, VkVertexInputAttributeDescription *va, uint32_t *nva) {
    enum { T_NONE, T_FLOAT, T_INT, T_UINT }; static uint8_t kind[65536]; static uint8_t cols[65536]; static uint32_t ptr_to[65536]; static int32_t loc[65536]; static uint8_t is_in[65536];
    uint32_t bound = w[3]; if (bound > 65536) return 0; memset(kind, 0, bound); memset(cols, 0, bound); memset(ptr_to, 0, bound * 4); memset(is_in, 0, bound); for (uint32_t i = 0; i < bound; i++) loc[i] = -1;
    for (uint32_t i = 5; i < n;) { uint32_t len = w[i] >> 16, op = w[i] & 0xffff; if (!len || i + len > n) break;
        if (op == 71 && w[i + 2] == 30) loc[w[i + 1]] = (int32_t)w[i + 3];                        // OpDecorate Location
        else if (op == 22 && len >= 3) { kind[w[i + 1]] = T_FLOAT; cols[w[i + 1]] = 1; }            // OpTypeFloat
        else if (op == 21 && len >= 4) { kind[w[i + 1]] = w[i + 3] ? T_INT : T_UINT; cols[w[i + 1]] = 1; }   // OpTypeInt
        else if (op == 23 && len >= 4) { kind[w[i + 1]] = kind[w[i + 2]]; cols[w[i + 1]] = 1; }    // OpTypeVector
        else if (op == 24 && len >= 4) { kind[w[i + 1]] = kind[w[i + 2]]; cols[w[i + 1]] = (uint8_t)w[i + 3]; }   // OpTypeMatrix
        else if (op == 28 && len >= 3) { kind[w[i + 1]] = kind[w[i + 2]]; cols[w[i + 1]] = cols[w[i + 2]]; }    // OpTypeArray（按元素） | by element
        else if (op == 32 && len >= 4) { ptr_to[w[i + 1]] = w[i + 3]; }                             // OpTypePointer
        else if (op == 59 && len >= 4 && w[i + 3] == 1) { is_in[w[i + 2]] = 1; ptr_to[w[i + 2]] = w[i + 1]; }   // OpVariable Input：记变量 id → 指针类型 | variable id -> pointer type
        i += len; }
    uint32_t k = 0;
    for (uint32_t id = 0; id < bound && k < 32; id++) if (is_in[id] && loc[id] >= 0) { uint32_t t = ptr_to[ptr_to[id]]; uint8_t kd = kind[t], nc = cols[t] ? cols[t] : 1;
        for (uint8_t c = 0; c < nc && k < 32; c++) { va[k].location = (uint32_t)loc[id] + c; va[k].binding = 0; va[k].offset = ((uint32_t)loc[id] + c) * 16; va[k].format = kd == T_INT ? VK_FORMAT_R32G32B32A32_SINT : kd == T_UINT ? VK_FORMAT_R32G32B32A32_UINT : VK_FORMAT_R32G32B32A32_SFLOAT; k++; } }
    *nva = k; return 1;
}
static void m6_real(const char *dir) {
    g_test = "M6";
    DIR *d = opendir(dir); if (!d) { INFO("M6 没有真实着色器目录 %s，跳过", dir); KV("m6", "skipped"); return; }
    static const VkShaderStageFlagBits stages[6] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_COMPUTE_BIT};
    int clip_crashes = skipped("M3.0"); uint32_t skipped_clip = 0; if (clip_crashes) INFO("M6：M3.0（ClipDistance 声明形状）曾让驱动崩溃，带这种声明的顶点着色器一律跳过不建管线（另用去 ClipDistance 的着色器集重跑）");
    VkRenderPass rp = rp_make(0, 0, 0); uint32_t n = 0, crashed_shaders = 0, mod_fail = 0, by_stage[6] = {0}, pipe_try[6] = {0}, pipe_fail[6] = {0}, verr_shaders = 0; uint64_t t_mod = 0, t_pipe = 0; char fails[1600] = ""; size_t fl = 0;
    struct dirent *e; uint8_t *buf = NULL; size_t cap = 0;
    while ((e = readdir(d))) {
        size_t L = strlen(e->d_name); if (L < 5 || strcmp(e->d_name + L - 4, ".xgs")) continue;
        char path[1200]; snprintf(path, sizeof path, "%s/%s", dir, e->d_name); FILE *f = fopen(path, "rb"); if (!f) continue; fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if ((size_t)sz > cap) { cap = (size_t)sz + 65536; buf = realloc(buf, cap); } if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); continue; } fclose(f);
        xgs_hdr h; memcpy(&h, buf, sizeof h); if (h.magic != 0x31534758 || h.version != 1 || h.stage > 5 || h.binding_count > 256) continue;
        size_t off = sizeof h + (size_t)h.binding_count * sizeof(xgs_bind) + (size_t)h.input_count * 4 + h.c1_bytes; if (off + h.spirv_bytes != (size_t)sz) continue;
        const uint32_t *w = (const uint32_t *)(buf + off); uint32_t nw = h.spirv_bytes / 4; n++; by_stage[h.stage]++; int e0 = v_err;
        if (clip_crashes && (h.stage == 0 || h.stage == 1 || h.stage == 2) && has_clip_decl(w, nw)) { skipped_clip++; continue; }
        char stepn[96]; snprintf(stepn, sizeof stepn, "M6.%s", e->d_name); if (skipped(stepn)) { crashed_shaders++; if (fl < sizeof fails - 40) fl += (size_t)snprintf(fails + fl, sizeof fails - fl, "%s:driver-crash ", e->d_name); continue; }
        step_begin(stepn);
        uint64_t t0 = now_ns(); VkShaderModule m = mod(w, nw); t_mod += now_ns() - t0;
        if (!m) { mod_fail++; step_end(); if (fl < sizeof fails - 40) fl += (size_t)snprintf(fails + fl, sizeof fails - fl, "%s:module ", e->d_name); continue; }
        if (h.stage == 0 || h.stage == 5) {
            VkDescriptorSetLayoutBinding bs[256]; uint32_t nb = 0; const xgs_bind *xb = (const xgs_bind *)(buf + sizeof h);
            for (uint32_t i = 0; i < h.binding_count; i++) { if (xb[i].type > VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) continue; bs[nb].binding = xb[i].binding; bs[nb].descriptorType = (VkDescriptorType)xb[i].type; bs[nb].descriptorCount = xb[i].count ? xb[i].count : 1; bs[nb].stageFlags = stages[h.stage]; bs[nb].pImmutableSamplers = NULL; nb++; }
            VkDescriptorSetLayout dsl = dsl_make(bs, nb); VkPipelineLayout pl = pl_make(dsl); VkPipeline p = VK_NULL_HANDLE; VkResult r; pipe_try[h.stage]++;
            t0 = now_ns();
            if (h.stage == 5) { VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = m; ci.stage.pName = "main"; ci.layout = pl; r = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p); }
            else { VkVertexInputAttributeDescription va[32]; uint32_t nva = 0; reflect_inputs(w, nw, va, &nva); VkVertexInputBindingDescription vb = {0, 1024, VK_VERTEX_INPUT_RATE_VERTEX};
                VkPipelineVertexInputStateCreateInfo vin = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vin.vertexBindingDescriptionCount = nva ? 1 : 0; vin.pVertexBindingDescriptions = &vb; vin.vertexAttributeDescriptionCount = nva; vin.pVertexAttributeDescriptions = va;
                r = gp_make(m, VK_NULL_HANDLE, pl, rp, 0, 0, &vin, NULL, &p); }
            t_pipe += now_ns() - t0;
            if (r || !p) { pipe_fail[h.stage]++; if (fl < sizeof fails - 40) fl += (size_t)snprintf(fails + fl, sizeof fails - fl, "%s:%d ", e->d_name, (int)r); }
            if (p) vkDestroyPipeline(dev, p, NULL); vkDestroyPipelineLayout(dev, pl, NULL); vkDestroyDescriptorSetLayout(dev, dsl, NULL);
        }
        if (v_err != e0) verr_shaders++;
        vkDestroyShaderModule(dev, m, NULL); step_end();
    }
    closedir(d); free(buf); vkDestroyRenderPass(dev, rp, NULL);
    KV("m6", "shaders=%u skipped_clip_decl=%u driver_crashed=%u module_fail=%u stages=%u/%u/%u/%u/%u/%u vs_pipelines=%u fail=%u cs_pipelines=%u fail=%u shaders_with_verr=%u module_ms=%llu pipeline_ms=%llu", n, skipped_clip, crashed_shaders, mod_fail, by_stage[0], by_stage[1], by_stage[2], by_stage[3], by_stage[4], by_stage[5], pipe_try[0], pipe_fail[0], pipe_try[5], pipe_fail[5], verr_shaders, (unsigned long long)(t_mod / 1000000), (unsigned long long)(t_pipe / 1000000));
    if (fails[0]) INFO("M6 失败清单（前若干）：%s", fails);
    if (!n) { INFO("M6 目录里没有 .xgs"); return; }
    if (skipped_clip) FAIL("M6 %u 个带 ClipDistance 声明的顶点 / 细分着色器因 M3.0 崩溃而未测（要用去声明的着色器集重跑）", skipped_clip);
    if (crashed_shaders) FAIL("M6 %u 个真实着色器让驱动崩溃（逐个见 vkm.driver_crash / 失败清单）", crashed_shaders);
    if (!mod_fail) OK("M6 真实着色器 %u 个全部建模块成功（%llu ms）", n, (unsigned long long)(t_mod / 1000000)); else FAIL("M6 真实着色器：%u / %u 个建模块失败", mod_fail, n);
    if (pipe_try[0] && !pipe_fail[0]) OK("M6 %u 个游戏顶点着色器（含 ClipDistance 声明）全部建成顶点管线（%llu ms）", pipe_try[0], (unsigned long long)(t_pipe / 1000000)); else if (pipe_try[0]) FAIL("M6 顶点管线：%u / %u 失败", pipe_fail[0], pipe_try[0]);
    if (pipe_try[5] && !pipe_fail[5]) OK("M6 %u 个游戏计算着色器全部建成计算管线（语义另看 M1）", pipe_try[5]); else if (pipe_try[5]) FAIL("M6 计算管线：%u / %u 失败", pipe_fail[5], pipe_try[5]);
    if (verr_shaders) INFO("M6 有 %u 个着色器触发验证层错误（首条见 vkm.verr_first）", verr_shaders);
}

int dm_probe_vkmicro(FILE *out, const char *shaders_dir) {
    o = out; g_fail = 0; g_nskip = 0; g_marker[0] = g_skipfile[0] = 0;
    if (shaders_dir) {   // ext/shaders → ext/vkm_running.txt、ext/vkm_skip.txt | marker and skip list beside the shaders dir
        char ext[1100]; snprintf(ext, sizeof ext, "%s", shaders_dir); char *sl = strrchr(ext, '/'); if (sl) *sl = 0;
        snprintf(g_marker, sizeof g_marker, "%s/vkm_running.txt", ext); snprintf(g_skipfile, sizeof g_skipfile, "%s/vkm_skip.txt", ext);
        char line[128]; FILE *f = fopen(g_marker, "r");
        if (f) { if (fgets(line, sizeof line, f)) { line[strcspn(line, "\n")] = 0; FILE *k = fopen(g_skipfile, "a"); if (k) { fprintf(k, "%s\n", line); fclose(k); } KV("driver_crash", "%s", line); INFO("上一次运行驱动在 %s 处把进程弄崩了：这次跳过它", line); } fclose(f); unlink(g_marker); }
        f = fopen(g_skipfile, "r"); if (f) { while (g_nskip < 64 && fgets(line, sizeof line, f)) { line[strcspn(line, "\n")] = 0; if (line[0]) snprintf(g_skip[g_nskip++], sizeof g_skip[0], "%s", line); } fclose(f); }
        if (g_nskip) KV("skip_list", "%d", g_nskip);
    }
    void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL); if (!lib) { INFO("vkmicro：无 libvulkan"); return 0; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
#define LOADI(n) n = (PFN_##n)gipa(NULL, #n);
    LOADI(vkEnumerateInstanceLayerProperties) LOADI(vkCreateInstance)
    uint32_t nl = 0; vkEnumerateInstanceLayerProperties(&nl, NULL); VkLayerProperties lp[32]; if (nl > 32) nl = 32; vkEnumerateInstanceLayerProperties(&nl, lp);
    for (uint32_t i = 0; i < nl; i++) if (!strcmp(lp[i].layerName, "VK_LAYER_KHRONOS_validation")) { has_vvl = 1; KV("validation_layer", "%s spec=%u.%u.%u impl=%u", lp[i].layerName, VK_VERSION_MAJOR(lp[i].specVersion), VK_VERSION_MINOR(lp[i].specVersion), VK_VERSION_PATCH(lp[i].specVersion), lp[i].implementationVersion); }
    if (!has_vvl) KV("validation_layer", "none");
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.pApplicationName = "dm-probe-vkmicro"; ai.apiVersion = VK_API_VERSION_1_1;
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"}; const char *iext[] = {"VK_EXT_debug_utils"};
    VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &ai; ci.enabledLayerCount = has_vvl; ci.ppEnabledLayerNames = layers; ci.enabledExtensionCount = has_vvl; ci.ppEnabledExtensionNames = iext;
    VkResult r = vkCreateInstance(&ci, NULL, &inst); if (r) { FAIL("vkmicro：vkCreateInstance %d", (int)r); return g_fail; }
#define LOADX(n) n = (PFN_##n)gipa(inst, #n);
    VK_FNS(LOADX)
    if (has_vvl && vkCreateDebugUtilsMessengerEXT) { VkDebugUtilsMessengerCreateInfoEXT mi = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT}; mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT; mi.pfnUserCallback = on_msg; vkCreateDebugUtilsMessengerEXT(inst, &mi, NULL, &msgr); }
    uint32_t np = 1; vkEnumeratePhysicalDevices(inst, &np, &pd); if (!np) { FAIL("vkmicro：无物理设备"); return g_fail; }
    VkPhysicalDeviceSubgroupSizeControlFeatures sscf = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES}; VkPhysicalDeviceShaderDrawParametersFeatures sdp = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES, &sscf};
    uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL); VkExtensionProperties *ex = calloc(ne ? ne : 1, sizeof *ex); vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, ex); for (uint32_t i = 0; i < ne; i++) if (!strcmp(ex[i].extensionName, "VK_EXT_subgroup_size_control")) has_sgsc = 1; free(ex);
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, has_sgsc ? (void *)&sdp : (void *)&sdp}; if (!has_sgsc) sdp.pNext = NULL; vkGetPhysicalDeviceFeatures2(pd, &f2); feat = f2.features;
    VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; sgp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES; sgp.pNext = NULL; p2.pNext = &sgp; vkGetPhysicalDeviceProperties2(pd, &p2); vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    // 启用的特性：引擎会用到且设备有的（不含 ClipDistance：设备为 0）| enabled features: those the engine uses and the device has (not ClipDistance: 0 on this device)
    VkPhysicalDeviceFeatures en = {0}; en.robustBufferAccess = feat.robustBufferAccess; en.shaderStorageImageWriteWithoutFormat = feat.shaderStorageImageWriteWithoutFormat; en.shaderStorageImageReadWithoutFormat = feat.shaderStorageImageReadWithoutFormat; en.shaderInt64 = feat.shaderInt64; en.shaderInt16 = feat.shaderInt16; en.geometryShader = feat.geometryShader; en.tessellationShader = feat.tessellationShader;
    en.shaderImageGatherExtended = feat.shaderImageGatherExtended; en.shaderStorageImageExtendedFormats = feat.shaderStorageImageExtendedFormats; en.samplerAnisotropy = feat.samplerAnisotropy; en.fragmentStoresAndAtomics = feat.fragmentStoresAndAtomics; en.independentBlend = feat.independentBlend; en.dualSrcBlend = feat.dualSrcBlend; en.logicOp = feat.logicOp; en.multiDrawIndirect = feat.multiDrawIndirect; en.depthClamp = feat.depthClamp; en.depthBiasClamp = feat.depthBiasClamp; en.occlusionQueryPrecise = feat.occlusionQueryPrecise; en.shaderClipDistance = feat.shaderClipDistance; en.shaderCullDistance = feat.shaderCullDistance;
    sdp.shaderDrawParameters = sdp.shaderDrawParameters; sscf.subgroupSizeControl = sscf.subgroupSizeControl; sscf.computeFullSubgroups = sscf.computeFullSubgroups;
    float prio = 1; VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    const char *dext[] = {"VK_EXT_subgroup_size_control"}; VkDeviceCreateInfo di = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &sdp}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &en; di.enabledExtensionCount = has_sgsc; di.ppEnabledExtensionNames = dext;
    r = vkCreateDevice(pd, &di, NULL, &dev); if (r) { FAIL("vkmicro：vkCreateDevice %d", (int)r); return g_fail; }
#define LOADD(n) { PFN_vkVoidFunction f_ = vkGetDeviceProcAddr(dev, #n); if (f_) n = (PFN_##n)f_; }
    VK_FNS(LOADD)
    pfn_fmt = (PFN_vkGetPhysicalDeviceFormatProperties)gipa(inst, "vkGetPhysicalDeviceFormatProperties");
    vkGetDeviceQueue(dev, 0, 0, &q); VkCommandPoolCreateInfo pi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; vkCreateCommandPool(dev, &pi, NULL, &pool); VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; vkAllocateCommandBuffers(dev, &cai, &cb);
    INFO("vkmicro：设备就绪（验证层 %s，subgroup_size_control %s，子组 %u 道）", has_vvl ? "开" : "无", has_sgsc ? "有" : "无", sgp.subgroupSize);
    m1_subgroup(); m2_quad(); m3_clip(); m4_depth(VK_FORMAT_D16_UNORM, "D16"); m4_depth(VK_FORMAT_X8_D24_UNORM_PACK32, "X8D24"); m5_store(); m7_items();
    if (shaders_dir) m6_real(shaders_dir); else { INFO("M6 未指定真实着色器目录，跳过"); KV("m6", "skipped"); }
    vkDeviceWaitIdle(dev); KV("verr", "%d", v_err); KV("vwarn", "%d", v_warn); for (int i = 0; i < v_nfirst; i++) { char k[24]; snprintf(k, sizeof k, "verr_first.%d", i); KV(k, "%s", v_first[i]); }
    if (has_vvl && !v_err) OK("vkmicro：验证层 0 错误（%d 警告）", v_warn); else if (has_vvl) INFO("vkmicro：验证层 %d 错误 %d 警告", v_err, v_warn);
    vkDestroyCommandPool(dev, pool, NULL); vkDestroyDevice(dev, NULL); if (msgr) vkDestroyDebugUtilsMessengerEXT(inst, msgr, NULL); vkDestroyInstance(inst, NULL); dlclose(lib);
    return g_fail;
}
