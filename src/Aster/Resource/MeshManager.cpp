#include "MeshManager.h"
#include <iostream>
#include <format>
#include <filesystem>
#ifdef ASTER_ENABLE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#endif

#include "Path.h"

namespace aster
{

// 添加一个辅助函数来检测文件格式
enum class FileFormat
{
    GLB,
    FBX,
    UNKNOWN
};

FileFormat GetFileFormat(const std::string &filename)
{
    std::string ext = filename.substr(filename.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "glb" || ext == "gltf")
    {
        return FileFormat::GLB;
    }
    else if (ext == "fbx")
    {
        return FileFormat::FBX;
    }
    return FileFormat::UNKNOWN;
}

// 以下（GetGlobalTransform / GetHead / GetBoneTailExact / processNode /
// LoadMesh(dict,filename) / LoadMesh(aiMesh,...)）依赖 assimp，仅在启用时编译；
// LoadMeshFromRawData / Get / Clear 等无 assimp 依赖，始终可用。
#ifdef ASTER_ENABLE_ASSIMP

// 改进的全局变换获取函数，处理FBX的坐标系差异
aiMatrix4x4 GetGlobalTransform(aiNode *node, FileFormat format = FileFormat::GLB)
{
    aiMatrix4x4 transform = node->mTransformation;
    aiNode *parent = node->mParent;

    // FBX可能需要额外的坐标系转换
    if (format == FileFormat::FBX)
    {
        // FBX使用Y轴向上，而OpenGL使用Y轴向上，但需要处理Z轴方向
        // 这里添加一个可选的坐标系转换
        aiMatrix4x4 fbxToGlm;
        // 根据实际需要调整，这里假设不需要额外转换
        // 如果需要转换到右手坐标系，可以在这里添加
    }

    while (parent)
    {
        transform = parent->mTransformation * transform;
        parent = parent->mParent;
    }
    return transform;
}

// 改进的获取骨骼头位置函数
glm::vec3 GetHead(aiNode *node, FileFormat format = FileFormat::GLB)
{
    aiMatrix4x4 global = GetGlobalTransform(node, format);
    aiVector3D origin(0, 0, 0);
    aiVector3D worldPos = global * origin;
    return glm::vec3(worldPos.x, worldPos.y, worldPos.z);
}

// 改进的获取骨骼尾位置函数，支持FBX的多种命名约定
glm::vec3 GetBoneTailExact(aiNode *node, FileFormat format = FileFormat::GLB)
{
    aiMatrix4x4 global = GetGlobalTransform(node, format);

    // 查找尾节点 - 支持多种命名约定
    aiNode *tailNode = nullptr;

    // FBX中骨骼尾节点可能有不同的命名模式
    std::vector<std::string> tailSuffixes = {
        "_end", "_End", "end", "End",
        "_tip", "_Tip", "tip", "Tip",
        "_eff", "_Eff", "eff", "Eff" // effector
    };

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        std::string childName = node->mChildren[i]->mName.C_Str();

        // 检查各种尾节点命名模式
        for (const auto &suffix : tailSuffixes)
        {
            if (childName.find(suffix) != std::string::npos)
            {
                tailNode = node->mChildren[i];
                break;
            }
        }

        // 对于FBX，也检查是否是末端骨骼（只有一个子节点且该子节点没有更多子节点）
        if (format == FileFormat::FBX && !tailNode)
        {
            aiNode *child = node->mChildren[i];
            if (child->mNumChildren == 0 &&
                (childName.find("_") == std::string::npos ||
                 childName.find("_end") == std::string::npos))
            {
                // 可能是末端骨骼
                tailNode = child;
                break;
            }
        }

        if (tailNode)
            break;
    }

    if (tailNode)
    {
        aiMatrix4x4 tailGlobal = GetGlobalTransform(tailNode, format);
        aiVector3D tailPos = tailGlobal * aiVector3D(0, 0, 0);
        return glm::vec3(tailPos.x, tailPos.y, tailPos.z);
    }
    else
    {
        // 没有尾节点，根据格式选择合适的默认长度
        float defaultLength = (format == FileFormat::FBX) ? 0.05f : 0.1f;

        // 尝试从骨骼名称推断方向
        std::string nodeName = node->mName.C_Str();
        aiVector3D direction(0, defaultLength, 0);

        // 根据骨骼名称推断可能的轴向（常见命名模式）
        if (nodeName.find("Spine") != std::string::npos ||
            nodeName.find("Neck") != std::string::npos ||
            nodeName.find("Head") != std::string::npos)
        {
            direction = aiVector3D(0, defaultLength, 0);
        }
        else if (nodeName.find("Arm") != std::string::npos ||
                 nodeName.find("ForeArm") != std::string::npos ||
                 nodeName.find("Leg") != std::string::npos ||
                 nodeName.find("Shin") != std::string::npos)
        {
            direction = aiVector3D(defaultLength, 0, 0);
        }
        else
        {
            direction = aiVector3D(0, defaultLength, 0);
        }

        aiVector3D defaultTail = global * direction;
        return glm::vec3(defaultTail.x, defaultTail.y, defaultTail.z);
    }
}

