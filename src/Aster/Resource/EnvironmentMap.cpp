#include "EnvironmentMap.h"

// tinyexr 是单头库：在唯一一个翻译单元里展开实现。
// 用 stb_image / stb_image_write 内置的 zlib（项目已编译这两个库），
// 避免依赖 miniz.h / 系统 zlib（TINYEXR_USE_MINIZ 默认 1 需要 miniz.h）。
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

// ============================================================================
// 常量（各贴图分辨率与采样数——演示级别，兼顾质量与加载耗时）
// ============================================================================
namespace
{
constexpr int kEnvCubeSize = 256;        // 环境 cubemap 每面分辨率
constexpr int kIrradianceSize = 48;      // 漫反射 irradiance cubemap 分辨率（加大 → 太阳区域更平滑）
constexpr int kPrefilterBaseSize = 128;  // 高光预过滤 mip0 分辨率
constexpr int kPrefilterMips = 6;        // 高光预过滤 mip 级数（128..4）
constexpr int kIrradianceSamples = 512;  // 漫反射半球采样数（加大 → 方差更低）
constexpr int kPrefilterSamples = 64;    // 高光卷积每 texel 采样数
constexpr int kBRDFLutSize = 256;        // BRDF LUT 分辨率
constexpr int kBRDFLutSamples = 256;     // BRDF LUT 每 texel 采样数
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEnvConvolutionClamp = 100.0f; // 卷积时极亮样本（太阳，HDR 可达数万）的亮度上限（火苗抑制）
} // namespace

namespace aster
{

// ----------------------------------------------------------------------------
// 伪随机 / 采样序列辅助
// ----------------------------------------------------------------------------
namespace
{
// Hammersley 序列（低差异，均匀覆盖 [0,1)²）
float RadicalInverseVdC(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

// GGX 重要性采样：给定 (xi0, xi1) 与粗糙度 a，返回半程向量 H（切空间 z-up）
void ImportanceSampleGGX(float xi0, float xi1, float roughness, float outH[3])
{
    float a = roughness * roughness;
    float phi = 2.0f * kPi * xi1;
    float cosTheta = std::sqrt((1.0f - xi0) / (1.0f + (a * a - 1.0f) * xi0));
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    outH[0] = std::cos(phi) * sinTheta;
    outH[1] = std::sin(phi) * sinTheta;
    outH[2] = cosTheta;
}

// GGX 法线分布函数
float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

// 几何遮蔽（Schlick-GGX 近似）
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// 由法线构造正交基（T, B, N）
void BuildBasis(float nx, float ny, float nz, float outT[3], float outB[3])
{
    // 避免与法线接近平行的上向量
    float ux = 0.0f, uy = 0.0f, uz = 1.0f;
    if (std::abs(nz) > 0.999f)
    {
        ux = 0.0f; uy = 1.0f; uz = 0.0f;
    }
    // T = normalize(cross(up, N))
    float tx = uy * nz - uz * ny;
    float ty = uz * nx - ux * nz;
    float tz = ux * ny - uy * nx;
    float tl = std::sqrt(tx * tx + ty * ty + tz * tz) + 1e-8f;
    outT[0] = tx / tl; outT[1] = ty / tl; outT[2] = tz / tl;
    // B = cross(N, T)
    outB[0] = ny * outT[2] - nz * outT[1];
    outB[1] = nz * outT[0] - nx * outT[2];
    outB[2] = nx * outT[1] - ny * outT[0];
}

// 火苗抑制（firefly clamp）：把极亮样本（如太阳，值可达数万）按亮度压到有界值。
// 蒙特卡洛卷积中，极小极亮的太阳若被个别采样点命中，会把该 texel 瞬间抬到数百倍
// 亮度（邻域 texel 却只有几倍）→ 物体表面出现一簇“亮方块”。压到 maxLum 后，
// 太阳区域在 irradiance / 预过滤贴图里变成平滑的亮斑而不是噪声方块。
void ClampFirefly(float col[3], float maxLum)
{
    float l = 0.2126f * col[0] + 0.7152f * col[1] + 0.0722f * col[2];
    if (l > maxLum && l > 1e-6f)
    {
        float s = maxLum / l;
        col[0] *= s;
        col[1] *= s;
        col[2] *= s;
    }
}
} // namespace

// ----------------------------------------------------------------------------
// 等距柱状投影 → cubemap 的面方向约定（与 OpenGL/Vulkan cubemap 布局一致）
// face 0..5 = +X, -X, +Y, -Y, +Z, -Z
// 传入面内坐标 (a, b) ∈ [-1,1]（a 沿面横轴、b 沿面纵轴）
// ----------------------------------------------------------------------------
namespace
{
void CubeFaceDirection(int face, float a, float b, float out[3])
{
    switch (face)
    {
    case 0: out[0] = 1.0f;  out[1] = -b; out[2] = -a; break; // +X
    case 1: out[0] = -1.0f; out[1] = -b; out[2] =  a; break; // -X
    case 2: out[0] =  a;    out[1] = 1.0f; out[2] =  b; break; // +Y
    case 3: out[0] =  a;    out[1] = -1.0f; out[2] = -b; break; // -Y
    case 4: out[0] =  a;    out[1] = -b; out[2] = 1.0f; break; // +Z
    default: out[0] = -a;   out[1] = -b; out[2] = -1.0f; break; // -Z
    }
    float l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]) + 1e-8f;
    out[0] /= l; out[1] /= l; out[2] /= l;
}

// 由方向求 (face, u, v)，u,v ∈ [0,1]
int DirToCubeUV(float x, float y, float z, float &u, float &v)
{
    float ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
    int face = 0;
    if (ax >= ay && ax >= az)
    {
        if (x >= 0.0f) { face = 0; u = 0.5f * (-z / x + 1.0f); v = 0.5f * (-y / x + 1.0f); }
        else           { face = 1; u = 0.5f * (-z / x + 1.0f); v = 0.5f * ( y / x + 1.0f); } // 改
    }
    else if (ay >= ax && ay >= az)
    {
        if (y >= 0.0f) { face = 2; u = 0.5f * ( x / y + 1.0f); v = 0.5f * ( z / y + 1.0f); }
        else           { face = 3; u = 0.5f * (-x / y + 1.0f); v = 0.5f * ( z / y + 1.0f); } // 改
    }
    else
    {
        if (z >= 0.0f) { face = 4; u = 0.5f * ( x / z + 1.0f); v = 0.5f * (-y / z + 1.0f); }
        else           { face = 5; u = 0.5f * ( x / z + 1.0f); v = 0.5f * ( y / z + 1.0f); } // 改
    }
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    return face;
}
} // namespace

