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

    // 是否可拾取（鼠标拾取）：为 true 时该模型会进入“可拾取列表”，
    // 渲染时被画进离屏 id map（每个可拾取对象一个唯一 ID 颜色），
    // 鼠标点击后按 id map 像素值反查命中的对象。
    bool collectable = false;
};

} // namespace aster

