#include "VulkanRenderAPI.h"
#include "VulkanSceneRenderer.h"
#include "VulkanMeshBuffer.h"
#include "VulkanUtil.h"
#include "Mesh.h"  // 框架 Mesh：SubmitSceneMesh 懒创建 VulkanMeshBuffer
#include "Model.h" // 拾取：注册表持有 weak_ptr<Model>，需完整类型访问 Model::pickId

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

// 必须先包含 vulkan，再包含 GLFW，以启用 glfw 的 Vulkan 表面函数
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#ifndef VULKAN_SHADER_DIR
#define VULKAN_SHADER_DIR "assets/shaders"
#endif

// 复用共享小工具
using VulkanUtil::CheckVkResult;
using VulkanUtil::CreateShaderModule;
using VulkanUtil::ReadFile;

namespace aster
{

// ============================================================================
// VulkanRenderAPI 构造 / 析构
// ============================================================================

VulkanRenderAPI::~VulkanRenderAPI()
{
    Shutdown();
}

// ============================================================================
// Init —— 依次创建实例 / 设备 / 交换链 / 渲染流程 / 管线 / 命令 / 同步
// ============================================================================

bool VulkanRenderAPI::Init(GLFWwindow *window)
{
    this->window = window;

    // 校验层：Debug 构建默认开启，可通过环境变量 ASTER_VK_VALIDATION=0 关闭
    validationEnabled = false;
    if (const char *v = std::getenv("ASTER_VK_VALIDATION"))
        validationEnabled = (v != nullptr && std::string(v) != "0");

    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    if (!CreateInstance())
        return false;
    SetupDebugMessenger();
    if (!CreateSurface())
        return false;
    if (!PickPhysicalDevice())
        return false;
    if (!CreateLogicalDevice())
        return false;
    if (!CreateSwapchain())
        return false;
    CreateImageViews();
    CreateDepthResources();
    if (!CreateRenderPass())
        return false;
    // CreateTrianglePipeline();
    CreateFramebuffers();
    if (!CreateCommandPool())
        return false;
    if (!CreateSyncObjects())
        return false;

    // 演示场景（若着色器缺失会打印警告但不影响主流程）
    CreateSceneRenderer();

    std::cout << "[Vulkan] Backend initialized"
              << " (swapchain " << swapchainImages.size() << " images, "
              << framebufferWidth << "x" << framebufferHeight << ")" << std::endl;
    return true;
}

// ============================================================================
// 实例 / 调试信息
// ============================================================================

bool VulkanRenderAPI::CreateInstance()
{
    if (validationEnabled)
    {
        // 检查校验层是否可用
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        bool found = false;
        for (const auto &layer : availableLayers)
        {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::cerr << "[Vulkan] Validation layer requested but not available, disabling" << std::endl;
            validationEnabled = false;
        }
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Aster 3DGS";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Aster";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    // 从 GLFW 获取实例所需扩展（VK_KHR_surface + 平台表面扩展）
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions)
    {
        std::cerr << "[Vulkan] glfwGetRequiredInstanceExtensions failed (Vulkan support missing)" << std::endl;
        return false;
    }

    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
#ifdef __APPLE__
    // macOS 上 Vulkan 由 MoltenVK（portability 驱动）提供：
    // 1) 必须启用 VK_KHR_portability_enumeration 实例扩展，
    // 2) 并设置 VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 标志，
    //    否则 vkCreateInstance 会因枚举不到驱动返回 VK_ERROR_INCOMPATIBLE_DRIVER。
    // 注意：VK_KHR_portability_subset 是【设备】扩展，须在 CreateLogicalDevice 中启用。
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
    for (const char *ext : extensions)
        std::cout << "[Vulkan] Required instance extension: " << ext << std::endl;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (validationEnabled)
    {
        std::vector<const char *> layers = {"VK_LAYER_KHRONOS_validation"};
        createInfo.enabledLayerCount = (uint32_t)layers.size();
        for (const char *layer : layers)
            std::cout << "[Vulkan] Enabling validation layer: " << layer << std::endl;
        createInfo.ppEnabledLayerNames = layers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;
        createInfo.pNext = &debugCreateInfo;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create instance" << std::endl;
        return false;
    }
    return true;
}

void VulkanRenderAPI::SetupDebugMessenger()
{
    if (!validationEnabled)
        return;

    auto CreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (!CreateDebugUtilsMessengerEXT)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create debug messenger" << std::endl;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderAPI::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void * /*pUserData*/)
{
    if (messageSeverity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
    {
        std::cerr << "[Vulkan Validation] " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

// ============================================================================
// 物理设备 / 逻辑设备 / 表面
// ============================================================================

struct QueueFamilyIndices
{
    uint32_t graphics = std::numeric_limits<uint32_t>::max();
    uint32_t present = std::numeric_limits<uint32_t>::max();

    bool IsComplete() const
    {
        return graphics != std::numeric_limits<uint32_t>::max() &&
               present != std::numeric_limits<uint32_t>::max();
    }
};

static QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphics = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport)
            indices.present = i;

        if (indices.IsComplete())
            break;
    }
    return indices;
}

static bool CheckDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef __APPLE__
    // MoltenVK 设备扩展（该 SDK 头文件未提供宏，直接用字面字符串）
    requiredExtensions.insert("VK_KHR_portability_subset");
#endif
    for (const auto &extension : availableExtensions)
        requiredExtensions.erase(extension.extensionName);
    return requiredExtensions.empty();
}

bool VulkanRenderAPI::CreateSurface()
{
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create window surface" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderAPI::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        std::cerr << "[Vulkan] No physical devices with Vulkan support" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // 打分选择：独立 GPU 优先，其次集成 GPU
    auto scoreDevice = [&](VkPhysicalDevice device) -> int
    {
        QueueFamilyIndices indices = FindQueueFamilies(device, surface);
        if (!indices.IsComplete())
            return 0;
        if (!CheckDeviceExtensionSupport(device))
            return 0;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(device, &features);

        int score = 1;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            score += 100;
        score += props.limits.maxImageDimension2D;
        (void)features;
        return score;
    };

    int bestScore = 0;
    for (const auto &device : devices)
    {
        int score = scoreDevice(device);
        if (score > bestScore)
        {
            bestScore = score;
            physicalDevice = device;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE)
    {
        std::cerr << "[Vulkan] Failed to find a suitable GPU" << std::endl;
        return false;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    std::cout << "[Vulkan] Using GPU: " << props.deviceName << std::endl;
    return true;
}

bool VulkanRenderAPI::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamilies(physicalDevice, surface);
    graphicsFamily = indices.graphics;
    presentFamily = indices.present;

    // 收集需要创建的队列（graphics 与 present 可能相同）
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {graphicsFamily, presentFamily};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef __APPLE__
    // MoltenVK（portability 驱动）要求启用该设备扩展，否则设备创建失败
    // （该 SDK 头文件未提供宏，直接用字面字符串）
    deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    // 设备层的校验层在较新的 Vulkan 中已废弃，这里不重复启用

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create logical device" << std::endl;
        return false;
    }

    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
    return true;
}

// ============================================================================
// 交换链 / 图像视图 / 深度缓冲
// ============================================================================

static VkSurfaceFormatKHR SelectSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    for (const auto &format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }
    // 回退到第一个可用格式
    return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                           : formats[0];
}

