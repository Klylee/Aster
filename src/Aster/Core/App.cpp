// 说明：App.cpp 不直接调用 OpenGL，也不依赖 GLEW。
// GL 相关实现（OpenGLRenderAPI / LightManager / 资源类）各自在自己的 .cpp 中
// 先于任何 OpenGL 头包含 glew.h，因此这里无需再关心头文件包含顺序。
#include "App.h"
#include <iostream>

#include "Input.h"
#include "Scene.h"
#include "Camera.h"
#include "Renderer.h"
#include "GlobalTime.h"
#include "MeshManager.h"
#include "SceneManager.h"
#include "EventDispatcher.h"

namespace aster
{

App::App(int width, int height, const std::string &title, RenderAPIType apiType)
    : width(width), height(height), title(title), requestedApiType(apiType)
{
}

App::~App()
{
    // 析构不自动 Destroy：与旧版一致，避免对 glfwTerminate 的重复调用
}

bool App::Init()
{
    // 解析最终使用的渲染后端（编译期宏 > 环境变量 ASTER_RENDER_API > 构造参数）
    resolvedApiType = ResolveRenderAPIType(requestedApiType);

    if (!InitGLFW())
        return false;
    if (!InitRenderAPI())
        return false;

    // 初始化场景与摄像机
    SceneManager::Instance().SetCurrentScene(std::make_shared<Scene>());
    auto camera = std::dynamic_pointer_cast<Camera>(SceneObject::create("Camera", "main camera"));
    camera->speed = 0.5f;
    camera->transform.SetPosition(Vec3(0.0f, 0.0f, 1.5f));
    SceneManager::Instance().SetMainCamera(camera);
    SceneManager::Instance().AddObject(camera);

    if (!InitScene())
        return false;

    GlobalTime::Init();
    Input::init(window);
    running = true;

    return true;
}

void App::Destroy()
{
    if (renderAPI)
    {
        renderAPI->Shutdown();
        delete renderAPI;
        renderAPI = nullptr;
    }
    RenderAPI::SetCurrent(nullptr);

    MeshManager::Instance().Clear();

    glfwTerminate();
}

bool App::InitGLFW()
{
    std::cout << "Starting GLFW, rendering API: "
              << (resolvedApiType == RenderAPIType::Vulkan ? "Vulkan" : "OpenGL") << std::endl;
    glfwInit();

    if (resolvedApiType == RenderAPIType::Vulkan)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    else
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (window == nullptr)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    // OpenGL 路径需要把新窗口的上下文设为当前（Vulkan 路径无上下文）
    if (resolvedApiType == RenderAPIType::OpenGL)
        glfwMakeContextCurrent(window);

    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetDropCallback(window, DropCallback);

    return true;
}

bool App::InitRenderAPI()
{
    // 创建渲染后端（Vulkan 未编译时回退到 OpenGL）
    renderAPI = CreateRenderAPI(resolvedApiType);
    if (!renderAPI)
    {
        std::cerr << "[App] Backend unavailable, falling back to OpenGL" << std::endl;
        resolvedApiType = RenderAPIType::OpenGL;
        renderAPI = CreateRenderAPI(RenderAPIType::OpenGL);
    }
    if (!renderAPI)
        return false;

    if (!renderAPI->Init(window))
    {
        std::cerr << "[App] Failed to initialize " << renderAPI->Name() << " backend" << std::endl;
        return false;
    }
    if (!renderAPI->ImGuiInit())
    {
        std::cerr << "[App] Failed to initialize ImGui for " << renderAPI->Name() << std::endl;
        return false;
    }

    // 注册为当前活动后端：供 Model::draw() 等框架对象按后端分发绘制
    RenderAPI::SetCurrent(renderAPI);

    return true;
}

bool App::InitScene()
{
    return true;
}

void App::Run()
{
    while (running && !glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        GlobalTime::UpdateLastFrameTime();
        GlobalTime::UpdateCurrentFrameTime();
        float deltaTime = GlobalTime::GetFrameDeltaTime();

        ProcessEvents();

        // —— 固定步长更新（FixedUpdate）——
        // 使用累计器（accumulator）模式：把本帧实际 deltaTime 累积，按 fixedTimeStep
        // 分批消费执行 FixedUpdate()，保证逻辑以恒定频率推进（帧率无关、确定、顺滑）。
        // 超过 maxAccumulatorTime 的累积量直接丢弃，避免掉帧后“死亡螺旋”式追赶。
        fixedAccumulator += deltaTime;
        if (fixedAccumulator > maxAccumulatorTime)
            fixedAccumulator = maxAccumulatorTime;
        while (fixedAccumulator >= fixedTimeStep)
        {
            FixedUpdate();
            fixedAccumulator -= fixedTimeStep;
        }

        Update();

        RenderBefore();
        RenderClear();
        Render();
        RenderAfter();

        RenderImGuiBefore();
        RenderImGui();
        RenderImGuiAfter();

        renderAPI->Present();
    }
}

void App::FixedUpdate()
{
    // 默认空实现：子类按需覆写。可结合 GetFixedAlpha() 在渲染侧做插值。
}

void App::ProcessEvents()
{
    for (auto &e : Event::events())
    {
        Event::EventDispatcher::Instance().Dispatch(e);
    }
    Event::events().clear();
}

void App::Update()
{
    SceneManager::Instance().Update();
}

void App::RenderClear()
{
    // 清屏颜色来自主摄像机背景色，实际清屏由后端完成
    auto backgroundColor = SceneManager::Instance().GetMainCamera()->backgroundColor;
    renderAPI->Clear(glm::vec4(backgroundColor, 1.0f));
}

void App::RenderBefore() {}

void App::Render()
{
    // 注：场景渲染（Shader / Renderer / MeshManager）目前仍是 OpenGL 实现，
    // 因此仅 OpenGL 后端会执行；Vulkan 后端由 VulkanRenderAPI::Present()
    // 内部的演示管线 + ImGui 完成绘制（后续里程碑将把场景渲染迁移到 Vulkan）。
    if (renderAPI && renderAPI->IsOpenGL())
    {
        SceneManager::Instance().GetCurrentScene()->lightManager.UploadToGPU();
        SceneManager::Instance().GetCurrentScene()->lightManager.BindToShader(0); // UBO binding point 0

        auto camera = SceneManager::Instance().GetMainCamera();

        // 环境贴图（HDR IBL）：绘制天空盒背景 + 绑定环境贴图纹理（OpenGL 后端）。
        // Vulkan 后端的天空盒在场景渲染器内部处理，此调用为 no-op。
        renderAPI->RenderEnvironment(
            camera->GetViewMatrix(),
            camera->GetProjectionMatrix((float)width / (float)height),
            width, height);

        SceneManager::Instance().Draw(); // 提交绘制

        Renderer::Instance().FlushBatches(
            camera->GetViewMatrix(),
            camera->GetProjectionMatrix((float)width / (float)height));
        MeshManager::Instance().CleanupUnusedMeshes();
    }
    else if (renderAPI && renderAPI->IsVulkan())
    {
        // Vulkan 后端：提交场景对象绘制（Model::draw() 分发到 VulkanSceneRenderer），
        // 并设置场景相机与灯光；实际的命令录制与渲染在 Present() 中完成。
        auto &lightMgr = SceneManager::Instance().GetCurrentScene()->lightManager;
        lightMgr.UpdateLightData(); // CPU 侧填充灯光数据
        renderAPI->SetSceneLights(lightMgr.uboData); // 上传到 Vulkan 灯光 UBO

        SceneManager::Instance().Draw();

        if (auto camera = SceneManager::Instance().GetMainCamera())
            renderAPI->SetSceneCamera(camera->GetViewMatrix(),
                                      camera->GetProjectionMatrix((float)width / (float)height));
    }
}

void App::RenderAfter() {}

void App::RenderImGuiBefore()
{
    renderAPI->ImGuiNewFrame();
}

void App::RenderImGui() {}

void App::RenderImGuiAfter()
{
    renderAPI->ImGuiRender();
}

void App::KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        Event::events().push_back(std::make_shared<Event::KeyPressedEvent>(key));
    }
    else if (action == GLFW_RELEASE)
    {
        Event::events().push_back(std::make_shared<Event::KeyReleasedEvent>(key));
    }
}

