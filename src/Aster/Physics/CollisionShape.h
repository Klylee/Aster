#pragma once
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include "AABB.h"
#include "RigidBody.h"

namespace aster
{

// 碰撞体形状类型（2D 与 3D 共存于一个继承体系）
enum class ShapeType
{
    Circle2D, // 2D 圆形
    Rect2D,   // 2D 矩形（OBB，支持绕 z 轴旋转）
    Sphere3D, // 3D 球体
    Box3D,    // 3D 盒体（OBB，支持任意旋转）
};

// ============================================================================
// CollisionShape —— 碰撞体形状基类
// ----------------------------------------------------------------------------
// 每个形状持有局部几何定义（半径 / 半边长），通过所属刚体的变换计算世界 AABB
// （宽相 / BVH / 调试可视化用）。精确求交由 NarrowPhase 按 ShapeType 分发。
// 形状只依赖 glm（无 GL 依赖），可与 RigidBody 分离或组合。
// ============================================================================
class CollisionShape
{
public:
    virtual ~CollisionShape() = default;
    virtual ShapeType Type() const = 0;
    // 世界空间 AABB（2D 形状的 z 分量恒为 0）
    virtual AABB ComputeWorldAABB(const RigidBody &body) const = 0;
};

// ---- 2D 圆形 ----
struct CircleShape : public CollisionShape
{
    float radius = 1.0f;

    ShapeType Type() const override { return ShapeType::Circle2D; }

    AABB ComputeWorldAABB(const RigidBody &b) const override
    {
        const float r = radius;
        return {{b.position.x - r, b.position.y - r, 0.0f},
                {b.position.x + r, b.position.y + r, 0.0f}};
    }
};

// ---- 2D 矩形（OBB：绕 z 轴旋转，旋转取自刚体四元数的 z 欧拉角） ----
struct RectShape2D : public CollisionShape
{
    Vec2 halfExtents{1.0f, 1.0f};

    ShapeType Type() const override { return ShapeType::Rect2D; }

    // 世界 AABB = 旋转后的轴对齐包围盒（把半边长投影到世界轴）
    AABB ComputeWorldAABB(const RigidBody &b) const override
    {
        const float angle = glm::eulerAngles(b.rotation).z;
        const float c = std::cos(angle), s = std::sin(angle);
        const Vec2 ax(c, s), ay(-s, c);
        const Vec2 hx = ax * halfExtents.x, hy = ay * halfExtents.y;
        const float ex = std::fabs(hx.x) + std::fabs(hy.x);
        const float ey = std::fabs(hx.y) + std::fabs(hy.y);
        return {{b.position.x - ex, b.position.y - ey, 0.0f},
                {b.position.x + ex, b.position.y + ey, 0.0f}};
    }
};

// ---- 3D 球体 ----
struct SphereShape : public CollisionShape
{
    float radius = 1.0f;

    ShapeType Type() const override { return ShapeType::Sphere3D; }

    AABB ComputeWorldAABB(const RigidBody &b) const override
    {
        const Vec3 r(radius);
        return {b.position - r, b.position + r};
    }
};

// ---- 3D 盒体（OBB：任意旋转，旋转取自刚体四元数） ----
struct BoxShape : public CollisionShape
{
    Vec3 halfExtents{1.0f, 1.0f, 1.0f};

    ShapeType Type() const override { return ShapeType::Box3D; }

    // 世界 AABB = 旋转后把半边长投影到世界轴
    AABB ComputeWorldAABB(const RigidBody &b) const override
    {
        const glm::mat3 m = glm::mat3_cast(b.rotation);
        Vec3 e(0.0f);
        for (int i = 0; i < 3; i++)
        {
            // 第 i 列 = 旋转后的第 i 个局部轴
            const Vec3 axis(m[0][i], m[1][i], m[2][i]);
            e.x += std::fabs(axis.x) * halfExtents[i];
            e.y += std::fabs(axis.y) * halfExtents[i];
            e.z += std::fabs(axis.z) * halfExtents[i];
        }
        return {b.position - e, b.position + e};
    }
};

} // namespace aster
