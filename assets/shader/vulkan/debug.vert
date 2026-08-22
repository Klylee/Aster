#version 450

// 调试线框顶点着色器：输入世界空间 pos3 + color4，输出 viewProj 变换后的顶点。
// 由 VulkanPipeline::CreateDebugPipeline 使用（LINE_LIST，独立管线布局：
// 仅 push constant = mat4 viewProj，无描述集）。

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PushConstants
{
    mat4 viewProj;
} pc;

layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
    vColor = inColor;
}
