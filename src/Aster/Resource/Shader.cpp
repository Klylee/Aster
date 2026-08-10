#include <GL/glew.h>
#include "Shader.h"
#include <cassert>
#include <iostream>
#include <string>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace aster
{

static std::unordered_map<std::string, std::function<void(int, const std::any &)>> uniformSetters = {
    {"int", [](int loc, const std::any &value)
     {
         glUniform1i(loc, std::any_cast<int>(value));
     }},
    {"uint", [](int loc, const std::any &value)
     {
         glUniform1ui(loc, std::any_cast<unsigned int>(value));
     }},
    {"float", [](int loc, const std::any &value)
     {
         glUniform1f(loc, std::any_cast<float>(value));
     }},
    {"vec3f", [](int loc, const std::any &value)
     {
         const glm::vec3 &v = std::any_cast<glm::vec3>(value);
         glUniform3fv(loc, 1, glm::value_ptr(v));
     }},
    {"vec4f", [](int loc, const std::any &value)
     {
         const glm::vec4 &v = std::any_cast<glm::vec4>(value);
         glUniform4fv(loc, 1, glm::value_ptr(v));
     }},
    {"vec3i", [](int loc, const std::any &value)
     {
         const glm::ivec3 &v = std::any_cast<glm::ivec3>(value);
         glUniform3iv(loc, 1, glm::value_ptr(v));
     }},
    {"vec4i", [](int loc, const std::any &value)
     {
         const glm::ivec4 &v = std::any_cast<glm::ivec4>(value);
         glUniform4iv(loc, 1, glm::value_ptr(v));
     }},
    {"mat4", [](int loc, const std::any &value)
     {
         const glm::mat4 &m = std::any_cast<glm::mat4>(value);
         glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(m));
     }}};

ShaderSourceString Shader::PraseShaderSource(const std::string &file)
{
    std::ifstream source(file);
    if (!source.is_open())
    {
        std::cout << "the given file path maybe wrong" << std::endl;
        return {};
    }

    std::string vertex_source;
    std::string fragment_source;
    int type = -1; // specifies the shader mode
    std::string line;
    while (getline(source, line))
    {
        if (line.find("#shader") != std::string::npos) // need to prase
        {
            if (line.find("vertex") != std::string::npos)
                type = 0;
            else if (line.find("fragment") != std::string::npos)
                type = 1;
        }
        else
        {                  // start prase
            if (type == 0) // vertex mode
            {
                vertex_source += line;
                vertex_source += "\n";
            }
            else if (type == 1) // fragment mode
            {
                fragment_source += line;
                fragment_source += "\n";
            }
        }
    }
    return {vertex_source, fragment_source};
}

unsigned int Shader::CompileShader(unsigned int type, const std::string &shader_source)
{
    unsigned int shader;
    // before create shader, we need to check if the type given is right
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER)
    {
        std::cout << "give the wrong type of shader" << std::endl;
        return 0;
    }

    shader = glCreateShader(type);
    const char *c_src = shader_source.c_str();
    glShaderSource(shader, 1, &c_src, NULL);
    glCompileShader(shader);

    // check if compiling successfully
    int res;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &res);
    if (res == GL_FALSE)
    {
        int len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        char *info = new char[len];
        glGetShaderInfoLog(shader, len, &len, info);
        std::cout << info << std::endl;
        delete[] info;
    }
    return shader;
}

int Shader::getUniformLocation(const std::string &label)
{
    int loc = glGetUniformLocation(currentProgram, label.c_str());
    if (loc == -1)
    {
        std::cout << "get uniform location fail: \"" << label << "\"" << std::endl;
        return -1;
    }
    // std::cout << "\"" << label << "\" location: " << loc << std::endl;
    return loc;
}

Shader::Shader(const std::unordered_map<ShaderVariant, std::string> &shaderpaths)
{
    for (const auto &[variant, path] : shaderpaths)
    {
        ShaderSourceString ss = PraseShaderSource(path);

        int program = glCreateProgram();
        unsigned int vertex_shader = CompileShader(GL_VERTEX_SHADER, ss.vertex_source);
        unsigned int fragment_shader = CompileShader(GL_FRAGMENT_SHADER, ss.fragment_source);

        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glLinkProgram(program);     // link the two shader together
        glValidateProgram(program); // validate the program

        glDeleteShader(vertex_shader); // now we don't need the shader object
        glDeleteShader(fragment_shader);

        programs[variant] = program;
    }
    currentProgram = programs.at(ShaderVariant::Basic);
}

Shader::~Shader()
{
    Delete();
}

void Shader::Use(ShaderVariant variant)
{
    currentProgram = programs[variant];
    glUseProgram(currentProgram);
}

void Shader::Delete()
{
    for (auto var : programs)
        glDeleteProgram(var.second);
}

void Shader::SetUniform1f(const std::string &label, float v)
{
    int loc = getUniformLocation(label);
    if (loc >= 0)
        glUniform1f(loc, v);
}

void Shader::SetUniform1i(const std::string &label, int v)
{
    int loc = getUniformLocation(label);
    if (loc >= 0)
        glUniform1i(loc, v);
}

void Shader::SetUniformVec3f(const std::string &label, float v1, float v2, float v3)
{
    int loc = getUniformLocation(label);
    if (loc >= 0)
        glUniform3f(loc, v1, v2, v3);
}

void Shader::SetUniformVec3i(const std::string &label, int v1, int v2, int v3)
{
    int loc = getUniformLocation(label);
    if (loc >= 0)
        glUniform3i(loc, v1, v2, v3);
}

void Shader::SetUniformVec4f(const std::string &label, float v1, float v2, float v3, float v4)
{
    int loc = getUniformLocation(label);
    if (loc >= 0)
        glUniform4f(loc, v1, v2, v3, v4);
}

void Shader::SetUniformVec4i(const std::string &label, int v1, int v2, int v3, int v4)
{
    int loc = getUniformLocation(label);
    if (loc >= 0)
        glUniform4i(loc, v1, v2, v3, v4);
}

void Shader::SetUniformMat4x4f(const std::string &label, const glm::mat4 &mat)
{
    int loc = getUniformLocation(label);
    if (loc >= 0)
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::SetUniforms(const std::unordered_map<std::string, std::pair<std::string, std::any>> &uniforms)
{
    glUseProgram(currentProgram);
    for (const auto &[name, typeAndValue] : uniforms)
    {
        const auto &[type, value] = typeAndValue;
        auto loc = getUniformLocation(name);
        auto it = uniformSetters.find(type);
        if (it != uniformSetters.end())
        {
            it->second(loc, value);
        }
        else
        {
            std::cerr << "Error: unknown uniform type '" << type << "' for " << name << "\n";
        }
    }
}

} // namespace aster

