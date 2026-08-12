#include "Model.h"
#include <iostream>
#include <sstream>
#include <format>

// assimp 仅用于加载模型文件（Model::awake / AddBoneNodes）。
// 未启用（如无 assimp 依赖的演示目标）时，Model 的网格由外部直接填充（meshes），
// 渲染路径不受影响；MeshManager/SceneManager 头文件也一并收进守卫，
// 避免把 Camera/Light 等整条场景依赖链拖进无 assimp 的构建。
#ifdef ASTER_ENABLE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#endif
#include <env_config.h>

#include "RenderAPI.h" // RenderAPI::Current()：按后端分发绘制
#ifdef ASTER_ENABLE_VULKAN
#include "VulkanRenderAPI.h" // SubmitSceneMesh：提交框架 Mesh 到 Vulkan 场景渲染器
#endif
#include "Renderer.h"
#ifdef ASTER_ENABLE_ASSIMP
#include "MeshManager.h"
#include "SceneManager.h"
#endif

namespace aster
{

void Model::awake()
{
#ifdef ASTER_ENABLE_ASSIMP
    {
        auto asset = MeshManager::Instance().LoadMesh(directory, filename);
        meshes = std::get<0>(asset);
        auto asset_bones = std::get<1>(asset);
        if (asset_bones.size() > 0)
        {
            bones = asset_bones;
        }
    }
#else
    // 无 assimp：不自动加载文件，网格由外部代码通过 meshes 直接填充（如程序化网格）
#endif

    //     // if needing to normalize the whole model
    //     if (normalizeMesh)
    //     {
    //         // 第一步：计算所有mesh的全局边界
    //         float globalMinX = std::numeric_limits<float>::max();
    //         float globalMaxX = std::numeric_limits<float>::lowest();
    //         float globalMinY = std::numeric_limits<float>::max();
    //         float globalMaxY = std::numeric_limits<float>::lowest();
    //         float globalMinZ = std::numeric_limits<float>::max();
    //         float globalMaxZ = std::numeric_limits<float>::lowest();

    //         // 计算所有mesh的全局边界
    //         for (auto &mesh : meshes)
    //         {
    //             float minX = mesh->vertices[0], maxX = mesh->vertices[0];
    //             float minY = mesh->vertices[1], maxY = mesh->vertices[1];
    //             float minZ = mesh->vertices[2], maxZ = mesh->vertices[2];

    // #pragma omp parallel for reduction(min : minX, minY, minZ) reduction(max : maxX, maxY, maxZ)
    //             for (int i = 1; i < mesh->v_num; i++)
    //             {
    //                 float x = mesh->vertices[i * 8 + 0];
    //                 float y = mesh->vertices[i * 8 + 1];
    //                 float z = mesh->vertices[i * 8 + 2];

    //                 if (x < minX)
    //                     minX = x;
    //                 if (x > maxX)
    //                     maxX = x;
    //                 if (y < minY)
    //                     minY = y;
    //                 if (y > maxY)
    //                     maxY = y;
    //                 if (z < minZ)
    //                     minZ = z;
    //                 if (z > maxZ)
    //                     maxZ = z;
    //             }

    // // 更新全局边界
    // #pragma omp critical
    //             {
    //                 if (minX < globalMinX)
    //                     globalMinX = minX;
    //                 if (maxX > globalMaxX)
    //                     globalMaxX = maxX;
    //                 if (minY < globalMinY)
    //                     globalMinY = minY;
    //                 if (maxY > globalMaxY)
    //                     globalMaxY = maxY;
    //                 if (minZ < globalMinZ)
    //                     globalMinZ = minZ;
    //                 if (maxZ > globalMaxZ)
    //                     globalMaxZ = maxZ;
    //             }
    //         }

    //         // 计算全局的中心点和缩放因子
    //         float globalCenterX = (globalMinX + globalMaxX) / 2.0f;
    //         float globalCenterY = (globalMinY + globalMaxY) / 2.0f;
    //         float globalCenterZ = (globalMinZ + globalMaxZ) / 2.0f;
    //         globalCenter = Vec3(globalCenterX, globalCenterY, globalCenterZ);
    //         globalScale = std::max({globalMaxX - globalMinX, globalMaxY - globalMinY, globalMaxZ - globalMinZ});

    //         // 避免除零
    //         if (globalScale < 1e-6f)
    //         {
    //             globalScale = 1.0f;
    //         }
    //         else
    //         {
    //             globalScale = 1.0f / globalScale;
    //         }

    //         // 更改model的transform
    //         transform.SetPosition(-globalCenter * globalScale);
    //         transform.SetScale(Vec3(globalScale));
    //     }
}

Model::~Model()
{
}

void Model::draw()
{
#ifdef ASTER_ENABLE_VULKAN
    // Vulkan 后端：把每个网格提交到 Vulkan 场景渲染器
    // （首次提交懒创建 Mesh 的 VulkanMeshBuffer；相机在 Present 中由后端使用）
    RenderAPI *api = RenderAPI::Current();
    if (api && api->IsVulkan())
    {
        auto *vk = static_cast<VulkanRenderAPI *>(api);
        // 每对象材质参数（颜色 / 粗糙度 / 金属度 / AO / 纹理索引 / 管线）
        MaterialParams mp;
        if (material)
        {
            mp.color = material->color;
            mp.roughness = material->roughness;
            mp.metallic = material->metallic;
            mp.ao = material->ao;
            mp.textureIndex = material->textureIndex;
            mp.pipelineIndex = material->vulkanPipeline; // M3：自定义 shader 管线
            // M4：自定义材质 uniform（SetUniform(key,type,value)）→ binding 12 动态 UBO。
            // 指针指向 material 的 map/顺序，SubmitSceneMesh 本帧内打包完成，安全。
            if (material->HasCustomUniforms())
            {
                mp.customUniforms = &material->GetUniforms();
                mp.customUniformOrder = &material->GetUniformOrder();
            }
        }
        glm::mat4 modelMat = transform.GetLocalToWorld();
        // collectable：该对象画进离屏 id map，供鼠标拾取（PickAt）。
        // 传 weak_ptr<Model>：拾取注册表持弱引用，模型删除后自动失效，无悬垂指针。
        std::weak_ptr<Model> self = collectable ? weak_from_this() : std::weak_ptr<Model>{};
        for (const auto &mesh : meshes)
            vk->SubmitSceneMesh(*mesh, modelMat, mp, castsShadow, self);
        return;
    }
#endif

    // OpenGL 后端：走框架 Renderer（原有行为）
    for (int i = 0; i < meshes.size(); i++)
    {
        if (material)
            Renderer::Instance().SubmitDrawCall({meshes[i], material, transform.GetLocalToWorld()});
    }
}

std::string Model::info()
{
    int sumFace = 0;
    std::stringstream ss;
    ss << "mesh number: " << meshes.size() << std::endl;
    for (int i = 0; i < meshes.size(); i++)
    {
        ss << "  mesh" << i << ": " << meshes[i]->v_size / 8 << std::endl;
        sumFace += meshes[i]->v_size / 8;
    }
    ss << "total: " << sumFace << std::endl;
    return ss.str();
}

void Model::printBoneInfo()
{
    std::cout << "Bone Count: " << bones.size() << std::endl;
    for (const auto &[name, headAndTail] : bones)
    {
        auto [head, tail, parentName] = headAndTail;
        std::cout << name << ": Head(" << head.x << ", " << head.y << ", " << head.z << ") "
                  << "Tail(" << tail.x << ", " << tail.y << ", " << tail.z << ") "
                  << "Parent: " << parentName << std::endl;
    }
}

// 骨骼可视化需要加载 ico-sphere.obj / cone.obj（assimp）并挂到场景（SceneManager），
// 仅在启用 assimp 的构建（编辑器主目标）中提供。
#ifdef ASTER_ENABLE_ASSIMP
void Model::AddBoneNodes(const std::shared_ptr<Material> &nodeMaterial, const std::shared_ptr<Material> &linkMaterial)
{
    for (auto it : bones)
    {
        auto [nodeNmae, headAndTail] = it;
        auto [head, tail, parentName] = headAndTail;
        auto nodeObj = std::dynamic_pointer_cast<Model>(SceneObject::create("Model", filename + "_" + nodeNmae));
        nodeObj->directory = Path(ROOT_DIR) + "/assets";
        nodeObj->filename = "ico-sphere.obj";
        nodeObj->SetMaterial(nodeMaterial);
        nodeObj->awake();
        nodeObj->transform.SetScale(Vec3(0.01f));
        nodeObj->transform.SetPosition((head - globalCenter) * globalScale);
        SceneManager::Instance().AddObject(nodeObj);
        children.push_back(nodeObj);

        if (!parentName.empty() && bones.find(parentName) != bones.end())
        {
            auto parentHead = std::get<0>(bones[parentName]);
            auto linkObj = std::dynamic_pointer_cast<Model>(SceneObject::create("Model", filename + "_" + parentName + "-" + nodeNmae));
            linkObj->directory = Path(ROOT_DIR) + "/assets";
            linkObj->filename = "cone.obj";
            linkObj->SetMaterial(linkMaterial);
            linkObj->awake();

            Vec3 direction = glm::normalize(head - parentHead);
            float length = glm::distance(head, parentHead);

            // 计算圆锥体的缩放
            // cone.obj原始高度为2，所以需要缩放为实际骨骼长度的一半
            float heightScale = length / 2.0f;
            // 半径可以根据需要调整，这里设为高度的1/10
            float radiusScale = heightScale * 0.1f;
            glm::vec3 coneScale = glm::vec3(radiusScale, heightScale, radiusScale);
            linkObj->transform.SetScale(coneScale * globalScale);

            // 计算旋转
            glm::vec3 originalDirection(0.0f, 1.0f, 0.f); // cone.obj原始朝向（高度方向）
            linkObj->transform.Rotate(originalDirection, direction);

            // 计算位置
            linkObj->transform.SetPosition((parentHead - globalCenter) * globalScale);
            SceneManager::Instance().AddObject(linkObj);
            children.push_back(linkObj);
        }
    }
}
#endif // ASTER_ENABLE_ASSIMP

} // namespace aster

