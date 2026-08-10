#pragma once
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <utility>

namespace aster
{

class Input
{
public:
    static void init(GLFWwindow *win) { window = win; }

    static bool isKeyPressed(int key)
    {
        auto state = glfwGetKey(window, key);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }
    static bool isKeyReleased(int key)
    {
        auto state = glfwGetKey(window, key);
        return state == GLFW_RELEASE;
    }

    static bool isMouseButtonPressed(int button)
    {
        auto state = glfwGetMouseButton(window, button);
        return state == GLFW_PRESS;
    }

    static std::pair<double, double> getMousePosition()
    {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        return {xpos, ypos};
    }

    static bool isMouseCapturedByImGui()
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

private:
    static inline GLFWwindow *window = nullptr; // C++17 inline：纯头文件类也能有唯一定义
};

} // namespace aster

