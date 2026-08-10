#version 450 core

// Vulkan 网格片元着色器（多光源漫反射 + 平面投影阴影）。
// 注意：push constant 块必须与顶点着色器完全一致（含 model/color/shadow），
// 以保证 color / shadow 位于正确的偏移。
// 灯光来自 set 0 binding 1 的 LightsUBO（std140，与 LightData.h 布局一致）。

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 shadow;
    vec4 material; // x=粗糙度, y=金属度, z=AO, w=纹理索引(M2)
} pc;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;

struct GPULight {
    int type;            // 0=Directional, 1=Point, 2=Spot
    int padding00;
    int padding01;
    int padding02;
    vec3 position;
    float padding1;
    vec3 direction;
    float padding2;
    vec3 color;
    float intensity;
    float range;
    float innerCone;
    float outerCone;
    int castsShadow;     // 1 = 投射阴影
    int shadowMapIndex;  // 2D shadow map 数组索引
    int shadowType;      // 0=2D(方向/聚光), 1=点光源(cubemap)
    float padding3;
    float padding4;
};

layout(std140, set = 0, binding = 1) uniform LightsUBO {
    int lightCount;
    int padding0;
    int padding1;
    int padding2;
    GPULight lights[32];
} uLights;

const int MAX_SHADOW_MAPS = 4;

layout(set = 0, binding = 2) uniform sampler2D uShadowMaps[MAX_SHADOW_MAPS];

// 2D 阴影信息（方向光/聚光），与 VulkanPipeline::ShadowLightUBO 一致
struct ShadowLightInfo {
    mat4 lightViewProj;
    int lightIndex;
    int enabled;
    int lightType;
    float _pad;
};
layout(std140, set = 0, binding = 3) uniform ShadowUBO {
    int shadowLightCount;
    int softShadow;      // 1 = 软阴影(PCF)，0 = 硬阴影
    int shadowDebugView; // 0 = 正常，1 = 显示 2D shadow map 深度，2 = 显示点光源 cubemap 深度
    int _pad1;
    ShadowLightInfo lights[MAX_SHADOW_MAPS];
} uShadow;

layout(set = 0, binding = 4) uniform samplerCube uPointShadowMap;
layout(std140, set = 0, binding = 5) uniform PointShadowUBO {
    vec4 lightPos;   // xyz = 位置
    float farPlane;
    float enabled;
    float _pad0;
    float _pad1;
} uPointShadow;

// ============================================================================
// 环境贴图（Environment Map）—— 多种 IBL 实现的数据来源
//   binding 6 : 环境 cubemap（天空盒 / 反射）
//   binding 7 : 漫反射 irradiance cubemap（漫反射 IBL）
//   binding 8 : 高光预过滤 cubemap（mip 按粗糙度分层，高光 IBL）
//   binding 9 : BRDF LUT（2D，高光 IBL 的 split-sum 查找表）
//   binding 10: 环境 UBO（强度 / 模式 / 材质参数 / 相机位置等）
// EnvUBO 与 VulkanPipeline::EnvUBO 保持一致（std140，3 个 vec4）。
// ============================================================================
layout(set = 0, binding = 6) uniform samplerCube uEnvMap;
layout(set = 0, binding = 7) uniform samplerCube uIrradianceMap;
layout(set = 0, binding = 8) uniform samplerCube uPrefilteredMap;
layout(set = 0, binding = 9) uniform sampler2D uBRDFLUT;
layout(std140, set = 0, binding = 10) uniform EnvUBO {
    vec4 params0; // x=强度, y=模式(0关/1反射/2漫反射IBL/3漫反射+高光IBL), z=mip级数, w=AO
    vec4 params1; // x=粗糙度, y=金属度, z=方位角(弧度), w=曝光
    vec4 params2; // x=tonemap(0/1), yzw=相机位置
} uEnv;