static VkPresentModeKHR SelectPresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    // 优先 Mailbox（低延迟），否则 FIFO（垂直同步，Vulkan 保证可用）
    for (const auto &mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

bool VulkanRenderAPI::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    VkSurfaceFormatKHR surfaceFormat = SelectSurfaceFormat(physicalDevice, surface);
    VkPresentModeKHR presentMode = SelectPresentMode(physicalDevice, surface);

    // 交换链尺寸
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        swapchainExtent = capabilities.currentExtent;
    }
    else
    {
        swapchainExtent = {static_cast<uint32_t>(framebufferWidth),
                           static_cast<uint32_t>(framebufferHeight)};
        swapchainExtent.width = std::clamp(swapchainExtent.width,
                                           capabilities.minImageExtent.width,
                                           capabilities.maxImageExtent.width);
        swapchainExtent.height = std::clamp(swapchainExtent.height,
                                            capabilities.minImageExtent.height,
                                            capabilities.maxImageExtent.height);
    }

    // 图像数量：minImageCount + 1，并夹到上限
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        imageCount = capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // 图形与呈现队列可能不同，需共享模式
    QueueFamilyIndices indices = FindQueueFamilies(physicalDevice, surface);
    std::array<uint32_t, 2> queueFamilyIndices = {indices.graphics, indices.present};
    if (indices.graphics != indices.present)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create swapchain" << std::endl;
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainFormat = surfaceFormat.format;
    return true;
}

