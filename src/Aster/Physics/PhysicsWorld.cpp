#include "PhysicsWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include "BroadPhase.h"
#include "NarrowPhase.h"

namespace aster
{

void PhysicsWorld::AddBody(RigidBody *b)
{
    if (b && std::find(bodies_.begin(), bodies_.end(), b) == bodies_.end())
        bodies_.push_back(b);
}

void PhysicsWorld::RemoveBody(RigidBody *b)
{
    auto it = std::find(bodies_.begin(), bodies_.end(), b);
    if (it != bodies_.end())
        bodies_.erase(it);
}

void PhysicsWorld::ClearBodies()
{
    bodies_.clear();
}

void PhysicsWorld::Step(float dt)
{
    // 1) 清力：上一帧累积的力已消费（调用方在两个 Step 之间 AddForce，这里统一清零）
    for (auto *b : bodies_)
        b->ClearForces();

    // 2) 重力 / 外力 → 更新速度（半隐式欧拉）
    ApplyGravityAndForces(dt);

    // 3) 速度积分之后、位置积分之前：碰撞检测 + 速度冲量求解（子类重写）
    OnBeforeIntegratePosition(dt);

    // 4) 位置 / 旋转积分
    for (auto *b : bodies_)
        b->IntegratePosition(dt);

    // 5) 位置积分之后：位置修正（子类重写）
    OnAfterIntegratePosition(dt);
}

void PhysicsWorld3D::ApplyGravityAndForces(float dt)
{
    for (auto *b : bodies_)
        b->IntegrateVelocity(dt, gravity);
}

// ---- PhysicsWorld3D 碰撞管线（M3） ----

void PhysicsWorld3D::AddCollider(Collider3D *c)
{
    if (!c)
        return;
    if (std::find(colliders_.begin(), colliders_.end(), c) == colliders_.end())
        colliders_.push_back(c);
    if (c->body)
        AddBody(c->body);
    staticBvhDirty_ = true;
}

void PhysicsWorld3D::RemoveCollider(Collider3D *c)
{
    auto it = std::find(colliders_.begin(), colliders_.end(), c);
    if (it != colliders_.end())
        colliders_.erase(it);
    if (c->body)
    {
        bool stillUsed = false;
        for (auto *other : colliders_)
            if (other->body == c->body)
            {
                stillUsed = true;
                break;
            }
        if (!stillUsed)
            RemoveBody(c->body);
    }
    staticBvhDirty_ = true;
}

void PhysicsWorld3D::ClearColliders()
{
    colliders_.clear();
    staticBvh_.Clear();
    staticBvhDirty_ = true;
}

void PhysicsWorld3D::OnBeforeIntegratePosition(float dt)
{
    Collide(dt);
    for (int it = 0; it < velocityIterations; it++)
        for (auto &c : contacts_)
            ResolveVelocity(c);
}

void PhysicsWorld3D::OnAfterIntegratePosition(float)
{
    for (int it = 0; it < positionIterations; it++)
        for (auto &c : contacts_)
            ResolvePosition(c);
    CollectDebugLines();
}

void PhysicsWorld3D::Collide(float dt)
{
    (void)dt;
    contacts_.clear();
    broadPhasePairs = 0;
    bvhQueries = 0;

    // 分类：动态 / 静态（静态 = 无穷质量，进 BVH）
    std::vector<Collider3D *> dyn, stat;
    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        if (c->body->IsStatic())
            stat.push_back(c);
        else
            dyn.push_back(c);
    }

    auto narrowPair = [&](Collider3D *a, Collider3D *b)
    {
        Contact3D c;
        if (Collide3D(*a->shape, *a->body, *b->shape, *b->body, c))
        {
            c.restitution = std::max(a->restitution, b->restitution);
            c.friction = std::sqrt(a->friction * b->friction);
            contacts_.push_back(c);
            if (a->onContact)
                a->onContact(c);
            if (b->onContact)
                b->onContact(c);
        }
    };

