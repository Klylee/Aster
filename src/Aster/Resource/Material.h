#pragma once
#include <unordered_map>
#include <string>
#include <any>
#include <memory>
#include <glm/glm.hpp>

#include "Shader.h"

namespace aster
{

// 无 GL 依赖的渲染状态（数值与 OpenGL 枚举一致，避免头文件引入 GLEW）
struct RenderState
{
    bool depthTest = true;
    bool depthWrite = true;
    bool blend = false;
    bool cullFace = false;
    unsigned int depthFunc = 0x0203; // GL_LEQUAL
    unsigned int blendSrc = 0x0302;  // GL_SRC_ALPHA
    unsigned int blendDst = 0x0303;  // GL_ONE_MINUS_SRC_ALPHA
};

enum RenderQueue
{
    Background = 1000,
    Geometry = 2000,
    AlphaTest = 2450,
    Transparent = 3000,
    Overlay = 4000
};

class Material
{
    std::shared_ptr<Shader> shader;
    std::unordered_map<std::string, std::pair<std::string, std::any>> uniforms;

public:
    RenderState renderState;
    unsigned int renderQueue = RenderQueue::Geometry;

    // 材质元数据（MaterialManager 使用，调试 / 资源管理）：
    //   name     注册名（管理器中的唯一标识）
    //   baseName 若是“材质实例”，记录其基础材质名（共享 shader、参数独立）
    std::string name;      // 注册名
    std::string baseName;  // 实例的基础材质名（空 = 基础材质）

    // 单色材质颜色：Vulkan 后端使用（对应 mesh.frag 的 push constant color），
    // OpenGL 后端配合 shader 的 _color uniform 使用。
    glm::vec4 color = glm::vec4(1.0f);

    // ---- 每对象材质参数（Vulkan 后端经 push constant 传入 mesh shader） ----
    float roughness = 0.35f; // 粗糙度（高光 IBL / 高光分布）
    float metallic = 0.0f;   // 金属度
    float ao = 1.0f;         // 环境光遮蔽
    int textureIndex = -1;   // M2：材质纹理索引（-1=无纹理）
    int vulkanPipeline = 0;  // M3：自定义 shader 管线索引（0=默认 mesh）

    // 允许不带 shader 构造（Vulkan 场景演示 / 程序化材质），
    // OpenGL 渲染路径仍需 shader。
    Material() = default;

    Material(std::shared_ptr<Shader> shader) : shader(std::move(shader)) {}

    void SetUniform(const std::string &name, const std::string &type, const std::any &value)
    {
        uniforms[name] = {type, value};
    }
    std::shared_ptr<Shader> GetShader() const { return shader; }

    void ApplyUniforms();
    void ApplyRenderState();
};

} // namespace aster

