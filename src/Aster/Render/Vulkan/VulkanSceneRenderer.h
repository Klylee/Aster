#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <any>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "LightData.h" // LightUBO：灯光数据透传给管线片元 UBO
#include "EnvironmentMap.h" // 环境贴图数据（上传到管线）

namespace aster
{
class VulkanPipeline;
class VulkanMeshBuffer;

// ============================================================================
// VulkanSceneRenderer —— Vulkan 场景渲染器（对应框架 Renderer 的 Vulkan 版）
// ----------------------------------------------------------------------------
// 每帧流程：
//   BeginFrame()                // 清空绘制列表
//   Submit(mesh, model, color)  // 记录绘制（网格 + 模型矩阵 + 材质颜色）
//   Record(cmd, view, proj)     // 更新相机 UBO、绑定管线、逐条录制绘制
//
// 与框架 Mesh / Material 的对应：
//   - 网格  -> VulkanMeshBuffer（顶点/索引缓冲）
//   - 材质  -> 目前支持单颜色（对应 .shader 中的 uniform vec4 color）
//   - 渲染器 -> 本类（相机 view/projection 通过相机 UBO 传入）
//
// 顶点布局与框架 Mesh 一致：pos3 + nor3 + uv2（stride 8 * sizeof(float) = 32B），
// 对应着色器 assets/shader/vulkan/mesh.vert/.frag。
// ============================================================================

class VulkanSceneRenderer
{
public:
    VulkanSceneRenderer() = default;
    ~VulkanSceneRenderer() { Shutdown(); }
    VulkanSceneRenderer(const VulkanSceneRenderer &) = delete;
    VulkanSceneRenderer &operator=(const VulkanSceneRenderer &) = delete;

    // shaderDir: 存放 mesh.vert.spv / mesh.frag.spv 的目录
    // queue / cmdPool：创建环境贴图占位资源（布局转换）用
    bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
              VkQueue queue, VkCommandPool cmdPool, const std::string &shaderDir);

    void Shutdown();

    void BeginFrame();
    // material = (粗糙度, 金属度, AO, 纹理索引)；-1 纹理 = 无纹理
    // pipelineIndex：0 = 默认 mesh 管线；>=1 = CreateMaterialPipeline 返回的自定义管线
    // customUniforms/customUniformOrder（M4）：自定义材质参数（OpenGL 风格
    // SetUniform(key,type,value)），打包到 binding 12 动态 UBO；可传 nullptr = 无。
    void Submit(const VulkanMeshBuffer &mesh, const glm::mat4 &model, const glm::vec4 &color,
                const glm::vec4 &material, bool castsShadow = true, int pipelineIndex = 0,
                const std::unordered_map<std::string, std::pair<std::string, std::any>> *customUniforms = nullptr,
                const std::vector<std::string> *customUniformOrder = nullptr);
    // 录制阴影映射 pass（必须在主 render pass 开始前调用，Present 中先于主 pass）
    void RecordShadow(VkCommandBuffer cmd);
    void Record(VkCommandBuffer cmd, const glm::mat4 &view, const glm::mat4 &proj);

    // ---- M3：自定义材质管线 ----
    // 创建自定义顶点/片元管线（共享描述集布局/顶点布局/push constants），返回索引（1 起）。
    // shaderDir = spv 目录；vertName/fragName 如 "toon.vert"/"toon.frag"。
    // enableBlend / enableDepthWrite：混合 / 深度写开关（半透明网格线等）。
    // depthBias：多边形深度偏移（贴地叠加层消除 z-fight）。
    int CreateMaterialPipeline(const std::string &shaderDir,
                               const std::string &vertName, const std::string &fragName,
                               bool enableBlend = false, bool enableDepthWrite = true,
                               float depthBias = 0.0f);

    // 设置本帧灯光数据（App 每帧调用；Record 时上传到灯光 UBO）
    void SetLights(const LightUBO &lights);

    // 设置平面投影阴影的接收平面高度（y）。默认 0.02。
    // 场景中存在 SpotLight 时，castsShadow 的网格会把阴影压到该平面绘制。
    void SetShadowPlane(float y) { shadowPlaneY_ = y; }

    // 软阴影开关：true = PCF 软阴影，false = 硬阴影
    void SetSoftShadow(bool soft) { softShadow_ = soft; }

    // shadowmap 调试视图：0=正常，1=显示 2D shadow map 深度，2=显示点光源 cubemap 深度
    void SetShadowDebugView(int mode) { shadowDebugView_ = mode; }

    // ---- M2：每对象材质纹理 ----
    // 注册一张 RGBA8 纹理到 binding 11 数组，返回索引（-1 失败）；索引 0 = 白色。
    int RegisterMaterialTexture(VkQueue queue, VkCommandPool cmdPool,
                                const uint8_t *rgba8, int width, int height);

    // ---- 环境贴图（IBL） ----
    // 上传环境贴图（cubemap / irradiance / 预过滤 / BRDF LUT）到管线。
    // 返回 false 表示上传失败（环境功能不可用）。
    bool UploadEnvironmentMap(VkQueue queue, VkCommandPool cmdPool,
                              const EnvironmentMap &env);

