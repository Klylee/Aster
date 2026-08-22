#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Transform.h" // Vec2/Vec3/Vec4/Mat4/Quat 全局别名（glm，无 GL 依赖）

namespace aster
{

// ============================================================================
// RigidBody —— 刚体（纯运动学数据 + 积分，无渲染 / 场景依赖）
// ----------------------------------------------------------------------------
// 只保存运动状态与受力累积，积分由 PhysicsWorld 驱动：
//   Step(dt)：清力 → 重力 / 外力积分速度 → 半隐式欧拉积分位置 / 旋转 → 清力
//
//   - 3D 世界使用全部 Vec3 分量；2D 世界只用 xy（z 恒为 0，见 PhysicsWorld2D）。
//   - invMass == 0 表示静态（无穷质量，不响应力 / 重力 / 位置积分）。
//   - isKinematic 表示运动学体：不响应力 / 重力（IntegrateVelocity 跳过），
//     但位置 / 旋转仍按 velocity 推进（用于由外部驱动的物体，如平台 / 摄像机跟随体）。
//
// 坐标系：+Y 向上。角度单位为弧度，旋转用四元数。
// ============================================================================
class RigidBody
{
public:
    RigidBody() = default;

    // ---- 质量 ----
    float mass = 1.0f;
    float invMass = 1.0f; // 0 = 静态（无穷质量）
    void SetMass(float m);
    bool IsStatic() const { return invMass == 0.0f; }
    void SetStatic(bool s); // 切换静态 / 动态（动态恢复质量 1）

    // ---- 运动状态 ----
    Vec3 position{0.0f};          // 世界坐标（2D 只用 xy）
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // 世界旋转
    Vec3 linearVelocity{0.0f};    // 线速度（单位 / 秒）
    Vec3 angularVelocity{0.0f};   // 角速度（弧度 / 秒，方向 = 旋转轴）

    // ---- 动力学参数 ----
    float gravityScale = 1.0f;    // 重力缩放（负值 = 反重力）
    float linearDamping = 0.0f;   // 线速度阻尼（比例 / 秒：每帧乘 (1 - damping*dt)）
    float angularDamping = 0.0f;  // 角速度阻尼
    bool isKinematic = false;     // 运动学体：不响应力 / 重力，由 velocity 驱动

    // ---- 力 / 力矩累积（每帧 Step 开始时清零） ----
    Vec3 forceAccum{0.0f};
    Vec3 torqueAccum{0.0f};

    void AddForce(const Vec3 &f);
    void AddForceAtPoint(const Vec3 &f, const Vec3 &worldPoint); // 产生力矩
    void AddTorque(const Vec3 &t);
    void SetVelocity(const Vec3 &v) { linearVelocity = v; }
    void SetAngularVelocity(const Vec3 &w) { angularVelocity = w; }
    void ClearForces() { forceAccum = Vec3(0.0f); torqueAccum = Vec3(0.0f); }

    // 积分速度：v += (force * invMass + gravity * gravityScale - damping) * dt
    // 静态 / 运动学体不响应（跳过）。
    void IntegrateVelocity(float dt, const Vec3 &gravity);

    // 积分位置 / 旋转（半隐式欧拉）：p += v * dt；q += 0.5 * omega_q * q * dt
    // 静态体跳过；运动学体只做位置 / 旋转积分。
    void IntegratePosition(float dt);
};

} // namespace aster
