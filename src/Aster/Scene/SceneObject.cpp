// SceneObject —— 场景对象基类实现
// 声明见 SceneObject.h；非模板实现下沉到本文件，减小头文件耦合。
#include "SceneObject.h"

#include <algorithm>

namespace aster
{

SceneObject::SceneObject() : transform()
{
    transform.owner = this;
}

SceneObject::~SceneObject() = default;

void SceneObject::SetActive(bool isActive)
{
    active = isActive;
    for (auto &child : children)
    {
        child->SetActive(isActive);
    }
}

void SceneObject::AddChild(const std::shared_ptr<SceneObject> &child)
{
    children.push_back(child);
}

SceneObject::operator std::string() const
{
    return "<" + className + ">" + objName + "</" + className + ">";
}

std::unordered_map<std::string, std::function<std::shared_ptr<SceneObject>()>> &SceneObject::registry()
{
    static std::unordered_map<std::string, std::function<std::shared_ptr<SceneObject>()>> impl;
    return impl;
}

std::shared_ptr<SceneObject> SceneObject::create(const std::string &_className, const std::string &_objName)
{
    auto it = registry().find(_className);
    if (it != registry().end())
    {
        auto obj = it->second();
        obj->className = _className;
        obj->objName = _objName;
        return obj;
    }
    return nullptr;
}

void SceneObject::link(const std::shared_ptr<SceneObject> &parent, const std::shared_ptr<SceneObject> &child)
{
    if (child->parent)
    {
        // remove from old parent
        auto &siblings = child->parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }
    child->parent = parent;
    child->transform.MarkWorldDirty();
    if (parent && child)
        parent->AddChild(child);
}

} // namespace aster

