#include "VulkanSceneRenderer.h"

#include "VulkanPipeline.h"
#include "VulkanMeshBuffer.h"

#include <algorithm>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>

namespace aster
{

// 与 assets/shader/vulkan/mesh.* 中 push constant 块保持一致：
//   mat4 model (64) + vec4 color (16) + vec4 shadow (16) = 96 字节
struct MeshPushConstants
{
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 shadow; // xyz=阴影灯光位置, w=阴影投影平面Y（0=非阴影绘制）
};

// Vulkan NDC 的 y 轴向下，而 glm::perspective 是 OpenGL 风格（y 轴向上）。
// 直接使用会导致场景上下颠倒。对投影矩阵左乘 y 翻转矩阵，使 NDC y 与
// viewport（正高度）匹配；主 pass 与阴影 pass 必须用同一翻转约定，
// 否则阴影深度写入与采样 UV 会错位。
static glm::mat4 VulkanYFlip(const glm::mat4 &m)
{
    return glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f)) * m;
}

bool VulkanSceneRenderer::Init(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkRenderPass renderPass, VkQueue queue,
                               VkCommandPool cmdPool, const std::string &shaderDir)
{
    this->device = device;
    pipeline = new VulkanPipeline();

    // 顶点布局与框架 Mesh 一致：pos3 + nor3 + uv2（stride 32 字节）
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 8 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0; // pos
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = 12; // normal
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = 24; // uv

    if (!pipeline->Create(device, physicalDevice, renderPass,
                          shaderDir + "/mesh.vert.spv", shaderDir + "/mesh.frag.spv",
                          sizeof(MeshPushConstants),
                          &binding, 1, attrs, 3,
                          /*enableDepthTest=*/true, /*enableBlend=*/false))
    {
        std::cerr << "[VulkanSceneRenderer] Pipeline creation failed "
                     "(shaders missing? SPIR-V not compiled?)" << std::endl;
        Shutdown();
        return false;
    }

    // 启用阴影映射（多 2D shadow map：方向光 + 聚光灯；点光源 cubemap）。
    // 失败时回退到平面投影阴影。
    int shadow2D = pipeline->EnableShadowMaps(shaderDir, 2048, VulkanPipeline::MAX_2D_SHADOW_MAPS);
    bool pointShadow = pipeline->EnablePointShadowMap(shaderDir, 1024);
    if (shadow2D <= 0 && !pointShadow)
        std::cerr << "[VulkanSceneRenderer] Shadow maps disabled - fallback to planar shadow" << std::endl;

    // 环境贴图描述符（占位资源 + 天空盒管线）。保证 mesh.frag 的 binding 6-10 始终有效。
    pipeline->CreateEnvironmentResources(queue, cmdPool, shaderDir);

    return true;
}

bool VulkanSceneRenderer::UploadEnvironmentMap(VkQueue queue, VkCommandPool cmdPool,
                                               const EnvironmentMap &env)
{
    if (!pipeline)
        return false;
    return pipeline->UploadEnvironmentMap(queue, cmdPool, env);
}

bool VulkanSceneRenderer::HasEnvironment() const
{
    return pipeline && pipeline->HasEnvironment();
}

void VulkanSceneRenderer::Shutdown()
{
    if (pipeline)
    {
        pipeline->Destroy();
        delete pipeline;
        pipeline = nullptr;
    }
    drawCalls.clear();
    device = VK_NULL_HANDLE;
}

void VulkanSceneRenderer::BeginFrame()
{
    drawCalls.clear();
}

void VulkanSceneRenderer::Submit(const VulkanMeshBuffer &mesh,
                                 const glm::mat4 &model, const glm::vec4 &color,
                                 bool castsShadow)
{
    if (!pipeline)
        return;
    drawCalls.push_back({&mesh, model, color, castsShadow});
}

void VulkanSceneRenderer::SetLights(const LightUBO &lights)
{
    lights_ = lights;
    hasLights_ = true;
}

// 立方体 6 面的 view 矩阵（点光源 cubemap 阴影用，各面 90° FOV）
static glm::mat4 PointShadowFaceView(int face, const glm::vec3 &lightPos)
{
    // 面方向与 up（OpenGL cubemap 约定；90° FOV，YFlip 由主投影统一处理）
    static const glm::vec3 dirs[6] = {
        glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
        glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
        glm::vec3(0, 0, 1), glm::vec3(0, 0, -1),
    };
    static const glm::vec3 ups[6] = {
        glm::vec3(0, -1, 0), glm::vec3(0, -1, 0),
        glm::vec3(0, 0, 1),  glm::vec3(0, 0, -1),
        glm::vec3(0, -1, 0), glm::vec3(0, -1, 0),
    };
    return glm::lookAt(lightPos, lightPos + dirs[face], ups[face]);
}