void VulkanRenderAPI::CreateImageViews()
{
    swapchainImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create image view " << i << std::endl;
        }
    }
}

static VkFormat FindDepthFormat(VkPhysicalDevice physicalDevice)
{
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT};

    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}

void VulkanRenderAPI::CreateDepthResources()
{
    depthFormat = FindDepthFormat(physicalDevice);
    if (depthFormat == VK_FORMAT_UNDEFINED)
    {
        std::cerr << "[Vulkan] Failed to find a supported depth format" << std::endl;
        return;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width;
    imageInfo.extent.height = swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &depthImage) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create depth image" << std::endl;
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((memRequirements.memoryTypeBits & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &depthMemory) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to allocate depth image memory" << std::endl;
        return;
    }
    vkBindImageMemory(device, depthImage, depthMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create depth image view" << std::endl;
    }
}

// ============================================================================
// 渲染流程
// ============================================================================

bool VulkanRenderAPI::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = (uint32_t)attachments.size();
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create render pass" << std::endl;
        return false;
    }
    return true;
}

// ============================================================================
// 三角形演示管线（push constants，无顶点缓冲）
// ============================================================================

void VulkanRenderAPI::CreateTrianglePipeline()
{
    std::vector<char> vertCode = ReadFile(std::string(VULKAN_SHADER_DIR) + "/triangle.vert.spv");
    std::vector<char> fragCode = ReadFile(std::string(VULKAN_SHADER_DIR) + "/triangle.frag.spv");
    if (vertCode.empty() || fragCode.empty())
    {
        std::cerr << "[Vulkan] Triangle shaders not found under '" << VULKAN_SHADER_DIR
                  << "', demo triangle disabled (clear + ImGui only)" << std::endl;
        return;
    }

    VkShaderModule vertModule = CreateShaderModule(device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

    // 顶点输入：三角形用 gl_VertexIndex 生成，无需顶点缓冲
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 本管线不使用深度测试（ImGui 与三角形都在近平面渲染）
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 动态视口 + 裁剪
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    struct TrianglePushConstants
    {
        glm::vec3 color;
        float time;
    };

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TrianglePushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &trianglePipelineLayout) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create triangle pipeline layout" << std::endl;
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = trianglePipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    // if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &trianglePipeline) != VK_SUCCESS)
    // {
    //     std::cerr << "[Vulkan] Failed to create triangle pipeline" << std::endl;
    // }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

// ============================================================================
// 演示场景（旋转立方体）
// 通过 VulkanSceneRenderer 验证框架 Mesh / Material / Renderer 的 Vulkan 迁移：
//   VulkanMeshBuffer(网格) + 颜色(材质) -> Submit() -> Record(cmd, view, proj)
// ============================================================================

bool VulkanRenderAPI::CreateSceneRenderer()
{
    sceneRenderer = new VulkanSceneRenderer();
    if (!sceneRenderer->Init(device, physicalDevice, renderPass, graphicsQueue,
                             commandPool, VULKAN_SHADER_DIR))
    {
        std::cerr << "[Vulkan] Scene renderer init failed "
                     "(shaders missing? SPIR-V not compiled?) - mesh disabled" << std::endl;
        DestroySceneRenderer();
        return false;
    }

    // 拾取 id map（尺寸 = 交换链，保证鼠标坐标 1:1 映射；失败仅提示、不影响主流程）
    sceneRenderer->EnablePickMap(VULKAN_SHADER_DIR,
                                 swapchainExtent.width, swapchainExtent.height);

    // // 24 顶点立方体（pos3 + nor3 + uv2，stride 32B），36 索引
    // const float cubeVertices[] = {
    //     // front (+z)
    //     -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
    //      0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
    //      0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
    //     -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f,
    //     // back (-z)
    //      0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f,  0.0f, 0.0f,
    //     -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f,  1.0f, 0.0f,
    //     -0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f,  1.0f, 1.0f,
    //      0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f,  0.0f, 1.0f,
    //     // right (+x)
    //      0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f,
    //      0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f,
    //      0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  1.0f, 1.0f,
    //      0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f,
    //     // left (-x)
    //     -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f,  0.0f, 0.0f,
    //     -0.5f, -0.5f,  0.5f, -1.0f, 0.0f, 0.0f,  1.0f, 0.0f,
    //     -0.5f,  0.5f,  0.5f, -1.0f, 0.0f, 0.0f,  1.0f, 1.0f,
    //     -0.5f,  0.5f, -0.5f, -1.0f, 0.0f, 0.0f,  0.0f, 1.0f,
    //     // top (+y)
    //     -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
    //      0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f,
    //      0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f,
    //     -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f,
    //     // bottom (-y)
    //     -0.5f, -0.5f, -0.5f,  0.0f, -1.0f, 0.0f,  0.0f, 0.0f,
    //      0.5f, -0.5f, -0.5f,  0.0f, -1.0f, 0.0f,  1.0f, 0.0f,
    //      0.5f, -0.5f,  0.5f,  0.0f, -1.0f, 0.0f,  1.0f, 1.0f,
    //     -0.5f, -0.5f,  0.5f,  0.0f, -1.0f, 0.0f,  0.0f, 1.0f,
    // };
    // const uint32_t cubeIndices[] = {
    //     0,  1,  2,  0,  2,  3,
    //     4,  5,  6,  4,  6,  7,
    //     8,  9,  10, 8,  10, 11,
    //     12, 13, 14, 12, 14, 15,
    //     16, 17, 18, 16, 18, 19,
    //     20, 21, 22, 20, 22, 23,
    // };

    // cubeMesh = new VulkanMeshBuffer();
    // if (!cubeMesh->Create(device, physicalDevice,
    //                       cubeVertices, 8 * sizeof(float), 24,
    //                       cubeIndices, 36))
    // {
    //     std::cerr << "[Vulkan] Failed to create cube buffers" << std::endl;
    //     DestroySceneRenderer();
    //     return false;
    // }

    std::cout << "[Vulkan] Scene renderer ready (rotating cube, 36 indices)" << std::endl;
    return true;
}

void VulkanRenderAPI::DestroySceneRenderer()
{
    if (cubeMesh)
    {
        cubeMesh->Destroy();
        delete cubeMesh;
        cubeMesh = nullptr;
    }
    if (sceneRenderer)
    {
        sceneRenderer->Shutdown();
        delete sceneRenderer;
        sceneRenderer = nullptr;
    }
}

void VulkanRenderAPI::SubmitSceneMesh(const Mesh &mesh, const glm::mat4 &model,
                                      const MaterialParams &params, bool castsShadow,
                                      const std::weak_ptr<Model> &pickOwner)
{
    if (!sceneRenderer)
        return;
    if (mesh.v_num <= 0 || !mesh.vertices || mesh.i_num <= 0 || !mesh.indices)
        return;

    // 懒创建该 Mesh 的 Vulkan 缓冲（host-visible，首次提交时创建一次）
    if (!mesh.vulkanBuffer)
    {
        auto *buf = new VulkanMeshBuffer();
        if (!buf->Create(device, physicalDevice,
                         mesh.vertices, 8 * sizeof(float), (uint32_t)mesh.v_num,
                         mesh.indices, (uint32_t)mesh.i_num))
        {
            std::cerr << "[Vulkan] Failed to create buffer for scene mesh" << std::endl;
            delete buf;
            return;
        }
        const_cast<Mesh &>(mesh).vulkanBuffer = buf;
    }

    // 材质参数 → push constant vec4（粗糙度, 金属度, AO, 纹理索引）
    glm::vec4 material(params.roughness, params.metallic, params.ao,
                       (float)params.textureIndex);
    // M4：自定义材质 uniform（OpenGL 风格 SetUniform(key,type,value)）→ binding 12 动态 UBO
    sceneRenderer->Submit(*mesh.vulkanBuffer, model, params.color, material,
                          castsShadow, params.pipelineIndex,
                          params.customUniforms, params.customUniformOrder);

    // 拾取：可拾取对象（pickOwner 有效）画进离屏 id map（稳定 ID → 像素颜色）。
    // 使用 weak_ptr：模型删除后 lock() 为空，注册表自动失效，无悬垂指针。
    if (auto owner = pickOwner.lock())
    {
        int pickId = owner->pickId;
        if (pickId <= 0) // 首次提交：分配稳定 ID 并登记弱引用
        {
            pickId = (int)pickIdToModel_.size(); // 0 = 背景占位
            if (pickId == 0)
            {
                pickIdToModel_.emplace_back(); // index 0 = 背景
                pickId = 1;
            }
            owner->pickId = pickId;
            pickIdToModel_.push_back(owner); // 弱引用
        }
        sceneRenderer->SubmitPick(*mesh.vulkanBuffer, model, pickId);
    }

    hasSceneMesh = true;
}

const Model *VulkanRenderAPI::PickAt(int x, int y)
{
    if (!sceneRenderer)
        return nullptr;
    // 窗口坐标 → 帧缓冲像素坐标（HiDPI / Retina 缩放）
    int winW = 0, winH = 0;
    glfwGetWindowSize(window, &winW, &winH);
    int px = x;
    int py = y;
    if (winW > 0 && winH > 0)
    {
        px = (int)((double)x * (double)framebufferWidth / (double)winW);
        py = (int)((double)y * (double)framebufferHeight / (double)winH);
    }
    int id = sceneRenderer->PickAt(px, py);
    if (id > 0 && id < (int)pickIdToModel_.size())
    {
        // 模型可能已被动态删除：弱引用 lock() 为空则视为未命中（避免悬垂指针）
        auto sp = pickIdToModel_[id].lock();
        return sp.get();
    }
    return nullptr;
}

void VulkanRenderAPI::SetSceneCamera(const glm::mat4 &view, const glm::mat4 &proj)
{
    sceneView = view;
    sceneProj = proj;
    hasSceneCamera = true;
}

void VulkanRenderAPI::SetSceneLights(const LightUBO &lights)
{
    if (sceneRenderer)
        sceneRenderer->SetLights(lights);
}

void VulkanRenderAPI::SetSoftShadow(bool soft)
{
    if (sceneRenderer)
        sceneRenderer->SetSoftShadow(soft);
}

void VulkanRenderAPI::SetShadowDebugView(int mode)
{
    if (sceneRenderer)
        sceneRenderer->SetShadowDebugView(mode);
}

void VulkanRenderAPI::DebugDrawLines(const std::vector<glm::vec3> &segments,
                                     const glm::vec4 &color)
{
    if (!sceneRenderer)
        return;
    for (size_t i = 0; i + 1 < segments.size(); i += 2)
        sceneRenderer->SubmitDebugLine(segments[i], segments[i + 1], color);
}

void VulkanRenderAPI::SetEnvironmentMap(const EnvironmentMap &env)
{
    if (sceneRenderer)
        sceneRenderer->UploadEnvironmentMap(graphicsQueue, commandPool, env);
}

void VulkanRenderAPI::SetEnvMode(int mode)
{
    if (sceneRenderer)
        sceneRenderer->SetEnvMode(mode);
}

void VulkanRenderAPI::SetEnvParams(float intensity, float roughness, float metallic,
                                   float ao, float yaw, float exposure, bool toneMap)
{
    if (sceneRenderer)
        sceneRenderer->SetEnvParams(intensity, roughness, metallic, ao, yaw, exposure, toneMap);
}

int VulkanRenderAPI::RegisterMaterialTexture(const uint8_t *rgba8, int width, int height)
{
    if (!sceneRenderer || !graphicsQueue || !commandPool)
        return -1;
    return sceneRenderer->RegisterMaterialTexture(graphicsQueue, commandPool, rgba8, width, height);
}

int VulkanRenderAPI::CreateMaterialPipeline(const std::string &vertName,
                                            const std::string &fragName,
                                            bool enableBlend, bool enableDepthWrite,
                                            float depthBias)
{
    if (!sceneRenderer)
        return -1;
    return sceneRenderer->CreateMaterialPipeline(VULKAN_SHADER_DIR, vertName, fragName,
                                                 enableBlend, enableDepthWrite, depthBias);
}

void VulkanRenderAPI::RenderShadowMap(VkCommandBuffer cmd)
{
    if (sceneRenderer)
        sceneRenderer->RecordShadow(cmd);
}

void VulkanRenderAPI::RenderPickMap(VkCommandBuffer cmd)
{
    if (!sceneRenderer)
        return;
    // 拾取 id map：用当前场景相机渲染可拾取对象 + 拷贝到读回缓冲（主 pass 之前）
    sceneRenderer->RecordPickMap(cmd, sceneView, sceneProj);
}

void VulkanRenderAPI::RenderScene(VkCommandBuffer cmd)
{
    if (!sceneRenderer)
        return;

    float time = (float)frameCount * 0.02f;

    // 相机：优先使用 App 通过 SetSceneCamera 设置的场景相机；
    // 未设置时（如无场景相机的 demo）使用演示环绕相机。
    glm::mat4 view, proj;
    if (hasSceneCamera)
    {
        view = sceneView;
        proj = sceneProj;
    }
    else
    {
        glm::vec3 camPos(3.5f * std::cos(time), 1.6f * std::sin(time * 0.5f), 3.5f * std::sin(time));
        view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        float aspect = (float)swapchainExtent.width / (float)swapchainExtent.height;
        proj = glm::perspective(glm::radians(60.0f), aspect, 0.01f, 100.0f);
    }

    // 本帧没有场景网格提交时，回退到演示旋转立方体（保证无 Model 的场景仍能看到内容）
    if (!hasSceneMesh && cubeMesh)
    {
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * 0.8f, glm::vec3(0.0f, 1.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(1.0f, 0.0f, 0.0f));
        sceneRenderer->Submit(*cubeMesh, model, glm::vec4(1.0f, 0.62f, 0.25f, 1.0f),
                              glm::vec4(0.35f, 0.0f, 1.0f, -1.0f));
    }

    // 本帧场景绘制结束，重置标记（下一帧由 Clear() 重新 BeginFrame）
    hasSceneMesh = false;

    sceneRenderer->Record(cmd, view, proj);
}

// ============================================================================
// 帧缓冲 / 命令池 / 命令缓冲 / 同步对象
// ============================================================================

void VulkanRenderAPI::CreateFramebuffers()
{
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachments = {swapchainImageViews[i], depthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = (uint32_t)attachments.size();
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create framebuffer " << i << std::endl;
        }
    }
}

