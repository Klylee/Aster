#pragma once

// ============================================================================
// EnvironmentMap —— HDR 环境贴图资源（多种环境贴图实现的核心数据源）
// ----------------------------------------------------------------------------
// 从等距柱状投影（equirectangular）HDR 文件（.exr）出发，在 CPU 上确定性地
// 生成整套可用于“基于图像的光照（IBL）”的贴图：
//
//   1. 环境 cubemap（envCube_）
//        天空盒背景 / 镜面反射（roughness≈0 的反射向量采样）
//   2. 漫反射 irradiance cubemap（irradiance_）
//        对半球做余弦加权卷积 → 漫反射 IBL（环境漫反射光照）
//   3. 高光预过滤 cubemap（prefiltered_，多 mip 按粗糙度分层）
//        GGX 重要性采样卷积 → 高光 IBL（split-sum 的前半部分）
//   4. BRDF LUT（brdfLUT_，2D）
//        GGX 积分 → 高光 IBL 的 BRDF 查找表（split-sum 的后半部分）
//
// 数据以 RGBA32F（每像素 4 个 float，行优先，第 0 行为图像顶部）保存；
// GPU 上传由各渲染后端负责：
//   - Vulkan 后端：VulkanPipeline::EnableEnvironmentMap()
//   - OpenGL 后端：OpenGL 侧对应上传函数（Windows 专用，macOS 用 Vulkan）
//
// 全部转换均为确定性 CPU 计算，加载时一次性完成，不依赖 GPU 中间 pass。
// ============================================================================

#include <string>
#include <vector>

namespace aster
{

class EnvironmentMap
{
public:
    EnvironmentMap() = default;
    ~EnvironmentMap() = default;
    EnvironmentMap(const EnvironmentMap &) = delete;
    EnvironmentMap &operator=(const EnvironmentMap &) = delete;

    // 从 .exr 文件加载 HDR（等距柱状投影）并生成全部 IBL 贴图。
    // 失败返回 false，reason 输出原因。
    bool LoadFromFile(const std::string &path, std::string *reason = nullptr);

    bool IsValid() const { return !envCube_.empty(); }
    const std::string &GetName() const { return name_; }

    // ---- 环境 cubemap（RGBA32F，6 face * envCubeSize_² * 4） ----
    const std::vector<float> &EnvCube() const { return envCube_; }
    int EnvCubeSize() const { return envCubeSize_; }

    // ---- 漫反射 irradiance cubemap（6 face * irradianceSize_² * 4） ----
    const std::vector<float> &Irradiance() const { return irradiance_; }
    int IrradianceSize() const { return irradianceSize_; }

    // ---- 高光预过滤 cubemap（每 mip 一张完整 cubemap） ----
    // prefiltered_[mip] = 6 face * size(mip)² * 4；mip 0 分辨率最高（= PrefilteredBaseSize_）
    const std::vector<std::vector<float>> &Prefiltered() const { return prefiltered_; }
    int PrefilteredBaseSize() const { return prefilteredBaseSize_; }
    int PrefilteredMips() const { return (int)prefiltered_.size(); }

    // ---- BRDF LUT（2D RGBA32F，size² * 4，.rg = (scale, bias)） ----
    const std::vector<float> &BRDFLUT() const { return brdfLUT_; }
    int BRDFLutSize() const { return brdfLutSize_; }

    // ---- CPU 采样辅助（静态工具，公开以便后端/测试复用） ----
    // 用方向 (x,y,z) 采样 cubemap（面内双线性，边缘 clamp）
    static void SampleCube(const std::vector<float> &cube, int size,
                           float x, float y, float z, float out[3]);
    // 用 (u,v) 采样等距柱状投影（双线性，u 环绕 / v clamp；row0=顶部）
    static void SampleEquirectBilinear(const std::vector<float> &equirect, int w, int h,
                                       float u, float v, float out[3]);

private:
    bool LoadEXR(const std::string &path, std::vector<float> &rgba, int &w, int &h);
    bool BuildEnvironmentCubemap(const std::vector<float> &equirect, int w, int h);
    bool BuildIrradiance();
    bool BuildPrefiltered();
    bool BuildBRDFLUT();

    std::string name_;
    std::vector<float> envCube_;     // 环境 cubemap
    int envCubeSize_ = 0;
    std::vector<float> irradiance_;  // 漫反射 irradiance cubemap
    int irradianceSize_ = 0;
    std::vector<std::vector<float>> prefiltered_; // 高光预过滤（按 mip）
    int prefilteredBaseSize_ = 0;
    std::vector<float> brdfLUT_;     // BRDF LUT
    int brdfLutSize_ = 0;
};

} // namespace aster
