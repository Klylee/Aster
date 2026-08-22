#pragma once
#include <vector>
#include "AABB.h"
#include "Ray.h"

namespace aster
{

// ============================================================================
// BVH —— Bounding Volume Hierarchy（轴对齐包围盒层次结构）
// ----------------------------------------------------------------------------
// 自顶向下建树（最长轴中位切分），叶子节点存原始 AABB 索引的范围。
// 用于静态碰撞体（static colliders）的宽相与射线加速：
//   - QueryAABB ：收集与给定 AABB 重叠的原始索引（dynamic-vs-static 宽相）
//   - QueryRay  ：收集被射线命中的原始索引（射线拾取 / 阴影）
// 2D 使用 z=0 的 AABB，重叠判断退化为 2D。
// ============================================================================
class BVH
{
public:
    void Build(const std::vector<AABB> &boxes, int leafSize = 4);
    void Clear();

    bool Empty() const { return nodes_.empty(); }
    int NodeCount() const { return (int)nodes_.size(); }
    int MaxDepth() const;
    size_t PrimitiveCount() const { return boxes_.size(); }
    const AABB &Bounds() const { return rootBounds_; }

    // 收集与 box 重叠的原始 AABB 索引到 out（无序，可能重复顺序不稳定）
    void QueryAABB(const AABB &box, std::vector<int> &out) const;
    // 收集在 maxDist 内被 ray 命中的原始 AABB 索引到 out
    void QueryRay(const Ray &ray, float maxDist, std::vector<int> &out) const;

private:
    struct Node
    {
        AABB bounds;
        int left = -1;   // 左子树节点索引（-1 = 叶子）
        int right = -1;  // 右子树节点索引
        int start = 0;   // 叶子：原始索引在 order_ 中的起始
        int count = 0;   // 叶子：原始索引数量
    };

    std::vector<Node> nodes_;
    std::vector<int> order_;   // 叶子内的原始 AABB 索引（建树时重排）
    std::vector<AABB> boxes_;  // 建树时的 AABB 快照
    AABB rootBounds_{};
    int leafSize_ = 4;

    // 递归建树，返回节点索引；indices 为当前分区的原始索引（建树后 = 最终排列）
    int BuildRecursive(std::vector<int> &indices, int begin, int end);
    int MaxDepthRecursive(int node, int depth) const;
};

} // namespace aster
