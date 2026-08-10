#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "LightData.h" // LightUBO：灯光 UBO 数据（std140，与着色器一致）
#include "EnvironmentMap.h" // 环境贴图 CPU 数据（上传到 GPU）

// ============================================================================
// VulkanPipeline —— Vulkan 图形管线封装（Shader 端口的第一步）
// ----------------------------------------------------------------------------
// 一个可复用的 Vulkan 图形管线：
//   - 从 SPIR-V 文件加载顶点 / 片元着色器
//   - 创建相机 UBO 描述符集布局（set 0, binding 0 = view / projection）
//   - 创建灯光 UBO（set 0, binding 1 = 灯光数组，供片元着色器使用）
//   - 创建管线布局（含 push constants，用于 model / 每对象 uniform）
//   - 从指定 render pass 创建图形管线（支持深度 / 混合开关）
//   - 管理相机 / 灯光 UBO 缓冲 + 描述符集
//
// 约定（与配套着色器 assets/shader/vulkan/mesh.* 对应）：
//   set 0 binding 0 : uniform buffer（CameraUBO，顶点阶段）
//   set 0 binding 1 : uniform buffer（LightsUBO，片元阶段）
//   push constants  : mat4 model + vec4 color（80 字节，顶点与片元一致声明）
// ============================================================================

namespace aster
{

class VulkanPipeline
{
public:
    VulkanPipeline() = default;
    ~VulkanPipeline() { Destroy(); }
    VulkanPipeline(const VulkanPipeline &) = delete;
    VulkanPipeline &operator=(const VulkanPipeline &) = delete;

    struct CameraUBO
    {
        glm::mat4 view;
        glm::mat4 projection;
    };

    // 创建管线。vertSpv / fragSpv 为 SPIR-V 文件路径。
    // pushConstantSize 必须与着色器 push constant 块大小一致（<=128 字节）。
    bool Create(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                const std::string &vertSpv, const std::string &fragSpv,
                size_t pushConstantSize,
                const VkVertexInputBindingDescription *bindings, uint32_t bindingCount,
                const VkVertexInputAttributeDescription *attrs, uint32_t attrCount,
                bool enableDepthTest = true, bool enableBlend = false);

    void Destroy();

    VkPipeline GetPipeline() const { return pipeline; }
    VkPipelineLayout GetLayout() const { return layout; }
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorSet GetDescriptorSet() const { return descriptorSet; }
    size_t PushConstantSize() const { return pushConstantSize_; }

    // 更新相机 UBO（view / projection）
    void UpdateCameraUBO(const glm::mat4 &view, const glm::mat4 &proj);

    // 更新阴影深度 pass 专用的相机 UBO（灯光视角）。
    // 必须与主 pass 的相机 UBO 分开：两者录制在同一 command buffer 中，
    // 若共用同一 UBO，提交后 GPU 执行 shadow pass 时会读到被主 pass 覆盖的主相机矩阵，
    // 导致 shadow map 深度错误（球被投影到远处 → 深度接近 far → 阴影失效）。
    void UpdateShadowCameraUBO(const glm::mat4 &view, const glm::mat4 &proj);

    // 更新灯光 UBO（片元着色器使用）
    void UpdateLightsUBO(const LightUBO &lights);

    // 在命令缓冲中绑定管线与相机描述集
    void Bind(VkCommandBuffer cmd) const;

    // 阴影深度 pass 使用的描述符集（binding 0 = 阴影专用相机 UBO，其余与主描述符集相同）
    VkDescriptorSet GetShadowDescriptorSet() const { return shadowDescriptorSet; }

    // ---- M3：自定义材质管线 ----
    // 创建一条与主管线共享描述集布局 / push constants / 顶点布局（pos3+nor3+uv2）的
    // 自定义材质管线（顶点+片元 SPIR-V）。返回索引：0 = 默认 mesh 管线，自定义从 1 起。
    // shaderDir = 存放 spv 的目录；vertName/fragName 如 "toon.vert"/"toon.frag"。
    // 所有管线共用同一描述符集（相机/灯光/环境/材质纹理），自定义管线可访问全部绑定。
    int CreateMaterialPipeline(const std::string &shaderDir,
                               const std::string &vertName, const std::string &fragName);
    // 取自定义材质管线（index 从 1 起；0 或非法返回 VK_NULL_HANDLE）
    VkPipeline GetMaterialPipeline(int index) const;
    // 已创建自定义管线数量
    int GetMaterialPipelineCount() const { return (int)materialPipelines.size(); }

