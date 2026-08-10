#pragma once

#include <chrono>

// GlobalTime —— 纯静态时间工具（无状态单例，head-only）
namespace aster
{

class GlobalTime
{
public:
    // 在循环开始前调用一次
    static void Init()
    {
        startTime = std::chrono::high_resolution_clock::now();
        lastFrameTime = startTime;
        currentFrameTime = startTime;
    }

    static float GetTime()
    {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float>(now - startTime).count();
    }

    static float GetFrameDeltaTime()
    {
        return std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
    }

    // 在每帧开始时调用
    static void UpdateLastFrameTime()
    {
        lastFrameTime = currentFrameTime;
    }
    // 在每帧开始时调用
    static void UpdateCurrentFrameTime()
    {
        currentFrameTime = std::chrono::high_resolution_clock::now();
    }

private:
    inline static std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
    inline static std::chrono::high_resolution_clock::time_point lastFrameTime = std::chrono::high_resolution_clock::now();
    inline static std::chrono::high_resolution_clock::time_point currentFrameTime = std::chrono::high_resolution_clock::now();
};

} // namespace aster

