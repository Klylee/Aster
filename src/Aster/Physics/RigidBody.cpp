#include "RigidBody.h"

#include <algorithm>
#include <glm/gtc/quaternion.hpp>

namespace aster
{

void RigidBody::SetMass(float m)
{
    mass = m;
    invMass = (m > 0.0f) ? 1.0f / m : 0.0f;
}

void RigidBody::SetStatic(bool s)
{
    if (s)
    {
        mass = 0.0f;
        invMass = 0.0f;
    }
    else
    {
        mass = 1.0f;
        invMass = 1.0f;
    }
}

void RigidBody::AddForce(const Vec3 &f)
{
    forceAccum += f;
}

void RigidBody::AddForceAtPoint(const Vec3 &f, const Vec3 &worldPoint)
{
    forceAccum += f;
    torqueAccum += glm::cross(worldPoint - position, f);
}

void RigidBody::AddTorque(const Vec3 &t)
{
    torqueAccum += t;
}

void RigidBody::IntegrateVelocity(float dt, const Vec3 &gravity)
{
    if (IsStatic() || isKinematic)
        return;

    // 外力（力 = 质量 * 加速度 → a = F / m）
    if (invMass > 0.0f)
        linearVelocity += (forceAccum * invMass) * dt;

    // 重力（缩放）
    linearVelocity += (gravity * gravityScale) * dt;

    // 角速度（简化：转动惯量按单位矩阵处理，力矩直接改角速度）
    angularVelocity += torqueAccum * dt;

    // 阻尼（每帧按比例衰减）
    if (linearDamping > 0.0f)
        linearVelocity *= std::max(0.0f, 1.0f - linearDamping * dt);
    if (angularDamping > 0.0f)
        angularVelocity *= std::max(0.0f, 1.0f - angularDamping * dt);
}

void RigidBody::IntegratePosition(float dt)
{
    if (IsStatic())
        return;

    // 半隐式欧拉：位置用“刚更新完的速度”
    position += linearVelocity * dt;

    // 旋转积分：q' = 0.5 * omega_q * q，omega_q = (0, wx, wy, wz)
    if (glm::length(angularVelocity) > 1e-6f)
    {
        Quat dq(0.0f,
                angularVelocity.x * dt * 0.5f,
                angularVelocity.y * dt * 0.5f,
                angularVelocity.z * dt * 0.5f);
        rotation = glm::normalize(rotation + dq * rotation);
    }
}

} // namespace aster