// ----------------------------------------------------------------------------
// CPU 采样
// ----------------------------------------------------------------------------
void EnvironmentMap::SampleCube(const std::vector<float> &cube, int size,
                                float x, float y, float z, float out[3])
{
    float u = 0.0f, v = 0.0f;
    int face = DirToCubeUV(x, y, z, u, v);

    // 面内双线性（边缘 clamp，简单起见不跨面）
    float px = u * size - 0.5f;
    float py = v * size - 0.5f;
    int x0 = (int)std::floor(px);
    int y0 = (int)std::floor(py);
    float fx = px - (float)x0;
    float fy = py - (float)y0;
    x0 = std::clamp(x0, 0, size - 1);
    int x1 = std::clamp(x0 + 1, 0, size - 1);
    y0 = std::clamp(y0, 0, size - 1);
    int y1 = std::clamp(y0 + 1, 0, size - 1);

    const float *f0 = &cube[((size_t)face * size + (size_t)y0) * size * 4 + (size_t)x0 * 4];
    const float *f1 = &cube[((size_t)face * size + (size_t)y0) * size * 4 + (size_t)x1 * 4];
    const float *f2 = &cube[((size_t)face * size + (size_t)y1) * size * 4 + (size_t)x0 * 4];
    const float *f3 = &cube[((size_t)face * size + (size_t)y1) * size * 4 + (size_t)x1 * 4];

    for (int c = 0; c < 3; c++)
    {
        float top = f0[c] * (1.0f - fx) + f1[c] * fx;
        float bot = f2[c] * (1.0f - fx) + f3[c] * fx;
        out[c] = top * (1.0f - fy) + bot * fy;
    }
}