    if (useBVH && !stat.empty())
    {
        // 静态集合变化时重建 BVH（测时）
        if (staticBvhDirty_)
        {
            std::vector<AABB> boxes;
            boxes.reserve(stat.size());
            for (auto *c : stat)
                boxes.push_back(c->shape->ComputeWorldAABB(*c->body));
            const auto t0 = std::chrono::high_resolution_clock::now();
            staticBvh_.Build(boxes);
            const auto t1 = std::chrono::high_resolution_clock::now();
            bvhBuildMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
            bvhNodes = staticBvh_.NodeCount();
            staticBvhDirty_ = false;
        }
        // dynamic-dynamic：暴力
        for (size_t i = 0; i < dyn.size(); i++)
        {
            const AABB ai = dyn[i]->shape->ComputeWorldAABB(*dyn[i]->body);
            for (size_t j = i + 1; j < dyn.size(); j++)
            {
                const AABB aj = dyn[j]->shape->ComputeWorldAABB(*dyn[j]->body);
                if (!ai.Overlaps(aj))
                    continue;
                broadPhasePairs++;
                narrowPair(dyn[i], dyn[j]);
            }
        }
        // dynamic-static：BVH 查询
        std::vector<int> hits;
        for (auto *d : dyn)
        {
            const AABB da = d->shape->ComputeWorldAABB(*d->body);
            hits.clear();
            staticBvh_.QueryAABB(da, hits);
            bvhQueries++;
            for (int k : hits)
            {
                broadPhasePairs++;
                narrowPair(d, stat[(size_t)k]);
            }
        }
    }
    else
    {
        // 全暴力（含 static-static，供对比）
        std::vector<BodyPair3D> pairs;
        BroadPhase::BruteForce3D(colliders_, pairs);
        broadPhasePairs = (int)pairs.size();
        for (auto &p : pairs)
            narrowPair(p.a, p.b);
    }
}

void PhysicsWorld3D::ResolveVelocity(const Contact3D &c)
{
    RigidBody *a = c.a;
    RigidBody *b = c.b;
    const float invA = a->invMass, invB = b->invMass;
    const float invSum = invA + invB;
    if (invSum == 0.0f)
        return;

    const Vec3 rv = b->linearVelocity - a->linearVelocity;
    const float velAlongNormal = glm::dot(rv, c.normal);
    if (velAlongNormal > 0.0f)
        return;

    const float j = -(1.0f + c.restitution) * velAlongNormal / invSum;
    const Vec3 impulse = c.normal * j;
    a->linearVelocity -= impulse * invA;
    b->linearVelocity += impulse * invB;

    // 摩擦（库仑）：沿切向抵消相对切向速度，|jt| 限制为 mu * 法向冲量
    const Vec3 rv2 = b->linearVelocity - a->linearVelocity;
    const Vec3 t = glm::normalize(rv2 - c.normal * glm::dot(rv2, c.normal));
    if (glm::length(t) > 1e-6f)
    {
        float jt = -glm::dot(rv2, t) / invSum;
        const float maxF = c.friction * j;
        jt = std::max(-maxF, std::min(maxF, jt));
        const Vec3 tImpulse = t * jt;
        a->linearVelocity -= tImpulse * invA;
        b->linearVelocity += tImpulse * invB;
    }
}

void PhysicsWorld3D::ResolvePosition(const Contact3D &c)
{
    RigidBody *a = c.a;
    RigidBody *b = c.b;
    const float invSum = a->invMass + b->invMass;
    if (invSum == 0.0f)
        return;

    const float pen = std::max(c.penetration - slop, 0.0f);
    const Vec3 correction = c.normal * (pen / invSum) * positionCorrectionPercent;
    a->position -= correction * a->invMass;
    b->position += correction * b->invMass;
}

