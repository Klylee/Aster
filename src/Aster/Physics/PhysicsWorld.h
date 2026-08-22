#pragma once
#include <memory>
#include <vector>
#include "BVH.h"
#include "Collider.h"
#include "Ray.h"
#include "RigidBody.h"
#include "Transform.h" // Vec2 / Vec3 / Vec4 别名

namespace aster
{

// 调试线段（世界空间，颜色 RGBA）—— 由物理世界收集，demo 转交 RenderAPI 绘制
struct DebugLine
{
    Vec3 a;
    Vec3 b;
    Vec4 color;
};

// ============================================================================
// PhysicsWorld —— 物理世界基类（持有刚体列表 + 固定步长推进）
// ----------------------------------------------------------------------------
// 由子类提供维度相关的重力与积分：
//   - PhysicsWorld3D：Vec3 重力，全三维运动。
//   - PhysicsWorld2D ：Vec2 重力，z 分量恒为 0（独立 2D 世界，与 3D 并存）。
//
// 生命周期约定：AddBody / AddCollider 只存指针，不负责释放。调用方需保证
// 刚体 / 碰撞体的存储地址稳定（如 unique_ptr / 堆分配），并在销毁前移除。
//
// 固定步长推进（Step）：在 App::FixedUpdate（默认 120Hz）中调用，与渲染帧率解耦。
// 管线：清力 → 重力 / 外力积分速度 → [碰撞检测 + 速度冲量求解]
//       → 位置 / 旋转积分 → [位置修正]
// ============================================================================
class PhysicsWorld
{
public:
    virtual ~PhysicsWorld() = default;

    void AddBody(RigidBody *b);    // 幂等：已存在则不重复加入
    void RemoveBody(RigidBody *b); // 幂等：不存在则忽略
    void ClearBodies();            // 清空（不释放刚体）
    size_t BodyCount() const { return bodies_.size(); }

    const std::vector<RigidBody *> &Bodies() const { return bodies_; }
    std::vector<RigidBody *> &Bodies() { return bodies_; }

    // 固定步长推进一帧
    virtual void Step(float dt);

protected:
    // 子类实现：把维度相关的重力 / 外力作用到每个刚体（即调用 IntegrateVelocity）
    virtual void ApplyGravityAndForces(float dt) = 0;

    // 碰撞管线钩子（子类按需重写）
    //  OnBeforeIntegratePosition：速度积分之后、位置积分之前 —— 碰撞检测 + 速度冲量求解
    virtual void OnBeforeIntegratePosition(float) {}
    //  OnAfterIntegratePosition：位置积分之后 —— 位置修正（Baumgarte）
    virtual void OnAfterIntegratePosition(float) {}

    std::vector<RigidBody *> bodies_;
};

// ---- 3D 物理世界 ----
class PhysicsWorld3D : public PhysicsWorld
{
public:
    Vec3 gravity{0.0f, -9.81f, 0.0f}; // 默认：地球重力，+Y 向上
    void SetGravity(const Vec3 &g) { gravity = g; }

    // ---- 碰撞体管理（只存指针，不负责释放） ----
    void AddCollider(Collider3D *c);
    void RemoveCollider(Collider3D *c);
    void ClearColliders();
    const std::vector<Collider3D *> &Colliders() const { return colliders_; }
    // 本帧求解后的活跃接触（调试 / 事件用）
    const std::vector<Contact3D> &Contacts() const { return contacts_; }

    // ---- 求解器参数 ----
    int velocityIterations = 8;
    int positionIterations = 4;
    float positionCorrectionPercent = 0.8f;
    float slop = 0.005f;

    // ---- 调试绘制（世界空间线段，每帧 Step 后刷新） ----
    bool debugDraw = true;
    bool debugDrawAABB = false;
    const std::vector<DebugLine> &DebugLines() const { return debugLines_; }

    // ---- M5：BVH 宽相加速（静态碰撞体） ----
    bool useBVH = true;         // true = dynamic-static 走 BVH；false = 全暴力（性能对比）
    int broadPhasePairs = 0;    // 本帧生成的候选对数量
    int bvhQueries = 0;         // 本帧 BVH AABB 查询次数
    float bvhBuildMs = 0.0f;    // 最近一次 BVH 重建耗时（毫秒）
    int bvhNodes = 0;           // 最近一次 BVH 节点数