    // ---- 阴影映射（多光源：方向光/聚光灯用 2D shadow map，点光源用 cubemap） ----
    static constexpr int MAX_2D_SHADOW_MAPS = 4;  // 方向光 + 聚光灯（2D shadow map 数）
    static constexpr int MAX_POINT_SHADOWS = 1;   // 点光源（cubemap shadow map 数）

    // 每盏 2D 阴影灯的信息（std140，64+16=80 字节）
    struct ShadowLightUBO
    {
        glm::mat4 lightViewProj;  // @0
        int lightIndex;           // @64 对应 LightsUBO 中灯光索引
        int enabled;              // @68
        int lightType;            // @72 0=Directional, 2=Spot
        float _pad;               // @76
    };

    // 2D 阴影信息 UBO（方向光/聚光灯）
    struct ShadowUBO
    {
        int shadowLightCount;        // @0
        int softShadow;              // @4  1=软阴影(PCF)，0=硬阴影
        int shadowDebugView;         // @8  0=正常，1=显示 2D shadow map 深度，2=显示点光源 cubemap 深度
        int _pad;                    // @12
        ShadowLightUBO lights[MAX_2D_SHADOW_MAPS]; // @16（每个 80 字节）
    };

    // 设置软阴影开关（写入 ShadowUBO.softShadow）
    void UpdateSoftShadow(bool soft);

    // 设置 shadowmap 调试视图（0=正常，1=2D shadow map，2=点光源 cubemap）
    void UpdateShadowDebugView(int mode);

    // 点光源 cubemap 阴影参数
    struct PointShadowUBO
    {
        glm::vec4 lightPos;  // @0 xyz=位置
        float farPlane;      // @16 点光源影响范围（作为阴影远平面）
        float enabled;       // @20
        float _pad[2];       // @24
    };

    // 创建 2D shadow maps（方向光 + 聚光灯共用）+ 深度渲染 pass + 深度管线。
    // 返回创建的 2D shadow map 数量（成功创建数）。
    int EnableShadowMaps(const std::string &shaderDir, uint32_t size = 2048, int count = 2);
    // 创建点光源 cubemap shadow map（6 面）
    bool EnablePointShadowMap(const std::string &shaderDir, uint32_t size = 1024);
    bool HasShadowMap() const { return shadowMapCount2D_ > 0 || pointShadowMapEnabled_; }

    // 设置第 idx 盏 2D 阴影灯（lightIndex 对应 LightsUBO 索引）
    void UpdateShadowLight(int idx, const glm::mat4 &lightViewProj, bool enabled, int lightIndex, int lightType);
    // 设置点光源阴影参数
    void UpdatePointShadow(const glm::vec3 &lightPos, float farPlane, bool enabled);

    // 深度 pass：在指定 2D shadow map 帧缓冲中渲染场景（相机 UBO 需先设为灯光视角）
    void BeginShadowPass(VkCommandBuffer cmd, int idx = 0);
    // 深度 pass：渲染点光源 cubemap 的某个面（face 0..5）
    void BeginPointShadowPass(VkCommandBuffer cmd, int face);
    void EndShadowPass(VkCommandBuffer cmd);
    void EndPointShadowPass(VkCommandBuffer cmd, int face);

    VkPipeline GetShadowPipeline() const { return shadowPipeline; }
    VkFramebuffer GetShadowFramebuffer(int idx = 0) const { return (idx >= 0 && idx < (int)shadowFramebuffers.size()) ? shadowFramebuffers[idx] : VK_NULL_HANDLE; }
    VkExtent2D GetShadowMapExtent() const { return shadowExtent; }
    int Get2DShadowMapCount() const { return shadowMapCount2D_; }
    bool HasPointShadowMap() const { return pointShadowMapEnabled_; }