void PhysicsWorld3D::CollectDebugLines()
{
    debugLines_.clear();
    if (!debugDraw)
        return;

    const Vec4 shapeColor(1.0f, 1.0f, 1.0f, 1.0f);
    const Vec4 aabbColor(0.2f, 0.9f, 0.9f, 1.0f);
    const Vec4 contactColor(1.0f, 0.30f, 0.30f, 1.0f);

    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        AddShapeDebug(*c, shapeColor);
        if (debugDrawAABB)
            AddAABBDebug(*c, aabbColor);
    }
    for (auto &c : contacts_)
    {
        // 接触点十字（法线方向小段 + 两个正交切线方向）
        const Vec3 p = c.point;
        const Vec3 n = c.normal * 0.25f;
        Vec3 t1 = glm::cross(c.normal, Vec3(1.0f, 0.0f, 0.0f));
        if (glm::length(t1) < 1e-6f) // normal 平行 x 轴时换参考轴
            t1 = glm::cross(c.normal, Vec3(0.0f, 1.0f, 0.0f));
        t1 = glm::normalize(t1) * 0.25f;
        const Vec3 t2 = glm::normalize(glm::cross(c.normal, t1)) * 0.25f;
        debugLines_.push_back({p - t1, p + t1, contactColor});
        debugLines_.push_back({p - t2, p + t2, contactColor});
        debugLines_.push_back({p - n, p + n, contactColor});
    }
}

void PhysicsWorld3D::AddShapeDebug(const Collider3D &c, const Vec4 &color)
{
    constexpr float kTwoPi = 6.283185307179586f;
    switch (c.shape->Type())
    {
    case ShapeType::Sphere3D:
    {
        // 3 个大圆（XY / XZ / YZ 平面）表示球
        const auto &s = static_cast<const SphereShape &>(*c.shape);
        const Vec3 center = c.body->position;
        const int segs = 20;
        for (int i = 0; i < segs; i++)
        {
            const float a0 = kTwoPi * (float)i / (float)segs;
            const float a1 = kTwoPi * (float)(i + 1) / (float)segs;
            const float c0 = std::cos(a0), s0 = std::sin(a0);
            const float c1 = std::cos(a1), s1 = std::sin(a1);
            const Vec3 xy0 = center + Vec3(c0, s0, 0.0f) * s.radius;
            const Vec3 xy1 = center + Vec3(c1, s1, 0.0f) * s.radius;
            const Vec3 xz0 = center + Vec3(c0, 0.0f, s0) * s.radius;
            const Vec3 xz1 = center + Vec3(c1, 0.0f, s1) * s.radius;
            const Vec3 yz0 = center + Vec3(0.0f, c0, s0) * s.radius;
            const Vec3 yz1 = center + Vec3(0.0f, c1, s1) * s.radius;
            debugLines_.push_back({xy0, xy1, color});
            debugLines_.push_back({xz0, xz1, color});
            debugLines_.push_back({yz0, yz1, color});
        }
        break;
    }
    case ShapeType::Box3D:
    {
        const auto &s = static_cast<const BoxShape &>(*c.shape);
        const glm::mat3 m = glm::mat3_cast(c.body->rotation);
        const Vec3 axes[3] = {m[0], m[1], m[2]};
        const Vec3 hx = axes[0] * s.halfExtents.x;
        const Vec3 hy = axes[1] * s.halfExtents.y;
        const Vec3 hz = axes[2] * s.halfExtents.z;
        const Vec3 center = c.body->position;
        const Vec3 corners[8] = {
            center - hx - hy - hz, center + hx - hy - hz,
            center + hx + hy - hz, center - hx + hy - hz,
            center - hx - hy + hz, center + hx - hy + hz,
            center + hx + hy + hz, center - hx + hy + hz,
        };
        const int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 底面
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // 顶面
            {0, 4}, {1, 5}, {2, 6}, {3, 7}, // 竖边
        };
        for (auto &e : edges)
            debugLines_.push_back({corners[e[0]], corners[e[1]], color});
        break;
    }
    default:
        break; // 2D 形状（PhysicsWorld2D 处理）
    }
}

void PhysicsWorld3D::AddAABBDebug(const Collider3D &c, const Vec4 &color)
{
    const AABB box = c.shape->ComputeWorldAABB(*c.body);
    const Vec3 mn = box.min, mx = box.max;
    const Vec3 corners[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
    };
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (auto &e : edges)
        debugLines_.push_back({corners[e[0]], corners[e[1]], color});
}

