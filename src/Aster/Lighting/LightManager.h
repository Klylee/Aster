#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "Light.h"
#include "LightData.h"   // GPULight / LightUBO / MAX_LIGHTS（无 GL 依赖，OpenGL/Vulkan 共用）
#include "RenderAPI.h"   // RenderAPI::Current()：Vulkan 后端无 GL 上下文

// LightManager —— 灯光管理器
// 头文件不依赖 GLEW（GL 调用集中在 LightManager.cpp）；
// ubo 为 OpenGL UBO 句柄，类型用 unsigned int（与 GLuint 兼容）。
namespace aster
{

class LightManager
{
public:
    unsigned int ubo = 0; // OpenGL UBO 句柄；Vulkan 路径不使用
    static constexpr int MAX_LIGHTS = 32;

    std::vector<std::weak_ptr<Light>> lights; // actual lights in scene
    LightUBO uboData;                         // CPU-side buffer

    // Vulkan 后端无 GL 上下文，构造时自动跳过 GL 资源创建
    LightManager();

    void AddLight(const std::shared_ptr<Light> &l);
    void RemoveLight(const std::shared_ptr<Light> &l);

    // 仅更新 CPU 侧数据（OpenGL / Vulkan 共用；不含 GL 调用）
    void UpdateLightData();

    // OpenGL：把 CPU 侧灯光数据上传到 GL UBO
    void UploadToGPU();

    void BindToShader(int bindingPoint);
};

} // namespace aster

