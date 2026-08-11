#include "ModelDemo.h"
#include "ModelDemoApp.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>

#include <env_config.h> // ROOT_DIR（由 CMake configure_file 生成）

#include "Aster/Resource/Shader.h"
#include "Aster/Resource/MaterialManager.h"
#include "Aster/Render/Renderer.h"
#include "Aster/Core/Path.h"
#include "Aster/Lighting/Light.h"
#include "Aster/Core/GlobalTime.h"
#include "Aster/Scene/SceneManager.h"
#ifdef ASTER_ENABLE_VULKAN
#include "Aster/Render/Vulkan/VulkanRenderAPI.h"
#endif

using namespace aster;

// ============================================================================
// 程序化 icosphere（细分）—— 函数体与旧版一致，改为 ModelDemoApp 静态成员
// ============================================================================
void ModelDemoApp::BuildIcoSphere(int subdivisions, std::vector<float> &positions,
                                  std::vector<float> &normals,
                                  std::vector<unsigned int> &indices,
                                  float radius)
{
    // 正二十面体 12 顶点 / 20 三角形
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<glm::vec3> verts = {
        glm::normalize(glm::vec3(-1, t, 0)), glm::normalize(glm::vec3(1, t, 0)),
        glm::normalize(glm::vec3(-1, -t, 0)), glm::normalize(glm::vec3(1, -t, 0)),
        glm::normalize(glm::vec3(0, -1, t)), glm::normalize(glm::vec3(0, 1, t)),
        glm::normalize(glm::vec3(0, -1, -t)), glm::normalize(glm::vec3(0, 1, -t)),
        glm::normalize(glm::vec3(t, 0, -1)), glm::normalize(glm::vec3(t, 0, 1)),
        glm::normalize(glm::vec3(-t, 0, -1)), glm::normalize(glm::vec3(-t, 0, 1)),
    };
    std::vector<std::array<int, 3>> faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };

    // 边中点缓存（细分时共享顶点）
    std::unordered_map<uint64_t, int> midCache;
    auto midIndex = [&](int a, int b) -> int
    {
        if (a > b)
            std::swap(a, b);
        uint64_t key = (uint64_t(a) << 32) | uint32_t(b);
        auto it = midCache.find(key);
        if (it != midCache.end())
            return it->second;
        glm::vec3 m = glm::normalize((verts[a] + verts[b]) * 0.5f);
        verts.push_back(m);
        int idx = (int)verts.size() - 1;
        midCache[key] = idx;
        return idx;
    };

    for (int sub = 0; sub < subdivisions; ++sub)
    {
        std::vector<std::array<int, 3>> newFaces;
        newFaces.reserve(faces.size() * 4);
        for (const auto &f : faces)
        {
            int a = midIndex(f[0], f[1]);
            int b = midIndex(f[1], f[2]);
            int c = midIndex(f[2], f[0]);
            newFaces.push_back({f[0], a, c});
            newFaces.push_back({f[1], b, a});
            newFaces.push_back({f[2], c, b});
            newFaces.push_back({a, b, c});
        }
        faces.swap(newFaces);
    }

    positions.reserve(verts.size() * 3);
    normals.reserve(verts.size() * 3);
    for (const auto &v : verts)
    {
        positions.push_back(v.x * radius);
        positions.push_back(v.y * radius);
        positions.push_back(v.z * radius);
        normals.push_back(v.x); // 球面法线 = 归一化位置
        normals.push_back(v.y);
        normals.push_back(v.z);
    }
    indices.reserve(faces.size() * 3);
    for (const auto &f : faces)
    {
        indices.push_back((unsigned int)f[0]);
        indices.push_back((unsigned int)f[1]);
        indices.push_back((unsigned int)f[2]);
    }
}

// ============================================================================
// 程序化平面（地面）：size x size，位于 y=0，法线朝上 (0,1,0)
// ============================================================================
void ModelDemoApp::BuildPlane(float size, std::vector<float> &positions,
                              std::vector<float> &normals,
                              std::vector<unsigned int> &indices)
{
    float h = size * 0.5f;
    const float P[4][3] = {
        {-h, 0.0f, -h},
        {h, 0.0f, -h},
        {h, 0.0f, h},
        {-h, 0.0f, h},
    };
    for (int i = 0; i < 4; i++)
    {
        positions.push_back(P[i][0]);
        positions.push_back(P[i][1]);
        positions.push_back(P[i][2]);
        normals.push_back(0.0f);
        normals.push_back(1.0f);
        normals.push_back(0.0f);
    }
    indices = {0, 2, 1, 0, 3, 2}; // 两个三角形，逆时针（从上方看）
}

