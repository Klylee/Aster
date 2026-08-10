#pragma once

#include <fstream>
#include <unordered_map>
#include <any>
#include <glm/glm.hpp>

namespace aster
{

struct ShaderSourceString
{
    std::string vertex_source;
    std::string fragment_source;
};

enum class ShaderVariant
{
    Basic,
    Instanced,
    Shadow
};

class Shader
{
    std::unordered_map<ShaderVariant, unsigned int> programs;
    unsigned int currentProgram = 0;

    ShaderSourceString PraseShaderSource(const std::string &file);
    unsigned int CompileShader(unsigned int type, const std::string &shader_source);

    int getUniformLocation(const std::string &label);

public:
    Shader(const std::unordered_map<ShaderVariant, std::string> &shaderpaths);
    ~Shader();

    void Use(ShaderVariant variant = ShaderVariant::Basic);
    void Delete();

    void SetUniform1f(const std::string &label, float v);
    void SetUniform1i(const std::string &label, int v);
    void SetUniformVec3f(const std::string &label, float v1, float v2, float v3);
    void SetUniformVec3i(const std::string &label, int v1, int v2, int v3);
    void SetUniformVec4f(const std::string &label, float v1, float v2, float v3, float v4);
    void SetUniformVec4i(const std::string &label, int v1, int v2, int v3, int v4);
    void SetUniformMat4x4f(const std::string &label, const glm::mat4 &mat);

    void SetUniforms(const std::unordered_map<std::string, std::pair<std::string, std::any>> &uniforms);
};

} // namespace aster