void EnvironmentMap::SampleEquirectBilinear(const std::vector<float> &equirect, int w, int h,
                                            float u, float v, float out[3])
{
    u = u - std::floor(u); // 水平环绕
    v = std::clamp(v, 0.0f, 1.0f);

    float px = u * w - 0.5f;
    float py = v * h - 0.5f;
    int x0 = (int)std::floor(px);
    int y0 = (int)std::floor(py);
    float fx = px - (float)x0;
    float fy = py - (float)y0;

    x0 = ((x0 % w) + w) % w;
    int x1 = (x0 + 1) % w;
    y0 = std::clamp(y0, 0, h - 1);
    int y1 = std::clamp(y0 + 1, 0, h - 1);

    const float *f0 = &equirect[((size_t)y0 * w + (size_t)x0) * 4];
    const float *f1 = &equirect[((size_t)y0 * w + (size_t)x1) * 4];
    const float *f2 = &equirect[((size_t)y1 * w + (size_t)x0) * 4];
    const float *f3 = &equirect[((size_t)y1 * w + (size_t)x1) * 4];

    for (int c = 0; c < 3; c++)
    {
        float top = f0[c] * (1.0f - fx) + f1[c] * fx;
        float bot = f2[c] * (1.0f - fx) + f3[c] * fx;
        out[c] = top * (1.0f - fy) + bot * fy;
    }
}

// 用方向采样等距柱状投影（u: 经度，v: 纬度；row0=图像顶部）
namespace
{
void SampleEquirectByDir(const std::vector<float> &equirect, int w, int h,
                         float x, float y, float z, float out[3])
{
    // u = 0.5 + atan2(z, x)/(2π)；+X 为图像水平中心
    // v = 0.5 - asin(y)/π；+Y(上) 对应图像顶部（row0）
    float u = 0.5f + std::atan2(z, x) / (2.0f * kPi);
    float v = 0.5f - std::asin(std::clamp(y, -1.0f, 1.0f)) / kPi;
    EnvironmentMap::SampleEquirectBilinear(equirect, w, h, u, v, out);
}
} // namespace

// ----------------------------------------------------------------------------
// 无缝化 cubemap 面边界（消除接缝）
// ----------------------------------------------------------------------------
namespace
{
// 只处理距面边界 1 texel 内的 texel：用跨面方向采样做 3x3 平均。
// 相邻面在共享边缘的离散采样间隙会使反射/天空在部分方向不连贯（接缝），
// 边界 texel 与相邻面数据混合后两面趋于连续；内部（如太阳）保持锐利不模糊。
void SeamFixCube(std::vector<float> &cube, int size)
{
    std::vector<float> work = cube;
    for (int f = 0; f < 6; f++)
    {
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                bool onEdge = (x == 0 || y == 0 || x == size - 1 || y == size - 1);
                if (!onEdge)
                    continue;
                float a = 2.0f * (x + 0.5f) / size - 1.0f;
                float b = 2.0f * (y + 0.5f) / size - 1.0f;
                float N[3];
                CubeFaceDirection(f, a, b, N);
                float T[3], B[3];
                BuildBasis(N[0], N[1], N[2], T, B);
                const float step = 2.0f / (float)size; // 相邻 texel 的角跨度
                float acc[3] = {0.0f, 0.0f, 0.0f};
                int cnt = 0;
                for (int dy = -1; dy <= 1; dy++)
                {
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        float wx = N[0] + (dx * step) * T[0] + (dy * step) * B[0];
                        float wy = N[1] + (dx * step) * T[1] + (dy * step) * B[1];
                        float wz = N[2] + (dx * step) * T[2] + (dy * step) * B[2];
                        float col[3];
                        EnvironmentMap::SampleCube(cube, size, wx, wy, wz, col);
                        acc[0] += col[0]; acc[1] += col[1]; acc[2] += col[2];
                        cnt++;
                    }
                }
                float *dst = &work[((size_t)f * size + y) * size * 4 + x * 4];
                dst[0] = acc[0] / (float)cnt;
                dst[1] = acc[1] / (float)cnt;
                dst[2] = acc[2] / (float)cnt;
            }
        }
    }
    cube.swap(work);
}
} // namespace

