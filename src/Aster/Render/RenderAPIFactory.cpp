#include "RenderAPI.h"

#ifndef ASTER_FORCE_RENDER_API_VULKAN
#include "OpenGLRenderAPI.h"
#endif

#ifdef ASTER_ENABLE_VULKAN
#include "VulkanRenderAPI.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

namespace aster
{

// 当前活动后端（RenderAPI::Current() / SetCurrent()）
RenderAPI *RenderAPI::s_current = nullptr;

RenderAPIType ParseRenderAPIType(const std::string &name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c)
                   { return (char)std::tolower(c); });

    if (lower == "vulkan" || lower == "vk")
        return RenderAPIType::Vulkan;
    return RenderAPIType::OpenGL;
}

RenderAPIType ResolveRenderAPIType(RenderAPIType defaultType)
{
#ifdef ASTER_FORCE_RENDER_API_VULKAN
    return RenderAPIType::Vulkan;
#elif defined(ASTER_FORCE_RENDER_API_OPENGL)
    return RenderAPIType::OpenGL;
#else
    // 运行期通过环境变量 ASTER_RENDER_API 覆盖（"opengl" / "vulkan"）
    if (const char *env = std::getenv("ASTER_RENDER_API"))
    {
        if (env[0] != '\0')
            return ParseRenderAPIType(env);
    }
    return defaultType;
#endif
}

RenderAPI *CreateRenderAPI(RenderAPIType type)
{
    switch (type)
    {
    case RenderAPIType::OpenGL:
#ifdef ASTER_FORCE_RENDER_API_VULKAN
    // 当强制使用Vulkan时，OpenGL请求自动转为Vulkan
    std::cerr << "[RenderAPI] OpenGL requested but Vulkan is forced. "
                << "Using Vulkan instead." << std::endl;
#ifdef ASTER_ENABLE_VULKAN
        return new VulkanRenderAPI();
#else
        return nullptr;
#endif
#else
        return new OpenGLRenderAPI();
#endif

    case RenderAPIType::Vulkan:
#ifdef ASTER_ENABLE_VULKAN
        return new VulkanRenderAPI();
#else
        std::cerr << "[RenderAPI] Vulkan backend was not compiled in this build "
                     "(enable ASTER_ENABLE_VULKAN)" << std::endl;
        return nullptr;
#endif
    }
    return nullptr;
}

} // namespace aster