bool PhysicsWorld3D::Raycast(const Ray &ray, float maxDist, RaycastHit &out) const
{
    bool found = false;
    out = RaycastHit{};
    float best = maxDist;

    // 动态体暴力 + 静态体走 BVH（若已构建）
    std::vector<Collider3D *> dyn, stat;
    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        (c->body->IsStatic() ? stat : dyn).push_back(c);
    }

    auto test = [&](const Collider3D *c)
    {
        float t = 0.0f;
        Vec3 n(0.0f);
        bool hit = false;
        const Vec3 &center = c->body->position;
        if (c->shape->Type() == ShapeType::Sphere3D)
        {
            const auto &s = static_cast<const SphereShape &>(*c->shape);
            hit = RaySphere(ray, center, s.radius, best, t, n);
        }
        else if (c->shape->Type() == ShapeType::Box3D)
        {
            const auto &s = static_cast<const BoxShape &>(*c->shape);
            hit = RayOBB(ray, center, glm::mat3_cast(c->body->rotation),
                         s.halfExtents, best, t, n);
        }
        if (hit && t < best)
        {
            best = t;
            out.hit = true;
            out.t = t;
            out.point = ray.origin + ray.direction * t;
            out.normal = n;
            out.collider3D = const_cast<Collider3D *>(c);
            found = true;
        }
    };

    for (auto *c : dyn)
        test(c);
    if (useBVH && !staticBvh_.Empty())
    {
        std::vector<int> hits;
        staticBvh_.QueryRay(ray, best, hits);
        for (int k : hits)
            test(stat[(size_t)k]);
    }
    else
    {
        for (auto *c : stat)
            test(c);
    }
    return found;
}

bool PhysicsWorld3D::RaycastAll(const Ray &ray, float maxDist,
                                std::vector<RaycastHit> &out) const
{
    out.clear();
    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        float t = 0.0f;
        Vec3 n(0.0f);
        bool hit = false;
        const Vec3 &center = c->body->position;
        if (c->shape->Type() == ShapeType::Sphere3D)
        {
            const auto &s = static_cast<const SphereShape &>(*c->shape);
            hit = RaySphere(ray, center, s.radius, maxDist, t, n);
        }
        else if (c->shape->Type() == ShapeType::Box3D)
        {
            const auto &s = static_cast<const BoxShape &>(*c->shape);
            hit = RayOBB(ray, center, glm::mat3_cast(c->body->rotation),
                         s.halfExtents, maxDist, t, n);
        }
        if (hit)
        {
            RaycastHit h;
            h.hit = true;
            h.t = t;
            h.point = ray.origin + ray.direction * t;
            h.normal = n;
            h.collider3D = c;
            out.push_back(h);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const RaycastHit &a, const RaycastHit &b) { return a.t < b.t; });
    return !out.empty();
}

// ---- PhysicsWorld2D ----

void PhysicsWorld2D::AddCollider(Collider2D *c)
{
    if (!c)
        return;
    if (std::find(colliders_.begin(), colliders_.end(), c) == colliders_.end())
        colliders_.push_back(c);
    if (c->body)
        AddBody(c->body);
    staticBvhDirty_ = true;
}

void PhysicsWorld2D::RemoveCollider(Collider2D *c)
{
    auto it = std::find(colliders_.begin(), colliders_.end(), c);
    if (it != colliders_.end())
        colliders_.erase(it);
    // 若刚体不再被任何碰撞体引用，从积分列表移除
    if (c->body)
    {
        bool stillUsed = false;
        for (auto *other : colliders_)
            if (other->body == c->body)
            {
                stillUsed = true;
                break;
            }
        if (!stillUsed)
            RemoveBody(c->body);
    }
    staticBvhDirty_ = true;
}

void PhysicsWorld2D::ClearColliders()
{
    colliders_.clear();
    staticBvh_.Clear();
    staticBvhDirty_ = true;
}

void PhysicsWorld2D::ApplyGravityAndForces(float dt)
{
    const Vec3 g3(gravity.x, gravity.y, 0.0f); // 2D 重力只作用于 XY 平面
    for (auto *b : bodies_)
    {
        b->IntegrateVelocity(dt, g3);
        // 2D 约束：z 恒为 0，角速度只允许绕 z 轴（XY 平面自旋）
        b->position.z = 0.0f;
        b->linearVelocity.z = 0.0f;
        b->angularVelocity.x = 0.0f;
        b->angularVelocity.y = 0.0f;
    }
}