bool VulkanRenderAPI::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create command pool" << std::endl;
        return false;
    }

    // 每个交换链图像一个命令缓冲（每帧重录）
    commandBuffers.resize(swapchainImages.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to allocate command buffers" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderAPI::CreateSyncObjects()
{
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 第一帧可直接提交

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create synchronization objects" << std::endl;
            return false;
        }
    }

    imagesInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);
    return true;
}

// ============================================================================
// 帧循环
// ============================================================================

void VulkanRenderAPI::Clear(const glm::vec4 &color)
{
    clearColor = color;

    // 新一帧开始：清空场景绘制列表。
    // 本帧内 Model::draw() 等会经 SubmitSceneMesh 累积绘制，Present() 时统一录制。
    if (sceneRenderer)
        sceneRenderer->BeginFrame();
    hasSceneMesh = false;
}

void VulkanRenderAPI::Present()
{
    // 交换链尺寸变化检查（窗口为非可调大小，但 HiDPI 等场景可能变化）
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW != framebufferWidth || fbH != framebufferHeight)
    {
        if (fbW > 0 && fbH > 0)
            RecreateSwapchain(fbW, fbH);
    }

    // 等待当前 in-flight 帧槽位完成
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                            imageAvailableSemaphores[currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain(framebufferWidth, framebufferHeight);
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        CheckVkResult(result);
        return;
    }

    // 确保该 image 未被其他 in-flight 帧占用
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    // ---- 录制命令缓冲 ----
    VkCommandBuffer cmd = commandBuffers[imageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to begin command buffer" << std::endl;
        return;
    }

    // 阴影映射 pass：必须在主 render pass 开始之前单独录制（深度渲染），
    // 否则会与主 pass 嵌套而崩溃。
    RenderShadowMap(cmd);

    // 拾取 id map pass：在主 pass 之前渲染可拾取对象为 ID 颜色并拷贝到读回缓冲。
    // （与阴影类似，是独立的离屏 render pass，不能与主 pass 嵌套。）
    RenderPickMap(cmd);

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = renderPass;
    rpInfo.framebuffer = swapchainFramebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = swapchainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    clearValues[1].depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = (uint32_t)clearValues.size();
    rpInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f,
                        (float)swapchainExtent.width, (float)swapchainExtent.height,
                        0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, swapchainExtent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    frameCount++;

    // ---- 演示场景（旋转立方体：VulkanSceneRenderer 渲染） ----
    RenderScene(cmd);

    // // ---- 演示三角形（若着色器加载成功） ----
    // if (trianglePipeline != VK_NULL_HANDLE)
    // {
    //     struct TrianglePushConstants
    //     {
    //         glm::vec3 color;
    //         float time;
    //     };
    //     TrianglePushConstants pc;
    //     pc.color = triangleColor;
    //     pc.time = (float)frameCount * 0.02f;

    //     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
    //     vkCmdPushConstants(cmd, trianglePipelineLayout,
    //                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    //                        0, sizeof(pc), &pc);
    //     vkCmdDraw(cmd, 3, 1, 0, 0);
    // }

    // ---- ImGui ----
    if (ImGui::GetDrawData())
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to end command buffer" << std::endl;
        return;
    }

    // ---- 提交 ----
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to submit draw command buffer" << std::endl;
        return;
    }

    // 拾取 id map 读回：等待本帧提交完成，保证拾取读回缓冲已被 GPU 写入。
    // （代价是每帧 GPU 同步；demo 场景轻量，可接受。若去掉则 PickAt 可能读到
    //   上一帧/未定义数据。）
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // ---- 呈现 ----
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        RecreateSwapchain(framebufferWidth, framebufferHeight);
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// ImGui 集成
// ============================================================================

