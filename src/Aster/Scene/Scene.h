#pragma once
#include <type_traits>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>

#include "Camera.h"
#include "SceneObject.h"
#include "LightManager.h"

namespace aster
{

class Scene
{
public:
    LightManager lightManager;

    // 添加对象（注册进场景）；实现见 Scene.cpp
    void AddSceneObject(const std::shared_ptr<SceneObject> &obj);

    // 通过名字查找对象（模板实现，保留在头文件）
    template <typename T>
    std::shared_ptr<T> GetObject(const std::string &name)
    {
        static_assert(std::is_base_of_v<SceneObject, T>, "T must derive from SceneObject");

        auto it = sceneObjectMap.find(name);
        if (it == sceneObjectMap.end())
            return nullptr;

        auto basePtr = it->second.lock();
        if (!basePtr)
            return nullptr;

        return std::dynamic_pointer_cast<T>(basePtr);
    }

    // 移除对象和它的子对象；实现见 Scene.cpp
    void Remove(const std::string &name);

    // 调用所有对象的更新与绘制；实现见 Scene.cpp
    void UpdateAll();
    void DrawAll();

    // 获取所有对象
    const std::vector<std::shared_ptr<SceneObject>> &GetObjects() const;

    // 主相机
    void SetMainCamera(const std::shared_ptr<Camera> &camera);
    std::shared_ptr<Camera> GetMainCamera() const;

private:
    std::vector<std::shared_ptr<SceneObject>> sceneObjects;
    std::unordered_map<std::string, std::weak_ptr<SceneObject>> sceneObjectMap;
    std::shared_ptr<Camera> mainCamera;
};

} // namespace aster

