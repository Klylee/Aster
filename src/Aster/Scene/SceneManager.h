#pragma once
#include <memory>
#include <vector>
#include "Scene.h"
#include "GlobalTime.h" // 延迟销毁倒计时用

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
        ProcessPendingDeletes(); // 延迟销毁：在所有对象 update 之后、渲染之前处理
    }

    // ---- Unity 风格销毁：Destroy(obj, delay) / Destroy(name, delay) ----
    // delay <= 0 立即移除；delay > 0 时对象在 delay 秒内仍正常 update/draw，
    // 到期后从场景移除（引用计数归零即销毁，GPU 资源随之释放）。
    // 队列持 weak_ptr：对象被提前移除/销毁时自动跳过，不会悬垂。
    // Unity 风格销毁：delay 秒后（或下一帧 Update，delay<=0）从场景移除。
    // 一律进延迟队列、在下一帧 Update（渲染提交之前）统一处理：
    //   - 在任何阶段调用（Update / ImGui / 事件回调）都安全；
    //   - 绝不会在本帧 draw 提交（drawCalls 持有网格原始指针）之后销毁对象，
    //     避免 Vulkan 后端 Present 录制时使用悬垂的 VulkanMeshBuffer*。
    void Destroy(const std::shared_ptr<SceneObject> &obj, float delay = 0.0f)
    {
        if (!obj || !currentScene)
            return;
        pendingDeletes.push_back({obj, delay > 0.0f ? delay : 0.0f});
    }

    void Destroy(const std::string &name, float delay = 0.0f)
    {
        if (!currentScene)
            return;
        if (auto obj = currentScene->GetObject<SceneObject>(name))
            Destroy(obj, delay);
    }

    // 是否有待销毁对象（调试用）
    size_t PendingDeleteCount() const { return pendingDeletes.size(); }

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

    // Unity 风格延迟销毁队列（一律延迟到下一帧 Update 处理，保证渲染提交前移除）
    struct PendingDelete
    {
        std::weak_ptr<SceneObject> target; // 弱引用：对象被提前移除时自动跳过
        float remaining;                   // 剩余秒数
    };
    std::vector<PendingDelete> pendingDeletes;

    void ProcessPendingDeletes()
    {
        float dt = GlobalTime::GetFrameDeltaTime();
        for (auto it = pendingDeletes.begin(); it != pendingDeletes.end();)
        {
            it->remaining -= dt;
            if (it->remaining <= 0.0f)
            {
                if (currentScene)
                {
                    if (auto target = it->target.lock())
                        currentScene->Remove(target);
                }
                it = pendingDeletes.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::shared_ptr<Scene> currentScene = nullptr;
};

} // namespace aster

