#pragma once

#include "Shader.h"
#include <assimp/scene.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "Texture.h"

// Vulkan 后端 GPU 缓冲的前向声明（避免 Mesh.h 依赖 Vulkan 头文件）
namespace aster
{
#ifdef ASTER_ENABLE_VULKAN
class VulkanMeshBuffer;
#endif

// vertices: n * 8
// pos.x   pos.y   pos.z   nor.x   nor.y   nor.z   tex.u   tex.v
class Mesh
{
public:
    int v_size;
    int i_size;
    int v_num;
    int i_num;
    float *vertices;
    unsigned int *indices;
    std::vector<Texture> textures;

    unsigned int vao;
    unsigned int vbo;
    unsigned int ibo;
    unsigned int instanceVBO;

    // Vulkan 后端 GPU 缓冲（懒创建，由 VulkanRenderAPI::SubmitSceneMesh 填充，
    // Mesh 析构时释放）。OpenGL 后端不使用。
#ifdef ASTER_ENABLE_VULKAN
    VulkanMeshBuffer *vulkanBuffer = nullptr;
#endif

    Mesh() : v_size(0), i_size(0), vertices(nullptr), indices(nullptr), vao(0), vbo(0), ibo(0), instanceVBO(0) {}
    Mesh(const std::vector<float> &mesh_vertices, const std::vector<unsigned int> &mesh_faces, const std::vector<float> &normals);
    Mesh(aiMesh *mesh, const aiScene *scence, const std::string &dict);
    virtual ~Mesh();
    virtual void initialize();

    virtual void draw(std::shared_ptr<Shader> shader);

    void drawInstanced(const std::shared_ptr<Shader> &shader,
                       const std::vector<glm::mat4> &modelMatrices);
};

} // namespace aster

