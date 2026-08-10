#version 450 core

// M3 自定义材质管线示例：卡通（toon）顶点着色器。
// 与 mesh.vert 相同：输出法线 / UV / 世界坐标。
// 顶点布局：pos3 + nor3 + uv2（stride 32 字节）；push constant 与 mesh.* 一致（112 字节）。

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
    vec4 material; // x=粗糙度, y=金属度, z=AO, w=纹理索引
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;

void main()
{
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    gl_Position = camera.projection * camera.view * worldPos;
    vNormal = mat3(pc.model) * aNormal;   // 转到世界空间
    vWorldPos = vec3(worldPos);
    vUV = aUV;
}
