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
    if (it != sceneObjectMap.end())
    {
        auto target = it->second.lock();
        if (target)
        {
            for (auto &child : target->children)
            {
                Remove(child->objName);
            }
            sceneObjects.erase(
                std::remove_if(sceneObjects.begin(), sceneObjects.end(),
                               [&](auto &o)
                               { return o == target; }),
                sceneObjects.end());

            auto lightPtr = std::dynamic_pointer_cast<Light>(target);
            if (lightPtr)
            {
                lightManager.RemoveLight(lightPtr);
            }
        }
        sceneObjectMap.erase(it);
        std::cout << "Removed " << "<" << target->className << ">" << name << std::endl;
    }
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

