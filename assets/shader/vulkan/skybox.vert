#version 450 core

// 天空盒顶点着色器：用全屏三角形（无顶点缓冲）+ 逆 VP 矩阵算出每个像素的
// 观察方向，交给片元着色器采样环境 cubemap。
//
// push constants：
//   mat4 invViewProj —— 逆 (view_rotation * projection)，view 平移被去掉，
//                       使天空盒“位于无穷远”跟随相机旋转。
//   vec4 yaw         —— yaw.x = 环境方位角（弧度），用于旋转采样方向。

layout(push_constant) uniform SkyboxPC {
    mat4 invViewProj;
    vec4 yaw;
} sky;

layout(location = 0) out vec3 vDir;

void main()
{
    // 全屏三角形覆盖 NDC：顶点 0=(-1,-1)，1=(3,-1)，2=(-1,3)
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // (0,0),(2,0),(0,2)
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = sky.invViewProj * clip;
    vDir = world.xyz / max(world.w, 1e-6);
    gl_Position = clip;
}
