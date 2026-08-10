#include "Renderer.h"

namespace aster
{

uint64_t MakeSortKey(const std::shared_ptr<Material> &material,
                     const std::shared_ptr<Mesh> &mesh)
{
    auto h1 = reinterpret_cast<uint64_t>(material.get());
    auto h2 = reinterpret_cast<uint64_t>(mesh.get());
    return (h1 << 24) ^ (h2 & 0xFFFFFF);
}

void Renderer::SubmitDrawCall(const DrawCall &drawCall)
{
    std::shared_ptr<Material> material = drawCall.material;
    if (material->renderQueue >= RenderQueue::Transparent && material->renderQueue < RenderQueue::Overlay)
    {
        renderQueues[material->renderQueue].transparentDrawCalls.push_back(drawCall);
    }
    else
    {
        renderQueues[material->renderQueue].batches[BatchKey{drawCall.mesh, drawCall.material}].push_back({drawCall.modelMatrix});
    }
}

void Renderer::FlushBatches(const glm::mat4 &viewMatrix, const glm::mat4 &projMatrix)
{
    struct SortedBatch
    {
        uint64_t sortKey;
        std::shared_ptr<Material> material;
        std::shared_ptr<Mesh> mesh;
        std::vector<RenderInstance> *instances;
    };

    for (auto &[rq, bucket] : renderQueues)
    {
        if (rq < RenderQueue::Transparent || rq >= RenderQueue::Overlay)
        {
            std::vector<SortedBatch> sortedBatches;
            sortedBatches.reserve(bucket.batches.size());
            for (auto &[key, instances] : bucket.batches)
            {
                SortedBatch batch;
                batch.material = key.material;
                batch.mesh = key.mesh;
                batch.instances = &instances;
                batch.sortKey = MakeSortKey(key.material, key.mesh);
                sortedBatches.push_back(batch);
            }

            std::sort(sortedBatches.begin(), sortedBatches.end(),
                      [](const SortedBatch &a, const SortedBatch &b)
                      {
                          return a.sortKey < b.sortKey;
                      });

            std::shared_ptr<Shader> currentShader = nullptr;

            for (auto &batch : sortedBatches)
            {
                auto &mesh = batch.mesh;
                auto &material = batch.material;
                auto &instances = batch.instances;

                if (currentShader != material->GetShader())
                {
                    currentShader = material->GetShader();
                }

                if (instances->size() > 1)
                    currentShader->Use(ShaderVariant::Instanced);
                else
                    currentShader->Use(ShaderVariant::Basic);
                currentShader->SetUniformMat4x4f("view", viewMatrix);
                currentShader->SetUniformMat4x4f("projection", projMatrix);

                material->ApplyRenderState();
                material->ApplyUniforms();

                if (instances->size() == 1)
                {
                    currentShader->SetUniformMat4x4f("model", instances->at(0).modelMatrix);
                    mesh->draw(currentShader);
                }
                else
                {
                    // std::cout << "Drawing " << instances->size() << " instances of mesh." << std::endl;
                    // 实例化渲染
                    std::vector<glm::mat4> modelMatrices;
                    modelMatrices.reserve(instances->size());
                    for (auto &inst : *instances)
                    {
                        modelMatrices.push_back(inst.modelMatrix);
                    }
                    mesh->drawInstanced(currentShader, modelMatrices);
                }
            }

            bucket.batches.clear();
        }
        else
        {
            // 透明物体单独处理，按距离摄像机远近排序后再绘制
            std::vector<std::pair<float, DrawCall>> transparentDrawCallsWithDistance;
            for (auto &drawCall : bucket.transparentDrawCalls)
            {
                glm::vec4 modelPos = glm::vec4(drawCall.modelMatrix[3][0], drawCall.modelMatrix[3][1], drawCall.modelMatrix[3][2], 1.0f);
                glm::vec4 viewPos = viewMatrix * modelPos;
                float distance = -viewPos.z; // 负值，越大越远
                transparentDrawCallsWithDistance.push_back({distance, drawCall});
            }

            std::sort(transparentDrawCallsWithDistance.begin(), transparentDrawCallsWithDistance.end(),
                      [](const std::pair<float, DrawCall> &a, const std::pair<float, DrawCall> &b)
                      {
                          return a.first > b.first; // 从远到近
                      });

            for (auto &[distance, drawCall] : transparentDrawCallsWithDistance)
            {
                auto &mesh = drawCall.mesh;
                auto &material = drawCall.material;
                auto shader = material->GetShader();
                shader->Use(ShaderVariant::Basic);
                shader->SetUniformMat4x4f("view", viewMatrix);
                shader->SetUniformMat4x4f("projection", projMatrix);
                shader->SetUniformMat4x4f("model", drawCall.modelMatrix);

                material->ApplyRenderState();
                material->ApplyUniforms();

                mesh->draw(shader);
            }

            bucket.transparentDrawCalls.clear();
        }
    }
}

} // namespace aster
