#include "BroadPhase.h"

#include "CollisionShape.h"

namespace aster
{

void BroadPhase::BruteForce2D(const std::vector<Collider2D *> &colliders,
                              std::vector<BodyPair> &out)
{
    out.clear();
    const size_t n = colliders.size();
    for (size_t i = 0; i < n; i++)
    {
        Collider2D *ca = colliders[i];
        if (!ca || !ca->enabled || !ca->shape || !ca->body)
            continue;
        const AABB aa = ca->shape->ComputeWorldAABB(*ca->body);

        for (size_t j = i + 1; j < n; j++)
        {
            Collider2D *cb = colliders[j];
            if (!cb || !cb->enabled || !cb->shape || !cb->body)
                continue;
            const AABB ab = cb->shape->ComputeWorldAABB(*cb->body);
            if (aa.Overlaps2D(ab))
                out.push_back({ca, cb});
        }
    }
}

void BroadPhase::BruteForce3D(const std::vector<Collider3D *> &colliders,
                              std::vector<BodyPair3D> &out)
{
    out.clear();
    const size_t n = colliders.size();
    for (size_t i = 0; i < n; i++)
    {
        Collider3D *ca = colliders[i];
        if (!ca || !ca->enabled || !ca->shape || !ca->body)
            continue;
        const AABB aa = ca->shape->ComputeWorldAABB(*ca->body);

        for (size_t j = i + 1; j < n; j++)
        {
            Collider3D *cb = colliders[j];
            if (!cb || !cb->enabled || !cb->shape || !cb->body)
                continue;
            const AABB ab = cb->shape->ComputeWorldAABB(*cb->body);
            if (aa.Overlaps(ab))
                out.push_back({ca, cb});
        }
    }
}

} // namespace aster