    // ---- 环境贴图（Environment Map / IBL） ----
    // EnvUBO 与着色器 EnvUBO 一致（std140，3 个 vec4）：
    //   params0 = (强度, 模式, mip级数, AO)
    //   params1 = (粗糙度, 金属度, 方位角弧度, 曝光)
    //   params2 = (tonemap 开关, 相机位置.xyz)
    struct EnvUBO
    {
        glm::vec4 params0;
        glm::vec4 params1;
        glm::vec4 params2;
    };

    // 创建环境贴图描述符资源（占位 1x1 黑色 cubemap + LUT，保证 binding 6-10 始终有效）
    // 以及天空盒管线。真实环境数据可在之后用 UploadEnvironmentMap 上传。
    // queue / cmdPool 用于把占位贴图转换到可采样布局。
    // 返回 false 表示占位资源创建失败；天空盒着色器缺失时仅提示降级（仍返回 true）。
    bool CreateEnvironmentResources(VkQueue queue, VkCommandPool cmdPool,
                                    const std::string &shaderDir);

    // 上传真实环境贴图（环境 cubemap / irradiance / 预过滤 mip 链 / BRDF LUT），
    // 替换占位资源并更新描述符。成功后 HasEnvironment() 返回 true。
    bool UploadEnvironmentMap(VkQueue queue, VkCommandPool cmdPool,
                              const EnvironmentMap &env);

    // 更新环境 UBO（强度 / 模式 / 材质参数 / 相机位置等）
    void UpdateEnvUBO(const EnvUBO &env);

    // 环境贴图是否已上传（有真实数据）
    bool HasEnvironment() const { return environmentReady_; }

    // 预过滤 cubemap 的 mip 级数（EnvUBO 的 mipCount 用）
    int GetPrefilteredMips() const { return prefilteredMips_; }

    // ---- M2：每对象材质纹理（binding 11 数组） ----
    // 数组容量（与 mesh.frag 的 uMaterialTextures[16] 保持一致）
    static constexpr int MAX_MATERIAL_TEXTURES = 16;

    // 注册一张 RGBA8 材质纹理（staging 上传到 device-local 并更新 binding 11 元素）。
    // 返回纹理索引（供 Material::textureIndex 使用），失败返回 -1。
    // 索引 0 恒为 1x1 白色纹理（无纹理对象采样它 = 纯色）。
    int RegisterMaterialTexture(VkQueue queue, VkCommandPool cmdPool,
                                const uint8_t *rgba8, int width, int height);

    // 已注册材质纹理数量（含索引 0 的白色占位）
    int GetMaterialTextureCount() const { return (int)materialImageViews.size(); }

    // 天空盒：在主 render pass 内、绘制场景之前调用（深度写关闭，深度 = 1.0 远平面）。
    // invViewProj 由调用方用“仅旋转的 view”计算（天空盒位于无穷远）。
    void RecordSkybox(VkCommandBuffer cmd, const glm::mat4 &invViewProj,
                      float yaw, float exposure);

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE; // 主 render pass（天空盒管线复用）
    size_t pushConstantSize_ = 0;

    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // 相机 UBO
    VkBuffer uboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uboMemory = VK_NULL_HANDLE;
    void *uboMapped = nullptr;

    // 阴影深度 pass 专用相机 UBO（灯光视角，与主 pass 相机 UBO 分离）
    VkBuffer shadowCamBuffer = VK_NULL_HANDLE;
    VkDeviceMemory shadowCamMemory = VK_NULL_HANDLE;
    void *shadowCamMapped = nullptr;

    // 阴影深度 pass 专用描述符集
    VkDescriptorSet shadowDescriptorSet = VK_NULL_HANDLE;

    // 灯光 UBO
    VkBuffer lightsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lightsMemory = VK_NULL_HANDLE;
    void *lightsMapped = nullptr;

    // 阴影映射资源（2D shadow maps：方向光 + 聚光灯）
    int shadowMapCount2D_ = 0;
    bool shadowMapEnabled_ = false;
    std::vector<VkImage> shadowImages;          // 每个 2D 阴影光源一张
    std::vector<VkDeviceMemory> shadowImageMemories;
    std::vector<VkImageView> shadowImageViews;
    std::vector<VkFramebuffer> shadowFramebuffers;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkExtent2D shadowExtent{2048, 2048};

