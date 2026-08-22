#include "BVH.h"

#include <algorithm>

namespace aster
{

void BVH::Clear()
{
    nodes_.clear();
    order_.clear();
    boxes_.clear();
    rootBounds_ = AABB{};
}

void BVH::Build(const std::vector<AABB> &boxes, int leafSize)
{
    Clear();
    leafSize_ = std::max(1, leafSize);
    boxes_ = boxes;
    const size_t n = boxes_.size();
    if (n == 0)
        return;

    std::vector<int> indices(n);
    for (size_t i = 0; i < n; i++)
        indices[i] = (int)i;

    BuildRecursive(indices, 0, (int)n);
    rootBounds_ = nodes_.front().bounds;
    order_ = indices; // 建树后的最终排列：叶子 [start, start+count) 切片有效
}

int BVH::BuildRecursive(std::vector<int> &indices, int begin, int end)
{
    Node n;
    n.start = begin;
    n.count = end - begin;
    for (int i = begin; i < end; i++)
        n.bounds.Expand(boxes_[indices[i]]);
    const int idx = (int)nodes_.size();
    nodes_.push_back(n);

    if (end - begin <= leafSize_)
        return idx; // 叶子

    // 最长轴中位切分
    const Vec3 e = n.bounds.Extents();
    int axis = 0;
    if (e.y > e.x && e.y > e.z)
        axis = 1;
    else if (e.z > e.x && e.z > e.y)
        axis = 2;

    const int mid = (begin + end) / 2;
    std::nth_element(indices.begin() + begin, indices.begin() + mid, indices.begin() + end,
                     [&](int a, int b)
                     { return boxes_[a].Center()[axis] < boxes_[b].Center()[axis]; });
    if (mid == begin || mid == end)
        return idx; // 退化（中心完全相同）：强制为叶子

    nodes_[idx].left = BuildRecursive(indices, begin, mid);
    nodes_[idx].right = BuildRecursive(indices, mid, end);
    return idx;
}

int BVH::MaxDepth() const
{
    if (nodes_.empty())
        return 0;
    return MaxDepthRecursive(0, 0);
}

int BVH::MaxDepthRecursive(int node, int depth) const
{
    const Node &n = nodes_[node];
    if (n.left < 0 && n.right < 0)
        return depth;
    const int dl = (n.left >= 0) ? MaxDepthRecursive(n.left, depth + 1) : depth;
    const int dr = (n.right >= 0) ? MaxDepthRecursive(n.right, depth + 1) : depth;
    return std::max(dl, dr);
}

void BVH::QueryAABB(const AABB &box, std::vector<int> &out) const
{
    if (nodes_.empty())
        return;
    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty())
    {
        const int ni = stack.back();
        stack.pop_back();
        const Node &n = nodes_[ni];
        if (!n.bounds.Overlaps(box))
            continue;
        if (n.left < 0)
        {
            for (int i = n.start; i < n.start + n.count; i++)
                out.push_back(order_[i]);
        }
        else
        {
            stack.push_back(n.left);
            stack.push_back(n.right);
        }
    }
}

void BVH::QueryRay(const Ray &ray, float maxDist, std::vector<int> &out) const
{
    if (nodes_.empty())
        return;
    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty())
    {
        const int ni = stack.back();
        stack.pop_back();
        const Node &n = nodes_[ni];
        float t = 0.0f;
        Vec3 nrm(0.0f);
        if (!RayAABB(ray, n.bounds, maxDist, t, nrm))
            continue;
        if (n.left < 0)
        {
            for (int i = n.start; i < n.start + n.count; i++)
                out.push_back(order_[i]);
        }
        else
        {
            stack.push_back(n.left);
            stack.push_back(n.right);
        }
    }
}

} // namespace aster
