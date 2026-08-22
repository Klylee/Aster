#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "Transform.h" // Vec3 别名

namespace aster
{

// 取 Vec3 的 XY 分量（2D 物理便捷；glm 默认无 .xy swizzle，需显式构造）
inline Vec2 XY(const Vec3 &v)
{
    return Vec2(v.x, v.y);
}

// ============================================================================
// AABB —— 轴对齐包围盒
// ----------------------------------------------------------------------------
// 宽相（broad phase）与 BVH 的基础。2D 形状的 AABB 令 z = 0（见 CollisionShape）。
// ============================================================================
struct AABB
{
    Vec3 min{0.0f};
    Vec3 max{0.0f};

    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extents() const { return (max - min) * 0.5f; }
    Vec3 Size() const { return max - min; }
    float Volume() const
    {
        Vec3 s = Size();
        return s.x * s.y * s.z;
    }

    bool Contains(const Vec3 &p) const
    {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    bool Overlaps(const AABB &o) const
    {
        return min.x <= o.max.x && max.x >= o.min.x &&
               min.y <= o.max.y && max.y >= o.min.y &&
               min.z <= o.max.z && max.z >= o.min.z;
    }

    // 2D 重叠（忽略 z）
    bool Overlaps2D(const AABB &o) const
    {
        return min.x <= o.max.x && max.x >= o.min.x &&
               min.y <= o.max.y && max.y >= o.min.y;
    }

    void Expand(const AABB &o)
    {
        min = glm::min(min, o.min);
        max = glm::max(max, o.max);
    }

    void Expand(const Vec3 &p)
    {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
};

// 计算一组 AABB 的并集（空列表返回退化盒 {0,0}）
inline AABB UnionAABB(const std::vector<AABB> &boxes)
{
    if (boxes.empty())
        return AABB{};
    AABB u = boxes[0];
    for (size_t i = 1; i < boxes.size(); i++)
        u.Expand(boxes[i]);
    return u;
}

// 两盒并集
inline AABB UnionAABB(const AABB &a, const AABB &b)
{
    AABB u = a;
    u.Expand(b);
    return u;
}

} // namespace aster
