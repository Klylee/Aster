#pragma once

// Vulkan 后端共享的小工具（内联函数，供 VulkanRenderAPI / VulkanPipeline /
// VulkanMeshBuffer 等使用）

#include <vulkan/vulkan.h>

#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace VulkanUtil
{

inline void CheckVkResult(VkResult err)
{
    if (err != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] VkResult error: " << (int)err << std::endl;
    }
}

inline std::vector<char> ReadFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        return {};
    std::streamsize size = file.tellg();
    std::vector<char> buffer((size_t)size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

inline VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char> &code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create shader module" << std::endl;
    }
    return module;
}

// 选择满足指定 propertyFlags 的内存类型；找不到返回 UINT32_MAX
inline uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                               VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

} // namespace VulkanUtil
