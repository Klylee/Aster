#pragma once

// ============================================================================
// OpenGLEnvironment —— OpenGL 后端的 HDR 环境贴图（天空盒 + IBL）
// ----------------------------------------------------------------------------
// 仅 Windows（非 Apple）编译：macOS 使用 Vulkan 后端。
//   - Upload()：把 EnvironmentMap 的 CPU 数据上传为 GL 立方体纹理（环境 /
//     irradiance / 预过滤 mip 链）与 BRDF LUT（GL_TEXTURE_2D）。
//   - DrawSkybox()：用 skybox.shader（全屏三角形）绘制 HDR 背景。
//   - BindSceneTextures()：把 4 张贴图绑定到纹理单元 6-9，供场景材质
//     （env_ibl.shader）采样。环境参数（模式/强度/粗糙度等）由材质 uniform
//     传递（demo 每帧 SetUniform）。
// ============================================================================

#include <glm/glm.hpp>
#include "EnvironmentMap.h"

namespace aster
{

class OpenGLEnvironment
{
public:
    OpenGLEnvironment() = default;
    ~OpenGLEnvironment();
    OpenGLEnvironment(const OpenGLEnvironment &) = delete;
    OpenGLEnvironment &operator=(const OpenGLEnvironment &) = delete;

    // 上传环境贴图数据（GL 立方体纹理 + BRDF LUT）。失败返回 false。
    bool Upload(const EnvironmentMap &env);

    void Shutdown();

    bool IsReady() const { return ready_; }

    // 环境模式：0=关闭, 1=反射, 2=漫反射IBL, 3=漫反射+高光IBL
    void SetMode(int mode) { mode_ = mode; }

    // 环境参数（天空盒曝光/方位角用）
    void SetParams(float intensity, float roughness, float metallic, float ao,
                   float yaw, float exposure, bool toneMap);

    // 绘制天空盒（全屏三角形）。view 只用旋转部分（位于无穷远）。
    void DrawSkybox(const glm::mat4 &view, const glm::mat4 &proj, int width, int height);

    // 绑定环境贴图到纹理单元 6-9（供 env_ibl.shader 采样）
    void BindSceneTextures();

private:
    void CreateSkyboxProgram();

    bool ready_ = false;
    unsigned int envCubeTex_ = 0;       // 环境 cubemap
    unsigned int irradianceTex_ = 0;    // 漫反射 irradiance cubemap
    unsigned int prefilteredTex_ = 0;   // 高光预过滤 cubemap（mip 链）
    unsigned int brdfLutTex_ = 0;       // BRDF LUT（2D）
    int prefilteredMips_ = 6;

    // 天空盒
    unsigned int skyboxProgram_ = 0;    // GL 程序
    unsigned int skyboxVAO_ = 0;

    // 参数
    int mode_ = 0;
    float intensity_ = 1.0f;
    float roughness_ = 0.3f;
    float metallic_ = 0.0f;
    float ao_ = 1.0f;
    float yaw_ = 0.0f;
    float exposure_ = 1.0f;
    bool toneMap_ = true;
};

} // namespace aster