    // ---- 射线求交（M4） ----
    // 遍历启用的碰撞体，返回最近命中（t 最小）；maxDist 限制射程。
    bool Raycast(const Ray &ray, float maxDist, RaycastHit &out) const;
    // 返回全部命中（按 t 升序）。
    bool RaycastAll(const Ray &ray, float maxDist, std::vector<RaycastHit> &out) const;

protected:
    void ApplyGravityAndForces(float dt) override;
    void OnBeforeIntegratePosition(float dt) override; // 碰撞检测 + 速度求解
    void OnAfterIntegratePosition(float dt) override;  // 位置修正 + 调试线收集

private:
    std::vector<Collider3D *> colliders_;
    std::vector<Contact3D> contacts_;
    std::vector<DebugLine> debugLines_;
    BVH staticBvh_;              // 静态碰撞体的 BVH（staticBvhDirty_ 时重建）
    bool staticBvhDirty_ = true;

    void Collide(float dt);
    void ResolveVelocity(const Contact3D &c); // 冲量（含摩擦）
    void ResolvePosition(const Contact3D &c); // 位置修正
    void CollectDebugLines();
    void AddShapeDebug(const Collider3D &c, const Vec4 &color);
    void AddAABBDebug(const Collider3D &c, const Vec4 &color);
};

// ---- 2D 物理世界（XY 平面，z 恒为 0） ----
class PhysicsWorld2D : public PhysicsWorld
{
public:
    Vec2 gravity{0.0f, -9.81f}; // 默认：向下重力
    void SetGravity(const Vec2 &g) { gravity = g; }

    // ---- 碰撞体管理（只存指针，不负责释放） ----
    // AddCollider 同时把 body 登记到积分列表（幂等）。
    void AddCollider(Collider2D *c);
    void RemoveCollider(Collider2D *c);
    void ClearColliders();
    const std::vector<Collider2D *> &Colliders() const { return colliders_; }
    // 本帧求解后的活跃接触（调试 / 事件用）
    const std::vector<Contact2D> &Contacts() const { return contacts_; }

    // ---- 求解器参数 ----
    int velocityIterations = 8;           // 速度冲量迭代次数
    int positionIterations = 4;           // 位置修正迭代次数
    float positionCorrectionPercent = 0.8f; // 每次位置修正穿透的比例
    float slop = 0.005f;                  // 允许的微小穿透（防抖动）

    // ---- 调试绘制（世界空间线段，每帧 Step 后刷新） ----
    bool debugDraw = true;
    bool debugDrawAABB = false;
    const std::vector<DebugLine> &DebugLines() const { return debugLines_; }

    // ---- M5：BVH 宽相加速（静态碰撞体） ----
    bool useBVH = true;
    int broadPhasePairs = 0;
    int bvhQueries = 0;
    float bvhBuildMs = 0.0f;
    int bvhNodes = 0;

    // ---- 射线求交（M4） ----
    bool Raycast(const Ray &ray, float maxDist, RaycastHit &out) const;
    bool RaycastAll(const Ray &ray, float maxDist, std::vector<RaycastHit> &out) const;

protected:
    void ApplyGravityAndForces(float dt) override;
    void OnBeforeIntegratePosition(float dt) override; // 碰撞检测 + 速度求解
    void OnAfterIntegratePosition(float dt) override;  // 位置修正 + 调试线收集

private:
    std::vector<Collider2D *> colliders_;  // 碰撞体（原始指针，不持有）
    std::vector<Contact2D> contacts_;      // 本帧活跃接触
    std::vector<DebugLine> debugLines_;    // 本帧调试线
    BVH staticBvh_;              // 静态碰撞体的 BVH（staticBvhDirty_ 时重建）
    bool staticBvhDirty_ = true;

    void Collide(float dt);                       // 宽相 + 窄相
    void ResolveVelocity(const Contact2D &c);     // 单接触点冲量（含摩擦）
    void ResolvePosition(const Contact2D &c);     // 位置修正
    void CollectDebugLines();                     // 形状线框 + 接触点十字
    void AddShapeDebug(const Collider2D &c, const Vec4 &color);
    void AddAABBDebug(const Collider2D &c, const Vec4 &color);
};

} // namespace aster