// M2：每对象材质纹理数组（索引来自 pc.material.w，-1=无纹理）。
// 索引 0 为 1x1 白色纹理（无纹理时采样它 = 纯色）。
const int MAX_MATERIAL_TEXTURES = 16;
layout(set = 0, binding = 11) uniform sampler2D uMaterialTextures[MAX_MATERIAL_TEXTURES];

layout(location = 0) out vec4 FragColor;

// 单盏灯对表面法线 N 的漫反射贡献（世界空间；不含高光）
vec3 CalcLight(GPULight L, vec3 N)
{
    float attenuation = 1.0;

    float type = float(L.type);
    vec3 lightPos = L.position;
    vec3 lightColor = L.color;
    vec3 lightDir = L.direction; // 从表面指向光源
    float intensity = L.intensity;
    float range = L.range;
    float innerCone = L.innerCone;
    float outerCone = L.outerCone;

    if (type == 0.0) // directional
    {
        lightDir = normalize(-lightDir);
    }
    else if (type == 1.0) // point
    {
        vec3 toLight = lightPos - vWorldPos;
        float dist = length(toLight);
        lightDir = toLight / max(dist, 1e-4);
        float r = max(range, 1e-3);
        float att = clamp(1.0 - (dist * dist) / (r * r), 0.0, 1.0);
        attenuation = att * att;
    }
    else if (type == 2.0) // spot
    {
        vec3 toLight = lightPos - vWorldPos;
        float dist = length(toLight);
        lightDir = toLight / max(dist, 1e-4);
        float r = max(range, 1e-3);
        float att = clamp(1.0 - (dist * dist) / (r * r), 0.0, 1.0);
        attenuation = att * att;

        // 聚光锥：-lightDir（光→表面）与灯光指向的夹角平滑过渡
        float cosAngle = dot(-lightDir, normalize(L.direction));
        float cosOuter = cos(outerCone);
        float cosInner = cos(innerCone);
        float spot = smoothstep(cosOuter, cosInner, cosAngle);
        attenuation *= spot;
    }

    float ndl = max(dot(N, lightDir), 0.0);
    return lightColor * intensity * ndl * attenuation;
}