bool VulkanRenderAPI::ImGuiInit()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontDefault()->Scale = 1.6f;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_2;
    info.Instance = instance;
    info.PhysicalDevice = physicalDevice;
    info.Device = device;
    info.QueueFamily = graphicsFamily;
    info.Queue = graphicsQueue;
    info.DescriptorPoolSize = 1000; // 让后端自动创建字体描述符池
    info.MinImageCount = 2;
    info.ImageCount = (uint32_t)swapchainImages.size();
    info.PipelineCache = VK_NULL_HANDLE;
    info.PipelineInfoMain.RenderPass = renderPass;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.Allocator = nullptr;
    info.CheckVkResultFn = CheckVkResult;

    if (!ImGui_ImplVulkan_Init(&info))
    {
        std::cerr << "[Vulkan] Failed to init ImGui Vulkan backend" << std::endl;
        return false;
    }
    return true;
}

void VulkanRenderAPI::ImGuiNewFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void VulkanRenderAPI::ImGuiRender()
{
    // 注意：不要在这里调用 ImGui::End()！用户代码已配平 Begin/End，
    // 隐式回退窗口（Debug##Default）由 imgui 的 Render()/EndFrame() 内部自动关闭，
    // 手动再 End() 会触发 "Calling End() too many times!" 断言。
    ImGui::Render();
    // 实际的 ImGui 绘制在 Present() 的命令缓冲中通过
    // ImGui_ImplVulkan_RenderDrawData() 完成
}

