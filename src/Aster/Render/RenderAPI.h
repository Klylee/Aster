#pragma once

// ============================================================================
// RenderAPI —— 图形 API 抽象层
// ----------------------------------------------------------------------------
// 该接口让 App 不再直接依赖 OpenGL 或 Vulkan，而是通过 RenderAPI 驱动渲染。
// 目前提供两种实现：
//   - OpenGLRenderAPI : 完整的 OpenGL 后端（默认，行为与旧版完全一致）
//   - VulkanRenderAPI : Vulkan 后端（实例/设备/交换链/渲染流程/ImGui）
//
// 运行期切换：
//   1. 通过环境变量 ASTER_RENDER_API=opengl|vulkan
//   2. 通过 App 构造函数传入 RenderAPIType
//   3. 编译期强制 ASTER_FORCE_RENDER_API
//
// 注意：底层的 Mesh/Shader/Material/Renderer 仍为 OpenGL 实现，
//       它们会在后续里程碑中迁移到 Vulkan。Vulkan 模式下框架会
//       使用内置的演示管线（清屏 + 三角形 + ImGui）跑通整套流程。
// ============================================================================

#include <string>
#include <unordered_map>
#include <any>
#include <vector>
#include <glm/glm.hpp>
#include "LightData.h"      // LightUBO：灯光数据（无 GL 依赖）
#include "EnvironmentMap.h" // 环境贴图资源（无 GL/Vulkan 依赖）

// ============================================================================
// 每对象材质参数（Vulkan 场景渲染用）
//   color           —— 基础色 / 反照率（push constant）
//   roughness       —— 粗糙度（高光 IBL / 高光分布）
//   metallic        —— 金属度
//   ao              —— 环境光遮蔽
//   textureIndex    —— M2：材质纹理索引（-1 = 无纹理）
//   pipelineIndex   —— M3：自定义 shader 管线索引（0 = 默认 mesh 管线）
//   customUniforms  —— M4：自定义 uniform（OpenGL 风格 SetUniform(key,type,value)）。
//                      Vulkan 后端打包到每对象动态 UBO（set0 binding12，std140）；
//                      OpenGL 后端忽略（Material::ApplyUniforms 自行处理）。
//                      指针指向 Material 的 uniform map / 注册顺序，仅本帧有效。
// ============================================================================
struct MaterialParams
{
    glm::vec4 color = glm::vec4(1.0f);
    float roughness = 0.35f;
    float metallic = 0.0f;
    float ao = 1.0f;
    int textureIndex = -1;
    int pipelineIndex = 0;

    // M4：自定义 uniform（指针，默认 null = 无自定义参数）。
    // customUniformOrder 为 SetUniform 的注册顺序（Vulkan 打包顺序，可与 shader 字段一一对应）。
    const std::unordered_map<std::string, std::pair<std::string, std::any>> *customUniforms = nullptr;
    const std::vector<std::string> *customUniformOrder = nullptr;
};

// 前向声明，避免把 GLFW 头文件带到所有包含方
struct GLFWwindow;

namespace aster
{
class Model; // 前向声明：PickAt 返回被拾取的 Model 指针

enum class RenderAPIType
{
    OpenGL,
    Vulkan
};

// 把字符串解析成 RenderAPIType（"opengl" / "vulkan" / "opengl4" 等容错）
RenderAPIType ParseRenderAPIType(const std::string &name);

// 返回当前应使用的 RenderAPIType：
//   优先级：编译期宏 > 环境变量 ASTER_RENDER_API > defaultType
RenderAPIType ResolveRenderAPIType(RenderAPIType defaultType = RenderAPIType::OpenGL);

// ----------------------------------------------------------------------------
// RenderAPI 抽象接口
// ----------------------------------------------------------------------------
class RenderAPI
{
public:
    virtual ~RenderAPI() = default;

    // ---- 当前活动后端（全局） ----
    // 由 App / demo 在创建后端后调用 SetCurrent 注册，
    // 供 Model::draw() 等框架对象按后端分发绘制，无需传递指针。
    static RenderAPI *Current() { return s_current; }
    static void SetCurrent(RenderAPI *api) { s_current = api; }

    // 初始化图形 API（创建上下文 / 实例、设备、交换链等）
    virtual bool Init(GLFWwindow *window) = 0;

    // 释放所有图形资源
    virtual void Shutdown() = 0;

    // 清屏：OpenGL 后端直接执行 glClear；Vulkan 后端记录清屏颜色，
    // 实际的 clear 在 Present() 记录的 command buffer 中执行。
    virtual void Clear(const glm::vec4 &color) = 0;

    // 提交一帧并显示：OpenGL 后端为 glfwSwapBuffers；
    // Vulkan 后端记录 command buffer、提交队列并 Present。
    virtual void Present() = 0;