void VulkanSceneRenderer::RecordShadow(VkCommandBuffer cmd)
{
    if (!pipeline || !pipeline->HasShadowMap())
        return;

    // 把灯光 UBO 里的阴影标记同步到本帧（shadowMapIndex 由这里分配）
    // 遍历所有光源，为每个 castsShadow 的光源：
    //   - 方向光/聚光灯 → 渲染到对应 2D shadow map
    //   - 点光源 → 渲染到 cubemap 6 面
    int lightCount = std::min((int)lights_.lightCount, MAX_LIGHTS);
    int shadow2DIdx = 0;
    bool pointShadowDone = false;

    VkDescriptorSet shadowDesc = pipeline->GetShadowDescriptorSet();

    for (int i = 0; i < lightCount; i++)
    {
        const GPULight &g = lights_.lights[i];
        if (!g.castsShadow)
            continue;

        glm::vec3 lightPos(g.position[0], g.position[1], g.position[2]);

        if (g.type == 1) // Point → cubemap
        {
            if (!pipeline->HasPointShadowMap())
                continue;
            float farPlane = std::max(g.range, 1.0f);
            pipeline->UpdatePointShadow(lightPos, farPlane, true);
            pointShadowDone = true;

            // 渲染 6 面（90° FOV 透视）
            const glm::mat4 proj = VulkanYFlip(glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane));
            for (int face = 0; face < 6; face++)
            {
                glm::mat4 view = PointShadowFaceView(face, lightPos);
                pipeline->BeginPointShadowPass(cmd, face);
                pipeline->UpdateShadowCameraUBO(view, proj); // 阴影相机 UBO = 灯光视角（不覆盖主相机 UBO）
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetShadowPipeline());
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetLayout(),
                                        0, 1, &shadowDesc, 0, nullptr);
                for (const auto &call : drawCalls)
                {
                    if (!call.castsShadow)
                        continue;
                    MeshPushConstants pc{};
                    pc.model = call.model;
                    pc.color = glm::vec4(1.0f);
                    pc.shadow = glm::vec4(0.0f);
                    vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(pc), &pc);
                    call.mesh->Bind(cmd);
                    call.mesh->Draw(cmd);
                }
                pipeline->EndPointShadowPass(cmd, face);
            }
        }
        else // Directional(0) / Spot(2) → 2D shadow map
        {
            if (shadow2DIdx >= pipeline->Get2DShadowMapCount())
                continue;

            glm::mat4 lightView;
            glm::mat4 lightProj;
            if (g.type == 0) // Directional：正交投影
            {
                glm::vec3 dir(g.direction[0], g.direction[1], g.direction[2]);
                dir = glm::normalize(dir);
                // 正交视锥覆盖场景中心附近区域（demo 场景范围 ±50）
                lightView = glm::lookAt(lightPos, lightPos + dir, glm::vec3(0.0f, 1.0f, 0.0f));
                lightProj = VulkanYFlip(glm::ortho(-40.0f, 40.0f, -40.0f, 40.0f, 0.1f, 120.0f));
            }
            else // Spot：透视投影
            {
                glm::vec3 dir(g.direction[0], g.direction[1], g.direction[2]);
                dir = glm::normalize(dir);
                float outerCone = g.outerCone; // 已是弧度
                float fovY = std::max(outerCone, 0.2f) * 2.0f;
                lightView = glm::lookAt(lightPos, lightPos + dir, glm::vec3(0.0f, 1.0f, 0.0f));
                float farPlane = std::max(g.range, 3.0f);
                // near 不能太小：glm::perspective 深度非线性，near 越小球/地面在 NDC z 上越挤在一起。
                // demo 球心距灯约 16.7，near=8 时球心/地面深度差约 0.087（near=0.5 时仅 0.005，
                // 会被 bias/PCF 吞掉导致阴影不可见）。near 取灯光到场景最近物体的 1/2 左右。
                float nearPlane = std::min(std::max(g.range * 0.15f, 0.5f), 8.0f);
                lightProj = VulkanYFlip(glm::perspective(fovY, 1.0f, nearPlane, farPlane));
            }

            glm::mat4 lightViewProj = lightProj * lightView;
            pipeline->UpdateShadowLight(shadow2DIdx, lightViewProj, true, i, g.type);
            // 同步 shadowMapIndex 到灯光 UBO（shader 用它定位 shadow map）
            const_cast<GPULight &>(g).shadowMapIndex = shadow2DIdx;

            pipeline->BeginShadowPass(cmd, shadow2DIdx);
            pipeline->UpdateShadowCameraUBO(lightView, lightProj); // 阴影相机 UBO = 灯光视角（不覆盖主相机 UBO）
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetShadowPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetLayout(),
                                    0, 1, &shadowDesc, 0, nullptr);
            for (const auto &call : drawCalls)
            {
                if (!call.castsShadow)
                    continue;
                MeshPushConstants pc{};
                pc.model = call.model;
                pc.color = glm::vec4(1.0f);
                pc.shadow = glm::vec4(0.0f); // 深度 pass 不做平面投影
                vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(pc), &pc);
                call.mesh->Bind(cmd);
                call.mesh->Draw(cmd);
            }
            pipeline->EndShadowPass(cmd);
            shadow2DIdx++;
        }
    }

    // 剩余 2D shadow map 槽位关闭阴影（避免陈旧深度产生伪影）
    for (int k = shadow2DIdx; k < pipeline->Get2DShadowMapCount(); k++)
        pipeline->UpdateShadowLight(k, glm::mat4(1.0f), false, -1, 0);
    if (!pointShadowDone && pipeline->HasPointShadowMap())
        pipeline->UpdatePointShadow(glm::vec3(0.0f), 1.0f, false);
}

