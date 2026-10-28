// 探针的 Vulkan 部分：设备 / 驱动身份（支持记录要记的 deviceID / driverVersion）与引擎准入要看的能力：队列族、子组、格式（深度比较过滤、BC / ASTC、无格式存储图像）、
// 描述符上限、内存类型与一致性、扩展。只查不画。通过 libvulkan.so 动态加载（没有 Vulkan 的机器只记一行）。
// The probe's Vulkan part: device / driver identity (deviceID / driverVersion for the support record) and the capabilities the engine's admission looks at: queue families, subgroups,
// formats (depth compare filtering, BC / ASTC, formatless storage images), descriptor limits, memory types and coherence, extensions. Query only, no drawing. Loaded dynamically through libvulkan.so.
#include "dm_probe.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

static FILE *o;
#define KV(k, ...) do { fprintf(o, "KV    vk.%s=", k); fprintf(o, __VA_ARGS__); fputc('\n', o); } while (0)
#define INFO(...) do { fprintf(o, "INFO  " __VA_ARGS__); fputc('\n', o); } while (0)

static const char *fmt_name(VkFormat f) {
    switch (f) { case VK_FORMAT_D16_UNORM: return "D16"; case VK_FORMAT_X8_D24_UNORM_PACK32: return "X8D24"; case VK_FORMAT_D24_UNORM_S8_UINT: return "D24S8"; case VK_FORMAT_D32_SFLOAT: return "D32"; case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32S8";
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return "BC1"; case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return "BC1s"; case VK_FORMAT_BC2_UNORM_BLOCK: return "BC2"; case VK_FORMAT_BC3_UNORM_BLOCK: return "BC3"; case VK_FORMAT_BC3_SRGB_BLOCK: return "BC3s";
        case VK_FORMAT_BC4_UNORM_BLOCK: return "BC4"; case VK_FORMAT_BC5_UNORM_BLOCK: return "BC5"; case VK_FORMAT_BC6H_UFLOAT_BLOCK: return "BC6H"; case VK_FORMAT_BC7_UNORM_BLOCK: return "BC7"; case VK_FORMAT_BC7_SRGB_BLOCK: return "BC7s";
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return "ASTC4x4"; case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return "ASTC4x4s"; case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return "ETC2"; case VK_FORMAT_R8G8B8A8_UNORM: return "RGBA8"; case VK_FORMAT_R8G8B8A8_SRGB: return "RGBA8s";
        case VK_FORMAT_R16G16B16A16_SFLOAT: return "RGBA16F"; case VK_FORMAT_R32G32B32A32_SFLOAT: return "RGBA32F"; case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "B10G11R11F"; case VK_FORMAT_R32_UINT: return "R32U"; case VK_FORMAT_R8_UNORM: return "R8"; case VK_FORMAT_R16_UNORM: return "R16"; case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2BGR10";
        default: return "?"; }
}

