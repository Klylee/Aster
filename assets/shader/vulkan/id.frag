#version 450 core

// 拾取 id 片元着色器：把 push constant 的 color（编码了对象拾取 ID）原样输出，
// 不做任何光照 / 混合。配合离屏 id map 渲染（mesh.vert 变换顶点）+ 读回缓冲，
// 用于鼠标拾取：每个可拾取对象以唯一颜色（RGB 各 8bit 编码 ID）填充。
// 背景清为黑色 → 解码 ID = 0 → 未命中。

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;    // = 编码的拾取 ID（见 EncodePickID）
    vec4 shadow;
    vec4 material;
} pc;

layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = pc.color;
}
