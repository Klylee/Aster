#version 450 core

// 地面网格线顶点着色器（自定义材质管线）。
// 顶点布局：pos3 + nor3 + uv2（stride 32 字节，与 mesh.vert 相同的相机 UBO + push constants）。
// 只输出世界坐标，供 grid.frag 在片元里按世界坐标程序化绘制网格线。

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

layout(location = 0) out vec3 vWorldPos;

void main()
{
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    gl_Position = camera.projection * camera.view * worldPos;
    vWorldPos = vec3(worldPos);
}