void PhysicsWorld2D::OnBeforeIntegratePosition(float dt)
{
    Collide(dt);
    // 速度冲量求解（迭代：让堆叠 / 多接触收敛）
    for (int it = 0; it < velocityIterations; it++)
        for (auto &c : contacts_)
            ResolveVelocity(c);
}

void PhysicsWorld2D::OnAfterIntegratePosition(float)
{
    // 位置修正（迭代）
    for (int it = 0; it < positionIterations; it++)
        for (auto &c : contacts_)
            ResolvePosition(c);
    CollectDebugLines();
}

void PhysicsWorld2D::Collide(float dt)
{
    (void)dt;
    contacts_.clear();
    broadPhasePairs = 0;
    bvhQueries = 0;

    std::vector<Collider2D *> dyn, stat;
    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        if (c->body->IsStatic())
            stat.push_back(c);
        else
            dyn.push_back(c);
    }

    auto narrowPair = [&](Collider2D *a, Collider2D *b)
    {
        Contact2D c;
        if (Collide2D(*a->shape, *a->body, *b->shape, *b->body, c))
        {
            c.restitution = std::max(a->restitution, b->restitution);
            c.friction = std::sqrt(a->friction * b->friction);
            contacts_.push_back(c);
            if (a->onContact)
                a->onContact(c);
            if (b->onContact)
                b->onContact(c);
        }
    };

    if (useBVH && !stat.empty())
    {
        if (staticBvhDirty_)
        {
            std::vector<AABB> boxes;
            boxes.reserve(stat.size());
            for (auto *c : stat)
                boxes.push_back(c->shape->ComputeWorldAABB(*c->body));
            const auto t0 = std::chrono::high_resolution_clock::now();
            staticBvh_.Build(boxes);
            const auto t1 = std::chrono::high_resolution_clock::now();
            bvhBuildMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
            bvhNodes = staticBvh_.NodeCount();
            staticBvhDirty_ = false;
        }
        // dynamic-dynamic：暴力
        for (size_t i = 0; i < dyn.size(); i++)
        {
            const AABB ai = dyn[i]->shape->ComputeWorldAABB(*dyn[i]->body);
            for (size_t j = i + 1; j < dyn.size(); j++)
            {
                const AABB aj = dyn[j]->shape->ComputeWorldAABB(*dyn[j]->body);
                if (!ai.Overlaps2D(aj))
                    continue;
                broadPhasePairs++;
                narrowPair(dyn[i], dyn[j]);
            }
        }
        // dynamic-static：BVH 查询
        std::vector<int> hits;
        for (auto *d : dyn)
        {
            const AABB da = d->shape->ComputeWorldAABB(*d->body);
            hits.clear();
            staticBvh_.QueryAABB(da, hits);
            bvhQueries++;
            for (int k : hits)
            {
                broadPhasePairs++;
                narrowPair(d, stat[(size_t)k]);
            }
        }
    }
    else
    {
        std::vector<BodyPair> pairs;
        BroadPhase::BruteForce2D(colliders_, pairs);
        broadPhasePairs = (int)pairs.size();
        for (auto &p : pairs)
            narrowPair(p.a, p.b);
    }
}

void PhysicsWorld2D::ResolveVelocity(const Contact2D &c)
{
    RigidBody *a = c.a;
    RigidBody *b = c.b;
    const float invA = a->invMass, invB = b->invMass;
    const float invSum = invA + invB;
    if (invSum == 0.0f)
        return; // 双静态

    const Vec2 va(a->linearVelocity.x, a->linearVelocity.y);
    const Vec2 vb(b->linearVelocity.x, b->linearVelocity.y);
    const Vec2 rv = vb - va; // b 相对 a 的速度
    const float velAlongNormal = glm::dot(rv, c.normal);
    if (velAlongNormal > 0.0f)
        return; // 正在分离，不施加冲量

    // 法向冲量（恢复系数 e 已在窄相取 max）
    const float j = -(1.0f + c.restitution) * velAlongNormal / invSum;
    const Vec2 impulse = c.normal * j;
    a->linearVelocity.x -= impulse.x * invA;
    a->linearVelocity.y -= impulse.y * invA;
    b->linearVelocity.x += impulse.x * invB;
    b->linearVelocity.y += impulse.y * invB;

    // 切向摩擦冲量（库仑：|jt| 限制为 mu * 法向冲量）
    const Vec2 rv2(b->linearVelocity.x - a->linearVelocity.x,
                   b->linearVelocity.y - a->linearVelocity.y);
    const Vec2 t(-c.normal.y, c.normal.x);
    float jt = glm::dot(rv2, t) / invSum;
    const float maxF = c.friction * j;
    jt = std::max(-maxF, std::min(maxF, jt));
    const Vec2 tImpulse = t * jt;
    a->linearVelocity.x += tImpulse.x * invA;
    a->linearVelocity.y += tImpulse.y * invA;
    b->linearVelocity.x -= tImpulse.x * invB;
    b->linearVelocity.y -= tImpulse.y * invB;
}

