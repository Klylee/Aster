#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Material.h"
#include "Shader.h"

namespace aster
{

// ============================================================================
// 全局材质管理器（Meyer 单例，与 SceneManager / MeshManager 一致）
// ----------------------------------------------------------------------------
// 目标：Material 复用 + “同一 shader、不同参数”：
//
//   1. Shader 按名注册复用 —— 同一 name 只持有 1 份 shared_ptr<Shader>，
//      所有使用它的材质共享它（OpenGL 后端避免每材质重复编译/加载）。
//   2. 基础材质按名注册复用 —— RegisterMaterial(name, ...) 缓存，
//      GetMaterial(name) 返回同一实例；多个物体引用同一材质对象时真正共享。
//   3. 材质实例（CreateMaterialInstance）—— 以基础材质为模板创建“实例”：
//      shader 与 base 共享（同一 shared_ptr<Shader>），参数（颜色/粗糙度/
//      金属度/AO/纹理/管线/OpenGL uniform）复制 base 当前值后**独立可改**。
//      这正是“物体 A 和 B 用同一 shader，只是颜色/粗糙度不同”的用法。
//
// 注意：实例是“快照”——创建后改 base 不影响已创建的实例；改实例不影响 base。
// ============================================================================
class MaterialManager
{
public:
    static MaterialManager &Instance();

    // ---- Shader 注册 / 复用 ----
    // 注册（或取回已注册）一个 shader。同一 name 只创建一份，后续调用返回缓存。
    // shaderPath 传给 Shader 的 Basic 变体（如 .shader 文件路径）。
    std::shared_ptr<Shader> RegisterShader(const std::string &name,
                                           const std::string &shaderPath);
    std::shared_ptr<Shader> GetShader(const std::string &name);
    bool HasShader(const std::string &name) const;

    // ---- 基础材质注册 / 复用 ----
    // 以已注册的 shaderName 创建并缓存基础材质（OpenGL：自动附 color uniform；
    // shaderName 未注册时退化为无 shader 材质，供 Vulkan 程序化材质使用）。
    // 同名已注册则直接返回缓存实例（幂等）。
    std::shared_ptr<Material> RegisterMaterial(const std::string &name,
                                               const std::string &shaderName,
                                               const glm::vec4 &color = glm::vec4(1.0f));
    // Vulkan 程序化材质（无 shader）：以参数创建并缓存。
    std::shared_ptr<Material> RegisterMaterial(const std::string &name,
                                               const glm::vec4 &color = glm::vec4(1.0f));
    // 取回已注册材质；未注册返回 nullptr。
    std::shared_ptr<Material> GetMaterial(const std::string &name);
    bool HasMaterial(const std::string &name) const;

    // ---- 材质实例（共享 shader、参数独立） ----
    // 以 baseName 基础材质为模板创建实例：shader 共享、参数/uniforms 复制后独立。
    // instanceName 缓存（幂等）。baseName 不存在返回 nullptr。
    std::shared_ptr<Material> CreateMaterialInstance(const std::string &instanceName,
                                                     const std::string &baseName);

    // 遍历（调试 / ImGui 展示）
    const std::unordered_map<std::string, std::shared_ptr<Shader>> &GetShaders() const
    {
        return shaders;
    }
    const std::unordered_map<std::string, std::shared_ptr<Material>> &GetMaterials() const
    {
        return materials;
    }
    size_t ShaderCount() const { return shaders.size(); }
    size_t MaterialCount() const { return materials.size(); }

    // 清空全部注册（后端切换 / 测试）
    void Clear();

private:
    MaterialManager() = default;
    ~MaterialManager() = default;
    MaterialManager(const MaterialManager &) = delete;
    MaterialManager &operator=(const MaterialManager &) = delete;

    std::unordered_map<std::string, std::shared_ptr<Shader>> shaders;
    std::unordered_map<std::string, std::shared_ptr<Material>> materials;
};

} // namespace aster