    // 点光源 cubemap shadow map
    bool pointShadowMapEnabled_ = false;
    VkImage pointShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory pointShadowImageMemory = VK_NULL_HANDLE;
    VkImageView pointShadowImageView = VK_NULL_HANDLE;       // cube view（采样）
    std::vector<VkImageView> pointShadowFaceViews;           // 6 个 face view（渲染）
    std::vector<VkFramebuffer> pointShadowFaceFramebuffers;  // 6 个 face framebuffer
    VkSampler pointShadowSampler = VK_NULL_HANDLE;
    VkRenderPass pointShadowRenderPass = VK_NULL_HANDLE;
    VkPipeline pointShadowPipeline = VK_NULL_HANDLE;
    VkExtent2D pointShadowExtent{1024, 1024};

    // 阴影 UBO
    VkBuffer shadowUboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory shadowUboMemory = VK_NULL_HANDLE;
    void *shadowUboMapped = nullptr;
    VkBuffer pointShadowUboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory pointShadowUboMemory = VK_NULL_HANDLE;
    void *pointShadowUboMapped = nullptr;

    // ---- 环境贴图资源 ----
    bool environmentReady_ = false; // 真实环境数据是否已上传
    VkBuffer envUboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory envUboMemory = VK_NULL_HANDLE;
    void *envUboMapped = nullptr;

    // 占位资源（1x1 黑色 cubemap / 2D），保证 binding 6-9 在真实数据上传前有效
    VkImage dummyCubeImage = VK_NULL_HANDLE;
    VkDeviceMemory dummyCubeMemory = VK_NULL_HANDLE;
    VkImageView dummyCubeView = VK_NULL_HANDLE;
    VkImage dummyLutImage = VK_NULL_HANDLE;
    VkDeviceMemory dummyLutMemory = VK_NULL_HANDLE;
    VkImageView dummyLutView = VK_NULL_HANDLE;

    // 环境 cubemap（1 mip，skybox / 反射）
    VkImage envImage = VK_NULL_HANDLE;
    VkDeviceMemory envImageMemory = VK_NULL_HANDLE;
    VkImageView envImageView = VK_NULL_HANDLE;
    int envCubeSize_ = 0;
    // 漫反射 irradiance cubemap（1 mip）
    VkImage irrImage = VK_NULL_HANDLE;
    VkDeviceMemory irrImageMemory = VK_NULL_HANDLE;
    VkImageView irrImageView = VK_NULL_HANDLE;
    // 高光预过滤 cubemap（多 mip，粗糙度分层）
    VkImage prefilteredImage = VK_NULL_HANDLE;
    VkDeviceMemory prefilteredImageMemory = VK_NULL_HANDLE;
    VkImageView prefilteredImageView = VK_NULL_HANDLE;
    int prefilteredMips_ = 1;
    // BRDF LUT（2D）
    VkImage brdfImage = VK_NULL_HANDLE;
    VkDeviceMemory brdfImageMemory = VK_NULL_HANDLE;
    VkImageView brdfImageView = VK_NULL_HANDLE;

    // 采样器
    VkSampler envSampler = VK_NULL_HANDLE;         // 环境 / irradiance（线性，无 mip）
    VkSampler prefilteredSampler = VK_NULL_HANDLE; // 预过滤（mipmap 线性）
    VkSampler brdfSampler = VK_NULL_HANDLE;        // BRDF LUT（线性，clamp）

    // 天空盒管线（全屏三角形，push constant = mat4 invViewProj + vec4）
    VkPipeline skyboxPipeline = VK_NULL_HANDLE;

    // M3：自定义材质管线（索引 0 = 默认 mesh 管线，不在此列表；自定义从 materialPipelines[0] 起）
    std::vector<VkPipeline> materialPipelines;

    // 阴影管线复用的顶点输入（Create 时保存）
    std::vector<VkVertexInputBindingDescription> vertexBindings_;
    std::vector<VkVertexInputAttributeDescription> vertexAttrs_;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // ---- M2：每对象材质纹理（binding 11，RGBA8） ----
    VkSampler materialSampler = VK_NULL_HANDLE;
    std::vector<VkImage> materialImages;
    std::vector<VkDeviceMemory> materialImageMemories;
    std::vector<VkImageView> materialImageViews;
};

} // namespace aster

