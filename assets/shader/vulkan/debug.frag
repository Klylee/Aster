#version 450

// 调试线框片元着色器：直接输出顶点色（用于碰撞体线框 / 接触点 / 射线 / BVH 盒）。

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;
}