// ============================================================================
// 尺寸变化 / 交换链重建
// ============================================================================

void VulkanRenderAPI::OnResize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    RecreateSwapchain(width, height);
}

void VulkanRenderAPI::CleanupDepthResources()
{
    if (depthImageView)
    {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage)
    {
        vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthMemory)
    {
        vkFreeMemory(device, depthMemory, nullptr);
        depthMemory = VK_NULL_HANDLE;
    }
}

void VulkanRenderAPI::CleanupSwapchain()
{
    for (auto framebuffer : swapchainFramebuffers)
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    swapchainFramebuffers.clear();

    CleanupDepthResources();

    for (auto imageView : swapchainImageViews)
        vkDestroyImageView(device, imageView, nullptr);
    swapchainImageViews.clear();

    if (swapchain)
    {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void VulkanRenderAPI::RecreateSwapchain(int width, int height)
{
    vkDeviceWaitIdle(device);

    framebufferWidth = width;
    framebufferHeight = height;

    CleanupSwapchain();

    CreateSwapchain();
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();

    imagesInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);

    // 拾取 id map 尺寸随交换链变化（保证鼠标坐标 1:1），重建
    if (sceneRenderer)
    {
        sceneRenderer->DestroyPickMap();
        sceneRenderer->EnablePickMap(VULKAN_SHADER_DIR,
                                     swapchainExtent.width, swapchainExtent.height);
    }

    // 通知 ImGui 交换链图像数变化
    ImGui_ImplVulkan_SetMinImageCount((uint32_t)swapchainImages.size());

    std::cout << "[Vulkan] Swapchain recreated: " << framebufferWidth << "x"
              << framebufferHeight << " (" << swapchainImages.size() << " images)" << std::endl;
}

// ============================================================================
// 释放资源
// ============================================================================

void VulkanRenderAPI::Shutdown()
{
    if (!device)
        return;

    vkDeviceWaitIdle(device);

    // 仅在 ImGui 上下文确实创建后才关闭（Init 可能在中途失败）
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (inFlightFences[i])
            vkDestroyFence(device, inFlightFences[i], nullptr);
        if (renderFinishedSemaphores[i])
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        if (imageAvailableSemaphores[i])
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    }
    inFlightFences.clear();
    renderFinishedSemaphores.clear();
    imageAvailableSemaphores.clear();
    imagesInFlight.clear();

    if (commandPool)
    {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    // 演示场景（VulkanSceneRenderer / VulkanMeshBuffer）
    DestroySceneRenderer();

    CleanupSwapchain();

    // if (trianglePipeline)
    // {
    //     vkDestroyPipeline(device, trianglePipeline, nullptr);
    //     trianglePipeline = VK_NULL_HANDLE;
    // }
    if (trianglePipelineLayout)
    {
        vkDestroyPipelineLayout(device, trianglePipelineLayout, nullptr);
        trianglePipelineLayout = VK_NULL_HANDLE;
    }
    if (renderPass)
    {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    if (surface)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (device)
    {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (validationEnabled && debugMessenger)
    {
        auto DestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (DestroyDebugUtilsMessengerEXT)
            DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    if (instance)
    {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    window = nullptr;
}

} // namespace aster



