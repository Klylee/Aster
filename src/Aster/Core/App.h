#pragma once
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

#include "RenderAPI.h"

// ============================================================================
// App —— 应用基类
// ----------------------------------------------------------------------------
// App 不再直接调用 OpenGL，而是通过 RenderAPI 抽象驱动渲染后端。
// 渲染后端在运行期可选择：
//   - OpenGL（默认，完整行为与旧版一致）
//   - Vulkan（ASTER_RENDER_API=vulkan 或构造函数传入 RenderAPIType::Vulkan）
//
// 子类（如 GsEditor）可通过 renderAPI->IsOpenGL() / IsVulkan() 查询当前后端。
//
// 更新循环：Run() 采用“固定步长 + 累计器”模式。
//   - FixedUpdate()：以固定频率（默认 120Hz）执行，帧率无关、确定、顺滑；
//     适合物理与确定性逻辑。可通过 SetFixedTimeStep() 调整频率。
//   - Update()：每帧执行一次，适合相机、输入、UI 等帧相关逻辑。
//   - GetFixedAlpha() 返回 0~1 插值系数，供渲染侧在两个固定状态间做平滑插值。
// ============================================================================

namespace aster
{

class App
{
public:
    App(int width = 1200, int height = 900, const std::string &title = "OpenGL Application",
        RenderAPIType apiType = RenderAPIType::OpenGL);
    virtual ~App();

    bool Init();
    void Run();
    void Destroy();

    GLFWwindow *GetWindow() const { return window; }
    int Width() const { return width; }
    int Height() const { return height; }
    RenderAPI *GetRenderAPI() const { return renderAPI; }

    // —— 固定步长（FixedUpdate）配置 ——
    // 固定更新频率为 fixedTimeStep 秒/次，默认 1/120s（120Hz）。
    void SetFixedTimeStep(float fixedTimeStep) { this->fixedTimeStep = fixedTimeStep; }
    float GetFixedTimeStep() const { return fixedTimeStep; }
    // 当前帧累计的固定步长余量（0 ~ fixedTimeStep），供子类在渲染时做插值
    float GetFixedAccumulator() const { return fixedAccumulator; }
    // 固定步长插值系数 = fixedAccumulator / fixedTimeStep（0 ~ 1），
    // 用于渲染层在两个固定状态之间做平滑插值（alpha blending）。
    float GetFixedAlpha() const { return fixedTimeStep > 0.0f ? fixedAccumulator / fixedTimeStep : 0.0f; }

protected:
    virtual bool InitGLFW();
    virtual bool InitRenderAPI(); // 创建并初始化 RenderAPI（含 ImGui 后端）

    // 用于初始化场景资源，例如加载模型、材质，着色器等
    virtual bool InitScene();

    // 固定步长逻辑更新：以固定频率执行（默认 120Hz），不受帧率波动影响。
    // 物理、确定性逻辑等应放在这里；普通每帧逻辑仍放 Update()。
    virtual void FixedUpdate();
    virtual void ProcessEvents();
    virtual void Update();
    virtual void RenderBefore();
    virtual void RenderClear();
    virtual void Render();
    virtual void RenderAfter();
    virtual void RenderImGuiBefore();
    virtual void RenderImGui();
    virtual void RenderImGuiAfter();

    virtual void OnKeyEvent(int key, int action);
    virtual void OnMouseButtonEvent(int button, int action, double xpos, double ypos);
    virtual void OnCursorPosEvent(double xpos, double ypos);
    virtual void OnScrollEvent(double xoffset, double yoffset, double xpos, double ypos);
    virtual void OnDropEvent(int count, const char **paths);

    GLFWwindow *window = nullptr;
    int width;
    int height;
    std::string title;
    bool running = false;

    // 固定步长（秒/次），默认 1/120s
    float fixedTimeStep = 1.0f / 120.0f;
    // 固定步长累计器（秒）：Run() 中累积 deltaTime 并按 fixedTimeStep 分批消费
    float fixedAccumulator = 0.0f;
    // 单帧允许累积的最大时间（秒），防止“死亡螺旋”导致 FixedUpdate 一次性追赶过多
    float maxAccumulatorTime = 0.25f;

    RenderAPI *renderAPI = nullptr;
    RenderAPIType requestedApiType = RenderAPIType::OpenGL;
    RenderAPIType resolvedApiType = RenderAPIType::OpenGL;

private:
    static void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow *window, double xpos, double ypos);
    static void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset);
    static void DropCallback(GLFWwindow *window, int count, const char **paths);
};

} // namespace aster
