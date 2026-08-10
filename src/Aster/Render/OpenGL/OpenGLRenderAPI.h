#pragma once

#include "RenderAPI.h"
#include <GL/glew.h>

// ============================================================================
// OpenGLRenderAPI —— OpenGL 后端
// ----------------------------------------------------------------------------
// 负责 OpenGL 上下文初始化（GLEW）、ImGui 的 OpenGL3 后端、清屏与交换缓冲。
// 该后端与旧版 App 行为完全一致：场景渲染仍由 Aster 框架中的
// Shader / Renderer / MeshManager 等 OpenGL 实现完成。
// 环境贴图（HDR IBL）由 OpenGLEnvironment 提供：天空盒 + 纹理绑定。
// ============================================================================

namespace aster
{

class OpenGLEnvironment;

class OpenGLRenderAPI : public RenderAPI
{
public:
    OpenGLRenderAPI() = default;
    ~OpenGLRenderAPI() override;

    bool Init(GLFWwindow *window) override;
    void Shutdown() override;

    void Clear(const glm::vec4 &color) override;
    void Present() override;

    bool ImGuiInit() override;
    void ImGuiNewFrame() override;
    void ImGuiRender() override;

    void OnResize(int framebufferWidth, int framebufferHeight) override;

    // ---- 环境贴图（HDR IBL）----
    void SetEnvironmentMap(const EnvironmentMap &env) override;
    void SetEnvMode(int mode) override;
    void SetEnvParams(float intensity, float roughness, float metallic,
                      float ao, float yaw, float exposure, bool toneMap) override;
    // 每帧场景绘制前调用：绘制天空盒 + 绑定环境贴图（纹理单元 6-9）
    void RenderEnvironment(const glm::mat4 &view, const glm::mat4 &proj,
                           int width, int height) override;

    RenderAPIType Type() const override { return RenderAPIType::OpenGL; }
    const char *Name() const override { return "OpenGL"; }

private:
    GLFWwindow *window = nullptr;
    OpenGLEnvironment *environment_ = nullptr; // 环境贴图（天空盒 + IBL）
};

} // namespace aster

