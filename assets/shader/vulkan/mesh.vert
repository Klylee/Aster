#version 450 core

// Vulkan 网格顶点着色器（与 VulkanSceneRenderer 的顶点布局对应）：
//   顶点格式（与框架 Mesh 一致）：pos3 + nor3 + uv2（stride 32 字节）
//   set 0 binding 0 : CameraUBO（view / projection）
//   push constants  : mat4 model + vec4 color + vec4 shadow（96 字节）
//     shadow = (灯光位置.xyz, 阴影投影平面Y)；shadow.w==0 表示非阴影绘制

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 shadow;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;

void main()
{
    vec4 worldPos = pc.model * vec4(aPos, 1.0);

    // 平面投影阴影：把顶点沿灯光方向压到平面 y = pc.shadow.w
    if (pc.shadow.w != 0.0)
    {
        vec3 L = pc.shadow.xyz;
        float denom = worldPos.y - L.y;
        if (abs(denom) > 1e-5)
        {
            float t = (pc.shadow.w - L.y) / denom;
            worldPos = vec4(L + t * (worldPos.xyz - L), 1.0);
        }
        else
        {
            worldPos.y = pc.shadow.w; // 退化：与灯光同高时压到平面附近
        }
    }

    gl_Position = camera.projection * camera.view * worldPos;
    vNormal = mat3(pc.model) * aNormal;   // 转到世界空间
    vWorldPos = vec3(worldPos);
    vUV = aUV;
}
