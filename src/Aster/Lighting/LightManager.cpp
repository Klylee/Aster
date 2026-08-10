// LightManager —— 灯光管理器实现
// 声明见 LightManager.h。GL 相关调用（构造 UBO / UploadToGPU / BindToShader）
// 全部集中在本文件，使 LightManager.h 不再依赖 GLEW，头文件保持可移植。
#include "LightManager.h"

#include <algorithm>
#include <GL/glew.h>

namespace aster
{

LightManager::LightManager()
{
    // Vulkan 后端没有 GL 上下文，跳过 GL 资源创建
    //（UploadToGPU / BindToShader 仅 OpenGL 渲染路径调用）
    RenderAPI *api = RenderAPI::Current();
    if (api && api->IsVulkan())
        return;
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(LightUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void LightManager::AddLight(const std::shared_ptr<Light> &l)
{
    lights.push_back(l);
}

void LightManager::RemoveLight(const std::shared_ptr<Light> &l)
{
    lights.erase(
        std::remove_if(lights.begin(), lights.end(),
                       [&](const std::weak_ptr<Light> &w)
                       {
                           return w.lock() == l;
                       }),
        lights.end());
}

// 仅更新 CPU 侧数据（OpenGL / Vulkan 共用；不含 GL 调用）
void LightManager::UpdateLightData()
{
    // 清理过期的灯光
    lights.erase(
        std::remove_if(lights.begin(), lights.end(),
                       [](const std::weak_ptr<Light> &w)
                       {
                           return w.expired();
                       }),
        lights.end());

    int count = std::min((int)lights.size(), MAX_LIGHTS);
    uboData.lightCount = count;

    for (int i = 0; i < count; i++)
    {
        std::shared_ptr<Light> l = lights[i].lock();
        if (!l)
            continue;
        GPULight &g = uboData.lights[i];

        g.type = (int)l->type;
        g.color = glm::vec3(l->color.r, l->color.g, l->color.b);
        g.intensity = l->intensity;

        g.position = l->transform.GetPosition();

        // 阴影字段默认：不投射（shadowMapIndex 由 VulkanSceneRenderer 分配）
        g.castsShadow = l->castsShadow ? 1 : 0;
        g.shadowMapIndex = -1;
        g.shadowType = (l->type == LightType::Point) ? 1 : 0;

        if (l->type == LightType::Directional)
        {
            auto dir = std::static_pointer_cast<DirectionalLight>(l);
            g.direction = dir->direction;
        }
        else if (l->type == LightType::Point)
        {
            auto p = std::static_pointer_cast<PointLight>(l);
            g.range = p->range;
        }
        else if (l->type == LightType::Spot)
        {
            auto s = std::static_pointer_cast<SpotLight>(l);
            g.range = s->range;
            g.innerCone = s->innerCone;
            g.outerCone = s->outerCone;
            g.direction = s->direction;
        }
    }
}

// OpenGL：把 CPU 侧灯光数据上传到 GL UBO
void LightManager::UploadToGPU()
{
    UpdateLightData();

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightUBO), &uboData);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void LightManager::BindToShader(int bindingPoint)
{
    glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, ubo);
}

} // namespace aster