    // 设置场景相机（view / projection）。
    //  - Vulkan 后端：在 Present() 录制场景绘制时使用；
    //  - OpenGL 后端：FlushBatches 已自行获得相机，忽略即可。
    // 在调用 RenderScene 相关的场景绘制前由 App 每帧设置。
    virtual void SetSceneCamera(const glm::mat4 &view, const glm::mat4 &proj)
    {
        (void)view;
        (void)proj;
    }

    // 设置场景灯光（每帧由 App::Render 调用）。
    //  - Vulkan 后端：上传到灯光 UBO 供片元着色器使用；
    //  - OpenGL 后端：忽略（LightManager 已通过 glBindBufferBase 直接绑定 UBO）。
    virtual void SetSceneLights(const LightUBO &lights) { (void)lights; }

    // 软阴影开关（Vulkan 后端实现；OpenGL 忽略）
    virtual void SetSoftShadow(bool soft) { (void)soft; }

    // shadowmap 调试视图（Vulkan 后端实现；OpenGL 忽略）
    virtual void SetShadowDebugView(int mode) { (void)mode; }

    // 鼠标拾取：返回 (x, y)（窗口坐标）处命中的可拾取 Model，未命中返回 nullptr。
    //  - Vulkan 后端：渲染 id map 并读回像素 ID，反查对象；
    //  - OpenGL 后端：默认未实现，返回 nullptr。
    // 注意：读回的是“最近一帧已提交完成”的 id map（本帧渲染在 Present 中完成，
    // 之后才能读到准确结果）。
    virtual const Model *PickAt(int x, int y)
    {
        (void)x;
        (void)y;
        return nullptr;
    }

    // 调试线框绘制（M2，物理调试可视化）：提交世界空间线段列表。
    // segments 为成对顶点（a0,b0,a1,b1,...），全部使用同一 color。
    //  - Vulkan 后端：记录到场景渲染器，主 pass 末尾统一绘制（深度测试开）；
    //  - OpenGL 后端：默认 no-op。
    // 需在每帧 BeginFrame（Clear）之后、Present 之前调用（如 Render 阶段）。
    virtual void DebugDrawLines(const std::vector<glm::vec3> &segments, const glm::vec4 &color)
    {
        (void)segments;
        (void)color;
    }

    // 上传环境贴图数据（cubemap / irradiance / 预过滤 / BRDF LUT）。
    //  - Vulkan 后端：上传到 GPU 图像并更新描述符；
    //  - OpenGL 后端：上传到 GL cubemap / 2D 纹理。
    // 上传成功后天空盒 + 物体 IBL 可用。
    virtual void SetEnvironmentMap(const EnvironmentMap &env) { (void)env; }

    // 环境模式：0=关闭，1=反射，2=漫反射 IBL，3=漫反射+高光 IBL（PBR）
    virtual void SetEnvMode(int mode) { (void)mode; }

    // 环境参数：强度 / 粗糙度 / 金属度 / AO / 方位角(弧度) / 曝光 / 是否 tone map
    virtual void SetEnvParams(float intensity, float roughness, float metallic,
                              float ao, float yaw, float exposure, bool toneMap)
    {
        (void)intensity;
        (void)roughness;
        (void)metallic;
        (void)ao;
        (void)yaw;
        (void)exposure;
        (void)toneMap;
    }

    // 每帧场景绘制前调用（OpenGL 后端：绘制天空盒 + 绑定环境贴图纹理；
    // Vulkan 后端：天空盒在场景渲染器内部处理，忽略）。
    virtual void RenderEnvironment(const glm::mat4 &view, const glm::mat4 &proj,
                                   int width, int height)
    {
        (void)view;
        (void)proj;
        (void)width;
        (void)height;
    }

    // ImGui 相关
    virtual bool ImGuiInit() = 0;      // 创建 ImGui 渲染后端
    virtual void ImGuiNewFrame() = 0;  // 每帧开始时更新 ImGui 输入/帧数据
    virtual void ImGuiRender() = 0;    // 生成/绘制 ImGui 绘制数据

    // 窗口/交换链尺寸变化（物理像素 / framebuffer 尺寸）
    virtual void OnResize(int framebufferWidth, int framebufferHeight) = 0;

    virtual RenderAPIType Type() const = 0;
    virtual const char *Name() const = 0;

    bool IsOpenGL() const { return Type() == RenderAPIType::OpenGL; }
    bool IsVulkan() const { return Type() == RenderAPIType::Vulkan; }

private:
    static RenderAPI *s_current; // 当前活动后端（见 Current/SetCurrent）
};

// 通过类型创建 RenderAPI 实例（需要调用方负责 delete）
// 若对应后端未编译（例如未启用 Vulkan），返回 nullptr。
RenderAPI *CreateRenderAPI(RenderAPIType type);

} // namespace aster

