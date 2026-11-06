// lane 号启动自检的执行体（从 xbg_game.cpp laneid_probe 抽出）：建一个 64 线程、有 shuffle 的计算管线，各线程写自己的 gl_SubgroupInvocationID，数不同值。
// Vulkan 调用全走函数表，单测用假 Vulkan 注入失败与超时（tests/laneid_probe_run_test.cpp）。
// 回收规则（一审 A3 / A4）：映射过的内存释放前先 vkUnmapMemory（每个失败出口都一样）；fence 等待不是 VK_SUCCESS 时，先 vkQueueWaitIdle 让队列静止再回收，
// 设备丢失（VK_ERROR_DEVICE_LOST）可以直接回收，队列等不到静止就有意不回收这一小撮对象（一次性泄漏，好过 GPU 还在用就释放）。
// Execution of the lane-id start-up self-test (taken out of xbg_game.cpp laneid_probe): a 64-invocation compute pipeline with a shuffle, each invocation writes its
// gl_SubgroupInvocationID, the distinct values are counted. Every Vulkan call goes through a function table so the unit test injects failures and timeouts with a fake Vulkan
// (tests/laneid_probe_run_test.cpp). Teardown rules (first audit A3 / A4): mapped memory is unmapped before it is freed, on every failure exit; when the fence wait is not
// VK_SUCCESS the queue is first waited idle, a lost device (VK_ERROR_DEVICE_LOST) may be torn down at once, and if the queue cannot be brought idle the handful of
// objects is deliberately left alive (a one-time leak beats freeing what the GPU may still use).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan.h>
struct XbgProbeVk {
    PFN_vkCreateBuffer CreateBuffer; PFN_vkDestroyBuffer DestroyBuffer; PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory; PFN_vkFreeMemory FreeMemory; PFN_vkBindBufferMemory BindBufferMemory; PFN_vkMapMemory MapMemory; PFN_vkUnmapMemory UnmapMemory;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout; PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreatePipelineLayout CreatePipelineLayout; PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateShaderModule CreateShaderModule; PFN_vkDestroyShaderModule DestroyShaderModule;
    PFN_vkCreateComputePipelines CreateComputePipelines; PFN_vkDestroyPipeline DestroyPipeline;
    PFN_vkCreateDescriptorPool CreateDescriptorPool; PFN_vkDestroyDescriptorPool DestroyDescriptorPool; PFN_vkAllocateDescriptorSets AllocateDescriptorSets; PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers; PFN_vkFreeCommandBuffers FreeCommandBuffers; PFN_vkBeginCommandBuffer BeginCommandBuffer; PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdBindPipeline CmdBindPipeline; PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets; PFN_vkCmdDispatch CmdDispatch; PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCreateFence CreateFence; PFN_vkDestroyFence DestroyFence; PFN_vkQueueSubmit QueueSubmit; PFN_vkWaitForFences WaitForFences; PFN_vkQueueWaitIdle QueueWaitIdle;
};
struct XbgProbeIn {
    VkDevice device; VkQueue queue; VkCommandPool command_pool; const VkPhysicalDeviceMemoryProperties *memories;
    const uint32_t *code; size_t code_bytes; unsigned expect; uint64_t timeout_ns;
};
struct XbgProbeOut { unsigned distinct = 0; VkResult wait = VK_NOT_READY; VkResult idle = VK_NOT_READY; bool submitted = false, released = true; };
// 返回 1 可用、0 不可用、-1 没跑成（含等待超时）| returns 1 usable, 0 unusable, -1 could not run (a wait timeout included)
int xbg_laneid_probe_run(const XbgProbeVk &vk, const XbgProbeIn &in, XbgProbeOut &out);
