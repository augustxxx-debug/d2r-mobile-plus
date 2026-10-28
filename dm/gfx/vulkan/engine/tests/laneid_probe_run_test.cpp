// lane 号启动自检的回收（一审 A3 / A4）：假 Vulkan 记录每个对象的创建 / 释放、内存映射状态与「已提交、GPU 可能还在用」状态。
// 用例：成功路径；15 个可失败调用逐个注入失败（每个失败出口）；fence 等待超时后队列静止成功 / 设备丢失 / 静止失败；fence 直接报设备丢失。
// 断言：映射着的内存不得释放；GPU 可能还在用时不得销毁任何对象；除「队列等不到静止」有意保留外，所有对象都回收；超时可控（等待时限来自调用方）。
// clang++ -std=c++17 -I<只放 vulkan/ 头的目录（NDK 的 sysroot/usr/include/vulkan 要单独链出来，免得混进安卓 libc 头）> tests/laneid_probe_run_test.cpp xbg_laneid_probe_run.cpp -o <仓外产物>
// Build: -I<a directory holding only vulkan/ headers (link the NDK sysroot/usr/include/vulkan out on its own so Android libc headers stay out)>
// Teardown of the lane-id start-up self-test (first audit A3 / A4): a fake Vulkan records every object's creation / release, the memory mapping state and the
// "submitted, the GPU may still use it" state. Cases: the success path; each of the 15 fallible calls failing in turn (every failure exit); a fence wait timeout followed by
// the queue going idle / the device lost / the idle wait failing; the fence itself reporting a lost device. Asserts: mapped memory is never freed; nothing is destroyed
// while the GPU may still use it; everything is released except the deliberate keep when the queue cannot be brought idle; the timeout is controllable (the caller's limit).
#include "../xbg_laneid_probe_run.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
namespace {
struct Fake {
    int fallible = 0, fail_at = -1; VkResult wait_rc = VK_SUCCESS, idle_rc = VK_SUCCESS; uint64_t next = 0x1000, timeout_seen = 0;
    std::set<uint64_t> live; bool mapped = false, in_flight = false; int idle_calls = 0; std::string violation; uint32_t buffer[64];
    VkResult step() { return fallible++ == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS; }
    template <class H> VkResult make(H *h) { VkResult r = step(); if (r == VK_SUCCESS) { *h = reinterpret_cast<H>(next); live.insert(next); next += 0x10; } return r; }
    template <class H> void drop(H h, const char *what) {
        if (!h) return; if (in_flight && violation.empty()) violation = std::string("destroyed ") + what + " while the GPU may still use it";
        if (!live.erase(reinterpret_cast<uint64_t>(h)) && violation.empty()) violation = std::string("double or unknown release of ") + what;
    }
} F;
VkDevice DEV = reinterpret_cast<VkDevice>(uintptr_t(0x77)); VkQueue Q = reinterpret_cast<VkQueue>(uintptr_t(0x78));
VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice, const VkBufferCreateInfo *, const VkAllocationCallbacks *, VkBuffer *b) { return F.make(b); }
VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice, VkBuffer b, const VkAllocationCallbacks *) { F.drop(b, "buffer"); }
VKAPI_ATTR void VKAPI_CALL GetReq(VkDevice, VkBuffer, VkMemoryRequirements *r) { r->size = 256; r->alignment = 16; r->memoryTypeBits = 1; }
VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice, const VkMemoryAllocateInfo *, const VkAllocationCallbacks *, VkDeviceMemory *m) { return F.make(m); }
VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice, VkDeviceMemory m, const VkAllocationCallbacks *) { if (m && F.mapped && F.violation.empty()) F.violation = "freed memory that is still host mapped"; F.drop(m, "memory"); }
VKAPI_ATTR VkResult VKAPI_CALL BindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) { return F.step(); }
VKAPI_ATTR VkResult VKAPI_CALL MapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void **p) { VkResult r = F.step(); if (r == VK_SUCCESS) { F.mapped = true; *p = F.buffer; } return r; }
VKAPI_ATTR void VKAPI_CALL UnmapMemory(VkDevice, VkDeviceMemory) { if (!F.mapped && F.violation.empty()) F.violation = "unmapped memory that is not mapped"; if (F.in_flight && F.violation.empty()) F.violation = "unmapped while the GPU may still write"; F.mapped = false; }
VKAPI_ATTR VkResult VKAPI_CALL CreateDSL(VkDevice, const VkDescriptorSetLayoutCreateInfo *, const VkAllocationCallbacks *, VkDescriptorSetLayout *x) { return F.make(x); }
VKAPI_ATTR void VKAPI_CALL DestroyDSL(VkDevice, VkDescriptorSetLayout x, const VkAllocationCallbacks *) { F.drop(x, "set layout"); }
VKAPI_ATTR VkResult VKAPI_CALL CreatePL(VkDevice, const VkPipelineLayoutCreateInfo *, const VkAllocationCallbacks *, VkPipelineLayout *x) { return F.make(x); }
VKAPI_ATTR void VKAPI_CALL DestroyPL(VkDevice, VkPipelineLayout x, const VkAllocationCallbacks *) { F.drop(x, "pipeline layout"); }
VKAPI_ATTR VkResult VKAPI_CALL CreateSM(VkDevice, const VkShaderModuleCreateInfo *, const VkAllocationCallbacks *, VkShaderModule *x) { return F.make(x); }
VKAPI_ATTR void VKAPI_CALL DestroySM(VkDevice, VkShaderModule x, const VkAllocationCallbacks *) { F.drop(x, "shader module"); }
VKAPI_ATTR VkResult VKAPI_CALL CreateCP(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo *, const VkAllocationCallbacks *, VkPipeline *x) { return F.make(x); }
VKAPI_ATTR void VKAPI_CALL DestroyPipe(VkDevice, VkPipeline x, const VkAllocationCallbacks *) { F.drop(x, "pipeline"); }
VKAPI_ATTR VkResult VKAPI_CALL CreateDP(VkDevice, const VkDescriptorPoolCreateInfo *, const VkAllocationCallbacks *, VkDescriptorPool *x) { return F.make(x); }
VKAPI_ATTR void VKAPI_CALL DestroyDP(VkDevice, VkDescriptorPool x, const VkAllocationCallbacks *) { F.drop(x, "descriptor pool"); }
VKAPI_ATTR VkResult VKAPI_CALL AllocDS(VkDevice, const VkDescriptorSetAllocateInfo *, VkDescriptorSet *s) { VkResult r = F.step(); if (r == VK_SUCCESS) *s = reinterpret_cast<VkDescriptorSet>(uintptr_t(0x55)); return r; }
VKAPI_ATTR void VKAPI_CALL UpdateDS(VkDevice, uint32_t, const VkWriteDescriptorSet *, uint32_t, const VkCopyDescriptorSet *) {}
VKAPI_ATTR VkResult VKAPI_CALL AllocCB(VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *c) { VkResult r = F.step(); if (r == VK_SUCCESS) { *c = reinterpret_cast<VkCommandBuffer>(uintptr_t(F.next)); F.live.insert(F.next); F.next += 0x10; } return r; }
VKAPI_ATTR void VKAPI_CALL FreeCB(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *c) { F.drop(*c, "command buffer"); }
VKAPI_ATTR VkResult VKAPI_CALL BeginCB(VkCommandBuffer, const VkCommandBufferBeginInfo *) { return F.step(); }
VKAPI_ATTR VkResult VKAPI_CALL EndCB(VkCommandBuffer) { return F.step(); }
VKAPI_ATTR void VKAPI_CALL BindPipe(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) {}
VKAPI_ATTR void VKAPI_CALL BindDS(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *) {}
VKAPI_ATTR void VKAPI_CALL Dispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t) {}
VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *) {}
VKAPI_ATTR VkResult VKAPI_CALL CreateFence(VkDevice, const VkFenceCreateInfo *, const VkAllocationCallbacks *, VkFence *x) { return F.make(x); }
VKAPI_ATTR void VKAPI_CALL DestroyFence(VkDevice, VkFence x, const VkAllocationCallbacks *) { F.drop(x, "fence"); }
VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue, uint32_t, const VkSubmitInfo *, VkFence) { VkResult r = F.step(); if (r == VK_SUCCESS) F.in_flight = true; return r; }
VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t timeout) {
    F.timeout_seen = timeout;
    if (F.wait_rc == VK_SUCCESS) { for (uint32_t i = 0; i < 64; i++) F.buffer[i] = i; F.in_flight = false; }
    if (F.wait_rc == VK_ERROR_DEVICE_LOST) F.in_flight = false;   // 设备丢失后对象可以销毁 | objects may be destroyed after device loss
    return F.wait_rc;
}
VKAPI_ATTR VkResult VKAPI_CALL WaitIdle(VkQueue) { F.idle_calls++; if (F.idle_rc == VK_SUCCESS || F.idle_rc == VK_ERROR_DEVICE_LOST) F.in_flight = false; return F.idle_rc; }
const XbgProbeVk VK{CreateBuffer, DestroyBuffer, GetReq, AllocateMemory, FreeMemory, BindBufferMemory, MapMemory, UnmapMemory, CreateDSL, DestroyDSL, CreatePL, DestroyPL, CreateSM, DestroySM,
                    CreateCP, DestroyPipe, CreateDP, DestroyDP, AllocDS, UpdateDS, AllocCB, FreeCB, BeginCB, EndCB, BindPipe, BindDS, Dispatch, Barrier, CreateFence, DestroyFence, Submit, Wait, WaitIdle};
