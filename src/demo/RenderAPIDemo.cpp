#include "RenderAPIDemo.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "Aster/Render/RenderAPI.h"
#include <imgui/imgui.h>

using namespace aster;

namespace
{

GLFWwindow *CreateWindow(RenderAPIType api, int width, int height)
{
    glfwInit();

    if (api == RenderAPIType::Vulkan)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    else
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    return glfwCreateWindow(width, height, "Aster RenderAPI Demo", nullptr, nullptr);
}

} // namespace

int RunRenderAPIDemo()
{
    RenderAPIType api = ResolveRenderAPIType(RenderAPIType::OpenGL);
    std::cout << "[Demo] Requested backend: "
              << (api == RenderAPIType::Vulkan ? "Vulkan" : "OpenGL") << std::endl;

    GLFWwindow *window = CreateWindow(api, 1200, 800);
    if (!window)
    {
        std::cerr << "[Demo] Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }

    RenderAPI *renderAPI = CreateRenderAPI(api);
    if (!renderAPI)
    {
        std::cerr << "[Demo] Requested backend unavailable, falling back to OpenGL" << std::endl;
        api = RenderAPIType::OpenGL;
        renderAPI = CreateRenderAPI(api);
    }

    if (!renderAPI || !renderAPI->Init(window))
    {
        std::cerr << "[Demo] Failed to init render backend" << std::endl;
        glfwTerminate();
        return -1;
    }
    if (!renderAPI->ImGuiInit())
    {
        std::cerr << "[Demo] Failed to init ImGui backend" << std::endl;
        return -1;
    }

    std::cout << "[Demo] Active backend: " << renderAPI->Name() << std::endl;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // 清屏 + ImGui 叠层 + Present 完全通过 RenderAPI 抽象驱动
        renderAPI->Clear(glm::vec4(0.08f, 0.09f, 0.12f, 1.0f));

        renderAPI->ImGuiNewFrame();
        ImGui::ShowDemoWindow();
        ImGui::Begin("Renderer");
        ImGui::Text("Active backend: %s", renderAPI->Name());
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::TextWrapped(
            "This demo drives the same RenderAPI abstraction with either the "
            "OpenGL or the Vulkan backend. Set ASTER_RENDER_API=vulkan to use "
            "the Vulkan backend (a demo triangle is drawn by the Vulkan pipeline).");
        ImGui::End();
        renderAPI->ImGuiRender();

        renderAPI->Present();
    }

    renderAPI->Shutdown();
    delete renderAPI;
    glfwTerminate();
    return 0;
}