// 改进的processNode函数，支持文件格式参数
void processNode(aiNode *node, const aiScene *scene, const std::string &dict,
                 const std::string &filename, const std::string &assetKey, std::vector<std::string> &meshes,
                 std::unordered_map<std::string, std::tuple<glm::vec3, glm::vec3, std::string>> &bones,
                 FileFormat format)
{
    for (int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        auto key = std::format("{}/{}_{}", dict, filename, meshes.size());
        MeshManager::Instance().LoadMesh(mesh, scene, key);
        meshes.push_back(key);
        MeshManager::Instance().meshToAssetMap[key] = assetKey; // 记录mesh到asset的映射

        for (unsigned int b = 0; b < mesh->mNumBones; ++b)
        {
            aiBone *bone = mesh->mBones[b];
            aiNode *boneNode = scene->mRootNode->FindNode(bone->mName);
            if (!boneNode)
                continue;

            // 获取骨骼头位置
            glm::vec3 head = GetHead(boneNode, format);

            // 获取骨骼尾位置
            glm::vec3 tail = GetBoneTailExact(boneNode, format);

            // 获取父骨骼名称
            std::string parentName = "";
            if (boneNode->mParent)
            {
                parentName = boneNode->mParent->mName.C_Str();
            }

            bones[bone->mName.C_Str()] = {head, tail, parentName};
        }
    }

    for (int i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene, dict, filename, assetKey, meshes, bones, format);
    }
}

// 修改后的LoadMesh函数，支持FBX
std::tuple<std::vector<std::shared_ptr<Mesh>>,
           std::unordered_map<std::string, std::tuple<glm::vec3, glm::vec3, std::string>>>
MeshManager::LoadMesh(const std::string &dict, const std::string &filename)
{
    Path path = Path(dict) + Path(filename);

    // 检测文件格式
    FileFormat format = GetFileFormat(filename);

    // 检查是否已加载
    if (meshAssets.find(path) != meshAssets.end())
    {
        auto &asset = meshAssets[path];
        std::vector<std::shared_ptr<Mesh>> sharedMeshes;
        for (const auto &meshKey : asset.meshes)
        {
            auto meshPtr = Get(meshKey);
            if (meshPtr)
            {
                sharedMeshes.push_back(meshPtr);
            }
        }
        return {sharedMeshes, asset.bones};
    }

    // 设置Assimp导入标志
    unsigned int importFlags = aiProcess_Triangulate | aiProcess_FlipUVs;

    // FBX特定标志
    if (format == FileFormat::FBX)
    {
        importFlags |= aiProcess_CalcTangentSpace;     // 计算切线空间
        importFlags |= aiProcess_GenSmoothNormals;     // 生成平滑法线
        importFlags |= aiProcess_ImproveCacheLocality; // 改善缓存局部性
        importFlags |= aiProcess_OptimizeMeshes;       // 优化网格
        // 可选：如果需要更好的骨骼支持
        importFlags |= aiProcess_LimitBoneWeights; // 限制骨骼权重数量
        importFlags |= aiProcess_SplitLargeMeshes; // 分割大网格
    }

    Assimp::Importer imp;
    const aiScene *scene = imp.ReadFile(path, importFlags);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "ERROR::ASSIMP::" << imp.GetErrorString() << std::endl;
        return {{}, {}};
    }

    // FBX特殊处理：确保所有节点都有正确的变换
    if (format == FileFormat::FBX)
    {
        // 可以在这里添加额外的FBX特定处理
        // 例如：处理FBX的全局变换
    }

    MeshAsset asset;
    asset.key = path;
    processNode(scene->mRootNode, scene, dict, filename, asset.key, asset.meshes, asset.bones, format);

    meshAssets[path] = asset;

    std::vector<std::shared_ptr<Mesh>> sharedMeshes;
    for (const auto &meshKey : asset.meshes)
    {
        auto meshPtr = Get(meshKey);
        if (meshPtr)
        {
            sharedMeshes.push_back(meshPtr);
        }
    }

    return {sharedMeshes, asset.bones};
}

std::shared_ptr<Mesh> MeshManager::LoadMesh(aiMesh *mesh, const aiScene *scene, const std::string &dict)
{
    // 使用地址哈希或dict作为唯一key
    std::string key = dict; //+ "_" + std::to_string(reinterpret_cast<uintptr_t>(mesh))
    auto it = meshCache.find(key);
    if (it != meshCache.end())
        return it->second;

    auto newMesh = std::make_shared<Mesh>(mesh, scene, dict);
    newMesh->initialize();
    meshCache[key] = newMesh;
    return newMesh;
}

