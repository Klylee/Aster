// Scene —— 场景容器实现
// 声明见 Scene.h；非模板实现下沉到本文件（GetObject<T> 为模板，留在头文件）。
#include "Scene.h"

#include <algorithm>
#include <iostream>

namespace aster
{

void Scene::AddSceneObject(const std::shared_ptr<SceneObject> &obj)
{
    if (!obj)
        return;

    if (sceneObjectMap.find(obj->objName) != sceneObjectMap.end())
        return;

    sceneObjects.push_back(obj);
    sceneObjectMap[obj->objName] = obj;

    auto lightPtr = std::dynamic_pointer_cast<Light>(obj);
    if (lightPtr)
    {
        lightManager.AddLight(lightPtr);
    }
}

void Scene::Remove(const std::string &name)
{
    auto it = sceneObjectMap.find(name);
    if (it == sceneObjectMap.end())
        return;
    auto target = it->second.lock();
    sceneObjectMap.erase(it); // 无论对象是否仍存活，都移除名字索引
    if (target)
        Remove(target);
}

void Scene::Remove(const std::shared_ptr<SceneObject> &target)
{
    if (!target)
        return;
    // 先删子对象
    for (auto &child : target->children)
    {
        Remove(child->objName);
    }
    // 从对象列表移除（引用计数归零即销毁，Mesh / VulkanMeshBuffer 随之释放）
    sceneObjects.erase(
        std::remove_if(sceneObjects.begin(), sceneObjects.end(),
                       [&](auto &o)
                       { return o == target; }),
        sceneObjects.end());
    // 从名字索引移除（若名字已被改则忽略遗留项）
    auto it = sceneObjectMap.find(target->objName);
    if (it != sceneObjectMap.end() && it->second.lock() == target)
        sceneObjectMap.erase(it);
    // 灯光
    auto lightPtr = std::dynamic_pointer_cast<Light>(target);
    if (lightPtr)
    {
        lightManager.RemoveLight(lightPtr);
    }
    std::cout << "Removed " << "<" << target->className << ">" << target->objName << std::endl;
}

void Scene::UpdateAll()
{
    for (auto &obj : sceneObjects)
    {
        if (obj && obj->active)
            obj->update();
    }
}

void Scene::DrawAll()
{
    for (auto &obj : sceneObjects)
    {
        if (obj && obj->active)
            obj->draw();
    }
}

const std::vector<std::shared_ptr<SceneObject>> &Scene::GetObjects() const
{
    return sceneObjects;
}

void Scene::SetMainCamera(const std::shared_ptr<Camera> &camera)
{
    mainCamera = camera;
}

std::shared_ptr<Camera> Scene::GetMainCamera() const
{
    return mainCamera;
}

} // namespace aster