    // 环境模式：0=关闭，1=反射，2=漫反射 IBL，3=漫反射+高光 IBL（PBR）
    void SetEnvMode(int mode) { envMode_ = mode; }

    // 环境参数：强度 / 粗糙度 / 金属度 / AO / 方位角(弧度) / 曝光 / 是否 tone map
    void SetEnvParams(float intensity, float roughness, float metallic, float ao,
                      float yaw, float exposure, bool toneMap)
    {
        envIntensity_ = intensity;
        envRoughness_ = roughness;
        envMetallic_ = metallic;
        envAO_ = ao;
        envYaw_ = yaw;
        envExposure_ = exposure;
        envToneMap_ = toneMap;
    }

    // 环境贴图是否已上传（有真实数据，天空盒 + IBL 可用）
    bool HasEnvironment() const;

    // ---- 拾取 id map（鼠标拾取） ----
    // 创建拾取 id map（离屏颜色图 + 读回缓冲 + 拾取管线）。宽高一般取交换链尺寸。
    // 之后每帧：SubmitPick 提交可拾取对象 → RecordPickMap（主 pass 前）渲染 + 拷贝读回 →
    // 提交完成后 PickAt 读回像素 ID。
    bool EnablePickMap(const std::string &shaderDir, uint32_t width, uint32_t height);
    void DestroyPickMap();
    bool HasPickMap() const;

    // 提交一个可拾取对象（mesh + 模型矩阵 + 拾取 ID）。每帧在 BeginFrame 后调用。
    void SubmitPick(const VulkanMeshBuffer &mesh, const glm::mat4 &model, int pickId);

    // 录制拾取 pass（渲染可拾取对象为 ID 颜色）+ 拷贝到读回缓冲。
    // 必须在主 render pass 之前调用（Present 中先于主 pass）。
    void RecordPickMap(VkCommandBuffer cmd, const glm::mat4 &view, const glm::mat4 &proj);

    // 读回 (x, y) 像素处的拾取 ID（0 = 背景 / 未命中）。
    // 需在 RecordPickMap 对应帧的 GPU 工作提交完成（fence 等待）后调用。
    int PickAt(int x, int y) const;

    // ---- 调试线框绘制（M2，物理调试可视化） ----
    // 提交一条世界空间线段（每帧 BeginFrame 后调用，Record 时统一绘制）。
    void SubmitDebugLine(const glm::vec3 &a, const glm::vec3 &b, const glm::vec4 &color);
    // 开关调试线绘制（false 时 Record 跳过）
    void SetDebugDrawEnabled(bool e) { debugDrawEnabled_ = e; }

    bool IsReady() const { return pipeline != nullptr; }

private:
    struct DrawCall
    {
        const VulkanMeshBuffer *mesh = nullptr;
        glm::mat4 model;
        glm::vec4 color;
        glm::vec4 material;   // (粗糙度, 金属度, AO, 纹理索引)
        bool castsShadow = true;
        int pipelineIndex = 0; // 0=默认 mesh 管线，>=1=自定义管线
        int paramSlot = 0;     // M4：binding 12 动态 UBO 槽位（0 = 无自定义参数）
    };

    VkDevice device = VK_NULL_HANDLE;
    VulkanPipeline *pipeline = nullptr;
    std::vector<DrawCall> drawCalls;

    // ---- 拾取 id map ----
    struct PickCall
    {
        const VulkanMeshBuffer *mesh = nullptr;
        glm::mat4 model;
        int pickId = 0; // 0 = 背景（未命中）
    };
    std::vector<PickCall> pickCalls; // 本帧可拾取对象（BeginFrame 清空）
    bool pickEnabled_ = false;       // id map 是否可用

    // M4：自定义材质参数动态 UBO 状态
    int paramSlotCounter_ = 0;   // 本帧已分配的槽位计数（BeginFrame 重置）
    bool hasCustomParams_ = false; // 本帧是否有绘制带自定义参数（决定是否 per-draw 绑定）

    LightUBO lights_{};
    bool hasLights_ = false;
    float shadowPlaneY_ = 0.02f; // 平面投影阴影的接收平面高度
    bool softShadow_ = true;     // 软阴影开关
    int shadowDebugView_ = 0;    // shadowmap 调试视图

    // 环境贴图状态（每帧 Record 时写入 EnvUBO）
    int envMode_ = 0;             // 0=关闭, 1=反射, 2=漫反射 IBL, 3=漫反射+高光 IBL
    float envIntensity_ = 1.0f;   // 环境光强度
    float envRoughness_ = 0.3f;   // 材质粗糙度（高光 IBL）
    float envMetallic_ = 0.0f;    // 金属度
    float envAO_ = 1.0f;          // 环境光遮蔽
    float envYaw_ = 0.0f;         // 环境方位角（弧度）
    float envExposure_ = 1.0f;    // 曝光
    bool envToneMap_ = true;      // 是否 tone map

    // ---- 调试线段（M2） ----
    std::vector<float> debugVertices_; // pos3 + color4 交错（7 float / 顶点），BeginFrame 清空
    bool debugDrawEnabled_ = true;     // 调试线绘制开关
};

} // namespace aster

