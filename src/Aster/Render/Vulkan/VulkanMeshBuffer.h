#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

// ============================================================================
// VulkanMeshBuffer —— Vulkan 顶点 / 索引缓冲封装
// ----------------------------------------------------------------------------
// 使用 host-visible + coherent 内存（映射后直接 memcpy），适合演示与原型阶段；
// 后续可替换为 staging buffer + device-local 内存以获得最佳性能。
// ============================================================================

namespace aster
{

class VulkanMeshBuffer
{
public:
    VulkanMeshBuffer() = default;
    ~VulkanMeshBuffer() { Destroy(); }
    VulkanMeshBuffer(const VulkanMeshBuffer &) = delete;
    VulkanMeshBuffer &operator=(const VulkanMeshBuffer &) = delete;

    bool Create(VkDevice device, VkPhysicalDevice physicalDevice,
                const void *vertices, VkDeviceSize vertexSize, uint32_t vertexCount,
                const uint32_t *indices, uint32_t indexCount);

    void Destroy();

    void Bind(VkCommandBuffer cmd) const;
    void Draw(VkCommandBuffer cmd) const;

    uint32_t IndexCount() const { return indexCount_; }

private:
    static bool CreateHostBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                                 VkDeviceSize size, VkBufferUsageFlags usage,
                                 const void *data,
                                 VkBuffer &outBuffer, VkDeviceMemory &outMemory);

    VkDevice device = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;
};

} // namespace aster

