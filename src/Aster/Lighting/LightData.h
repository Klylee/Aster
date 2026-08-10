#pragma once

#include <glm/glm.hpp>

// ============================================================================
// LightData —— 灯光 GPU 数据（无 GL / Vulkan 依赖的纯数据头）
// ----------------------------------------------------------------------------
// 该结构同时用于：
//   - OpenGL 的 UBO（LightManager::UploadToGPU -> glBufferSubData）
//   - Vulkan 的 UBO（VulkanPipeline::UpdateLightsUBO -> 映射内存 memcpy）
//   - 片元着色器（mesh.frag）的 std140 uniform 块
//
// 内存布局按 std140 规则对齐：
//   GPULight : type@0 | position@16 | direction@32 | color@48 | intensity@60
//              range@64 | innerCone@68 | outerCone@72 | 填充到 96 字节
//   LightUBO : lightCount@0 | padding@4 | lights@16（每个 96 字节）
// 与着色器端声明一一对应，两端都必须保持同步。
// ============================================================================

namespace aster
{

constexpr int MAX_LIGHTS = 32;

struct GPULight
{
    int type;
    float pad0[3];
    glm::vec3 position;
    float pad1;
    glm::vec3 direction;
    float pad2;
    glm::vec3 color;
    float intensity;
    float range;
    float innerCone;
    float outerCone;
    // 阴影字段（96 字节内复用尾部 padding，两端必须同步）
    // outerCone@72 -> castsShadow@76 | shadowMapIndex@80 | shadowType@84 | pad3@88 | pad4@92 = 96
    int castsShadow;        // 1 = 该灯光投射阴影
    int shadowMapIndex;     // 2D shadow map 数组索引（方向光/聚光），-1 = 无
    int shadowType;         // 阴影类型：0=2D(方向/聚光), 1=点光源(cubemap)
    float pad3;
    float pad4;
};

struct LightUBO
{
    int lightCount;         // @0
    int padding[3];         // @4
    GPULight lights[MAX_LIGHTS]; // @16（每个 96 字节）
};

} // namespace aster

