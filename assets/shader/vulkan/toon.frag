#version 450 core

// M3 自定义材质管线示例：卡通（toon/cel-shading）片元着色器。
// 说明：自定义 shader 可以访问共享描述集的全部绑定——
//   binding 1  = 灯光 UBO
//   binding 7  = 漫反射 irradiance cubemap（环境 IBL）
//   binding 10 = 环境 UBO（模式 / 强度 / 相机位置 / tone map）
//   binding 11 = 每对象材质纹理数组（pc.material.w 索引）
//   binding 12 = 自定义材质参数（M4：OpenGL 风格 SetUniform(key,type,value) 打包）
//   push constant = mat4 model + vec4 color + vec4 shadow + vec4 material（112B）
// 光照被量化成少数几个色带（cel shading），与默认 PBR mesh shader 形成明显差异。
//
// M4：自定义材质参数 —— CPU 端用 Material::SetUniform(key,type,value) 设置，
// 按注册顺序打包到 params[i]（每个 uniform 一个 vec4，mat4 占 4 个）。
// 本 shader 的约定（与 demo 中 ground 材质的 SetUniform 顺序一致）：
//   params[0].x   = bandThresh（卡通量化阈值，float）
//   params[1].x   = specPow（高光幂，float）
//   params[2].x   = rimStrength（边缘暗化强度，float）
//   params[3].rgb = toonTint（卡通染色，vec3f）
// 参数为 0 时回退到内建默认值（便于“未设置参数也能渲染”）。

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
    int castsShadow;
    int shadowMapIndex;
    int shadowType;
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

const int MAX_MATERIAL_TEXTURES = 16;
layout(set = 0, binding = 11) uniform sampler2D uMaterialTextures[MAX_MATERIAL_TEXTURES];

layout(set = 0, binding = 7) uniform samplerCube uIrradianceMap;

layout(std140, set = 0, binding = 10) uniform EnvUBO {
    vec4 params0; // x=强度, y=模式, z=mip级数, w=AO
    vec4 params1; // x=粗糙度, y=金属度, z=方位角, w=曝光
    vec4 params2; // x=tonemap, yzw=相机位置
} uEnv;

// M4：自定义材质参数（动态 UBO）。CPU 端按注册顺序打包，params[i] 与之对应。
layout(std140, set = 0, binding = 12) uniform MaterialParams {
    vec4 params[8];
} uMatParams;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 shadow;
    vec4 material; // x=粗糙度, y=金属度, z=AO, w=纹理索引
} pc;

layout(location = 0) out vec4 FragColor;

// M4：自定义参数（回退默认：值为 0 时用内建默认）
float kBandThresh = uMatParams.params[0].x > 0.001 ? uMatParams.params[0].x : 0.75;
float kSpecPow    = uMatParams.params[1].x > 0.001 ? uMatParams.params[1].x : 24.0;
float kRim        = uMatParams.params[2].x;
vec3  kToonTint   = uMatParams.params[3].rgb;

// 卡通量化：把连续漫反射强度分成 4 档
float ToonQuantize(float v)
{
    if (v > kBandThresh)       return 1.0;
    if (v > kBandThresh * 0.47) return 0.72;
    if (v > kBandThresh * 0.13) return 0.45;
    return 0.16;
}

void main()
{
    vec3 N = normalize(vNormal);

    // 每对象材质纹理（M2，与 mesh.frag 一致）
    vec3 albedo = pc.color.rgb;
    int texIdx = int(pc.material.w);
    if (texIdx >= 0 && texIdx < MAX_MATERIAL_TEXTURES)
        albedo = texture(uMaterialTextures[texIdx], vUV).rgb * albedo;
    albedo *= kToonTint; // M4：卡通染色

    // 取第一盏方向光作为卡通主光（没有则用默认方向）
    vec3 L = normalize(vec3(0.5, 0.8, 0.4));
    bool hasDir = false;
    int count = clamp(uLights.lightCount, 0, 32);
    for (int i = 0; i < count; i++)
    {
        if (uLights.lights[i].type == 0)
        {
            L = normalize(vec3(uLights.lights[i].direction[0],
                               uLights.lights[i].direction[1],
                               uLights.lights[i].direction[2]));
            hasDir = true;
            break;
        }
    }

    float ndl = max(dot(N, L), 0.0);
    float bands = ToonQuantize(ndl);

    // 卡通高光：Blinn-Phong，超过阈值即点亮（硬边界）
    vec3 camPos = uEnv.params2.yzw;
    vec3 V = normalize(camPos - vWorldPos);
    vec3 H = normalize(L + V);
    float ndh = max(dot(N, H), 0.0);
    float spec = pow(ndh, kSpecPow) > 0.6 ? 1.0 : 0.0;

    // 环境项：模式>=2 用漫反射 irradiance IBL，否则固定环境光
    vec3 ambient = albedo * 0.12;
    if (uEnv.params0.y >= 2.0)
    {
        vec3 irradiance = texture(uIrradianceMap, N).rgb;
        ambient = albedo * irradiance * uEnv.params0.x;
    }

    vec3 final = ambient + albedo * bands * 1.3 + vec3(spec) * 0.9;

    // 边缘暗化（模拟描边感）：法线朝相机越垂直越暗
    float rim = 1.0 - max(dot(N, V), 0.0);
    final *= 1.0 - rim * kRim;

    // tone map（与 mesh.frag 一致，避免 HDR 过曝）
    if (uEnv.params2.x >= 0.5)
        final = final / (final + vec3(1.0));

    FragColor = vec4(final, pc.color.a);
}
