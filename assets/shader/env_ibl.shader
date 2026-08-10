#shader vertex
#version 330 core

// OpenGL 环境贴图（IBL）着色器 —— transparent.shader 的环境版。
// 顶点格式与框架 Mesh 一致：location 0=pos，location 1=normal。
// 输入材质颜色（uniform vec4 color）+ 环境贴图参数（纹理单元 6-9）。

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vWorldPos;
out vec3 vNormal;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;
    vWorldPos = worldPos.xyz;
    vNormal = mat3(model) * aNormal;
}

#shader fragment
#version 330 core

out vec4 FragColor;

in vec3 vWorldPos;
in vec3 vNormal;

uniform vec4 color;

// ---- 环境贴图（与 OpenGLEnvironment 绑定的纹理单元一致：6-9） ----
uniform samplerCube uEnvMap;         // 环境 cubemap（天空盒 / 反射）
uniform samplerCube uIrradianceMap;  // 漫反射 irradiance
uniform samplerCube uPrefilteredMap; // 高光预过滤（mip 按粗糙度）
uniform sampler2D  uBRDFLUT;         // BRDF LUT（2D）
uniform int   uEnvMode;              // 0=关, 1=反射, 2=漫反射IBL, 3=漫反射+高光IBL
uniform float uEnvIntensity;         // 环境光强度
uniform float uEnvRoughness;         // 粗糙度
uniform float uEnvMetallic;          // 金属度
uniform float uEnvAO;                // 环境光遮蔽
uniform float uEnvYaw;               // 方位角（弧度）
uniform float uEnvExposure;          // 曝光
uniform float uEnvToneMap;           // 1=Reinhard tone map
uniform vec3  uCamPos;               // 相机世界位置

vec3 RotateYaw(vec3 d, float y)
{
    float c = cos(y);
    float s = sin(y);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 环境项（IBL ambient），模式 1/2/3 与 Vulkan mesh.frag 一致
vec3 ComputeEnvAmbient(vec3 N, vec3 V)
{
    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), color.rgb, uEnvMetallic);

    if (uEnvMode == 1) // 反射
    {
        vec3 R = reflect(-V, N);
        return texture(uEnvMap, RotateYaw(R, uEnvYaw)).rgb;
    }

    // 模式 2/3：漫反射 IBL
    vec3 irradiance = texture(uIrradianceMap, RotateYaw(N, uEnvYaw)).rgb;
    vec3 diffuse = irradiance * color.rgb;
    vec3 kS = FresnelSchlickRoughness(NdotV, F0, uEnvRoughness);
    vec3 kD = 1.0 - kS;
    vec3 ambient = kD * diffuse * uEnvAO;

    // 模式 3：高光 IBL（split-sum）
    if (uEnvMode >= 3)
    {
        vec3 R = reflect(-V, N);
        int mipCount = 6; // 与预过滤 mip 级数一致（OpenGLEnvironment::Upload）
        vec3 prefiltered = textureLod(uPrefilteredMap, RotateYaw(R, uEnvYaw),
                                      uEnvRoughness * float(mipCount - 1)).rgb;
        vec2 brdf = texture(uBRDFLUT, vec2(NdotV, uEnvRoughness)).rg;
        ambient += prefiltered * (F0 * brdf.x + brdf.y);
    }
    return ambient;
}

void main()
{
    vec3 N = normalize(vNormal);

    if (uEnvMode == 0)
    {
        // 关闭环境贴图：保持原透明着色器的外观（单色）
        FragColor = color;
        return;
    }

    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 ambient = ComputeEnvAmbient(N, V);

    // 简化方向光（保证物体有明暗层次），强度固定
    vec3 sunDir = normalize(vec3(0.4, 0.9, 0.3));
    vec3 direct = color.rgb * max(dot(N, sunDir), 0.0) * 0.6;

    vec3 final = ambient * uEnvIntensity + direct + color.rgb * 0.06;
    if (uEnvToneMap > 0.5)
    {
        final = final * uEnvExposure;
        final = final / (final + vec3(1.0)); // Reinhard
    }
    FragColor = vec4(final, color.a);
}