void dm_probe_vulkan(FILE *out) {
    o = out;
    void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL); if (!lib) { INFO("Vulkan：libvulkan.so 打不开"); KV("present", "0"); return; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr"); if (!gipa) { INFO("Vulkan：无 vkGetInstanceProcAddr"); return; }
#define GI(name) PFN_##name name = (PFN_##name)gipa(NULL, #name)
#define GX(inst, name) PFN_##name name = (PFN_##name)gipa(inst, #name)
    GI(vkCreateInstance); GI(vkEnumerateInstanceVersion); GI(vkEnumerateInstanceExtensionProperties);
    uint32_t iv = VK_API_VERSION_1_0; if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&iv);
    KV("instance_version", "%u.%u.%u", VK_VERSION_MAJOR(iv), VK_VERSION_MINOR(iv), VK_VERSION_PATCH(iv));
    uint32_t nie = 0; vkEnumerateInstanceExtensionProperties(NULL, &nie, NULL); KV("instance_ext_count", "%u", nie);
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.pApplicationName = "dm-probe"; ai.apiVersion = iv >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    const char *iext[] = {"VK_KHR_get_physical_device_properties2"}; VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &ai;
    if (iv < VK_API_VERSION_1_1) { ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = iext; }
    VkInstance inst = VK_NULL_HANDLE; VkResult r = vkCreateInstance(&ci, NULL, &inst);
    if (r != VK_SUCCESS) { INFO("Vulkan：vkCreateInstance 失败 %d", (int)r); KV("present", "0"); return; }
    KV("present", "1");
    GX(inst, vkEnumeratePhysicalDevices); GX(inst, vkGetPhysicalDeviceProperties2); GX(inst, vkGetPhysicalDeviceFeatures2); GX(inst, vkGetPhysicalDeviceMemoryProperties); GX(inst, vkGetPhysicalDeviceQueueFamilyProperties);
    GX(inst, vkEnumerateDeviceExtensionProperties); GX(inst, vkGetPhysicalDeviceFormatProperties); GX(inst, vkDestroyInstance); GX(inst, vkGetPhysicalDeviceProperties); GX(inst, vkGetPhysicalDeviceFeatures);
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, NULL); KV("physical_devices", "%u", n);
    if (!n) { vkDestroyInstance(inst, NULL); return; }
    VkPhysicalDevice pds[4]; if (n > 4) n = 4; vkEnumeratePhysicalDevices(inst, &n, pds); VkPhysicalDevice pd = pds[0];
    // 扩展 | extensions
    uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL); VkExtensionProperties *ex = calloc(ne ? ne : 1, sizeof *ex); vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, ex);
    int has_driver_props = 0, has_sgsc = 0, has_fff2 = 0, has_present_timing = 0, has_timeline = 0, has_desc_idx = 0, has_ray_query = 0, has_accel = 0, has_maint4 = 0, has_create_renderpass2 = 0;
    KV("device_ext_count", "%u", ne);
    for (uint32_t i = 0; i < ne; i++) {
        const char *e = ex[i].extensionName;
        if (!strcmp(e, "VK_KHR_driver_properties")) has_driver_props = 1; if (!strcmp(e, "VK_EXT_subgroup_size_control")) has_sgsc = 1; if (!strcmp(e, "VK_KHR_format_feature_flags2")) has_fff2 = 1;
        if (!strcmp(e, "VK_GOOGLE_display_timing") || !strcmp(e, "VK_EXT_present_timing")) has_present_timing = 1; if (!strcmp(e, "VK_KHR_timeline_semaphore")) has_timeline = 1;
        if (!strcmp(e, "VK_EXT_descriptor_indexing")) has_desc_idx = 1; if (!strcmp(e, "VK_KHR_ray_query")) has_ray_query = 1; if (!strcmp(e, "VK_KHR_acceleration_structure")) has_accel = 1;
        if (!strcmp(e, "VK_KHR_maintenance4")) has_maint4 = 1; if (!strcmp(e, "VK_KHR_create_renderpass2")) has_create_renderpass2 = 1;
    }
    fprintf(o, "INFO  Vulkan 设备扩展（%u）：", ne); for (uint32_t i = 0; i < ne; i++) fprintf(o, "%s%s", i ? " " : "", ex[i].extensionName); fputc('\n', o); free(ex);
    KV("ext.driver_properties", "%d", has_driver_props); KV("ext.subgroup_size_control", "%d", has_sgsc); KV("ext.format_feature_flags2", "%d", has_fff2); KV("ext.present_timing", "%d", has_present_timing);
    KV("ext.timeline_semaphore", "%d", has_timeline); KV("ext.descriptor_indexing", "%d", has_desc_idx); KV("ext.ray_query", "%d", has_ray_query); KV("ext.acceleration_structure", "%d", has_accel); KV("ext.maintenance4", "%d", has_maint4); KV("ext.create_renderpass2", "%d", has_create_renderpass2);
    // 属性 | properties
    VkPhysicalDeviceDriverProperties drv = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceSubgroupProperties sg = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, has_driver_props ? (void *)&drv : NULL};
    VkPhysicalDeviceSubgroupSizeControlProperties sgsc = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES, &sg};
    VkPhysicalDeviceIDProperties idp = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, has_sgsc ? (void *)&sgsc : (void *)&sg};
    VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &idp};
    if (vkGetPhysicalDeviceProperties2) vkGetPhysicalDeviceProperties2(pd, &p2); else vkGetPhysicalDeviceProperties(pd, &p2.properties);
    VkPhysicalDeviceProperties *p = &p2.properties; VkPhysicalDeviceLimits *L = &p->limits;
    KV("device_name", "%s", p->deviceName); KV("vendor_id", "%#x", p->vendorID); KV("device_id", "%#x", p->deviceID); KV("device_type", "%d", (int)p->deviceType);
    KV("api_version", "%u.%u.%u", VK_VERSION_MAJOR(p->apiVersion), VK_VERSION_MINOR(p->apiVersion), VK_VERSION_PATCH(p->apiVersion)); KV("driver_version_raw", "%#x", p->driverVersion);
    if (p->vendorID == 0x5143) KV("driver_version_qcom", "%u.%u.%u", VK_VERSION_MAJOR(p->driverVersion), VK_VERSION_MINOR(p->driverVersion), VK_VERSION_PATCH(p->driverVersion));   // Qualcomm 按 Vulkan 编码 | Qualcomm uses the Vulkan encoding
    if (p->vendorID == 0x13B5) KV("driver_version_arm", "%u.%u.%u", VK_VERSION_MAJOR(p->driverVersion), VK_VERSION_MINOR(p->driverVersion), VK_VERSION_PATCH(p->driverVersion));
    if (has_driver_props) { KV("driver_id", "%d", (int)drv.driverID); KV("driver_name", "%s", drv.driverName); KV("driver_info", "%s", drv.driverInfo); KV("conformance", "%u.%u.%u.%u", drv.conformanceVersion.major, drv.conformanceVersion.minor, drv.conformanceVersion.subminor, drv.conformanceVersion.patch); }
    { char u[80]; for (int i = 0; i < 16; i++) snprintf(u + i * 2, 3, "%02x", idp.deviceUUID[i]); KV("device_uuid", "%s", u); for (int i = 0; i < 16; i++) snprintf(u + i * 2, 3, "%02x", idp.driverUUID[i]); KV("driver_uuid", "%s", u); }
    KV("subgroup_size", "%u", sg.subgroupSize); KV("subgroup_stages", "%#x", sg.supportedStages); KV("subgroup_ops", "%#x", sg.supportedOperations); KV("subgroup_quad_all_stages", "%d", (int)sg.quadOperationsInAllStages);
    if (has_sgsc) { KV("subgroup_min", "%u", sgsc.minSubgroupSize); KV("subgroup_max", "%u", sgsc.maxSubgroupSize); KV("subgroup_sc_stages", "%#x", sgsc.requiredSubgroupSizeStages); }
    KV("max_image_2d", "%u", L->maxImageDimension2D); KV("max_bound_sets", "%u", L->maxBoundDescriptorSets); KV("max_per_stage_samplers", "%u", L->maxPerStageDescriptorSamplers); KV("max_per_stage_ubo", "%u", L->maxPerStageDescriptorUniformBuffers);
    KV("max_per_stage_ssbo", "%u", L->maxPerStageDescriptorStorageBuffers); KV("max_per_stage_sampled_images", "%u", L->maxPerStageDescriptorSampledImages); KV("max_per_stage_storage_images", "%u", L->maxPerStageDescriptorStorageImages); KV("max_per_stage_resources", "%u", L->maxPerStageResources);
    KV("max_set_samplers", "%u", L->maxDescriptorSetSamplers); KV("max_set_ubo_dynamic", "%u", L->maxDescriptorSetUniformBuffersDynamic); KV("max_set_ssbo_dynamic", "%u", L->maxDescriptorSetStorageBuffersDynamic); KV("max_set_sampled_images", "%u", L->maxDescriptorSetSampledImages);
    KV("max_push_constants", "%u", L->maxPushConstantsSize); KV("max_ubo_range", "%u", L->maxUniformBufferRange); KV("max_ssbo_range", "%u", L->maxStorageBufferRange); KV("min_ubo_align", "%llu", (unsigned long long)L->minUniformBufferOffsetAlignment); KV("min_ssbo_align", "%llu", (unsigned long long)L->minStorageBufferOffsetAlignment);
    KV("min_texel_buffer_align", "%llu", (unsigned long long)L->minTexelBufferOffsetAlignment); KV("non_coherent_atom", "%llu", (unsigned long long)L->nonCoherentAtomSize); KV("timestamp_period_ns", "%g", L->timestampPeriod); KV("timestamp_compute_graphics", "%d", (int)L->timestampComputeAndGraphics);
    KV("max_compute_wg_invocations", "%u", L->maxComputeWorkGroupInvocations); KV("max_compute_shared", "%u", L->maxComputeSharedMemorySize); KV("max_viewports", "%u", L->maxViewports); KV("max_color_attachments", "%u", L->maxColorAttachments);
    KV("framebuffer_color_samples", "%#x", L->framebufferColorSampleCounts); KV("max_sampler_anisotropy", "%g", L->maxSamplerAnisotropy); KV("max_memory_allocations", "%u", L->maxMemoryAllocationCount); KV("buffer_image_granularity", "%llu", (unsigned long long)L->bufferImageGranularity);
    // 特性 | features
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; if (vkGetPhysicalDeviceFeatures2) vkGetPhysicalDeviceFeatures2(pd, &f2); else vkGetPhysicalDeviceFeatures(pd, &f2.features);
    VkPhysicalDeviceFeatures *f = &f2.features;
    KV("feat.geometry", "%d", f->geometryShader); KV("feat.tessellation", "%d", f->tessellationShader); KV("feat.bc", "%d", f->textureCompressionBC); KV("feat.astc_ldr", "%d", f->textureCompressionASTC_LDR); KV("feat.etc2", "%d", f->textureCompressionETC2);
    KV("feat.storage_image_read_wo_format", "%d", f->shaderStorageImageReadWithoutFormat); KV("feat.storage_image_write_wo_format", "%d", f->shaderStorageImageWriteWithoutFormat); KV("feat.shader_int16", "%d", f->shaderInt16); KV("feat.shader_int64", "%d", f->shaderInt64); KV("feat.shader_float64", "%d", f->shaderFloat64);
    KV("feat.independent_blend", "%d", f->independentBlend); KV("feat.dual_src_blend", "%d", f->dualSrcBlend); KV("feat.logic_op", "%d", f->logicOp); KV("feat.multi_draw_indirect", "%d", f->multiDrawIndirect); KV("feat.depth_clamp", "%d", f->depthClamp); KV("feat.depth_bias_clamp", "%d", f->depthBiasClamp);
    KV("feat.fill_mode_nonsolid", "%d", f->fillModeNonSolid); KV("feat.wide_lines", "%d", f->wideLines); KV("feat.sampler_anisotropy", "%d", f->samplerAnisotropy); KV("feat.occlusion_query_precise", "%d", f->occlusionQueryPrecise); KV("feat.pipeline_statistics_query", "%d", f->pipelineStatisticsQuery);
    KV("feat.vertex_pipeline_stores_atomics", "%d", f->vertexPipelineStoresAndAtomics); KV("feat.fragment_stores_atomics", "%d", f->fragmentStoresAndAtomics); KV("feat.shader_image_gather_extended", "%d", f->shaderImageGatherExtended); KV("feat.storage_image_extended_formats", "%d", f->shaderStorageImageExtendedFormats);
    KV("feat.storage_image_multisample", "%d", f->shaderStorageImageMultisample); KV("feat.clip_distance", "%d", f->shaderClipDistance); KV("feat.cull_distance", "%d", f->shaderCullDistance); KV("feat.sparse_binding", "%d", f->sparseBinding); KV("feat.robust_buffer_access", "%d", f->robustBufferAccess);
    // 队列族 | queue families
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL); VkQueueFamilyProperties qf[8]; if (nq > 8) nq = 8; vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf);
    KV("queue_families", "%u", nq); for (uint32_t i = 0; i < nq; i++) { char k[32]; snprintf(k, sizeof k, "queue.%u", i); KV(k, "flags=%#x count=%u ts_bits=%u", qf[i].queueFlags, qf[i].queueCount, qf[i].timestampValidBits); }
    // 内存 | memory
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    KV("memory_heaps", "%u", mp.memoryHeapCount); for (uint32_t i = 0; i < mp.memoryHeapCount; i++) { char k[32]; snprintf(k, sizeof k, "heap.%u", i); KV(k, "size_mib=%llu flags=%#x", (unsigned long long)(mp.memoryHeaps[i].size >> 20), mp.memoryHeaps[i].flags); }
    KV("memory_types", "%u", mp.memoryTypeCount); for (uint32_t i = 0; i < mp.memoryTypeCount; i++) { char k[32]; snprintf(k, sizeof k, "memtype.%u", i); KV(k, "heap=%u flags=%#x", mp.memoryTypes[i].heapIndex, mp.memoryTypes[i].propertyFlags); }
    // 格式 | formats
    static const VkFormat fmts[] = { VK_FORMAT_D16_UNORM, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK, VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_BC6H_UFLOAT_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK,
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_ASTC_4x4_SRGB_BLOCK, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_R32_UINT, VK_FORMAT_R8_UNORM, VK_FORMAT_R16_UNORM, VK_FORMAT_A2B10G10R10_UNORM_PACK32 };
    for (size_t i = 0; i < sizeof fmts / sizeof *fmts; i++) {
        VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(pd, fmts[i], &fp); char k[32]; snprintf(k, sizeof k, "fmt.%s", fmt_name(fmts[i]));
        VkFormatFeatureFlags t = fp.optimalTilingFeatures;
        KV(k, "opt=%#x lin=%#x buf=%#x sampled=%d filter=%d depth=%d color=%d storage=%d atomic=%d blit_src=%d blit_dst=%d transfer=%d", t, fp.linearTilingFeatures, fp.bufferFeatures,
           !!(t & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT), !!(t & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT), !!(t & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT), !!(t & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
           !!(t & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT), !!(t & VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT), !!(t & VK_FORMAT_FEATURE_BLIT_SRC_BIT), !!(t & VK_FORMAT_FEATURE_BLIT_DST_BIT), !!(t & (VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)));
    }
    INFO("Vulkan：%s（vendor %#x device %#x driver %#x api %u.%u）子组 %u，队列族 %u，堆 %u", p->deviceName, p->vendorID, p->deviceID, p->driverVersion, VK_VERSION_MAJOR(p->apiVersion), VK_VERSION_MINOR(p->apiVersion), sg.subgroupSize, nq, mp.memoryHeapCount);
    vkDestroyInstance(inst, NULL); dlclose(lib);
}
