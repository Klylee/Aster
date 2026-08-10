#include "OpenGLRenderAPI.h"
#include "OpenGLEnvironment.h"

#include <iostream>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace aster
{

OpenGLRenderAPI::~OpenGLRenderAPI()
{
    Shutdown();
}

bool OpenGLRenderAPI::Init(GLFWwindow *window)
{
    this->window = window;

    // GLEW 需要当前线程已绑定 GL 上下文（glfwCreateWindow 会自动 MakeCurrent）
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "[OpenGL] Failed to initialize GLEW" << std::endl;
        return false;
    }

    std::cout << "[OpenGL] Vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "[OpenGL] Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "[OpenGL] Version: " << glGetString(GL_VERSION) << std::endl;

    return true;
}

void OpenGLRenderAPI::Shutdown()
{
    if (!window)
        return;

    if (environment_)
    {
        environment_->Shutdown();
        delete environment_;
        environment_ = nullptr;
    }

    // 仅在 ImGui 上下文确实创建后才关闭（Init 可能在中途失败）
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    window = nullptr;
}

// ---- 环境贴图（HDR IBL） ----

void OpenGLRenderAPI::SetEnvironmentMap(const EnvironmentMap &env)
{
    if (!environment_)
        environment_ = new OpenGLEnvironment();
    environment_->Upload(env);
}

void OpenGLRenderAPI::SetEnvMode(int mode)
{
    if (environment_)
        environment_->SetMode(mode);
}

void OpenGLRenderAPI::SetEnvParams(float intensity, float roughness, float metallic,
                                   float ao, float yaw, float exposure, bool toneMap)
{
    if (environment_)
        environment_->SetParams(intensity, roughness, metallic, ao, yaw, exposure, toneMap);
}

void OpenGLRenderAPI::RenderEnvironment(const glm::mat4 &view, const glm::mat4 &proj,
                                        int width, int height)
{
    if (!environment_ || !environment_->IsReady())
        return;
    // 天空盒背景 + 绑定环境贴图（纹理单元 6-9，供材质 env_ibl.shader 采样）
    environment_->DrawSkybox(view, proj, width, height);
    environment_->BindSceneTextures();
}

void OpenGLRenderAPI::Clear(const glm::vec4 &color)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0f);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderAPI::Present()
{
    glfwSwapBuffers(window);
}

bool OpenGLRenderAPI::ImGuiInit()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontDefault()->Scale = 1.6f;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
    return true;
}

void OpenGLRenderAPI::ImGuiNewFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void OpenGLRenderAPI::ImGuiRender()
{
    // 注意：不要在这里调用 ImGui::End()！用户代码已配平 Begin/End，
    // 隐式回退窗口由 imgui 的 Render()/EndFrame() 内部自动关闭。
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void OpenGLRenderAPI::OnResize(int /*framebufferWidth*/, int /*framebufferHeight*/)
{
    // OpenGL 自动适配视口，无需手动处理
}

} // namespace aster