// ============================================================================
// App 框架回调
// ============================================================================
bool ModelDemoApp::InitScene()
{
    // 相机：俯视地面与球
    if (auto cam = SceneManager::Instance().GetMainCamera())
    {
        cam->transform.SetPosition(Vec3(0.0f, 10.0f, 20.0f));
        Vec3 lookTarget(0.0f, 2.0f, 0.0f);
        Vec3 dir = glm::normalize(lookTarget - cam->transform.GetPosition());
        cam->transform.Rotate(Vec3(0.0f, 0.0f, -1.0f), dir);
        cam->speed = 4.0f;
    }

    // ---- 全局材质管理器（MaterialManager）：Shader / 材质复用 ----
    // OpenGL 后端注册 env_ibl shader（Vulkan 后端无需 shader）。
    auto &mm = MaterialManager::Instance();
    if (renderAPI && renderAPI->IsOpenGL())
        mm.RegisterShader("env_ibl",
                          Path(ROOT_DIR) + "assets/shader/opengl/env_ibl.shader");

    // 通过管理器注册“基础材质”（OpenGL 用 env_ibl shader + env 纹理单元；
    // Vulkan 用无 shader 程序化材质）。同名幂等：重复注册返回缓存实例。
    auto registerMaterial = [&](const std::string &name,
                                const glm::vec4 &c) -> std::shared_ptr<Material>
    {
        std::shared_ptr<Material> mat;
        if (renderAPI && renderAPI->IsOpenGL())
            mat = mm.RegisterMaterial(name, "env_ibl", c);
        else
            mat = mm.RegisterMaterial(name, c);
        if (renderAPI && renderAPI->IsOpenGL())
        {
            // 环境贴图纹理单元（与 OpenGLEnvironment::BindSceneTextures 一致）
            mat->SetUniform("uEnvMap", "int", 6);
            mat->SetUniform("uIrradianceMap", "int", 7);
            mat->SetUniform("uPrefilteredMap", "int", 8);
            mat->SetUniform("uBRDFLUT", "int", 9);
        }
        return mat;
    };

    // ---- 地面：100x100 平面（接收阴影，不投影） ----
    std::vector<float> pPos, pNor;
    std::vector<unsigned int> pIdx;
    BuildPlane(100.0f, pPos, pNor, pIdx);
    auto planeMesh = std::make_shared<Mesh>(pPos, pIdx, pNor);
    planeMesh->initialize();
    // M2：平面 UV（0..1 覆盖整张地面，供材质纹理采样）
    for (int i = 0; i < planeMesh->v_num; i++)
    {
        float x = planeMesh->vertices[i * 8 + 0];
        float z = planeMesh->vertices[i * 8 + 2];
        planeMesh->vertices[i * 8 + 6] = x * 0.02f + 0.5f;
        planeMesh->vertices[i * 8 + 7] = z * 0.02f + 0.5f;
    }
    auto planeModel = std::make_shared<Model>();
    planeModel->objName = "ground";
    planeModel->castsShadow = false; // 地面是接收体，不投影阴影
    planeModel->meshes.push_back(planeMesh);
    planeModel->material = registerMaterial("ground", glm::vec4(0.55f, 0.55f, 0.58f, 1.0f));
    groundMaterial = planeModel->material; // 保存引用（OpenGL 每帧更新环境 uniform）
    // M1：地面设为哑光材质（roughness=1 不反射环境），体现每对象材质差异
    groundMaterial->roughness = 1.0f;
    groundMaterial->metallic = 0.0f;
    groundMaterial->ao = 1.0f;

    // M3：创建自定义 toon（卡通）管线并赋给地面材质 → 地面用卡通 shader，球体用默认 PBR mesh shader
#ifdef ASTER_ENABLE_VULKAN
    if (renderAPI && renderAPI->IsVulkan())
    {
        auto *vk = static_cast<VulkanRenderAPI *>(renderAPI);
        toonPipeline = vk->CreateMaterialPipeline("toon.vert", "toon.frag");
        if (toonPipeline > 0)
            groundMaterial->vulkanPipeline = toonPipeline;

        // M4：自定义材质 uniform（OpenGL 风格 SetUniform(key,type,value)）。
        // 注册顺序 = toon.frag 中 MaterialParams.params[i] 的约定顺序：
        //   params[0].x=bandThresh / params[1].x=specPow / params[2].x=rimStrength /
        //   params[3].rgb=toonTint。Vulkan 后端打包到 binding 12 动态 UBO。
        groundMaterial->SetUniform("bandThresh", "float", toonBandThresh);
        groundMaterial->SetUniform("specPow", "float", toonSpecPow);
        groundMaterial->SetUniform("rimStrength", "float", toonRim);
        groundMaterial->SetUniform("toonTint", "vec3f", toonTint);
    }
#endif

    SceneManager::Instance().AddObject(planeModel);

    // ---- 棱角球：半径 2，位于地面 (0,2,0)，低细分让棱角与阴影轮廓可见 ----
    std::vector<float> positions, normals;
    std::vector<unsigned int> indices;
    BuildIcoSphere(1, positions, normals, indices, 2.0f); // 细分 1 = 明显棱角

    mesh = std::make_shared<Mesh>(positions, indices, normals);
    mesh->initialize(); // Vulkan 后端下内部跳过 OpenGL 缓冲创建

    // M2：icosphere 球面 UV（等距柱状投影，供材质纹理采样）
    {
        const double kPi = 3.14159265358979323846;
        for (int i = 0; i < mesh->v_num; i++)
        {
            glm::vec3 d(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
            d = glm::normalize(d);
            mesh->vertices[i * 8 + 6] = (float)(0.5 + std::atan2(d.z, d.x) / (2.0 * kPi));
            mesh->vertices[i * 8 + 7] = (float)(0.5 - std::asin(d.y) / kPi);
        }
    }

    model = std::make_shared<Model>();
    model->objName = "icosphere";
    model->castsShadow = true;
    model->meshes.push_back(mesh);
    material = registerMaterial("sphere", glm::vec4(1.0f, 0.62f, 0.25f, 1.0f));
    model->material = material;
    // M1：球体材质参数由滑块控制（每对象，与地面哑光材质不同）
    material->roughness = envRoughness;
    material->metallic = envMetallic;
    material->ao = envAO;

    // M2：棋盘材质纹理（RGBA8 256x256）——球体使用纹理，地面保持纯色
#ifdef ASTER_ENABLE_VULKAN
    if (renderAPI && renderAPI->IsVulkan())
    {
        const int TEX = 256;
        std::vector<uint8_t> checker((size_t)TEX * TEX * 4);
        for (int y = 0; y < TEX; y++)
        {
            for (int x = 0; x < TEX; x++)
            {
                bool light = ((x / 32) + (y / 32)) % 2 == 0;
                uint8_t c = light ? 230 : 40;
                size_t p = ((size_t)y * TEX + x) * 4;
                checker[p + 0] = c;
                checker[p + 1] = c;
                checker[p + 2] = c;
                checker[p + 3] = 255;
            }
        }
        auto *vk = static_cast<VulkanRenderAPI *>(renderAPI);
        int texIdx = vk->RegisterMaterialTexture(checker.data(), TEX, TEX);
        if (texIdx >= 0)
            material->textureIndex = texIdx; // 球体使用棋盘纹理
    }
#endif

    model->transform.SetPosition(Vec3(0.0f, 2.0f, 0.0f)); // 半径 2 落在地面 y=0
    SceneManager::Instance().AddObject(model);

    // ---- 第二颗球：MaterialManager 材质实例（共享 "sphere" 的 shader，参数独立） ----
    // 演示“物体 A / B 用同一 shader，只是颜色 / 粗糙度不同”
    material2 = mm.CreateMaterialInstance("sphere_blue", "sphere");
    if (material2)
    {
        material2->color = glm::vec4(0.30f, 0.55f, 0.95f, 1.0f); // 蓝色
        material2->roughness = 0.85f;                           // 高粗糙度（哑光）
        material2->metallic = 0.1f;
        material2->ao = 1.0f;
        material2->textureIndex = -1;   // 纯色（不用棋盘纹理）
        material2->SetUniform("color", "vec4f", material2->color); // OpenGL uniform
    }
    secondModel = std::make_shared<Model>();
    secondModel->objName = "sphereB";
    secondModel->castsShadow = true;
    secondModel->meshes.push_back(mesh); // 复用同一 icosphere 网格（Mesh 复用）
    secondModel->material = material2;
    secondModel->transform.SetPosition(Vec3(7.0f, 2.0f, 0.0f));
    secondModel->transform.SetScale(Vec3(0.7f)); // 稍小，与主球区分
    SceneManager::Instance().AddObject(secondModel);

    // ---- 聚光灯：远处照向球（同时作为阴影投影光源） ----
    auto spotLight = std::make_shared<SpotLight>();
    spotLight->objName = "keySpot";
    spotLight->color = glm::vec3(1.0f, 0.1f, 0.9f);
    // 强度不要过大：mesh.frag 中 lit = color*intensity*ndl*att，intensity=500 时
    // 即使距离 16.7 衰减后仍 >1，整个球/地面过曝成白色，阴影被淹没。
    // 用户验证 intensity≈10 时光影正常。
    spotLight->intensity = 1.0f;
    // 球心(0,2,0) 到灯(10,14,6) 距离约 16.7；range 必须大于该距离，
    // 否则衰减为 0，球与地面都不被照亮，阴影不可见。
    spotLight->range = 60.0f;
    spotLight->direction =
        glm::normalize(glm::vec3(0.0f, 2.0f, 0.0f) - glm::vec3(10.0f, 14.0f, 6.0f));
    spotLight->innerCone = glm::radians(20.0f);
    spotLight->outerCone = glm::radians(35.0f);
    spotLight->transform.SetPosition(Vec3(10.0f, 14.0f, 6.0f));
    SceneManager::Instance().AddObject(spotLight);

    // // ---- 方向光：斜上方补光（投射 2D shadow map 阴影） ----
    // auto dirLight = std::make_shared<DirectionalLight>();
    // dirLight->objName = "keyDir";
    // dirLight->color = glm::vec3(0.9f, 0.9f, 1.0f);
    // dirLight->intensity = 0.35f;
    // dirLight->direction = glm::normalize(glm::vec3(-1.0f, -2.0f, -0.6f)); // 从左上斜照
    // dirLight->transform.SetPosition(Vec3(20.0f, 40.0f, 10.0f)); // 方向光位置仅作阴影视锥参考
    // SceneManager::Instance().AddObject(dirLight);

    // ---- 点光源：放在球前方偏上，制造近距离的柔和阴影 ----
    // auto pointLight = std::make_shared<PointLight>();
    // pointLight->objName = "keyPoint";
    // pointLight->color = glm::vec3(1.0f, 0.85f, 0.7f);
    // pointLight->intensity = 10.0f;
    // pointLight->range = 25.0f;
    // pointLight->transform.SetPosition(Vec3(-6.0f, 8.0f, 4.0f));
    // SceneManager::Instance().AddObject(pointLight);

    // ---- 环境贴图：加载 HDR（.exr），生成 cubemap / irradiance / 预过滤 / BRDF LUT ----
    // 上传到后端后天空盒 + 多种 IBL 可用（Vulkan 后端；OpenGL 后端见 RenderAPI 默认实现）。
    if (renderAPI)
    {
        auto env = std::make_shared<EnvironmentMap>();
        std::string reason;
        if (env->LoadFromFile(Path(ROOT_DIR) + "assets/HDRIs/spiaggia_di_mondello_4k.exr", &reason))
        {
            environmentMap = env;
            renderAPI->SetEnvironmentMap(*env);
            renderAPI->SetEnvMode(envMode);
            renderAPI->SetEnvParams(envIntensity, envRoughness, envMetallic, envAO,
                                    glm::radians(envYawDeg), envExposure, envToneMap);
        }
        else
        {
            std::cerr << "[ModelDemo] Failed to load environment map: " << reason << std::endl;
        }
    }

    return true;
}

void ModelDemoApp::Update()
{
    App::Update(); // 场景对象更新（相机 WASD 控制等）

    // 软阴影开关 + shadowmap 调试视图同步到后端（按钮切换 + 初始状态）
    if (renderAPI)
    {
        renderAPI->SetSoftShadow(softShadow);
        renderAPI->SetShadowDebugView(shadowDebugView);

        // 环境贴图参数同步（模式 / 强度 / 粗糙度 / 金属度 / 方位角 / 曝光 / tone map）
        if (environmentMap)
        {
            renderAPI->SetEnvMode(envMode);
            renderAPI->SetEnvParams(envIntensity, envRoughness, envMetallic, envAO,
                                    glm::radians(envYawDeg), envExposure, envToneMap);

            // M1：每对象材质参数 —— 滑块控制球体材质（地面保持哑光 roughness=1）
            if (material)
            {
                material->roughness = envRoughness;
                material->metallic = envMetallic;
                material->ao = envAO;
            }

            // M4：Vulkan 后端每帧同步地面（toon）材质的自定义 uniform，
            // 滑块改动立即生效（打包到 binding 12 动态 UBO）。
            // 注：OpenGL 后端的地面用 env_ibl shader，不含这些参数，故跳过。
            if (renderAPI->IsVulkan() && groundMaterial && toonPipeline > 0)
            {
                groundMaterial->SetUniform("bandThresh", "float", toonBandThresh);
                groundMaterial->SetUniform("specPow", "float", toonSpecPow);
                groundMaterial->SetUniform("rimStrength", "float", toonRim);
                groundMaterial->SetUniform("toonTint", "vec3f", toonTint);
            }

            // OpenGL 后端：环境参数经材质 uniform 传给场景着色器（env_ibl.shader）
            if (renderAPI->IsOpenGL())
            {
                auto applyEnvTo = [&](const std::shared_ptr<Material> &m)
                {
                    if (!m)
                        return;
                    m->SetUniform("uEnvMode", "int", envMode);
                    m->SetUniform("uEnvIntensity", "float", envIntensity);
                    m->SetUniform("uEnvRoughness", "float", envRoughness);
                    m->SetUniform("uEnvMetallic", "float", envMetallic);
                    m->SetUniform("uEnvAO", "float", envAO);
                    m->SetUniform("uEnvYaw", "float", glm::radians(envYawDeg));
                    m->SetUniform("uEnvExposure", "float", envExposure);
                    m->SetUniform("uEnvToneMap", "float", envToneMap ? 1.0f : 0.0f);
                    if (auto cam = SceneManager::Instance().GetMainCamera())
                        m->SetUniform("uCamPos", "vec3f", cam->transform.GetPosition());
                };
                applyEnvTo(material);
                applyEnvTo(groundMaterial);
            }
        }
    }

    // 模型自转
    rotationTime += GlobalTime::GetFrameDeltaTime();
    model->transform.SetRotation(
        glm::angleAxis(rotationTime * 0.8f, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::angleAxis(rotationTime * 0.4f, glm::vec3(1.0f, 0.0f, 0.0f)));
    // 第二颗球以不同速度自转（材质实例，行为独立）
    if (secondModel)
    {
        secondModel->transform.SetRotation(
            glm::angleAxis(rotationTime * -0.5f, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(rotationTime * 0.3f, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
}

void ModelDemoApp::RenderImGui()
{
    // ImGui::ShowDemoWindow();
    ImGui::Begin("Model Demo");
    ImGui::Text("Active backend: %s", renderAPI->Name());
    ImGui::Text("Vertices: %d, Indices: %d", mesh->v_num, mesh->i_num);

    // ---- 全局材质管理器（MaterialManager）展示 ----
    auto &mm = MaterialManager::Instance();
    ImGui::Separator();
    ImGui::Text("MaterialManager: %zu shaders / %zu materials",
                mm.ShaderCount(), mm.MaterialCount());
    for (const auto &kv : mm.GetShaders())
        ImGui::BulletText("shader: %s", kv.first.c_str());
    for (const auto &kv : mm.GetMaterials())
    {
        const auto &m = kv.second;
        ImGui::BulletText("material: %s%s  (r=%.2f, m=%.2f, color=(%.2f,%.2f,%.2f))",
                          kv.first.c_str(),
                          m->baseName.empty() ? "" : (" <- " + m->baseName).c_str(),
                          m->roughness, m->metallic, m->color.r, m->color.g, m->color.b);
    }
    ImGui::TextWrapped("'sphere_blue' 是 'sphere' 的实例：共享同一 shader，颜色/粗糙度独立。");

    ImGui::Separator();
    ImGui::ColorEdit4("Material color", glm::value_ptr(material->color));

    // 软/硬阴影切换
    ImGui::Separator();
    if (ImGui::Button(softShadow ? "Soft Shadow: ON" : "Soft Shadow: OFF"))
    {
        softShadow = !softShadow;
        if (renderAPI)
            renderAPI->SetSoftShadow(softShadow);
    }
    ImGui::TextWrapped("PCF soft shadows on/off (Vulkan only).");

    // shadowmap 调试视图切换
    ImGui::Separator();
    const char *debugLabels[] = {"Normal", "2D ShadowMap", "Point ShadowMap"};
    int prevDebug = shadowDebugView;
    ImGui::Text("ShadowMap Debug View");
    for (int i = 0; i < 3; i++)
    {
        if (ImGui::RadioButton(debugLabels[i], shadowDebugView == i))
            shadowDebugView = i;
    }
    if (shadowDebugView != prevDebug && renderAPI)
        renderAPI->SetShadowDebugView(shadowDebugView);
    ImGui::TextWrapped("Visualize shadow map depth (blue=near, red=far).");

    // ---- M4：自定义材质 uniform（toon 地面）控制 ----
    // 通过 Material::SetUniform(key,type,value) 设置，Vulkan 打包到 binding 12 动态 UBO。
    ImGui::Separator();
    ImGui::Text("Custom Material Params (Vulkan, toon ground)");
    ImGui::TextWrapped("Set via Material::SetUniform(key, type, value); packed in "
                       "registration order into uMatParams.params[i] (binding 12).");
    if (renderAPI && renderAPI->IsVulkan() && toonPipeline > 0)
    {
        ImGui::SliderFloat("Toon Band Thresh", &toonBandThresh, 0.2f, 0.95f, "%.2f");
        ImGui::SliderFloat("Toon Spec Power", &toonSpecPow, 1.0f, 128.0f, "%.0f");
        ImGui::SliderFloat("Toon Rim Strength", &toonRim, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Toon Tint", glm::value_ptr(toonTint));
        ImGui::TextWrapped("params[0]=bandThresh, [1]=specPow, [2]=rim, [3]=tint. "
                           "Changes apply every frame.");
    }
    else
    {
        ImGui::TextWrapped("(Only available on the Vulkan backend with the toon pipeline.)");
    }

    // ---- 环境贴图（HDR IBL）控制 ----
    ImGui::Separator();
    ImGui::Text("Environment Map (HDR IBL)");
    ImGui::TextWrapped("Source: assets/HDRIs/spiaggia_di_mondello_4k.exr. "
                       "Loads env cubemap + irradiance + prefiltered + BRDF LUT.");
    if (environmentMap)
        ImGui::TextWrapped("Loaded: %s", environmentMap->GetName().c_str());
    else
        ImGui::TextWrapped("NOT loaded (see console).");

    const char *envLabels[] = {"Off", "Reflection", "Diffuse IBL", "Diffuse + Specular IBL (PBR)"};
    if (ImGui::Combo("Env Mode", &envMode, envLabels, 4))
        if (renderAPI)
            renderAPI->SetEnvMode(envMode);
    ImGui::SliderFloat("Env Intensity", &envIntensity, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Sphere Roughness", &envRoughness, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Sphere Metallic", &envMetallic, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Sphere AO", &envAO, 0.0f, 1.0f, "%.2f");
    ImGui::TextWrapped("Per-object material (M1): ground is matte (roughness=1), "
                       "sphere uses these sliders.");
    ImGui::SliderFloat("Env Yaw", &envYawDeg, -180.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat("Exposure", &envExposure, 0.1f, 5.0f, "%.2f");
    ImGui::Checkbox("Tone map (Reinhard)", &envToneMap);
    ImGui::TextWrapped("Multiple env approaches: Reflection / Diffuse IBL "
                       "(irradiance) / Specular IBL (prefiltered + BRDF LUT).");

    ImGui::TextWrapped("Model::draw() dispatches to the framework Renderer "
                       "(OpenGL) or the VulkanSceneRenderer (Vulkan).");
    ImGui::End();
}

// ============================================================================
// 入口（main_demo.cpp 仍调用 RunModelDemo()）
// ============================================================================
int RunModelDemo()
{
    RenderAPIType api = ResolveRenderAPIType(RenderAPIType::Vulkan);
    std::cout << "[ModelDemo] Requested backend: "
              << (api == RenderAPIType::Vulkan ? "Vulkan" : "OpenGL") << std::endl;

    auto app = std::make_shared<ModelDemoApp>(1200, 800, "Aster Model Demo", api);
    if (!app->Init())
    {
        std::cerr << "[ModelDemo] App init failed" << std::endl;
        return -1;
    }
    app->Run();
    app->Destroy();
    return 0;
}
