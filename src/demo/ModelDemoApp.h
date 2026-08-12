#pragma once

// ============================================================================
// ModelDemoApp —— 基于 App 框架的 Model 双后端演示（对标 GsEditor 的写法）
// ----------------------------------------------------------------------------
// 继承 App，由 App::Run() 驱动主循环；场景绘制走 App::Render()（OpenGL 用框架
// Renderer，Vulkan 用 VulkanRenderAPI::SubmitSceneMesh）。
// ============================================================================

#include <memory>
#include <string>
#include <vector>

// GLEW 必须先于任何 OpenGL 头（GLFW 在 macOS 默认引入 OpenGL/gl.h）包含
#include <GL/glew.h>

#include "Aster/Core/App.h"
#include "Aster/Scene/Model.h"
#include "Aster/Resource/Mesh.h"
#include "Aster/Resource/Material.h"
#include "Aster/Resource/EnvironmentMap.h"

// 框架类型位于 namespace aster，这里显式引入用到的类型
using aster::App;
using aster::EnvironmentMap;
using aster::Material;
using aster::Mesh;
using aster::Model;
using aster::RenderAPIType;

class ModelDemoApp : public App
{
public:
    ModelDemoApp(int width = 1200, int height = 800,
                 const std::string &title = "Aster Model Demo",
                 RenderAPIType apiType = RenderAPIType::Vulkan)
        : App(width, height, title, apiType)
    {
    }

protected:
    bool InitScene() override;    // 构建程序化 icosphere + 材质，加入场景
    void Update() override;       // 场景更新 + 模型自转
    void RenderImGui() override;  // 后端信息 / 顶点数 / 颜色面板

private:
    // 注：场景对象（mesh/model/material/grid 等）由 InitScene 以局部对象创建并交给
    // 场景持有，demo 不再持有强引用 —— 删除对象后引用计数能真正归零、资源可被回收。
    // 需要访问时通过 SceneManager::GetObject<Model>(name) 按名查询。
    int toonPipeline = -1;       // M3：自定义 toon 管线索引（地面材质使用）
    // ---- M4：地面（toon）材质的自定义 uniform（SetUniform(key,type,value)）----
    float toonBandThresh = 0.75f;  // params[0].x 卡通量化阈值
    float toonSpecPow = 24.0f;     // params[1].x 高光幂
    float toonRim = 0.3f;          // params[2].x 边缘暗化强度
    glm::vec3 toonTint = glm::vec3(1.0f); // params[3].rgb 卡通染色

    // ---- 地面网格线（自定义 grid shader，M4 自定义 uniform）----
    int gridPipeline = -1;               // 自定义 grid 管线索引（混合开启 / 深度写关闭）
    glm::vec3 gridColor = glm::vec3(0.10f, 0.85f, 0.45f); // params[0].rgb 线框颜色
    float gridOpacity = 0.6f;            // params[1].x 透明度
    float gridCellSize = 1.0f;           // params[2].x 格子边长（世界单位）
    float gridFadeStart = 10.0f;         // params[3].x 距离淡出起点
    float gridFadeEnd = 120.0f;          // params[4].x 距离淡出终点
    float gridLineWidth = 0.04f;         // params[5].x 线宽（相对格子比例）
    float rotationTime = 0.0f;
    bool softShadow = true;      // 软阴影开关（ImGui 按钮切换）
    int shadowDebugView = 0;     // shadowmap 调试视图（0=正常，1=2D，2=点光源 cubemap）

    // ---- 鼠标拾取（id map）----
    const Model *pickedModel = nullptr; // 最近一次左键点击命中的可拾取对象
    bool prevLeftDown_ = false;         // 上一帧左键状态（边沿检测，避免按住时每帧触发）
    float deleteDelay = 0.0f;           // 删除延迟（秒），演示 SceneManager::Destroy(obj, delay)

    // ---- 环境贴图（HDR IBL）控制 ----
    std::shared_ptr<EnvironmentMap> environmentMap; // 加载自 assets/HDRIs/*.exr
    int envMode = 3;             // 0=关闭, 1=反射, 2=漫反射 IBL, 3=漫反射+高光 IBL
    float envIntensity = 1.0f;   // 环境光强度
    float envRoughness = 0.35f;  // 材质粗糙度（高光 IBL）
    float envMetallic = 0.0f;    // 金属度
    float envAO = 1.0f;          // 环境光遮蔽
    float envYawDeg = 0.0f;      // 环境方位角（度，ImGui 用）
    float envExposure = 1.0f;    // 曝光
    bool envToneMap = true;      // 是否 tone map

    // 程序化 icosphere（无需 assimp）；radius 为半径
    static void BuildIcoSphere(int subdivisions,
                               std::vector<float> &positions,
                               std::vector<float> &normals,
                               std::vector<unsigned int> &indices,
                               float radius = 1.0f);

    // 程序化平面（100x100 地面），法线朝上
    static void BuildPlane(float size,
                           std::vector<float> &positions,
                           std::vector<float> &normals,
                           std::vector<unsigned int> &indices);
};
