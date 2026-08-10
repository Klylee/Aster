#version 450 core

// 天空盒片元着色器：按观察方向采样环境 cubemap。
// set 0 binding 6 = 环境 cubemap（与 mesh.frag 的 uEnvMap 同资源）。
// 输出经过简单 Reinhard tone map（可选曝光），避免 HDR 天空过曝成纯白。

layout(set = 0, binding = 6) uniform samplerCube uEnvMap;

layout(push_constant) uniform SkyboxPC {
    mat4 invViewProj;
    vec4 yaw; // yaw.x = 方位角（弧度），yaw.y = 曝光
} sky;

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 FragColor;

vec3 RotateYaw(vec3 d, float y)
{
    float c = cos(y);
    float s = sin(y);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

void main()
{
    vec3 dir = RotateYaw(normalize(vDir), sky.yaw.x);
    vec3 color = texture(uEnvMap, dir).rgb;
    color *= sky.yaw.y; // 曝光
    color = color / (color + vec3(1.0)); // Reinhard tone map
    FragColor = vec4(color, 1.0);
}
