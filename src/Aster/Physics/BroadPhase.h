#pragma once
#include <vector>
#include "Collider.h"

namespace aster
{

// ============================================================================
// BroadPhase —— 宽相（候选碰撞对生成）
// ----------------------------------------------------------------------------
// M2 用暴力 O(n²) AABB 对生成（物体数少时最快、最简单，作为 BVH 的对照基线）。
// M5 将静态物体改用 BVH 加速（dynamic-vs-static 用 BVH 查询）。
// ============================================================================

// 候选对：a / b 指向碰撞体（a 在列表中更靠前，避免重复对）
struct BodyPair
{
    Collider2D *a = nullptr;
    Collider2D *b = nullptr;
};

// 3D 候选对（M3）
struct BodyPair3D
{
    struct Collider3D *a = nullptr;
    struct Collider3D *b = nullptr;
};

class BroadPhase
{
public:
    // 暴力宽相：遍历所有启用的碰撞体，AABB（2D，z 忽略）相交的构成候选对。
    // out 清空后填充；排除 (a==b) 与 (a,b) 顺序重复。
    static void BruteForce2D(const std::vector<Collider2D *> &colliders,
                             std::vector<BodyPair> &out);

    // 3D 暴力宽相（M3）：AABB 全分量相交
    static void BruteForce3D(const std::vector<struct Collider3D *> &colliders,
                             std::vector<BodyPair3D> &out);
};

} // namespace aster
