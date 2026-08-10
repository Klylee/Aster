#pragma once
#include <memory>
#include "Scene.h"

// SceneManager —— 全局场景注册表（Meyers 单例）
// ----------------------------------------------------------------------------
// 统一约定：
//   - 持有全局状态的管理器 → 使用 Instance() 单例（SceneManager / MeshManager /
//     EventDispatcher 一致）；
//   - 无状态的静态工具类 → 仍用静态方法（Input / GlobalTime）；
//   - 当前活动后端 → RenderAPI::Current() / SetCurrent()（运行期切换的注册表）。
// ----------------------------------------------------------------------------
namespace aster
{

class SceneManager
{
public:
    static SceneManager &Instance()
    {
        static SceneManager instance;
        return instance;
    }
    SceneManager(const SceneManager &) = delete;
    SceneManager &operator=(const SceneManager &) = delete;

    void SetCurrentScene(const std::shared_ptr<Scene> &scene)
    {
        currentScene = scene;
    }

    std::shared_ptr<Scene> GetCurrentScene()
    {
        return currentScene;
    }

    // 以下封装直接代理给 currentScene
    void AddObject(const std::shared_ptr<SceneObject> &obj)
    {
        if (currentScene)
            currentScene->AddSceneObject(obj);
    }

    template <typename T>
    std::shared_ptr<T> GetObject(const std::string &name)
    {
        if (!currentScene)
            return nullptr;
        return currentScene->GetObject<T>(name);
    }

    void Update()
    {
        if (currentScene)
            currentScene->UpdateAll();
    }

    void Draw()
    {
        if (currentScene)
            currentScene->DrawAll();
    }

    void Remove(const std::string &name)
    {
        if (currentScene)
            currentScene->Remove(name);
    }

    std::shared_ptr<Camera> GetMainCamera()
    {
        if (currentScene)
            return currentScene->GetMainCamera();
        return nullptr;
    }

    void SetMainCamera(const std::shared_ptr<Camera> &camera)
    {
        if (currentScene)
            currentScene->SetMainCamera(camera);
    }

private:
    SceneManager() = default;

    std::shared_ptr<Scene> currentScene = nullptr;
};

} // namespace aster

