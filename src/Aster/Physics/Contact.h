#pragma once
#include "RigidBody.h"
#include "CollisionShape.h"

namespace aster
{

// ============================================================================
// 接触点（Contact）—— 窄相输出，宽相/求解器/调试可视化共用
// ----------------------------------------------------------------------------
// 约定：normal 为单位向量，从 a 指向 b；penetration 为穿透深度（>0 表示重叠）。
// a / b 指向所属刚体（静态刚体也参与，只是 invMass=0）。
// ============================================================================

// 2D 接触点（XY 平面）
struct Contact2D
{
    RigidBody *a = nullptr;
    RigidBody *b = nullptr;
    Vec2 point{0.0f};           // 接触点（世界）
    Vec2 normal{0.0f, 1.0f};    // 从 a 指向 b 的单位法线
    float penetration = 0.0f;   // 穿透深度
    float restitution = 0.0f;   // 恢复系数（求解时用，取两体较大）
    float friction = 0.0f;      // 摩擦系数（求解时用，取两体几何平均）
};

// 3D 接触点（M3 用）
struct Contact3D
{
    RigidBody *a = nullptr;
    RigidBody *b = nullptr;
    Vec3 point{0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    float penetration = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
};

} // namespace aster
