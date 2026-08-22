#pragma once
#include "CollisionShape.h"
#include "Contact.h"

namespace aster
{

// ============================================================================
// NarrowPhase —— 2D 窄相（精确求交 + 接触生成）
// ----------------------------------------------------------------------------
// 所有函数返回是否碰撞；命中时填充 out：
//   out.a / out.b = 对应刚体；out.normal = 从 a 指向 b；out.penetration > 0。
// 约定：传入函数时，第一个参数对应 a，第二个对应 b。
// ============================================================================

// 圆-圆
bool CircleCircle(const CircleShape &aShape, const RigidBody &aBody,
                  const CircleShape &bShape, const RigidBody &bBody,
                  Contact2D &out);

// 圆-矩形（圆为 a，矩形为 b）
bool CircleRect(const CircleShape &cShape, const RigidBody &cBody,
                const RectShape2D &rShape, const RigidBody &rBody,
                Contact2D &out);

// 矩形-矩形（SAT，2D OBB）
bool RectRect(const RectShape2D &aShape, const RigidBody &aBody,
              const RectShape2D &bShape, const RigidBody &bBody,
              Contact2D &out);

// 按形状类型分发（2D 形状）。aBody / bBody 为形状所属刚体。
// 任意顺序（圆-矩形 / 矩形-圆）都返回一致的 Contact2D（a=第一个形状的刚体）。
bool Collide2D(const CollisionShape &aShape, const RigidBody &aBody,
               const CollisionShape &bShape, const RigidBody &bBody,
               Contact2D &out);

// ============================================================================
// 3D 窄相（M3）—— 约定与 2D 一致：normal 从 a 指向 b，penetration > 0
// ============================================================================

// 球-球
bool SphereSphere(const SphereShape &aShape, const RigidBody &aBody,
                  const SphereShape &bShape, const RigidBody &bBody,
                  Contact3D &out);

// 球-盒（球为 a，盒为 b）
bool SphereBox(const SphereShape &sShape, const RigidBody &sBody,
               const BoxShape &bShape, const RigidBody &bBody,
               Contact3D &out);

// 盒-盒（SAT，3D OBB）
bool BoxBox(const BoxShape &aShape, const RigidBody &aBody,
            const BoxShape &bShape, const RigidBody &bBody,
            Contact3D &out);

// 按形状类型分发（3D 形状）
bool Collide3D(const CollisionShape &aShape, const RigidBody &aBody,
               const CollisionShape &bShape, const RigidBody &bBody,
               Contact3D &out);

} // namespace aster