void VulkanSceneRenderer::Record(VkCommandBuffer cmd, const glm::mat4 &view, const glm::mat4 &proj)
{
    if (!pipeline)
        return;

    // 软阴影开关 + shadowmap 调试视图写入 ShadowUBO（shader 据此选择渲染模式）
    pipeline->UpdateSoftShadow(softShadow_);
    pipeline->UpdateShadowDebugView(shadowDebugView_);

    // 更新灯光 UBO
    if (hasLights_)
        pipeline->UpdateLightsUBO(lights_);

    // ---- 环境贴图 UBO（强度/模式/材质/相机位置等） ----
    {
        VulkanPipeline::EnvUBO envUbo{};
        envUbo.params0 = glm::vec4(envIntensity_, (float)envMode_,
                                   (float)pipeline->GetPrefilteredMips(), envAO_);
        envUbo.params1 = glm::vec4(envRoughness_, envMetallic_, envYaw_, envExposure_);
        glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
        envUbo.params2 = glm::vec4(envToneMap_ ? 1.0f : 0.0f, camPos.x, camPos.y, camPos.z);
        pipeline->UpdateEnvUBO(envUbo);
    }

    // ---- 主 pass ----
    // Vulkan y 翻转：让场景在正高度 viewport 下不上下颠倒（与阴影 pass 约定一致）
    glm::mat4 yflipProj = VulkanYFlip(proj);
    pipeline->UpdateCameraUBO(view, yflipProj);

    // 天空盒（背景）：位于无穷远（只用 view 的旋转部分），深度 = 1.0 远平面。
    // 必须在场景几何之前绘制；深度写关闭，场景仍会盖在上面。
    if (pipeline->HasEnvironment())
    {
        glm::mat4 rotView = glm::mat4(glm::mat3(view)); // 去掉平移 → 天空盒跟随相机旋转
        glm::mat4 invViewProj = glm::inverse(yflipProj * rotView);
        pipeline->RecordSkybox(cmd, invViewProj, envYaw_, envExposure_);
    }

    // 重新绑定主 mesh 管线（RecordSkybox 已切换到天空盒管线）
    pipeline->Bind(cmd);

    for (const auto &call : drawCalls)
    {
        MeshPushConstants pc{};
        pc.model = call.model;
        pc.color = call.color;

        vkCmdPushConstants(cmd, pipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        call.mesh->Bind(cmd);
        call.mesh->Draw(cmd);
    }

    // ---- 平面投影阴影（仅当无 shadow map 时作为回退） ----
    if (!pipeline->HasShadowMap() && shadowPlaneY_ > 0.0f)
    {
        glm::vec3 spotPos(0.0f);
        bool hasSpot = false;
        int lightCount = std::min((int)lights_.lightCount, MAX_LIGHTS);
        for (int i = 0; i < lightCount; i++)
        {
            if (lights_.lights[i].type == 2)
            {
                spotPos = glm::vec3(lights_.lights[i].position[0], lights_.lights[i].position[1], lights_.lights[i].position[2]);
                hasSpot = true;
                break;
            }
        }
        if (hasSpot)
        {
            for (const auto &call : drawCalls)
            {
                if (!call.castsShadow)
                    continue;

                MeshPushConstants pc{};
                pc.model = call.model;
                pc.color = glm::vec4(1.0f);
                pc.shadow = glm::vec4(spotPos, shadowPlaneY_);

                vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(pc), &pc);
                call.mesh->Bind(cmd);
                call.mesh->Draw(cmd);
            }
        }
    }
}

} // namespace aster

