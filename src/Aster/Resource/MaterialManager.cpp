#include "MaterialManager.h"

#include <iostream>

namespace aster
{

MaterialManager &MaterialManager::Instance()
{
    static MaterialManager instance;
    return instance;
}

// ---- Shader 注册 / 复用 ----
std::shared_ptr<Shader> MaterialManager::RegisterShader(const std::string &name,
                                                        const std::string &shaderPath)
{
    auto it = shaders.find(name);
    if (it != shaders.end())
        return it->second; // 已注册：复用缓存实例

    auto shader = std::make_shared<Shader>(
        std::unordered_map<ShaderVariant, std::string>{{ShaderVariant::Basic, shaderPath}});
    shaders[name] = shader;
    return shader;
}

std::shared_ptr<Shader> MaterialManager::GetShader(const std::string &name)
{
    auto it = shaders.find(name);
    return it != shaders.end() ? it->second : nullptr;
}

bool MaterialManager::HasShader(const std::string &name) const
{
    return shaders.count(name) > 0;
}

// ---- 基础材质注册 / 复用 ----
std::shared_ptr<Material> MaterialManager::RegisterMaterial(const std::string &name,
                                                            const std::string &shaderName,
                                                            const glm::vec4 &color)
{
    auto it = materials.find(name);
    if (it != materials.end())
        return it->second; // 同名已注册：复用缓存实例

    std::shared_ptr<Shader> shader = GetShader(shaderName);
    if (!shader)
        std::cerr << "[MaterialManager] RegisterMaterial('" << name
                  << "') : shader '" << shaderName
                  << "' not registered (fallback to shader-less material)" << std::endl;

    auto mat = std::make_shared<Material>(shader);
    mat->name = name;
    mat->color = color;
    mat->SetUniform("color", "vec4f", color); // OpenGL 的 color uniform
    materials[name] = mat;
    return mat;
}

std::shared_ptr<Material> MaterialManager::RegisterMaterial(const std::string &name,
                                                            const glm::vec4 &color)
{
    auto it = materials.find(name);
    if (it != materials.end())
        return it->second; // 同名已注册：复用缓存实例

    auto mat = std::make_shared<Material>(); // 无 shader（Vulkan 程序化材质）
    mat->name = name;
    mat->color = color;
    materials[name] = mat;
    return mat;
}

std::shared_ptr<Material> MaterialManager::GetMaterial(const std::string &name)
{
    auto it = materials.find(name);
    return it != materials.end() ? it->second : nullptr;
}

bool MaterialManager::HasMaterial(const std::string &name) const
{
    return materials.count(name) > 0;
}

// ---- 材质实例（共享 shader、参数独立） ----
std::shared_ptr<Material> MaterialManager::CreateMaterialInstance(
    const std::string &instanceName, const std::string &baseName)
{
    auto it = materials.find(baseName);
    if (it == materials.end())
    {
        std::cerr << "[MaterialManager] CreateMaterialInstance('" << instanceName
                  << "') : base material '" << baseName << "' not found" << std::endl;
        return nullptr;
    }

    // 复制 base 创建实例：shader 共享（同一 shared_ptr），
    // 参数 / OpenGL uniforms 深拷贝后完全独立。
    auto inst = std::make_shared<Material>(*it->second);
    inst->name = instanceName;
    inst->baseName = baseName; // 记录基础材质名（Material::baseName）
    materials[instanceName] = inst;
    return inst;
}

// ---- 清空 ----
void MaterialManager::Clear()
{
    materials.clear();
    shaders.clear();
}

} // namespace aster
