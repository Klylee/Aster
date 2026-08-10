#pragma once
#include <map>
#include <memory>
#include "Aster/Resource/Material.h"
#include "Aster/Resource/Mesh.h"
#include <glm/glm.hpp>

namespace aster
{

struct DrawCall
{
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    glm::mat4 modelMatrix;
};

// RenderGraphNode[shader shader;
//                 parentPtr;
//                 inputs; // bufferIDs
// ] RenderGraph
// {
// }

// pass = 0, 1;

// sort(passes) for (auto pass : passes)
// {
//     FlushBatches();
// }

class Renderer
{
    struct RenderInstance
    {
        glm::mat4 modelMatrix;
    };

    struct BatchKey
    {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        bool operator==(const BatchKey &other) const
        {
            return mesh == other.mesh && material == other.material;
        }
    };

    struct BatchKeyHash
    {
        size_t operator()(const BatchKey &key) const noexcept
        {
            return std::hash<void *>()(key.mesh.get()) ^ (std::hash<void *>()(key.material.get()) << 1);
        }
    };

    struct RenderQueueBatch
    {
        std::unordered_map<BatchKey, std::vector<RenderInstance>, BatchKeyHash> batches;
        std::vector<DrawCall> transparentDrawCalls;
    };

    std::map<int, RenderQueueBatch> renderQueues; // key is RenderQueue enum

public:
    static Renderer &Instance()
    {
        static Renderer instance;
        return instance;
    }
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer() = default;

    void SubmitDrawCall(const DrawCall &drawCall);
    void FlushBatches(const glm::mat4 &viewMatrix, const glm::mat4 &projMatrix);
};

} // namespace aster
