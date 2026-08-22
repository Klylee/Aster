#pragma once
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include "AABB.h"
#include "Collider.h"

namespace aster
{

// ============================================================================
// Ray —— 射线（origin + 单位方向）+ 命中信息
// ----------------------------------------------------------------------------
// 纯数学部分（RaySphere / RayAABB / RayOBB / RayCircle2D / RayRect2D）为内联函数，
// 物理世界（PhysicsWorld2D / 3D）的 Raycast 遍历碰撞体并分发到这些函数。
// 2D 射线用于命中 XY 平面（z=0）上的圆 / 矩形；3D 用于球 / 盒。
// ============================================================================
struct Ray
{
    Vec3 origin{0.0f};
    Vec3 direction{0.0f, 0.0f, -1.0f}; // 应为单位向量（调用方保证）
};

struct RaycastHit
{
    bool hit = false;
    float t = 0.0f;                            // 命中距离（沿射线）
    Vec3 point{0.0f};                          // 命中点（世界）
    Vec3 normal{0.0f, 1.0f, 0.0f};             // 表面法线（世界）
    Collider2D *collider2D = nullptr;          // 2D 命中的碰撞体
    Collider3D *collider3D = nullptr;          // 3D 命中的碰撞体
};

// ---- 射线-球 ----
inline bool RaySphere(const Ray &ray, const Vec3 &center, float radius,
                      float maxDist, float &outT, Vec3 &outNormal)
{
    const Vec3 oc = ray.origin - center;
    const float a = glm::dot(ray.direction, ray.direction);
    const float b = 2.0f * glm::dot(oc, ray.direction);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f)
        return false;
    const float sq = std::sqrt(disc);
    float t = (-b - sq) / (2.0f * a);
    if (t < 0.0f)
        t = (-b + sq) / (2.0f * a); // 起点在球内 → 取后交点
    if (t < 0.0f || t > maxDist)
        return false;
    outT = t;
    const Vec3 p = ray.origin + ray.direction * t;
    outNormal = glm::normalize(p - center);
    return true;
}

// ---- 射线-AABB（slab 法） ----
inline bool RayAABB(const Ray &ray, const AABB &box,
                    float maxDist, float &outT, Vec3 &outNormal)
{
    float tmin = 0.0f;
    float tmax = maxDist;
    Vec3 n(0.0f);
    for (int i = 0; i < 3; i++)
    {
        const float invD = 1.0f / ray.direction[i];
        float t0 = (box.min[i] - ray.origin[i]) * invD;
        float t1 = (box.max[i] - ray.origin[i]) * invD;
        if (t0 > t1)
            std::swap(t0, t1);
        if (t0 > tmin)
        {
            tmin = t0;
            n = Vec3(0.0f);
            n[i] = (invD < 0.0f) ? 1.0f : -1.0f; // 进入面法线
        }
        if (t1 < tmax)
            tmax = t1;
        if (tmin > tmax)
            return false;
    }
    if (tmin > maxDist || tmin < 0.0f)
        return false;
    outT = tmin;
    outNormal = n;
    return true;
}

// ---- 射线-OBB（变换到局部空间再做 AABB 求交） ----
// axes 为旋转矩阵（列 = 局部轴）；halfExtents 为半边长。
inline bool RayOBB(const Ray &ray, const Vec3 &center, const glm::mat3 &axes,
                   const Vec3 &halfExtents, float maxDist, float &outT, Vec3 &outNormal)
{
    const glm::mat3 inv = glm::inverse(axes);
    const Ray lRay{inv * (ray.origin - center), inv * ray.direction};
    const AABB box{-halfExtents, halfExtents};
    if (!RayAABB(lRay, box, maxDist, outT, outNormal))
        return false;
    outNormal = axes * outNormal; // 局部法线 → 世界
    return true;
}

// ---- 2D：射线-圆盘（命中 z=0 平面上的圆） ----
inline bool RayCircle2D(const Ray &ray, const Vec2 &center, float radius,
                        float maxDist, float &outT)
{
    if (std::fabs(ray.direction.z) < 1e-6f)
        return false; // 平行于 XY 平面
    const float tPlane = -ray.origin.z / ray.direction.z;
    if (tPlane < 0.0f || tPlane > maxDist)
        return false;
    const Vec2 p = XY(ray.origin) + XY(ray.direction) * tPlane;
    const Vec2 d = p - center;
    if (glm::dot(d, d) > radius * radius)
        return false;
    outT = tPlane;
    return true;
}

// ---- 2D：射线-OBB（命中 z=0 平面上的旋转矩形） ----
inline bool RayRect2D(const Ray &ray, const Vec2 &center, const Vec2 &halfExtents,
                      float angle, float maxDist, float &outT)
{
    if (std::fabs(ray.direction.z) < 1e-6f)
        return false;
    const float tPlane = -ray.origin.z / ray.direction.z;
    if (tPlane < 0.0f || tPlane > maxDist)
        return false;
    const Vec2 p = XY(ray.origin) + XY(ray.direction) * tPlane;
    const float c = std::cos(angle), s = std::sin(angle);
    const Vec2 ax(c, s), ay(-s, c);
    const Vec2 local = p - center;
    const float lx = glm::dot(local, ax);
    const float ly = glm::dot(local, ay);
    if (std::fabs(lx) > halfExtents.x || std::fabs(ly) > halfExtents.y)
        return false;
    outT = tPlane;
    return true;
}

} // namespace aster
