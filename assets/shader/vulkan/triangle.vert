#version 450 core

// 演示三角形顶点着色器：无顶点缓冲，通过 gl_VertexIndex 生成
// push constants（与 VulkanRenderAPI.cpp 中 CPU 结构保持一致）：
//   vec3 uColor + float uTime = 16 字节

layout(location = 0) out vec3 vColor;

layout(push_constant) uniform PushConstants
{
    vec3  uColor;
    float uTime;
} pc;

void main()
{
    vec2 positions[3] = vec2[](
        vec2( 0.0, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5,  0.5)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vColor = pc.uColor * (0.6 + 0.4 * sin(pc.uTime + float(gl_VertexIndex) * 2.0));
}
