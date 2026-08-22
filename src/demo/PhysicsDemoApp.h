#pragma once

// ============================================================================
// PhysicsDemoApp —— Aster 物理模拟演示（App 子类）
// ----------------------------------------------------------------------------
// 按里程碑逐步扩展：
//   M1 运动学：2D 圆盘在重力下做抛物线（半隐式欧拉积分，RigidBody / PhysicsWorld2D）
//   M2 2D 碰撞：圆 / 矩形 + 检测（circle-circle/rect-rect/circle-rect）+ 冲量响应 + 调试线框
//   M3 3D 碰撞：球 / 盒（OBB，SAT）+ 响应 + 调试线框（地面 / 斜坡 / 堆叠）
//   M4 射线求交：鼠标射线拾取 / 拖拽（待实现）
//   M5 BVH 加速结构（待实现）
// 运行：ASTER_PHYSICS_DEMO=1 ./aster_demo（macOS 编译期强制 Vulkan）
// 2D / 3D 模式可在 ImGui 面板切换（重建场景）。
// ============================================================================

#include <memory>
#include <string>
#include <vector>

// GLEW 必须先于任何 OpenGL 头（GLFW 在 macOS 默认引入 OpenGL/gl.h）包含
#include <GL/glew.h>

#include "Aster/Core/App.h"
#include "Aster/Physics/Collider.h"
#include "Aster/Physics/CollisionShape.h"
#include "Aster/Physics/PhysicsWorld.h"
#include "Aster/Physics/Ray.h"
#include "Aster/Physics/RigidBody.h"

// 框架类型位于 namespace aster，这里显式引入用到的类型
using aster::App;
using aster::Collider2D;
using aster::Collider3D;
using aster::CollisionShape;
using aster::PhysicsWorld2D;
using aster::PhysicsWorld3D;
using aster::Ray;
using aster::RaycastHit;
using aster::RenderAPIType;
using aster::RigidBody;

class PhysicsDemoApp : public App
{
public:
    PhysicsDemoApp(int width = 1200, int height = 800,
                   const std::string &title = "Aster Physics Demo",
                   RenderAPIType apiType = RenderAPIType::Vulkan)
        : App(width, height, title, apiType)
    {
    }

protected:
    bool InitScene() override;   // 相机 + 初始模式场景
    void FixedUpdate() override; // 物理步进（App 已按 fixedTimeStep 恒定频率调用）
    void Update() override;      // 参数同步 + 空格冲量 + 安全复位
    void Render() override;      // App::Render() 场景绘制 + 提交物理调试线
    void RenderImGui() override; // 参数面板 + 模式切换 + 碰撞统计

private:
    // 通用动态体（2D / 3D 共用；kind 区分形状）。body / collider 是值成员，
    // PhysicsWorld 持有其指针 —— 必须用 unique_ptr 容器保证地址稳定。
    struct Body
    {
        std::shared_ptr<aster::Model> model;
        RigidBody body;
        std::shared_ptr<CollisionShape> shape;
        Collider2D collider2D;   // 2D 模式使用
        Collider3D collider3D;   // 3D 模式使用
        int kind = 0;            // 0=circle2D 1=rect2D 2=sphere3D 3=box3D
        float radius = 0.0f;     // circle / sphere 半径
        glm::vec2 half2{0.0f};   // rect2D 半边长
        glm::vec3 half3{0.0f};   // box3D 半边长
        glm::vec4 color{1.0f};   // 基础色（拾取高亮时恢复用）
    };
    struct StaticCollider2D
    {
        RigidBody body;
        std::shared_ptr<CollisionShape> shape;
        Collider2D collider;
    };
    struct StaticCollider3D
    {
        RigidBody body;
        std::shared_ptr<CollisionShape> shape;
        Collider3D collider;
        std::shared_ptr<aster::Model> model; // 可见模型（地面 / 斜坡 / 障碍）
    };

