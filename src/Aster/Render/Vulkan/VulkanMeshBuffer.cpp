#include "VulkanMeshBuffer.h"
#include "VulkanUtil.h"

#include <cstring>
#include <iostream>

using VulkanUtil::FindMemoryType;

namespace aster
{

bool VulkanMeshBuffer::CreateHostBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                                        VkDeviceSize size, VkBufferUsageFlags usage,
                                        const void *data,
                                        VkBuffer &outBuffer, VkDeviceMemory &outMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReq);

    uint32_t memType = FindMemoryType(physicalDevice, memReq.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == std::numeric_limits<uint32_t>::max())
        return false;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
        return false;
    if (vkBindBufferMemory(device, outBuffer, outMemory, 0) != VK_SUCCESS)
        return false;

    void *mapped = nullptr;
    if (vkMapMemory(device, outMemory, 0, size, 0, &mapped) != VK_SUCCESS)
        return false;
    if (data && size > 0)
        std::memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(device, outMemory);

    return true;
}

bool VulkanMeshBuffer::Create(VkDevice device, VkPhysicalDevice physicalDevice,
                              const void *vertices, VkDeviceSize vertexSize, uint32_t vertexCount,
                              const uint32_t *indices, uint32_t indexCount)
{
    this->device = device;
    indexCount_ = indexCount;

    if (!CreateHostBuffer(device, physicalDevice, vertexSize * vertexCount,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices,
                          vertexBuffer, vertexMemory))
    {
        std::cerr << "[VulkanMeshBuffer] Failed to create vertex buffer" << std::endl;
        Destroy();
        return false;
    }

    if (!CreateHostBuffer(device, physicalDevice, sizeof(uint32_t) * indexCount,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices,
                          indexBuffer, indexMemory))
    {
        std::cerr << "[VulkanMeshBuffer] Failed to create index buffer" << std::endl;
        Destroy();
        return false;
    }

    return true;
}

void VulkanMeshBuffer::Destroy()
{
    if (!device)
        return;

    if (indexBuffer)
    {
        vkDestroyBuffer(device, indexBuffer, nullptr);
        indexBuffer = VK_NULL_HANDLE;
    }
    if (indexMemory)
    {
        vkFreeMemory(device, indexMemory, nullptr);
        indexMemory = VK_NULL_HANDLE;
    }
    if (vertexBuffer)
    {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexMemory)
    {
        vkFreeMemory(device, vertexMemory, nullptr);
        vertexMemory = VK_NULL_HANDLE;
    }

    device = VK_NULL_HANDLE;
}

void VulkanMeshBuffer::Bind(VkCommandBuffer cmd) const
{
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void VulkanMeshBuffer::Draw(VkCommandBuffer cmd) const
{
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

} // namespace aster

