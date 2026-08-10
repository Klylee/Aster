#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include "Mesh.h"
#include "Material.h"

namespace aster
{

struct MeshAsset
{
    std::string key; // 唯一标识符（路径或hash）
    std::vector<std::string> meshes;
    std::unordered_map<std::string, std::tuple<glm::vec3, glm::vec3, std::string>> bones; // name-> <head, tail, parentName>
};

class MeshManager
{
public:
    static MeshManager &Instance()
    {
        static MeshManager instance;
        return instance;
    }

    MeshManager(const MeshManager &) = delete;
    MeshManager &operator=(const MeshManager &) = delete;

    // 已加载的Mesh资源
    std::unordered_map<std::string, MeshAsset> meshAssets;
    std::unordered_map<std::string, std::string> meshToAssetMap; // 由mesh key到asset key的映射，因为一个asset可能包含多个mesh部件

    // 通过路径加载或获取已有 Mesh
    std::tuple<std::vector<std::shared_ptr<Mesh>>, std::unordered_map<std::string, std::tuple<glm::vec3, glm::vec3, std::string>>>
    LoadMesh(const std::string &dict, const std::string &filename);

    // 从Assimp直接加载
    std::shared_ptr<Mesh> LoadMesh(aiMesh *mesh, const aiScene *scene, const std::string &dict);

    std::shared_ptr<Mesh> LoadMeshFromRawData(const std::string &dict,
                                              const std::vector<float> &vertices,
                                              const std::vector<unsigned int> &indices,
                                              const std::vector<float> &normals = std::vector<float>());

    // 根据唯一key（例如path或hash）获取已存在mesh
    std::shared_ptr<Mesh> Get(const std::string &key);

    // // 提交绘制请求(需要绘制的SceneObject在Draw里面调用）
    // void Submit(const std::shared_ptr<Mesh> &mesh,
    //             const std::shared_ptr<Material> &material,
    //             const glm::mat4 &modelMatrix);

    // 执行所有批次绘制
    // void FlushBatches(const glm::mat4 &viewMatrix, const glm::mat4 &projMatrix);

    // 定期清除没用的mesh
    void CleanupUnusedMeshes(int keepFrames = 300)
    { // 保持300帧（约5秒）
        currentFrame++;

        for (auto it = meshCache.begin(); it != meshCache.end();)
        {
            const std::string key = it->first;
            std::shared_ptr<Mesh> &mesh = it->second;

            // 检查是否长时间未使用且引用计数为1（只有cache持有）
            if (mesh.use_count() == 1)
            {
                if (currentFrame - meshLastUsedFrame[key] > keepFrames)
                {
                    std::cout << "Cleaning up unused mesh: " << key << std::endl;
                    std::string assetKey = meshToAssetMap[key];
                    meshAssets.erase(assetKey);
                    meshToAssetMap.erase(key);
                    it = meshCache.erase(it);
                    meshLastUsedFrame.erase(key);
                }
                else
                {
                    ++it;
                }
            }
            else
            {
                meshLastUsedFrame[key] = currentFrame;
                ++it;
            }
        }
    }

    // 清除所有资源（例如场景切换）
    void Clear();

    // 打印当前缓存状态
    void PrintStatus() const;

private:
    MeshManager() = default;

    // 缓存mesh资源
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshCache;
    std::unordered_map<std::string, int> meshLastUsedFrame;
    int currentFrame = 0;

    // 检查batches中是否包含某个mesh
    bool batchesContainsMesh(const std::shared_ptr<Mesh> &mesh)
    {
        for (const auto &[batchKey, instances] : batches)
        {
            if (batchKey.mesh == mesh)
            {
                return true;
            }
        }
        return false;
    }

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

    std::unordered_map<BatchKey, std::vector<RenderInstance>, BatchKeyHash> batches;
};

} // namespace aster