void App::CursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
    auto e = std::make_shared<Event::MouseMoveEvent>((float)xpos, (float)ypos);
    Event::events().push_back(e);
}

void App::MouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    Event::MouseButton btn = Event::MouseButton::Left;
    if (button == GLFW_MOUSE_BUTTON_LEFT)
        btn = Event::MouseButton::Left;
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
        btn = Event::MouseButton::Mid;
    else if (button == GLFW_MOUSE_BUTTON_RIGHT)
        btn = Event::MouseButton::Right;

    Event::MouseButtonEvent::Action act = (action == GLFW_PRESS) ? Event::MouseButtonEvent::Press : Event::MouseButtonEvent::Release;

    auto e = std::make_shared<Event::MouseButtonEvent>((float)xpos, (float)ypos, btn, act);
    Event::events().push_back(e);
}

void App::ScrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    auto e = std::make_shared<Event::MouseScrolledEvent>((float)xpos, (float)ypos, xoffset, yoffset);
    Event::events().push_back(e);
}

void App::DropCallback(GLFWwindow *window, int count, const char **paths)
{
    std::vector<std::string> pathVec;
    for (int i = 0; i < count; i++)
    {
        pathVec.push_back(paths[i]);
    }
    if (!pathVec.empty())
    {
        auto e = std::make_shared<Event::DropEvent>(pathVec);
        Event::events().push_back(e);
    }
}

void App::OnKeyEvent(int key, int action) {}
void App::OnMouseButtonEvent(int button, int action, double xpos, double ypos) {}
void App::OnCursorPosEvent(double xpos, double ypos) {}
void App::OnScrollEvent(double xoffset, double yoffset, double xpos, double ypos) {}
void App::OnDropEvent(int count, const char **paths) {}

} // namespace aster
