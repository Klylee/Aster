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
#include "Aster/Render/Renderer.h"
#include "Aster/Core/Path.h"
#include "Aster/Lighting/Light.h"
#include "Aster/Core/GlobalTime.h"
#include "Aster/Scene/SceneManager.h"

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
    }

    // 材质工厂：Vulkan 用单色（无 shader），OpenGL 用 env_ibl.shader
    // （环境贴图 IBL 版；uEnvMode=0 时退化为单色，与旧 transparent.shader 一致）。
    auto makeMaterial = [&](const glm::vec4 &c) -> std::shared_ptr<Material>
    {
        std::shared_ptr<Material> mat;
        if (renderAPI && renderAPI->IsOpenGL())
        {
            auto shader = std::make_shared<Shader>(std::unordered_map<ShaderVariant, std::string>{
                {ShaderVariant::Basic, Path(ROOT_DIR) + "assets/shader/env_ibl.shader"}});
            mat = std::make_shared<Material>(shader);
            // 环境贴图纹理单元（与 OpenGLEnvironment::BindSceneTextures 一致）
            mat->SetUniform("uEnvMap", "int", 6);
            mat->SetUniform("uIrradianceMap", "int", 7);
            mat->SetUniform("uPrefilteredMap", "int", 8);
            mat->SetUniform("uBRDFLUT", "int", 9);
        }
        else
        {
            mat = std::make_shared<Material>();
        }
        mat->color = c;
        mat->SetUniform("color", "vec4f", c); // OpenGL 的 color uniform
        return mat;
    };

    // ---- 地面：100x100 平面（接收阴影，不投影） ----
    std::vector<float> pPos, pNor;
    std::vector<unsigned int> pIdx;
    BuildPlane(100.0f, pPos, pNor, pIdx);
    auto planeMesh = std::make_shared<Mesh>(pPos, pIdx, pNor);
    planeMesh->initialize();
    auto planeModel = std::make_shared<Model>();
    planeModel->objName = "ground";
    planeModel->castsShadow = false; // 地面是接收体，不投影阴影
    planeModel->meshes.push_back(planeMesh);
    planeModel->material = makeMaterial(glm::vec4(0.55f, 0.55f, 0.58f, 1.0f));
    groundMaterial = planeModel->material; // 保存引用（OpenGL 每帧更新环境 uniform）
    SceneManager::Instance().AddObject(planeModel);

    // ---- 棱角球：半径 2，位于地面 (0,2,0)，低细分让棱角与阴影轮廓可见 ----
    std::vector<float> positions, normals;
    std::vector<unsigned int> indices;
    BuildIcoSphere(1, positions, normals, indices, 2.0f); // 细分 1 = 明显棱角

    mesh = std::make_shared<Mesh>(positions, indices, normals);
    mesh->initialize(); // Vulkan 后端下内部跳过 OpenGL 缓冲创建

    model = std::make_shared<Model>();
    model->objName = "icosphere";
    model->castsShadow = true;
    model->meshes.push_back(mesh);
    material = makeMaterial(glm::vec4(1.0f, 0.62f, 0.25f, 1.0f));
    model->material = material;
    model->transform.SetPosition(Vec3(0.0f, 2.0f, 0.0f)); // 半径 2 落在地面 y=0
    SceneManager::Instance().AddObject(model);

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
}

void ModelDemoApp::RenderImGui()
{
    ImGui::ShowDemoWindow();
    ImGui::Begin("Model Demo");
    ImGui::Text("Active backend: %s", renderAPI->Name());
    ImGui::Text("Vertices: %d, Indices: %d", mesh->v_num, mesh->i_num);
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
    ImGui::SliderFloat("Roughness", &envRoughness, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Metallic", &envMetallic, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("AO", &envAO, 0.0f, 1.0f, "%.2f");
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