    PhysicsWorld2D world2D_;
    PhysicsWorld3D world3D_;
    std::vector<std::unique_ptr<Body>> bodies;               // 动态体
    std::vector<std::unique_ptr<StaticCollider2D>> walls2D_; // 2D 边框墙
    std::vector<std::unique_ptr<StaticCollider3D>> walls3D_; // 3D 地面 / 斜坡 / 障碍

    bool mode2D_ = true; // 当前模式（ImGui 切换重建场景）

    // ---- 参数 ----
    float gravityY = -9.81f;
    float linearDamping = 0.0f;
    float restitution = 0.6f;
    float friction = 0.6f;
    int circleCount = 8;   // 2D 圆盘
    int rectCount = 6;     // 2D 矩形
    int sphereCount = 6;   // 3D 球
    int boxCount = 4;      // 3D 盒

    // ---- 调试可视化 ----
    bool showShapeDebug = true; // 形状线框（白）
    bool showAABB = false;      // 世界 AABB（青）

    // ---- M5：BVH 宽相加速 ----
    bool useBVH = true;         // 同步到世界：dynamic-static 走 BVH（false = 全暴力对比）

    // ---- M4 射线拾取（鼠标 → 相机射线 → Raycast） ----
    Body *pickedBody_ = nullptr; // 最近命中的动态体（高亮）
    bool prevLeftDown_ = false;  // 上一帧左键状态（边沿检测）
    bool rayValid_ = false;      // 是否有本帧射线（调试绘制）
    Vec3 rayOrigin_{0.0f};       // 射线起点（相机）
    Vec3 rayDir_{0.0f, 0.0f, -1.0f};
    float rayLength_ = 100.0f;   // 射线长度（命中点距离 / 未命中取射程）
    RaycastHit lastHit_;         // 最近一次命中（调试十字）

    // 程序化网格
    static void BuildDisc(float radius, int segments,
                          std::vector<float> &positions,
                          std::vector<float> &normals,
                          std::vector<unsigned int> &indices);
    static void BuildQuad(std::vector<float> &positions,
                          std::vector<float> &normals,
                          std::vector<unsigned int> &indices);
    static void BuildSphere(float radius, int stacks, int slices,
                            std::vector<float> &positions,
                            std::vector<float> &normals,
                            std::vector<unsigned int> &indices);
    static void BuildCube(std::vector<float> &positions,
                          std::vector<float> &normals,
                          std::vector<unsigned int> &indices);

    void SetupWorld();   // 按 mode2D_ 重建世界 + 场景
    void TeardownWorld();
    void Setup2D();
    void Setup3D();

    Body *SpawnCircle(float x, float y, float radius, const glm::vec4 &color);
    Body *SpawnRect(float x, float y, const glm::vec2 &halfExtents, const glm::vec4 &color);
    Body *SpawnSphere(const Vec3 &pos, float radius, const glm::vec4 &color);
    Body *SpawnBox(const Vec3 &pos, const glm::vec3 &halfExtents, const glm::vec4 &color);
    void AddWall2D(float halfW, float halfH, const Vec2 &center);
    void AddGround3D(float halfW, float halfH, float halfD, float y);
    void AddStaticBox3D(const Vec3 &center, const glm::vec3 &halfExtents,
                        const glm::quat &rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        const glm::vec4 &color = glm::vec4(0.55f, 0.55f, 0.58f, 1.0f));

    void ResetBody(Body &b);
    void ApplySurfaceParams(); // 把 restitution/friction 同步到动态碰撞体
    void SubmitDebugLines();   // 物理调试线 → RenderAPI（按颜色分组）

    // ---- M4 射线拾取辅助 ----
    void BuildPickRay(float mx, float my, Ray &ray) const; // 窗口坐标 → 世界射线
    Body *FindBody2D(Collider2D *c) const;
    Body *FindBody3D(Collider3D *c) const;
};

// 入口（main_demo.cpp 通过 ASTER_PHYSICS_DEMO=1 调用）
int RunPhysicsDemo();