void PhysicsWorld2D::ResolvePosition(const Contact2D &c)
{
    RigidBody *a = c.a;
    RigidBody *b = c.b;
    const float invSum = a->invMass + b->invMass;
    if (invSum == 0.0f)
        return;

    const float pen = std::max(c.penetration - slop, 0.0f);
    const Vec2 correction = c.normal * (pen / invSum) * positionCorrectionPercent;
    a->position.x -= correction.x * a->invMass;
    a->position.y -= correction.y * a->invMass;
    b->position.x += correction.x * b->invMass;
    b->position.y += correction.y * b->invMass;
}

void PhysicsWorld2D::CollectDebugLines()
{
    debugLines_.clear();
    if (!debugDraw)
        return;

    const Vec4 shapeColor(1.0f, 1.0f, 1.0f, 1.0f);
    const Vec4 aabbColor(0.2f, 0.9f, 0.9f, 1.0f);
    const Vec4 contactColor(1.0f, 0.30f, 0.30f, 1.0f);

    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        AddShapeDebug(*c, shapeColor);
        if (debugDrawAABB)
            AddAABBDebug(*c, aabbColor);
    }
    for (auto &c : contacts_)
    {
        // 接触点十字标记（法线方向 + 切线方向）
        const Vec2 p = c.point;
        const Vec2 n = c.normal * 0.25f;
        const Vec2 t(-n.y, n.x);
        debugLines_.push_back({Vec3(p - t, 0.0f), Vec3(p + t, 0.0f), contactColor});
        debugLines_.push_back({Vec3(p - n, 0.0f), Vec3(p + n, 0.0f), contactColor});
    }
}

void PhysicsWorld2D::AddShapeDebug(const Collider2D &c, const Vec4 &color)
{
    constexpr float kTwoPi = 6.283185307179586f;
    switch (c.shape->Type())
    {
    case ShapeType::Circle2D:
    {
        const auto &s = static_cast<const CircleShape &>(*c.shape);
        const int segs = 24;
        const Vec2 center = XY(c.body->position);
        for (int i = 0; i < segs; i++)
        {
            const float a0 = kTwoPi * (float)i / (float)segs;
            const float a1 = kTwoPi * (float)(i + 1) / (float)segs;
            const Vec2 p0 = center + Vec2(std::cos(a0), std::sin(a0)) * s.radius;
            const Vec2 p1 = center + Vec2(std::cos(a1), std::sin(a1)) * s.radius;
            debugLines_.push_back({Vec3(p0, 0.0f), Vec3(p1, 0.0f), color});
        }
        break;
    }
    case ShapeType::Rect2D:
    {
        const auto &s = static_cast<const RectShape2D &>(*c.shape);
        const float angle = glm::eulerAngles(c.body->rotation).z;
        const float cs = std::cos(angle), sn = std::sin(angle);
        const Vec2 ax(cs, sn), ay(-sn, cs);
        const Vec2 hx = ax * s.halfExtents.x, hy = ay * s.halfExtents.y;
        const Vec2 center = XY(c.body->position);
        const Vec2 corners[4] = {center - hx - hy, center + hx - hy,
                                 center + hx + hy, center - hx + hy};
        for (int i = 0; i < 4; i++)
            debugLines_.push_back({Vec3(corners[i], 0.0f),
                                   Vec3(corners[(i + 1) % 4], 0.0f), color});
        break;
    }
    default:
        break; // 3D 形状（M3）
    }
}

