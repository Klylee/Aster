#version 450 core

// 点光源 cubemap 阴影的深度 pass 片元着色器。
// 与 mesh.vert 配合（其输出 vWorldPos，location 2）。
// 写入到光源的归一化线性距离（length/far），以便与采样端
// PointShadowFactor 中的 length(dir)/farPlane 比较。

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;

layout(std140, set = 0, binding = 5) uniform PointShadowUBO {
    vec4 lightPos;   // xyz = 位置
    float farPlane;
    float enabled;
    float _pad[2];
} uPointShadow;

void main()
{
    float dist = length(vWorldPos - uPointShadow.lightPos.xyz);
    // 归一化线性深度 [0,1]，远离光源 → 更接近 1
    gl_FragDepth = dist / max(uPointShadow.farPlane, 1e-3);
}
