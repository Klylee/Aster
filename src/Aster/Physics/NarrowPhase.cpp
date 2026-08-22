#include "NarrowPhase.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace aster
{

// 2D 矩形的局部轴 + 半边长向量（OBB 表示）
static void OBB2D(const RectShape2D &shape, const RigidBody &body,
                  Vec2 axes[2], Vec2 half[2])
{
    const float angle = glm::eulerAngles(body.rotation).z;
    const float c = std::cos(angle), s = std::sin(angle);
    axes[0] = Vec2(c, s);
    axes[1] = Vec2(-s, c);
    half[0] = axes[0] * shape.halfExtents.x;
    half[1] = axes[1] * shape.halfExtents.y;
}

bool CircleCircle(const CircleShape &aShape, const RigidBody &aBody,
                  const CircleShape &bShape, const RigidBody &bBody,
                  Contact2D &out)
{
    out.a = const_cast<RigidBody *>(&aBody);
    out.b = const_cast<RigidBody *>(&bBody);

    const Vec2 d = XY(bBody.position) - XY(aBody.position);
    const float r = aShape.radius + bShape.radius;
    const float distSq = glm::dot(d, d);
    if (distSq > r * r)
        return false;

    const float dist = std::sqrt(distSq);
    if (dist > 1e-6f)
        out.normal = d / dist;          // 从 a 指向 b
    else
        out.normal = Vec2(0.0f, 1.0f);  // 完全重合，取默认法线
    out.penetration = r - dist;
    out.point = XY(aBody.position) + out.normal * (aShape.radius - out.penetration * 0.5f);
    return true;
}

bool CircleRect(const CircleShape &cShape, const RigidBody &cBody,
                const RectShape2D &rShape, const RigidBody &rBody,
                Contact2D &out)
{
    out.a = const_cast<RigidBody *>(&cBody);
    out.b = const_cast<RigidBody *>(&rBody);

    // 圆心变换到矩形局部空间
    Vec2 axes[2], half[2];
    OBB2D(rShape, rBody, axes, half);
    const Vec2 local = XY(cBody.position) - XY(rBody.position);
    const float lx = glm::dot(local, axes[0]);
    const float ly = glm::dot(local, axes[1]);
    const float hx = rShape.halfExtents.x, hy = rShape.halfExtents.y;

    // 矩形内距离圆心最近的点（clamp）
    const float nx = std::max(-hx, std::min(hx, lx));
    const float ny = std::max(-hy, std::min(hy, ly));
    const float dx = lx - nx, dy = ly - ny;
    const float distSq = dx * dx + dy * dy;
    const float r = cShape.radius;
    if (distSq > r * r)
        return false;

    Vec2 localNormal;
    float penetration = 0.0f;
    if (distSq > 1e-8f)
    {
        const float dist = std::sqrt(distSq);
        localNormal = Vec2(dx / dist, dy / dist); // 圆心 → 最近点（圆→矩形）
        penetration = r - dist;
    }
    else
    {
        // 圆心在矩形内部：取最小穿透轴
        const float px = hx - std::fabs(lx);
        const float py = hy - std::fabs(ly);
        if (px < py)
        {
            localNormal = Vec2(lx >= 0.0f ? 1.0f : -1.0f, 0.0f);
            penetration = r + px;
        }
        else
        {
            localNormal = Vec2(0.0f, ly >= 0.0f ? 1.0f : -1.0f);
            penetration = r + py;
        }
    }

    out.normal = axes[0] * localNormal.x + axes[1] * localNormal.y; // 局部 → 世界
    out.penetration = penetration;
    // 接触点 = 圆表面最靠近矩形的一点
    out.point = XY(cBody.position) + out.normal * (r - penetration);
    return true;
}

bool RectRect(const RectShape2D &aShape, const RigidBody &aBody,
              const RectShape2D &bShape, const RigidBody &bBody,
              Contact2D &out)
{
    out.a = const_cast<RigidBody *>(&aBody);
    out.b = const_cast<RigidBody *>(&bBody);

    Vec2 aAx[2], aHalf[2], bAx[2], bHalf[2];
    OBB2D(aShape, aBody, aAx, aHalf);
    OBB2D(bShape, bBody, bAx, bHalf);
    const Vec2 delta = XY(bBody.position) - XY(aBody.position); // 从 a 指向 b 的中心差

    // SAT：4 条候选轴（a 的 2 条局部轴 + b 的 2 条局部轴）
    const Vec2 axes[4] = {aAx[0], aAx[1], bAx[0], bAx[1]};
    float overlap = 1e30f;
    Vec2 minAxis(0.0f);
    for (int i = 0; i < 4; i++)
    {
        const Vec2 &axis = axes[i];
        const float ra = std::fabs(glm::dot(aHalf[0], axis)) + std::fabs(glm::dot(aHalf[1], axis));
        const float rb = std::fabs(glm::dot(bHalf[0], axis)) + std::fabs(glm::dot(bHalf[1], axis));
        const float dist = std::fabs(glm::dot(delta, axis));
        const float pen = (ra + rb) - dist;
        if (pen < 0.0f)
            return false; // 找到分离轴 → 不碰撞
        if (pen < overlap)
        {
            overlap = pen;
            minAxis = axis;
        }
    }

    out.normal = glm::normalize(minAxis);
    if (glm::dot(out.normal, delta) < 0.0f)
        out.normal = -out.normal; // 统一：法线从 a 指向 b
    out.penetration = overlap;
    // 接触点 ≈ a 沿法线方向最靠外表面的一点（略内缩，保证在重叠区）
    const float ra = std::fabs(glm::dot(aHalf[0], out.normal)) + std::fabs(glm::dot(aHalf[1], out.normal));
    out.point = XY(aBody.position) + out.normal * (ra - overlap * 0.5f);
    return true;
}

bool Collide2D(const CollisionShape &aShape, const RigidBody &aBody,
               const CollisionShape &bShape, const RigidBody &bBody,
               Contact2D &out)
{
    const ShapeType ta = aShape.Type(), tb = bShape.Type();

    if (ta == ShapeType::Circle2D && tb == ShapeType::Circle2D)
        return CircleCircle(static_cast<const CircleShape &>(aShape), aBody,
                            static_cast<const CircleShape &>(bShape), bBody, out);
    if (ta == ShapeType::Circle2D && tb == ShapeType::Rect2D)
        return CircleRect(static_cast<const CircleShape &>(aShape), aBody,
                          static_cast<const RectShape2D &>(bShape), bBody, out);
    if (ta == ShapeType::Rect2D && tb == ShapeType::Circle2D)
    {
        // 交换以保持 out.a = 原 a（矩形），out.b = 原 b（圆），法线从 a 指向 b
        if (CircleRect(static_cast<const CircleShape &>(bShape), bBody,
                       static_cast<const RectShape2D &>(aShape), aBody, out))
        {
            std::swap(out.a, out.b);
            out.normal = -out.normal;
            return true;
        }
        return false;
    }
    if (ta == ShapeType::Rect2D && tb == ShapeType::Rect2D)
        return RectRect(static_cast<const RectShape2D &>(aShape), aBody,
                        static_cast<const RectShape2D &>(bShape), bBody, out);

    return false; // 未知 / 3D 形状组合（走 Collide3D）
}

// ---- 3D 窄相（M3） ----

// 3D OBB 的局部轴 + 半边长向量
static void OBB3D(const BoxShape &shape, const RigidBody &body,
                  Vec3 axes[3], Vec3 half[3])
{
    const glm::mat3 m = glm::mat3_cast(body.rotation);
    for (int i = 0; i < 3; i++)
    {
        axes[i] = m[i]; // 列 = 旋转后的局部轴
        half[i] = axes[i] * shape.halfExtents[i];
    }
}

bool SphereSphere(const SphereShape &aShape, const RigidBody &aBody,
                  const SphereShape &bShape, const RigidBody &bBody,
                  Contact3D &out)
{
    out.a = const_cast<RigidBody *>(&aBody);
    out.b = const_cast<RigidBody *>(&bBody);

    const Vec3 d = bBody.position - aBody.position;
    const float r = aShape.radius + bShape.radius;
    const float distSq = glm::dot(d, d);
    if (distSq > r * r)
        return false;

    const float dist = std::sqrt(distSq);
    out.normal = (dist > 1e-6f) ? (d / dist) : Vec3(0.0f, 1.0f, 0.0f);
    out.penetration = r - dist;
    out.point = aBody.position + out.normal * (aShape.radius - out.penetration * 0.5f);
    return true;
}

bool SphereBox(const SphereShape &sShape, const RigidBody &sBody,
               const BoxShape &bShape, const RigidBody &bBody,
               Contact3D &out)
{
    out.a = const_cast<RigidBody *>(&sBody);
    out.b = const_cast<RigidBody *>(&bBody);

    Vec3 axes[3], half[3];
    OBB3D(bShape, bBody, axes, half);

    // 球心在盒局部空间的坐标（clamp 到盒内 = 盒上离球心最近的点）
    const Vec3 local = sBody.position - bBody.position;
    Vec3 closestLocal;
    for (int i = 0; i < 3; i++)
        closestLocal[i] = std::max(-bShape.halfExtents[i],
                                   std::min(bShape.halfExtents[i],
                                            glm::dot(local, axes[i])));
    const Vec3 closest = bBody.position + axes[0] * closestLocal.x +
                         axes[1] * closestLocal.y + axes[2] * closestLocal.z;

    const Vec3 delta = sBody.position - closest; // 从盒表面最近点 → 球心
    const float r = sShape.radius;
    const float distSq = glm::dot(delta, delta);
    if (distSq > r * r)
        return false;

    if (distSq > 1e-8f)
    {
        const float dist = std::sqrt(distSq);
        out.normal = -delta / dist; // 从球(a)指向盒(b)
        out.penetration = r - dist;
    }
    else
    {
        // 球心在盒内：取最小穿透轴
        float minPen = 1e30f;
        Vec3 n(0.0f);
        for (int i = 0; i < 3; i++)
        {
            const float pen = bShape.halfExtents[i] + r - std::fabs(glm::dot(local, axes[i]));
            if (pen < minPen)
            {
                minPen = pen;
                n = axes[i] * (glm::dot(local, axes[i]) >= 0.0f ? 1.0f : -1.0f);
            }
        }
        out.normal = n; // 从球(a)指向盒(b)
        out.penetration = minPen;
    }
    // 接触点 = 球表面最靠近盒的一点
    out.point = sBody.position + out.normal * (r - out.penetration);
    return true;
}

bool BoxBox(const BoxShape &aShape, const RigidBody &aBody,
            const BoxShape &bShape, const RigidBody &bBody,
            Contact3D &out)
{
    out.a = const_cast<RigidBody *>(&aBody);
    out.b = const_cast<RigidBody *>(&bBody);

    Vec3 aAx[3], aHalf[3], bAx[3], bHalf[3];
    OBB3D(aShape, aBody, aAx, aHalf);
    OBB3D(bShape, bBody, bAx, bHalf);
    const Vec3 delta = bBody.position - aBody.position;

    // SAT：候选轴 = 3 条 a 面法线 + 3 条 b 面法线 + 9 条边叉积（跳过近似平行）
    Vec3 axes[15];
    int axisCount = 0;
    for (int i = 0; i < 3; i++)
        axes[axisCount++] = aAx[i];
    for (int i = 0; i < 3; i++)
        axes[axisCount++] = bAx[i];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
        {
            const Vec3 c = glm::cross(aAx[i], bAx[j]);
            if (glm::length(c) > 1e-6f)
                axes[axisCount++] = glm::normalize(c);
        }
    if (axisCount == 0)
        return false;

    float overlap = 1e30f;
    Vec3 minAxis(0.0f);
    for (int k = 0; k < axisCount; k++)
    {
        const Vec3 &axis = axes[k];
        const float ra = std::fabs(glm::dot(aHalf[0], axis)) +
                         std::fabs(glm::dot(aHalf[1], axis)) +
                         std::fabs(glm::dot(aHalf[2], axis));
        const float rb = std::fabs(glm::dot(bHalf[0], axis)) +
                         std::fabs(glm::dot(bHalf[1], axis)) +
                         std::fabs(glm::dot(bHalf[2], axis));
        const float dist = std::fabs(glm::dot(delta, axis));
        const float pen = (ra + rb) - dist;
        if (pen < 0.0f)
            return false; // 分离轴
        if (pen < overlap)
        {
            overlap = pen;
            minAxis = axis;
        }
    }

    out.normal = glm::normalize(minAxis);
    if (glm::dot(out.normal, delta) < 0.0f)
        out.normal = -out.normal;
    out.penetration = overlap;
    // 接触点 ≈ a 沿法线方向最靠外表面的一点（单点流形，堆叠够用）
    const float ra = std::fabs(glm::dot(aHalf[0], out.normal)) +
                     std::fabs(glm::dot(aHalf[1], out.normal)) +
                     std::fabs(glm::dot(aHalf[2], out.normal));
    out.point = aBody.position + out.normal * (ra - overlap * 0.5f);
    return true;
}

bool Collide3D(const CollisionShape &aShape, const RigidBody &aBody,
               const CollisionShape &bShape, const RigidBody &bBody,
               Contact3D &out)
{
    const ShapeType ta = aShape.Type(), tb = bShape.Type();

    if (ta == ShapeType::Sphere3D && tb == ShapeType::Sphere3D)
        return SphereSphere(static_cast<const SphereShape &>(aShape), aBody,
                            static_cast<const SphereShape &>(bShape), bBody, out);
    if (ta == ShapeType::Sphere3D && tb == ShapeType::Box3D)
        return SphereBox(static_cast<const SphereShape &>(aShape), aBody,
                         static_cast<const BoxShape &>(bShape), bBody, out);
    if (ta == ShapeType::Box3D && tb == ShapeType::Sphere3D)
    {
        if (SphereBox(static_cast<const SphereShape &>(bShape), bBody,
                      static_cast<const BoxShape &>(aShape), aBody, out))
        {
            std::swap(out.a, out.b);
            out.normal = -out.normal;
            return true;
        }
        return false;
    }
    if (ta == ShapeType::Box3D && tb == ShapeType::Box3D)
        return BoxBox(static_cast<const BoxShape &>(aShape), aBody,
                      static_cast<const BoxShape &>(bShape), bBody, out);

    return false; // 未知组合
}

} // namespace aster