void PhysicsWorld2D::AddAABBDebug(const Collider2D &c, const Vec4 &color)
{
    const AABB box = c.shape->ComputeWorldAABB(*c.body);
    const Vec2 corners[4] = {{box.min.x, box.min.y}, {box.max.x, box.min.y},
                             {box.max.x, box.max.y}, {box.min.x, box.max.y}};
    for (int i = 0; i < 4; i++)
        debugLines_.push_back({Vec3(corners[i], 0.0f),
                               Vec3(corners[(i + 1) % 4], 0.0f), color});
}

bool PhysicsWorld2D::Raycast(const Ray &ray, float maxDist, RaycastHit &out) const
{
    bool found = false;
    out = RaycastHit{};
    float best = maxDist;

    std::vector<Collider2D *> dyn, stat;
    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        (c->body->IsStatic() ? stat : dyn).push_back(c);
    }

    auto test = [&](const Collider2D *c)
    {
        float t = 0.0f;
        const Vec3 &center = c->body->position;
        if (c->shape->Type() == ShapeType::Circle2D)
        {
            const auto &s = static_cast<const CircleShape &>(*c->shape);
            if (!RayCircle2D(ray, XY(center), s.radius, best, t))
                return;
            best = t;
            out.hit = true;
            out.t = t;
            out.point = ray.origin + ray.direction * t;
            out.normal = Vec3(glm::normalize(XY(out.point) - XY(center)), 0.0f);
            out.collider2D = const_cast<Collider2D *>(c);
            found = true;
        }
        else if (c->shape->Type() == ShapeType::Rect2D)
        {
            const auto &s = static_cast<const RectShape2D &>(*c->shape);
            const float angle = glm::eulerAngles(c->body->rotation).z;
            if (!RayRect2D(ray, XY(center), s.halfExtents, angle, best, t))
                return;
            best = t;
            out.hit = true;
            out.t = t;
            out.point = ray.origin + ray.direction * t;
            const float c2 = std::cos(angle), s2 = std::sin(angle);
            const Vec2 ax(c2, s2), ay(-s2, c2);
            const Vec2 local = XY(out.point) - XY(center);
            const float lx = glm::dot(local, ax), ly = glm::dot(local, ay);
            const Vec2 n2 = (std::fabs(lx) / s.halfExtents.x > std::fabs(ly) / s.halfExtents.y)
                                ? ax * (lx >= 0.0f ? 1.0f : -1.0f)
                                : ay * (ly >= 0.0f ? 1.0f : -1.0f);
            out.normal = Vec3(n2, 0.0f);
            out.collider2D = const_cast<Collider2D *>(c);
            found = true;
        }
    };

    for (auto *c : dyn)
        test(c);
    if (useBVH && !staticBvh_.Empty())
    {
        std::vector<int> hits;
        staticBvh_.QueryRay(ray, best, hits);
        for (int k : hits)
            test(stat[(size_t)k]);
    }
    else
    {
        for (auto *c : stat)
            test(c);
    }
    return found;
}

bool PhysicsWorld2D::RaycastAll(const Ray &ray, float maxDist,
                                std::vector<RaycastHit> &out) const
{
    out.clear();
    for (auto *c : colliders_)
    {
        if (!c || !c->enabled || !c->shape || !c->body)
            continue;
        const Vec3 &center = c->body->position;
        float t = 0.0f;
        bool hit = false;
        if (c->shape->Type() == ShapeType::Circle2D)
        {
            const auto &s = static_cast<const CircleShape &>(*c->shape);
            hit = RayCircle2D(ray, XY(center), s.radius, maxDist, t);
        }
        else if (c->shape->Type() == ShapeType::Rect2D)
        {
            const auto &s = static_cast<const RectShape2D &>(*c->shape);
            hit = RayRect2D(ray, XY(center), s.halfExtents,
                            glm::eulerAngles(c->body->rotation).z, maxDist, t);
        }
        if (hit)
        {
            RaycastHit h;
            h.hit = true;
            h.t = t;
            h.point = ray.origin + ray.direction * t;
            h.collider2D = c;
            out.push_back(h);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const RaycastHit &a, const RaycastHit &b) { return a.t < b.t; });
    return !out.empty();
}

} // namespace aster
