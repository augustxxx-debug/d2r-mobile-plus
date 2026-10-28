// 说明见 xbg_laneid_probe_run.h。| See xbg_laneid_probe_run.h.
#include "xbg_laneid_probe_run.h"
#include <cstring>
int xbg_laneid_probe_run(const XbgProbeVk &vk, const XbgProbeIn &in, XbgProbeOut &res) {
    res = XbgProbeOut{};
    VkDevice d = in.device; VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDescriptorSetLayout dsl = VK_NULL_HANDLE; VkPipelineLayout pl = VK_NULL_HANDLE;
    VkShaderModule sm = VK_NULL_HANDLE; VkPipeline pipe = VK_NULL_HANDLE; VkDescriptorPool dp = VK_NULL_HANDLE; VkCommandBuffer cb = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE;
    int rc = -1; const VkDeviceSize bytes = 64 * 4;
    void *map = nullptr;
    // 先解除映射再释放（一审 A3：旧版成功路径和映射后的 11 个失败出口都没有 vkUnmapMemory）| unmap before free (first audit A3)
    auto cleanup = [&] { if (map) { vk.UnmapMemory(d, mem); map = nullptr; } if (fence) vk.DestroyFence(d, fence, nullptr); if (cb) vk.FreeCommandBuffers(d, in.command_pool, 1, &cb); if (dp) vk.DestroyDescriptorPool(d, dp, nullptr); if (pipe) vk.DestroyPipeline(d, pipe, nullptr);
        if (sm) vk.DestroyShaderModule(d, sm, nullptr); if (pl) vk.DestroyPipelineLayout(d, pl, nullptr); if (dsl) vk.DestroyDescriptorSetLayout(d, dsl, nullptr); if (mem) vk.FreeMemory(d, mem, nullptr); if (buf) vk.DestroyBuffer(d, buf, nullptr); };
    do {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = bytes; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE; if (vk.CreateBuffer(d, &bi, nullptr, &buf) != VK_SUCCESS) break;
        VkMemoryRequirements req; vk.GetBufferMemoryRequirements(d, buf, &req); uint32_t type = UINT32_MAX; const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < in.memories->memoryTypeCount; i++) if ((req.memoryTypeBits & (1u << i)) && (in.memories->memoryTypes[i].propertyFlags & want) == want) { type = i; break; }
        if (type == UINT32_MAX) break; VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = type; if (vk.AllocateMemory(d, &ai, nullptr, &mem) != VK_SUCCESS || vk.BindBufferMemory(d, buf, mem, 0) != VK_SUCCESS) break;
        if (vk.MapMemory(d, mem, 0, bytes, 0, &map) != VK_SUCCESS) { map = nullptr; break; } memset(map, 0xff, size_t(bytes));
        VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dli.bindingCount = 1; dli.pBindings = &b; if (vk.CreateDescriptorSetLayout(d, &dli, nullptr, &dsl) != VK_SUCCESS) break;
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 1; pli.pSetLayouts = &dsl; if (vk.CreatePipelineLayout(d, &pli, nullptr, &pl) != VK_SUCCESS) break;
        VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = in.code_bytes; smi.pCode = in.code; if (vk.CreateShaderModule(d, &smi, nullptr, &sm) != VK_SUCCESS) break;
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, "main", nullptr}; ci.layout = pl; if (vk.CreateComputePipelines(d, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe) != VK_SUCCESS) break;
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}; VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets = 1; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps; if (vk.CreateDescriptorPool(d, &dpi, nullptr, &dp) != VK_SUCCESS) break;
        VkDescriptorSetAllocateInfo dsa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dsa.descriptorPool = dp; dsa.descriptorSetCount = 1; dsa.pSetLayouts = &dsl; VkDescriptorSet ds; if (vk.AllocateDescriptorSets(d, &dsa, &ds) != VK_SUCCESS) break;
        VkDescriptorBufferInfo dbi{buf, 0, bytes}; VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = ds; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &dbi; vk.UpdateDescriptorSets(d, 1, &w, 0, nullptr);
        VkCommandBufferAllocateInfo cba{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cba.commandPool = in.command_pool; cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cba.commandBufferCount = 1; if (vk.AllocateCommandBuffers(d, &cba, &cb) != VK_SUCCESS) break;
        VkCommandBufferBeginInfo bgi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bgi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; if (vk.BeginCommandBuffer(cb, &bgi) != VK_SUCCESS) break;
        vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe); vk.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, nullptr); vk.CmdDispatch(cb, 1, 1, 1);
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT; vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        if (vk.EndCommandBuffer(cb) != VK_SUCCESS) break;
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; if (vk.CreateFence(d, &fi, nullptr, &fence) != VK_SUCCESS) break;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; if (vk.QueueSubmit(in.queue, 1, &si, fence) != VK_SUCCESS) break;
        res.submitted = true;
        if ((res.wait = vk.WaitForFences(d, 1, &fence, VK_TRUE, in.timeout_ns)) != VK_SUCCESS) break;
        const uint32_t *o = static_cast<const uint32_t *>(map); uint64_t seen = 0; for (unsigned i = 0; i < 64; i++) { uint32_t id = o[i] & 0xffff; if (id < 64) seen |= uint64_t(1) << id; }
        for (unsigned i = 0; i < 64; i++) res.distinct += unsigned((seen >> i) & 1); rc = res.distinct >= in.expect ? 1 : 0;
    } while (false);
    // 提交了却没等到 fence（超时或别的错）：GPU 可能还在用这些对象，先让队列静止；设备丢失可直接回收；队列等不到静止就有意不回收（一审 A4）
    // Submitted but the fence was not reached (timeout or another error): the GPU may still use these objects, so the queue goes idle first; a lost device may be torn
    // down at once; if the queue cannot be brought idle the objects are deliberately kept (first audit A4)
    if (res.submitted && res.wait != VK_SUCCESS && res.wait != VK_ERROR_DEVICE_LOST) {
        res.idle = vk.QueueWaitIdle(in.queue);
        if (res.idle != VK_SUCCESS && res.idle != VK_ERROR_DEVICE_LOST) { res.released = false; return -1; }
    }
    cleanup(); return rc;
}
