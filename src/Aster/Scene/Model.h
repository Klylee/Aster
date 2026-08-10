#pragma once
#include <glm/glm.hpp>
#include "Shader.h"
#include "Mesh.h"
#include "SceneObject.h"
#include "Path.h"
#include "Material.h"

namespace aster
{

class Model : public SceneObject
{
public:
    REGISTER_SCENE_OBJECT(Model)

    std::shared_ptr<Material> material;
    std::vector<std::shared_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::tuple<Vec3, Vec3, std::string>> bones; // name-> <head, tail, parentName>

    ~Model() override;

    void awake() override;
    void update() override {}
    void draw() override;

    void SetMaterial(const std::shared_ptr<Material> mat) { material = std::move(mat); }
    std::string info();
    void printBoneInfo();

    // add bone nodes to scene, visualize with nodeMaterial
    void AddBoneNodes(const std::shared_ptr<Material> &nodeMaterial, const std::shared_ptr<Material> &linkMaterial);

    // void processNode(aiNode *node, const aiScene *scene);
    Path directory;
    std::string filename;

    bool normalizeMesh = false;
    Vec3 globalCenter = Vec3(0.0f);
    float globalScale = 1.0f;

    // 该模型是否向地面投影阴影（接收体如平面可设为 false）
    bool castsShadow = true;
};

} // namespace aster