// ----------------------------------------------------------------------------
// EXR 加载
// ----------------------------------------------------------------------------
bool EnvironmentMap::LoadEXR(const std::string &path, std::vector<float> &rgba, int &w, int &h)
{
    float *out = nullptr;
    const char *err = nullptr;
    if (::LoadEXR(&out, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS)
    {
        std::cerr << "[EnvironmentMap] Failed to load EXR: " << path
                  << (err ? err : "") << std::endl;
        if (err)
            ::free((void *)err);
        return false;
    }
    rgba.assign(out, out + (size_t)w * h * 4);
    ::free(out);
    return true;
}

// ----------------------------------------------------------------------------
// 生成环境 cubemap（等距柱状投影 → 6 面）
// ----------------------------------------------------------------------------
bool EnvironmentMap::BuildEnvironmentCubemap(const std::vector<float> &equirect, int w, int h)
{
    envCubeSize_ = kEnvCubeSize;
    envCube_.assign((size_t)6 * kEnvCubeSize * kEnvCubeSize * 4, 0.0f);

    for (int face = 0; face < 6; face++)
    {
        for (int y = 0; y < kEnvCubeSize; y++)
        {
            for (int x = 0; x < kEnvCubeSize; x++)
            {
                // 面内像素 → [-1,1] 面坐标
                float a = 2.0f * (x + 0.5f) / kEnvCubeSize - 1.0f;
                float b = 2.0f * (y + 0.5f) / kEnvCubeSize - 1.0f;
                float dir[3];
                CubeFaceDirection(face, a, b, dir);
                float col[3];
                SampleEquirectByDir(equirect, w, h, dir[0], dir[1], dir[2], col);

                float *dst = &envCube_[((size_t)face * kEnvCubeSize + y) * kEnvCubeSize * 4 + x * 4];
                dst[0] = col[0]; dst[1] = col[1]; dst[2] = col[2]; dst[3] = 1.0f;
            }
        }
    }

    // 无缝化面边界：消除相邻面在共享边缘的采样间隙（接缝），
    // 否则低粗糙度反射/天空盒在部分方向不连贯。内部保持锐利。
    // SeamFixCube(envCube_, kEnvCubeSize);
    return true;
}

// ----------------------------------------------------------------------------
// 无缝盒式模糊（用方向采样，跨 cubemap 面边界也正确）。
// 用于去除 irradiance 卷积对微小太阳的残余蒙特卡洛采样噪声（散乱亮斑）。
// 漫反射 irradiance 本应超低频（平滑），模糊不会损失有效细节。
// radius 以 texel 为单位，iterations 为迭代次数。
// ----------------------------------------------------------------------------
namespace
{
void BlurCube(const std::vector<float> &src, int size, int radius, int iterations,
              std::vector<float> &out)
{
    std::vector<float> tmp = src;
    std::vector<float> work(src.size(), 0.0f);
    for (int it = 0; it < iterations; it++)
    {
        for (int f = 0; f < 6; f++)
        {
            for (int y = 0; y < size; y++)
            {
                for (int x = 0; x < size; x++)
                {
                    float a = 2.0f * (x + 0.5f) / size - 1.0f;
                    float b = 2.0f * (y + 0.5f) / size - 1.0f;
                    float N[3];
                    CubeFaceDirection(f, a, b, N);
                    float T[3], B[3];
                    BuildBasis(N[0], N[1], N[2], T, B);
                    const float step = 2.0f / (float)size; // 相邻 texel 的角跨度
                    float acc[3] = {0.0f, 0.0f, 0.0f};
                    int cnt = 0;
                    for (int dy = -radius; dy <= radius; dy++)
                    {
                        for (int dx = -radius; dx <= radius; dx++)
                        {
                            float wx = N[0] + (dx * step) * T[0] + (dy * step) * B[0];
                            float wy = N[1] + (dx * step) * T[1] + (dy * step) * B[1];
                            float wz = N[2] + (dx * step) * T[2] + (dy * step) * B[2];
                            float col[3];
                            EnvironmentMap::SampleCube(tmp, size, wx, wy, wz, col);
                            acc[0] += col[0]; acc[1] += col[1]; acc[2] += col[2];
                            cnt++;
                        }
                    }
                    float *dst = &work[((size_t)f * size + y) * size * 4 + x * 4];
                    dst[0] = acc[0] / (float)cnt;
                    dst[1] = acc[1] / (float)cnt;
                    dst[2] = acc[2] / (float)cnt;
                    dst[3] = 1.0f;
                }
            }
        }
        tmp.swap(work);
    }
    out = tmp;
}
} // namespace

// ----------------------------------------------------------------------------
// 生成漫反射 irradiance cubemap（余弦加权半球卷积）
//   E(N) = (π / N) * Σ L(ω_i)    （余弦加权重要性采样蒙特卡洛）
// ----------------------------------------------------------------------------
bool EnvironmentMap::BuildIrradiance()
{
    irradianceSize_ = kIrradianceSize;
    irradiance_.assign((size_t)6 * kIrradianceSize * kIrradianceSize * 4, 0.0f);

    // 预计算半球采样方向（切空间 z-up）与 cosθ 权重
    static std::vector<float> sHemDirs;  // 3*N
    static std::vector<float> sHemCos;   // N
    if (sHemDirs.empty())
    {
        sHemDirs.resize(3 * kIrradianceSamples);
        sHemCos.resize(kIrradianceSamples);
        for (int i = 0; i < kIrradianceSamples; i++)
        {
            float xi0 = (float)(i + 0.5f) / kIrradianceSamples;
            float xi1 = RadicalInverseVdC((uint32_t)i);
            float z = std::sqrt(std::max(0.0f, 1.0f - xi0));
            float phi = 2.0f * kPi * xi1;
            float r = std::sqrt(xi0);
            sHemDirs[i * 3 + 0] = std::cos(phi) * r;
            sHemDirs[i * 3 + 1] = std::sin(phi) * r;
            sHemDirs[i * 3 + 2] = z;
            sHemCos[i] = z;
        }
    }

    const float scale = kPi / (float)kIrradianceSamples;

    for (int face = 0; face < 6; face++)
    {
        for (int y = 0; y < kIrradianceSize; y++)
        {
            for (int x = 0; x < kIrradianceSize; x++)
            {
                float a = 2.0f * (x + 0.5f) / kIrradianceSize - 1.0f;
                float b = 2.0f * (y + 0.5f) / kIrradianceSize - 1.0f;
                float N[3];
                CubeFaceDirection(face, a, b, N);

                float T[3], B[3];
                BuildBasis(N[0], N[1], N[2], T, B);

                float acc[3] = {0.0f, 0.0f, 0.0f};
                for (int i = 0; i < kIrradianceSamples; i++)
                {
                    // 把切空间采样方向旋转到世界空间
                    float dx = sHemDirs[i * 3 + 0], dy = sHemDirs[i * 3 + 1], dz = sHemDirs[i * 3 + 2];
                    float wx = dx * T[0] + dy * B[0] + dz * N[0];
                    float wy = dx * T[1] + dy * B[1] + dz * N[1];
                    float wz = dx * T[2] + dy * B[2] + dz * N[2];
                    float col[3];
                    SampleCube(envCube_, envCubeSize_, wx, wy, wz, col);
                    ClampFirefly(col, kEnvConvolutionClamp); // 火苗抑制：压住太阳极亮值 → 无亮方块
                    acc[0] += col[0]; acc[1] += col[1]; acc[2] += col[2];
                }

                float *dst = &irradiance_[((size_t)face * kIrradianceSize + y) * kIrradianceSize * 4 + x * 4];
                dst[0] = acc[0] * scale; dst[1] = acc[1] * scale; dst[2] = acc[2] * scale; dst[3] = 1.0f;
            }
        }
    }

    // 无缝盒式模糊：去除余弦加权蒙特卡洛对微小太阳的残余采样噪声（散乱亮斑）。
    // 漫反射 irradiance 本应超低频，模糊不会损失有效细节。
    std::vector<float> blurred;
    BlurCube(irradiance_, kIrradianceSize, 1, 2, blurred);
    irradiance_.swap(blurred);
    return true;
}

// ----------------------------------------------------------------------------
// 生成高光预过滤 cubemap（GGX 重要性采样，按粗糙度分层为 mip）
//   mip m 对应 roughness = m / (mips-1)；mip 分辨率逐层减半
// ----------------------------------------------------------------------------
bool EnvironmentMap::BuildPrefiltered()
{
    prefilteredBaseSize_ = kPrefilterBaseSize;
    prefiltered_.clear();

    for (int mip = 0; mip < kPrefilterMips; mip++)
    {
        int size = kPrefilterBaseSize >> mip;
        if (size < 2)
            break;
        float roughness = (float)mip / (float)(kPrefilterMips - 1);

        // 细/中 mip（低~中粗糙度）需要更多采样稳定解析微小太阳，否则太阳区域出现
        // 散乱亮斑；粗 mip 本身已按粗糙度模糊，64 采样足够。
        int samples = (mip >= 1 && mip <= 4) ? 256 : 64;

        std::vector<float> level((size_t)6 * size * size * 4, 0.0f);

        for (int face = 0; face < 6; face++)
        {
            for (int y = 0; y < size; y++)
            {
                for (int x = 0; x < size; x++)
                {
                    float a = 2.0f * (x + 0.5f) / size - 1.0f;
                    float b = 2.0f * (y + 0.5f) / size - 1.0f;
                    float N[3];
                    CubeFaceDirection(face, a, b, N);
                    // 取 N = V = R（split-sum 中反射方向即法线）
                    float V[3] = {N[0], N[1], N[2]};

                    float acc[3] = {0.0f, 0.0f, 0.0f};
                    float totalWeight = 0.0f;
                    for (int i = 0; i < samples; i++)
                    {
                        float xi0 = (float)(i + 0.5f) / samples;
                        float xi1 = RadicalInverseVdC((uint32_t)i);
                        float H[3];
                        ImportanceSampleGGX(xi0, xi1, roughness, H);

                        // 把切空间 H 旋转到世界空间
                        float T[3], B[3];
                        BuildBasis(N[0], N[1], N[2], T, B);
                        float hx = H[0] * T[0] + H[1] * B[0] + H[2] * N[0];
                        float hy = H[0] * T[1] + H[1] * B[1] + H[2] * N[1];
                        float hz = H[0] * T[2] + H[1] * B[2] + H[2] * N[2];

                        // L = reflect(-V, H) = 2*(V·H)H - V
                        float vdh = V[0] * hx + V[1] * hy + V[2] * hz;
                        float lx = 2.0f * vdh * hx - V[0];
                        float ly = 2.0f * vdh * hy - V[1];
                        float lz = 2.0f * vdh * hz - V[2];
                        float ndl = N[0] * lx + N[1] * ly + N[2] * lz;
                        if (ndl <= 0.0f)
                            continue;

                        float col[3];
                        SampleCube(envCube_, envCubeSize_, lx, ly, lz, col);
                        ClampFirefly(col, kEnvConvolutionClamp); // 火苗抑制（太阳高光同样压住）
                        acc[0] += col[0] * ndl;
                        acc[1] += col[1] * ndl;
                        acc[2] += col[2] * ndl;
                        totalWeight += ndl;
                    }

                    float *dst = &level[((size_t)face * size + y) * size * 4 + x * 4];
                    if (totalWeight > 1e-6f)
                    {
                        dst[0] = acc[0] / totalWeight;
                        dst[1] = acc[1] / totalWeight;
                        dst[2] = acc[2] / totalWeight;
                    }
                    dst[3] = 1.0f;
                }
            }
        }

        // // 跨面无缝模糊：消除面边界接缝（反射天空不连贯）与残余采样噪声。
        // // mip0（粗糙度 0）= 环境 cubemap，保持锐利不模糊。
        // if (mip >= 1)
        // {
        //     std::vector<float> blurred;
        //     BlurCube(level, size, 1, 1, blurred);
        //     level.swap(blurred);
        // }

        prefiltered_.push_back(std::move(level));
    }
    return true;
}

// ----------------------------------------------------------------------------
// 生成 BRDF LUT（GGX 的 split-sum BRDF 积分，输出 .rg = (scale, bias)）
//   行/列：x（水平）= NdotV，y（垂直）= roughness；row0 = roughness 0
// ----------------------------------------------------------------------------
bool EnvironmentMap::BuildBRDFLUT()
{
    brdfLutSize_ = kBRDFLutSize;
    brdfLUT_.assign((size_t)kBRDFLutSize * kBRDFLutSize * 4, 0.0f);

    for (int y = 0; y < kBRDFLutSize; y++)
    {
        float roughness = (float)(y + 0.5f) / kBRDFLutSize;
        for (int x = 0; x < kBRDFLutSize; x++)
        {
            float NdotV = (float)(x + 0.5f) / kBRDFLutSize;
            float V[3] = {std::sqrt(std::max(0.0f, 1.0f - NdotV * NdotV)), 0.0f, NdotV};
            float N[3] = {0.0f, 0.0f, 1.0f};

            float A = 0.0f, B = 0.0f;
            for (int i = 0; i < kBRDFLutSamples; i++)
            {
                float xi0 = (float)(i + 0.5f) / kBRDFLutSamples;
                float xi1 = RadicalInverseVdC((uint32_t)i);
                float H[3];
                ImportanceSampleGGX(xi0, xi1, roughness, H);

                // L = reflect(-V, H)
                float vdh = V[0] * H[0] + V[1] * H[1] + V[2] * H[2];
                float lx = 2.0f * vdh * H[0] - V[0];
                float ly = 2.0f * vdh * H[1] - V[1];
                float lz = 2.0f * vdh * H[2] - V[2];
                float NdotL = std::max(lz, 0.0f);
                if (NdotL <= 0.0f)
                    continue;

                float NdotH = std::max(H[2], 0.0f);
                float VdotH = std::max(vdh, 0.0f);
                float G = GeometrySmith(NdotV, NdotL, roughness);
                float GVis = (G * VdotH) / std::max(NdotH * NdotV, 1e-6f);
                float Fc = std::pow(1.0f - VdotH, 5.0f);
                A += (1.0f - Fc) * GVis;
                B += Fc * GVis;
            }
            A /= (float)kBRDFLutSamples;
            B /= (float)kBRDFLutSamples;

            float *dst = &brdfLUT_[((size_t)y * kBRDFLutSize + x) * 4];
            dst[0] = A; dst[1] = B; dst[2] = 0.0f; dst[3] = 1.0f;
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// 入口
// ----------------------------------------------------------------------------
bool EnvironmentMap::LoadFromFile(const std::string &path, std::string *reason)
{
    std::vector<float> equirect;
    int w = 0, h = 0;
    if (!LoadEXR(path, equirect, w, h) || w <= 0 || h <= 0)
    {
        if (reason)
            *reason = "LoadEXR failed";
        return false;
    }

    name_ = path;
    size_t pos = name_.find_last_of("/\\");
    if (pos != std::string::npos)
        name_ = name_.substr(pos + 1);

    std::cout << "[EnvironmentMap] Loaded " << name_ << " (" << w << "x" << h
              << " RGBA32F, " << (equirect.size() * 4 / (1024 * 1024)) << " MB)" << std::endl;

    if (!BuildEnvironmentCubemap(equirect, w, h))
    {
        if (reason)
            *reason = "BuildEnvironmentCubemap failed";
        return false;
    }
    std::cout << "[EnvironmentMap] Environment cubemap " << envCubeSize_ << "x"
              << envCubeSize_ << std::endl;

    if (!BuildIrradiance())
    {
        if (reason)
            *reason = "BuildIrradiance failed";
        return false;
    }
    std::cout << "[EnvironmentMap] Irradiance " << irradianceSize_ << "x"
              << irradianceSize_ << std::endl;

    if (!BuildPrefiltered())
    {
        if (reason)
            *reason = "BuildPrefiltered failed";
        return false;
    }
    std::cout << "[EnvironmentMap] Prefiltered " << prefilteredBaseSize_
              << " mips=" << prefiltered_.size() << std::endl;

    if (!BuildBRDFLUT())
    {
        if (reason)
            *reason = "BuildBRDFLUT failed";
        return false;
    }
    std::cout << "[EnvironmentMap] BRDF LUT " << brdfLutSize_ << "x" << brdfLutSize_
              << " — ready" << std::endl;

    equirect.clear();
    equirect.shrink_to_fit();
    return true;
}

} // namespace aster
