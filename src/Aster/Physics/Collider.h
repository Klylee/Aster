#pragma once
#include <functional>
#include <memory>
#include "RigidBody.h"
#include "CollisionShape.h"
#include "Contact.h"

namespace aster
{

// ============================================================================
// Collider —— 场景集成组件：把刚体 + 形状 + 表面参数绑定在一起
// ----------------------------------------------------------------------------
// 由 PhysicsWorld2D 持有；body 与 shape 至少一方为 nullptr 时不参与碰撞
// （body 为 nullptr 表示纯静态形状，shape 为 nullptr 表示纯运动学体）。
// 动态刚体的 body 必须同时被 PhysicsWorld 登记（AddBody / AddCollider 自动处理）。
// ============================================================================
struct Collider2D
{
    RigidBody *body = nullptr;                       // 所属刚体（可空 = 静态纯形状）
    std::shared_ptr<CollisionShape> shape = nullptr; // 碰撞形状（可空 = 无碰撞）
    bool enabled = true;                             // 是否参与碰撞
    float friction = 0.6f;                           // 表面摩擦
    float restitution = 0.4f;                        // 表面恢复系数
    // 接触回调（可选）：每次窄相产生接触时触发（含静态接触）
    std::function<void(const Contact2D &)> onContact = nullptr;
};

// ---- 3D 碰撞体（M3，语义与 Collider2D 一致） ----
struct Collider3D
{
    RigidBody *body = nullptr;
    std::shared_ptr<CollisionShape> shape = nullptr;
    bool enabled = true;
    float friction = 0.6f;
    float restitution = 0.4f;
    std::function<void(const Contact3D &)> onContact = nullptr;
};

} // namespace aster
