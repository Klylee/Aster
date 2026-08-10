#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include "Transform.h"

#define REGISTER_SCENE_OBJECT(Derived)                                                        \
    struct Derived##Factory                                                                   \
    {                                                                                         \
        Derived##Factory()                                                                    \
        {                                                                                     \
            SceneObject::registry()[#Derived] = []() { return std::make_shared<Derived>(); }; \
        }                                                                                     \
    };                                                                                        \
    static inline Derived##Factory global_##Derived##Factory;

namespace aster
{

struct SceneObject
{
    REGISTER_SCENE_OBJECT(SceneObject)

    std::string className;
    std::string objName;
    Transform transform;
    bool active = true;
    std::shared_ptr<SceneObject> parent = nullptr;
    std::vector<std::shared_ptr<SceneObject>> children;

    SceneObject();
    virtual ~SceneObject();

    virtual void awake() {}
    virtual void update() {}
    virtual void draw() {}
    virtual void SetActive(bool isActive);

    void AddChild(const std::shared_ptr<SceneObject> &child);

    operator std::string() const;

    // 场景对象工厂注册表（name -> factory），由 REGISTER_SCENE_OBJECT 宏填充
    static std::unordered_map<std::string, std::function<std::shared_ptr<SceneObject>()>> &registry();

    static std::shared_ptr<SceneObject> create(const std::string &_className, const std::string &_objName);

    static void link(const std::shared_ptr<SceneObject> &parent, const std::shared_ptr<SceneObject> &child);
};

} // namespace aster

