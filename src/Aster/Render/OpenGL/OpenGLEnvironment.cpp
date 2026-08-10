// OpenGL 后端的 HDR 环境贴图实现（仅 Windows / 非 Apple 编译）。
// 注意：本文件包含 GL 调用，macOS 上不参与构建（OpenGL 后端仅 Windows）。

#include "OpenGLEnvironment.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>

namespace aster
{

namespace
{
// 编译 GLSL 着色器（无 .shader 解析，用于内嵌天空盒源码）
unsigned int CompileGLShader(unsigned int type, const char *src)
{
    unsigned int sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    int ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        int len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 0 ? len : 1, '\0');
        glGetShaderInfoLog(sh, len, &len, log.data());
        std::cerr << "[OpenGLEnvironment] Shader compile error:\n" << log << std::endl;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// 创建 GL 立方体纹理。levels[m] = 6 面连续 RGBA32F，mip0 分辨率 baseSize（逐层减半）。
// 每个面一行 RGBA32F（面内行优先，row0 在顶部 —— 与 CPU 数据一致）。
unsigned int CreateCubeTexture(const std::vector<std::vector<float>> &levels,
                               int baseSize, int mipmap)
{
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

    int levelsCount = (int)levels.size();
    for (int m = 0; m < levelsCount; m++)
    {
        int s = baseSize >> m;
        const std::vector<float> &level = levels[m];
        for (int f = 0; f < 6; f++)
        {
            const float *face = &level[(size_t)f * s * s * 4];
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m, GL_RGBA32F,
                         s, s, 0, GL_RGBA, GL_FLOAT, face);
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    if (mipmap)
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levelsCount - 1);

    return tex;
}
} // namespace

OpenGLEnvironment::~OpenGLEnvironment()
{
    Shutdown();
}

void OpenGLEnvironment::SetParams(float intensity, float roughness, float metallic,
                                  float ao, float yaw, float exposure, bool toneMap)
{
    intensity_ = intensity;
    roughness_ = roughness;
    metallic_ = metallic;
    ao_ = ao;
    yaw_ = yaw;
    exposure_ = exposure;
    toneMap_ = toneMap;
}

bool OpenGLEnvironment::Upload(const EnvironmentMap &env)
{
    if (!env.IsValid())
        return false;

    // 环境 cubemap（单 mip，线性过滤）
    envCubeTex_ = CreateCubeTexture({env.EnvCube()}, env.EnvCubeSize(), 0);
    // 漫反射 irradiance cubemap（单 mip）
    irradianceTex_ = CreateCubeTexture({env.Irradiance()}, env.IrradianceSize(), 0);
    // 高光预过滤 cubemap（mip 链）
    prefilteredMips_ = env.PrefilteredMips();
    prefilteredTex_ = CreateCubeTexture(env.Prefiltered(), env.PrefilteredBaseSize(), 1);

    // BRDF LUT（2D，RG32F）
    glGenTextures(1, &brdfLutTex_);
    glBindTexture(GL_TEXTURE_2D, brdfLutTex_);
    int lutSize = env.BRDFLutSize();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, lutSize, lutSize, 0, GL_RG, GL_FLOAT,
                 env.BRDFLUT().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    CreateSkyboxProgram();
    ready_ = true;
    std::cout << "[OpenGLEnvironment] Environment map uploaded: cube "
              << env.EnvCubeSize() << ", irradiance " << env.IrradianceSize()
              << ", prefiltered " << env.PrefilteredBaseSize() << " mips="
              << prefilteredMips_ << ", BRDF LUT " << lutSize << std::endl;
    return true;
}

void OpenGLEnvironment::Shutdown()
{
    if (envCubeTex_) { glDeleteTextures(1, &envCubeTex_); envCubeTex_ = 0; }
    if (irradianceTex_) { glDeleteTextures(1, &irradianceTex_); irradianceTex_ = 0; }
    if (prefilteredTex_) { glDeleteTextures(1, &prefilteredTex_); prefilteredTex_ = 0; }
    if (brdfLutTex_) { glDeleteTextures(1, &brdfLutTex_); brdfLutTex_ = 0; }
    if (skyboxProgram_) { glDeleteProgram(skyboxProgram_); skyboxProgram_ = 0; }
    if (skyboxVAO_) { glDeleteVertexArrays(1, &skyboxVAO_); skyboxVAO_ = 0; }
    ready_ = false;
}

void OpenGLEnvironment::CreateSkyboxProgram()
{
    if (skyboxProgram_)
        return;

    // 与 assets/shader/skybox.shader 一致的 GLSL 330 源码（自包含，避免路径依赖）
    const char *vertSrc = R"(
#version 330 core
uniform mat4 uInvViewProj;
out vec3 vDir;
void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    vDir = world.xyz / max(world.w, 1e-6);
    gl_Position = clip;
}
)";
    const char *fragSrc = R"(
#version 330 core
uniform samplerCube uEnvMap;
uniform float uYaw;
uniform float uExposure;
in vec3 vDir;
out vec4 FragColor;
vec3 RotateYaw(vec3 d, float y)
{
    float c = cos(y);
    float s = sin(y);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}
void main()
{
    vec3 dir = RotateYaw(normalize(vDir), uYaw);
    vec3 color = texture(uEnvMap, dir).rgb;
    color *= uExposure;
    color = color / (color + vec3(1.0));
    FragColor = vec4(color, 1.0);
}
)";

    unsigned int vs = CompileGLShader(GL_VERTEX_SHADER, vertSrc);
    unsigned int fs = CompileGLShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }
    skyboxProgram_ = glCreateProgram();
    glAttachShader(skyboxProgram_, vs);
    glAttachShader(skyboxProgram_, fs);
    glLinkProgram(skyboxProgram_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &skyboxVAO_); // 空 VAO：全屏三角形用 gl_VertexID，无需顶点缓冲
}

void OpenGLEnvironment::DrawSkybox(const glm::mat4 &view, const glm::mat4 &proj,
                                   int width, int height)
{
    if (!ready_ || !skyboxProgram_)
        return;

    // 天空盒位于无穷远：只用 view 的旋转部分
    glm::mat4 rotView = glm::mat4(glm::mat3(view));
    glm::mat4 invViewProj = glm::inverse(proj * rotView);

    glViewport(0, 0, width, height);
    glUseProgram(skyboxProgram_);
    glUniformMatrix4fv(glGetUniformLocation(skyboxProgram_, "uInvViewProj"),
                       1, GL_FALSE, &invViewProj[0][0]);
    glUniform1i(glGetUniformLocation(skyboxProgram_, "uEnvMap"), 0);
    glUniform1f(glGetUniformLocation(skyboxProgram_, "uYaw"), yaw_);
    glUniform1f(glGetUniformLocation(skyboxProgram_, "uExposure"), exposure_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubeTex_);

    // 深度：不写深度，LEQUAL 测试（全屏三角形深度=1.0 远平面）
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(skyboxVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
}

void OpenGLEnvironment::BindSceneTextures()
{
    if (!ready_)
        return;
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubeTex_);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceTex_);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredTex_);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, brdfLutTex_);
}

} // namespace aster