#endif // ASTER_ENABLE_ASSIMP

std::shared_ptr<Mesh> MeshManager::LoadMeshFromRawData(
    const std::string &dict,
    const std::vector<float> &vertices,
    const std::vector<unsigned int> &indices,
    const std::vector<float> &normals)
{
    std::string key = dict; //+ "_" + std::to_string(std::hash<std::string>{}(dict));
    auto it = meshCache.find(key);
    if (it != meshCache.end())
        return it->second;

    auto newMesh = std::make_shared<Mesh>(vertices, indices, normals);
    newMesh->initialize();
    meshCache[key] = newMesh;
    return newMesh;
}

std::shared_ptr<Mesh> MeshManager::Get(const std::string &key)
{
    auto it = meshCache.find(key);
    if (it != meshCache.end())
        return it->second;
    return nullptr;
}

// void MeshManager::Submit(const std::shared_ptr<Mesh> &mesh,
//                          const std::shared_ptr<Material> &material,
//                          const glm::mat4 &modelMatrix)
// {
//     BatchKey key{mesh, material};
//     batches[key].push_back({modelMatrix});
// }

// uint64_t MakeSortKey(const std::shared_ptr<Material> &material,
//                      const std::shared_ptr<Mesh> &mesh)
// {
//     auto h1 = reinterpret_cast<uint64_t>(material.get());
//     auto h2 = reinterpret_cast<uint64_t>(mesh.get());
//     return (h1 << 24) ^ (h2 & 0xFFFFFF);
// }

// void MeshManager::FlushBatches(const glm::mat4 &viewMatrix, const glm::mat4 &projMatrix)
// {
//     struct SortedBatch
//     {
//         uint64_t sortKey;
//         std::shared_ptr<Material> material;
//         std::shared_ptr<Mesh> mesh;
//         std::vector<RenderInstance> *instances;
//     };
//     std::vector<SortedBatch> sortedBatches;
//     sortedBatches.reserve(batches.size());
//     for (auto &[key, instances] : batches)
//     {
//         SortedBatch batch;
//         batch.material = key.material;
//         batch.mesh = key.mesh;
//         batch.instances = &instances;
//         batch.sortKey = MakeSortKey(key.material, key.mesh);
//         sortedBatches.push_back(batch);
//     }

//     std::sort(sortedBatches.begin(), sortedBatches.end(),
//               [](const SortedBatch &a, const SortedBatch &b)
//               {
//                   return a.sortKey < b.sortKey;
//               });

//     std::shared_ptr<Shader> currentShader = nullptr;

//     // std::cout << sortedBatches.size() << std::endl;

//     for (auto &batch : sortedBatches)
//     {
//         auto &mesh = batch.mesh;
//         auto &material = batch.material;
//         auto &instances = batch.instances;

//         if (currentShader != material->GetShader())
//         {
//             currentShader = material->GetShader();
//         }

//         if (instances->size() > 1)
//             currentShader->Use(ShaderVariant::Instanced);
//         else
//             currentShader->Use(ShaderVariant::Basic);
//         currentShader->SetUniformMat4x4f("view", viewMatrix);
//         currentShader->SetUniformMat4x4f("projection", projMatrix);

//         material->ApplyUniforms();

//         if (instances->size() == 1)
//         {
//             currentShader->SetUniformMat4x4f("model", instances->at(0).modelMatrix);
//             mesh->draw(currentShader);
//         }
//         else
//         {
//             // std::cout << "Drawing " << instances->size() << " instances of mesh." << std::endl;
//             // 实例化渲染
//             std::vector<glm::mat4> modelMatrices;
//             modelMatrices.reserve(instances->size());
//             for (auto &inst : *instances)
//             {
//                 modelMatrices.push_back(inst.modelMatrix);
//             }
//             mesh->drawInstanced(currentShader, modelMatrices);
//         }
//     }

//     batches.clear();
// }

void MeshManager::Clear()
{
    meshCache.clear();
    batches.clear();
}

void MeshManager::PrintStatus() const
{
    std::cout << "[MeshManager] Loaded meshes: " << meshCache.size() << std::endl;
    for (auto &[key, val] : meshCache)
    {
        int lastUsedFrame;
        try
        {
            lastUsedFrame = meshLastUsedFrame.at(key);
        }
        catch (const std::out_of_range &)
        {
            lastUsedFrame = -1;
        }
        std::cout << " - " << key << " (use_count=" << val.use_count() << ")" << " LastUsed: " << lastUsedFrame << std::endl;
    }
}

} // namespace aster