int failures = 0;
void require(bool ok, const std::string &why) { if (!ok) { fprintf(stderr, "FAIL %s\n", why.c_str()); ++failures; } }
int run(int fail_at, VkResult wait, VkResult idle, XbgProbeOut &out) {
    F = Fake{}; F.fail_at = fail_at; F.wait_rc = wait; F.idle_rc = idle;
    VkPhysicalDeviceMemoryProperties mp{}; mp.memoryTypeCount = 1; mp.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    static const uint32_t code[] = {0x07230203u, 0, 0, 1, 0};
    XbgProbeIn in{DEV, Q, reinterpret_cast<VkCommandPool>(uintptr_t(0x99)), &mp, code, sizeof code, 64, 1234567};
    return xbg_laneid_probe_run(VK, in, out);
}
}
int main() { try {
    XbgProbeOut o; int rc = run(-1, VK_SUCCESS, VK_SUCCESS, o);
    require(rc == 1 && o.distinct == 64 && F.violation.empty() && F.live.empty() && !F.mapped && F.idle_calls == 0 && F.timeout_seen == 1234567, "success: usable, everything released and unmapped, caller's time limit used; " + F.violation);
    const int fallible = F.fallible; require(fallible == 15, "15 fallible calls on the success path"); if (fallible != 15) return 1;
    for (int k = 0; k < fallible; k++) {
        rc = run(k, VK_SUCCESS, VK_SUCCESS, o);
        require(rc == -1 && F.violation.empty() && F.live.empty() && !F.mapped, "failure at fallible call " + std::to_string(k) + ": released, unmapped; " + F.violation + (F.live.empty() ? "" : " (objects left)") + (F.mapped ? " (still mapped)" : ""));
    }
    rc = run(-1, VK_TIMEOUT, VK_SUCCESS, o);
    require(rc == -1 && F.idle_calls == 1 && F.violation.empty() && F.live.empty() && !F.mapped && o.released, "timeout, queue idle: waited idle before releasing; " + F.violation);
    rc = run(-1, VK_TIMEOUT, VK_ERROR_DEVICE_LOST, o);
    require(rc == -1 && F.idle_calls == 1 && F.violation.empty() && F.live.empty() && o.released, "timeout, device lost while waiting idle: released; " + F.violation);
    rc = run(-1, VK_TIMEOUT, VK_ERROR_OUT_OF_HOST_MEMORY, o);
    require(rc == -1 && F.idle_calls == 1 && F.violation.empty() && !F.live.empty() && !o.released, "timeout, queue not idle: objects deliberately kept, nothing destroyed while in flight; " + F.violation);
    rc = run(-1, VK_ERROR_DEVICE_LOST, VK_SUCCESS, o);
    require(rc == -1 && F.idle_calls == 0 && F.violation.empty() && F.live.empty(), "fence reports device lost: released at once; " + F.violation);
    if (failures) { fprintf(stderr, "%d case(s) failed\n", failures); return 1; }
    printf("PASS laneid_probe_run: success, %d failure exits, timeout -> idle / lost / not idle, device lost; unmap before free, no release while in flight\n", fallible);
    return 0;
} catch (const std::exception &e) { fprintf(stderr, "FAIL %s\n", e.what()); return 1; } }
