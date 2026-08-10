#pragma once

#include "RenderAPI.h"
#include <vector>
#include <vulkan/vulkan.h>

// 前向声明，避免在头文件引入更多依赖
namespace aster
{
class VulkanSceneRenderer;
class VulkanMeshBuffer;
class Mesh;

// ============================================================================
// VulkanRenderAPI —— Vulkan 后端
// ----------------------------------------------------------------------------
// 负责创建 Vulkan 实例 / 物理设备 / 逻辑设备 / 交换链 / 渲染流程 / 图形管线，
// 以及 ImGui 的 Vulkan 后端集成。OpenGL 后端保持对框架的完整支持，
// 而 Vulkan 后端当前提供框架级渲染流程：
//    1. 清屏（Clear 颜色）
//    2. 一个简单的演示三角形管线（push constants 驱动，无顶点缓冲）
//    3. ImGui 叠加层（使用官方 imgui_impl_vulkan 后端）
// 后续里程碑将把 Mesh / Shader / Material / Renderer 迁移到该 Vulkan 流程。
//
// 演示三角形着色器由 CMake 通过 glslc 编译为 SPIR-V（VULKAN_SHADER_DIR），
// 若运行时找不到着色器文件，三角形管线会被跳过，仅保留清屏 + ImGui。
// ============================================================================

class VulkanRenderAPI : public RenderAPI
{
public:
    VulkanRenderAPI() = default;
    ~VulkanRenderAPI() override;

    bool Init(GLFWwindow *window) override;
    void Shutdown() override;

    void Clear(const glm::vec4 &color) override;
    void Present() override;

    bool ImGuiInit() override;
    void ImGuiNewFrame() override;
    void ImGuiRender() override;

    void OnResize(int framebufferWidth, int framebufferHeight) override;

    // ---- 场景渲染（Model / 框架 Mesh 经此提交到 Vulkan） ----
    // 提交一个框架 Mesh（顶点布局 pos3+nor3+uv2）绘制。
    // 首次提交时会懒创建 Mesh 的 VulkanMeshBuffer；本帧在 Present() 中渲染。
    // params：每对象材质参数（颜色 / 粗糙度 / 金属度 / AO / 纹理索引 / 管线）。
    // castsShadow：该网格是否投影平面阴影（接收体如地面应传 false）。
    void SubmitSceneMesh(const Mesh &mesh, const glm::mat4 &model, const MaterialParams &params,
                         bool castsShadow = true);

    // 设置场景相机；RenderScene 使用它而非演示用的环绕相机。
    void SetSceneCamera(const glm::mat4 &view, const glm::mat4 &proj) override;

    // 设置场景灯光；由 App::Render 每帧传入，录制时上传到灯光 UBO。
    void SetSceneLights(const LightUBO &lights) override;

    // 软阴影开关（转发到 sceneRenderer）
    void SetSoftShadow(bool soft) override;

    // shadowmap 调试视图（转发到 sceneRenderer）
    void SetShadowDebugView(int mode) override;

    // ---- 环境贴图（IBL）----
    // 上传环境贴图数据到 sceneRenderer（GPU 图像 + 描述符）
    void SetEnvironmentMap(const EnvironmentMap &env) override;
    // 环境模式：0=关闭，1=反射，2=漫反射 IBL，3=漫反射+高光 IBL
    void SetEnvMode(int mode) override;
    // 环境参数：强度/粗糙度/金属度/AO/方位角/曝光/tone map
    void SetEnvParams(float intensity, float roughness, float metallic,
                      float ao, float yaw, float exposure, bool toneMap) override;

    // ---- M2：每对象材质纹理 ----
    // 注册一张 RGBA8 材质纹理（转发到场景渲染器 binding 11），返回索引（-1 失败）。
    int RegisterMaterialTexture(const uint8_t *rgba8, int width, int height);

    RenderAPIType Type() const override { return RenderAPIType::Vulkan; }
    const char *Name() const override { return "Vulkan"; }

private:
    // ---- 实例 / 设备 ----
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // ---- 交换链 ----
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    // ---- 渲染流程 ----
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout trianglePipelineLayout = VK_NULL_HANDLE;
    VkPipeline trianglePipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    // ---- 演示网格（通过 VulkanSceneRenderer 渲染） ----
    VulkanSceneRenderer *sceneRenderer = nullptr;
    VulkanMeshBuffer *cubeMesh = nullptr;

    // ---- 场景渲染状态（Model 提交的网格 + 场景相机） ----
    glm::mat4 sceneView{1.0f};
    glm::mat4 sceneProj{1.0f};
    bool hasSceneCamera = false;
    bool hasSceneMesh = false; // 本帧是否有场景网格提交（有则不画演示立方体）

    // ---- 命令 ----
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // ---- 同步 ----
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight; // 交换链每个 image 当前被哪一帧占用
    uint32_t currentFrame = 0;
    uint64_t frameCount = 0;

    // ---- 状态 ----
    GLFWwindow *window = nullptr;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec3 triangleColor{0.2f, 0.65f, 1.0f};
    bool validationEnabled = false;

    // ---- 内部方法 ----
    bool CreateInstance();
    void SetupDebugMessenger();
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSurface();
    bool CreateSwapchain();
    void CreateImageViews();
    void CreateDepthResources();
    bool CreateRenderPass();
    void CreateTrianglePipeline();
    void CreateFramebuffers();
    bool CreateCommandPool();
    bool CreateSyncObjects();
    void RecreateSwapchain(int width, int height);
    void CleanupSwapchain();
    void CleanupDepthResources();

    // 演示场景（旋转立方体，通过 VulkanSceneRenderer 验证 Mesh/Material/Renderer 迁移）
    bool CreateSceneRenderer();
    void DestroySceneRenderer();
    void RenderShadowMap(VkCommandBuffer cmd); // 阴影映射 pass（主 pass 之前）
    void RenderScene(VkCommandBuffer cmd);

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData);
};

} // namespace aster