// 2D shadow map（方向光/聚光）的 PCF 软阴影因子：遮挡时为 0，否则 1
float Shadow2DPCF(int mapIdx, vec3 worldPos)
{
    if (mapIdx < 0 || mapIdx >= MAX_SHADOW_MAPS)
        return 1.0;
    if (uShadow.lights[mapIdx].enabled < 0.5)
        return 1.0;

    vec4 clip = uShadow.lights[mapIdx].lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0; // 不在灯光视锥内 → 不受该灯光阴影影响

    float currentDepth = ndc.z; // 与 shadow map 同一坐标系（Vulkan 无 *0.5+0.5）
    float bias = 0.001;

    // 硬阴影：单次采样
    if (uShadow.softShadow < 0.5)
    {
        float d = texture(uShadowMaps[mapIdx], uv).r;
        return ((currentDepth - bias) > d) ? 0.0 : 1.0;
    }

    // 软阴影：PCF 3x3 采样取平均
    float shadow = 0.0;
    float texelSize = 1.0 / 2048.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float d = texture(uShadowMaps[mapIdx], uv + vec2(x, y) * texelSize).r;
            shadow += ((currentDepth - bias) > d) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// 点光源 cubemap 阴影：用方向向量采样，比较到光源距离
float PointShadowFactor(vec3 worldPos)
{
    if (uPointShadow.enabled < 0.5)
        return 1.0;

    vec3 dir = worldPos - uPointShadow.lightPos.xyz;
    float currentDepth = length(dir) / max(uPointShadow.farPlane, 1e-3); // 归一化 [0,1]
    float bias = 0.05;

    // 硬阴影：单次采样
    if (uShadow.softShadow < 0.5)
    {
        float d = texture(uPointShadowMap, normalize(dir)).r;
        return ((currentDepth - bias) > d) ? 0.0 : 1.0;
    }

    // 软阴影：PCF 围绕方向做 4 次小偏移采样
    float shadow = 0.0;
    float offset = 0.02;
    vec2 offsets[4] = vec2[](
        vec2(1.0, 1.0), vec2(-1.0, 1.0),
        vec2(1.0, -1.0), vec2(-1.0, -1.0));
    for (int i = 0; i < 4; i++)
    {
        vec3 s = normalize(dir + vec3(offsets[i] * offset, 0.0));
        float d = texture(uPointShadowMap, s).r;
        shadow += ((currentDepth - bias) > d) ? 0.0 : 1.0;
    }
    return shadow / 4.0;
}

// 对第 i 盏灯计算阴影因子（根据该灯的阴影类型分发）
float ShadowFactorForLight(int lightIndex, vec3 worldPos)
{
    if (lightIndex < 0 || lightIndex >= 32)
        return 1.0;
    GPULight L = uLights.lights[lightIndex];
    if (L.castsShadow < 0.5)
        return 1.0;

    if (L.shadowType == 1) // 点光源
    {
        return PointShadowFactor(worldPos);
    }
    else // 方向光/聚光：2D shadow map
    {
        return Shadow2DPCF(L.shadowMapIndex, worldPos);
    }
}

// ============================================================================
// 环境贴图 IBL 辅助
// ============================================================================

// 绕 Y 轴旋转方向（环境方位角），用于统一旋转天空盒与物体 IBL 的采样方向
vec3 RotateYaw(vec3 d, float y)
{
    float c = cos(y);
    float s = sin(y);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

// Schlick 菲涅尔（高光 IBL / 反射）
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 根据环境模式计算“环境项”（IBL ambient）：
//   模式 1 = 反射：用反射向量 R=reflect(-V,N) 采样环境 cubemap（近似镜面反射）
//   模式 2 = 漫反射 IBL：irradiance map × 漫反射系数 kD × AO
//   模式 3 = 漫反射 + 高光 IBL：上述漫反射 + 预过滤 cubemap × BRDF LUT（split-sum）
vec3 ComputeEnvAmbient(vec3 N, vec3 V, vec3 albedo)
{
    int envMode = int(uEnv.params0.y);
    float yaw = uEnv.params1.z;
    // 每对象材质参数（M1）：粗糙度/金属度/AO 来自 push constant 而非全局 EnvUBO
    float roughness = clamp(pc.material.x, 0.0, 1.0);
    float metallic = clamp(pc.material.y, 0.0, 1.0);
    float ao = pc.material.z;
    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 模式 1：反射
    if (envMode == 1)
    {
        vec3 R = reflect(-V, N);
        return texture(uEnvMap, RotateYaw(R, yaw)).rgb;
    }

    // 模式 2 / 3：漫反射 IBL（irradiance）
    vec3 irradiance = texture(uIrradianceMap, RotateYaw(N, yaw)).rgb;
    vec3 diffuse = irradiance * albedo;
    vec3 kS = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = 1.0 - kS;
    vec3 ambient = kD * diffuse * ao;

    // 模式 3：高光 IBL（split-sum）
    if (envMode >= 3)
    {
        vec3 R = reflect(-V, N);
        int mipCount = max(int(uEnv.params0.z), 1);
        vec3 prefiltered = textureLod(uPrefilteredMap, RotateYaw(R, yaw),
                                      roughness * float(mipCount - 1)).rgb;
        vec2 brdf = texture(uBRDFLUT, vec2(NdotV, roughness)).rg;
        vec3 specular = prefiltered * (F0 * brdf.x + brdf.y);
        ambient += specular;
    }

    return ambient;
}

// 最终颜色：可选曝光 + Reinhard tone map（避免 HDR 环境过曝成纯白）
vec3 ToneMapColor(vec3 color)
{
    if (uEnv.params2.x < 0.5)
        return color;
    vec3 c = color * uEnv.params1.w; // 曝光
    return c / (c + vec3(1.0));
}

void main()
{
    // 阴影绘制：输出扁平深色（平面投影阴影）
    if (pc.shadow.w != 0.0)
    {
        FragColor = vec4(0.03, 0.03, 0.05, 1.0);
        return;
    }

    vec3 N = normalize(vNormal);

    // ---- shadowmap 调试视图 ----
    // 把当前世界坐标投影到灯光空间，采样 shadow map 深度并以伪彩色显示，
    // 便于检查 shadow map 内容 / 视锥覆盖范围。
    if (uShadow.shadowDebugView == 1)
    {
        // 显示第一张启用的 2D shadow map（方向光/聚光）
        int mapIdx = -1;
        for (int k = 0; k < MAX_SHADOW_MAPS; k++)
        {
            if (uShadow.lights[k].enabled > 0.5)
            {
                mapIdx = k;
                break;
            }
        }
        if (mapIdx < 0)
        {
            FragColor = vec4(0.2, 0.0, 0.0, 1.0);
            return;
        }
        vec4 clip = uShadow.lights[mapIdx].lightViewProj * vec4(vWorldPos, 1.0);
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        {
            FragColor = vec4(1.0, 0.0, 1.0, 1.0); // 视锥外洋红色（区别于深度伪彩）
            return;
        }
        float d = texture(uShadowMaps[mapIdx], uv).r;
        // 伪彩色：近(0)蓝 → 中绿 → 远(1)红
        vec3 col = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), clamp(d * 2.0, 0.0, 1.0));
        col = mix(col, vec3(1.0, 0.0, 0.0), clamp(d * 2.0 - 1.0, 0.0, 1.0));
        FragColor = vec4(col, 1.0);
        return;
    }
    else if (uShadow.shadowDebugView == 2)
    {
        // 显示点光源 cubemap 深度（方向向量采样）
        if (uPointShadow.enabled < 0.5)
        {
            FragColor = vec4(0.2, 0.0, 0.0, 1.0);
            return;
        }
        vec3 dir = vWorldPos - uPointShadow.lightPos.xyz;
        float d = texture(uPointShadowMap, normalize(dir)).r;
        vec3 col = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), clamp(d * 2.0, 0.0, 1.0));
        col = mix(col, vec3(1.0, 0.0, 0.0), clamp(d * 2.0 - 1.0, 0.0, 1.0));
        FragColor = vec4(col, 1.0);
        return;
    }

    // ---- 每对象材质纹理（M2）：textureIndex>=0 采样纹理 × tint，否则纯色 ----
    vec3 albedo = pc.color.rgb;
    int texIdx = int(pc.material.w);
    if (texIdx >= 0 && texIdx < MAX_MATERIAL_TEXTURES)
        albedo = texture(uMaterialTextures[texIdx], vUV).rgb * albedo;

    vec3 lit = vec3(0.0);
    int count = clamp(uLights.lightCount, 0, 32);
    for (int i = 0; i < count; i++)
    {
        float shadow = ShadowFactorForLight(i, vWorldPos);
        lit += CalcLight(uLights.lights[i], N) * shadow;
    }

    // 没有灯光时回退到默认方向光，保证场景仍可见（如无场景的演示）
    if (count == 0)
    {
        vec3 lightDir = normalize(vec3(0.5, 0.8, 0.4));
        float ndl = max(dot(N, lightDir), 0.0);
        lit = vec3(0.5, 0.8, 0.4) * (0.35 + 0.65 * ndl);
    }

    // ---- 环境贴图（多种 IBL 模式）----
    vec3 final;
    int envMode = int(uEnv.params0.y);
    if (envMode == 0)
    {
        // 关闭环境贴图：保持原来的固定环境光
        final = albedo * 0.12 + albedo * lit;
    }
    else
    {
        // 环境 IBL 作为环境项，与直接光叠加
        vec3 camPos = uEnv.params2.yzw;
        vec3 V = normalize(camPos - vWorldPos);
        vec3 ambient = ComputeEnvAmbient(N, V, albedo);
        final = ambient * uEnv.params0.x + albedo * lit;
    }

    FragColor = vec4(ToneMapColor(final), pc.color.a);
}
