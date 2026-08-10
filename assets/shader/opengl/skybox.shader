#shader vertex
#version 330 core

// OpenGL 天空盒：全屏三角形（gl_VertexID，无顶点缓冲）+ 逆 VP 方向采样。
// 与 Vulkan 版 skybox.vert 逻辑一致。

uniform mat4 uInvViewProj;

out vec3 vDir;

void main()
{
    // 全屏三角形覆盖 NDC：顶点 0=(-1,-1)，1=(3,-1)，2=(-1,3)
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); // (0,0),(2,0),(0,2)
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    vDir = world.xyz / max(world.w, 1e-6);
    gl_Position = clip;
}

#shader fragment
#version 330 core

uniform samplerCube uEnvMap; // 环境 cubemap（单位 0）
uniform float uYaw;          // 方位角（弧度）
uniform float uExposure;     // 曝光

in vec3 vDir;
out vec4 FragColor;

vec3 RotateYaw(vec3 d, float y)
{
    float c = cos(y);
    float s = sin(y);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

void main()
{
    vec3 dir = RotateYaw(normalize(vDir), uYaw);
    vec3 color = texture(uEnvMap, dir).rgb;
    color *= uExposure;
    color = color / (color + vec3(1.0)); // Reinhard tone map
    FragColor = vec4(color, 1.0);
}
