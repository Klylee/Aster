#include "Mesh.h"

#include <GL/glew.h>
#include <stb_image.h>
#include <iostream>
#include <glm/glm.hpp>

#include "RenderAPI.h" // 判断当前后端：Vulkan 下跳过 OpenGL 缓冲创建

#ifdef ASTER_ENABLE_VULKAN
#include "VulkanMeshBuffer.h"
#endif

namespace aster
{

Mesh::Mesh(const std::vector<float> &mesh_vertices, const std::vector<unsigned int> &mesh_faces, const std::vector<float> &normals)
{
    v_num = mesh_vertices.size() / 3;
    i_num = mesh_faces.size();
    v_size = mesh_vertices.size() * 8 / 3;
    i_size = mesh_faces.size();
    vertices = new float[v_size];
    indices = new unsigned int[i_size];

    for (int i = 0; i < v_num; i++)
    {
        vertices[i * 8 + 0] = mesh_vertices[i * 3 + 0];
        vertices[i * 8 + 1] = mesh_vertices[i * 3 + 1];
        vertices[i * 8 + 2] = mesh_vertices[i * 3 + 2];
        vertices[i * 8 + 3] = normals[i * 3 + 0];
        vertices[i * 8 + 4] = normals[i * 3 + 1];
        vertices[i * 8 + 5] = normals[i * 3 + 2];
        vertices[i * 8 + 6] = 0.0f;
        vertices[i * 8 + 7] = 0.0f;
    }

    for (int i = 0; i < i_num; i++)
    {
        indices[i] = mesh_faces[i];
    }
}

Mesh::Mesh(aiMesh *mesh, const aiScene *scence, const std::string &dict)
    : vertices(nullptr), indices(nullptr), vao(0), vbo(0), ibo(0), instanceVBO(0)
{
#ifdef ASTER_ENABLE_ASSIMP
    v_num = mesh->mNumVertices;
    i_num = mesh->mNumFaces * 3;
    v_size = mesh->mNumVertices * 8;
    i_size = mesh->mNumFaces * 3;
    vertices = new float[v_size];
    indices = new unsigned int[i_size];

    for (int i = 0; i < mesh->mNumVertices; i++)
    {
        try
        {
            vertices[i * 8 + 0] = mesh->mVertices[i].x;
            vertices[i * 8 + 1] = mesh->mVertices[i].y;
            vertices[i * 8 + 2] = mesh->mVertices[i].z;
            vertices[i * 8 + 3] = mesh->mNormals[i].x;
            vertices[i * 8 + 4] = mesh->mNormals[i].y;
            vertices[i * 8 + 5] = mesh->mNormals[i].z;
            if (mesh->mTextureCoords[0])
            {
                vertices[i * 8 + 6] = mesh->mTextureCoords[0][i].x;
                vertices[i * 8 + 7] = mesh->mTextureCoords[0][i].y;
            }
            else
            {
                vertices[i * 8 + 6] = 0.0f;
                vertices[i * 8 + 7] = 0.0f;
            }
        }
        catch (...)
        {
            std::cout << "error happened when initialize vertices" << std::endl;
        }
    }

    for (int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        try
        {
            indices[i * 3 + 0] = face.mIndices[0];
            indices[i * 3 + 1] = face.mIndices[1];
            indices[i * 3 + 2] = face.mIndices[2];
        }
        catch (...)
        {
            std::cout << "error happened when initialize indices" << std::endl;
        }
    }

    if (mesh->mMaterialIndex >= 0)
    {
        aiMaterial *material = scence->mMaterials[mesh->mMaterialIndex];

        for (int i = 0; i < material->GetTextureCount(aiTextureType_DIFFUSE); i++)
        {
            aiString file;
            material->GetTexture(aiTextureType_DIFFUSE, i, &file);
            std::string texfile = file.C_Str();
            textures.push_back(Texture(dict, texfile, TextureType::DIFFUSE));
        }
        for (int i = 0; i < material->GetTextureCount(aiTextureType_SPECULAR); i++)
        {
            aiString file;
            material->GetTexture(aiTextureType_SPECULAR, i, &file);
            std::string texfile = file.C_Str();
            textures.push_back(Texture(dict, texfile, TextureType::SPECULAR));
        }
        for (int i = 0; i < material->GetTextureCount(aiTextureType_AMBIENT); i++)
        {
            aiString file;
            material->GetTexture(aiTextureType_AMBIENT, i, &file);
            std::string texfile = file.C_Str();
            textures.push_back(Texture(dict, texfile, TextureType::AMBIENT));
        }
    }
#else
    // 无 assimp：该构造函数仅用于 assimp 加载路径，保持空实现。
    v_num = 0;
    i_num = 0;
    v_size = 0;
    i_size = 0;
#endif
}
Mesh::~Mesh()
{
    if (vertices)
        delete[] vertices;
    if (indices)
        delete[] indices;

    RenderAPI *api = RenderAPI::Current();

    // 后端仍在且是 Vulkan：释放懒创建的 VulkanMeshBuffer（device 仍有效）
    if (api && api->IsVulkan())
    {
#ifdef ASTER_ENABLE_VULKAN
        if (vulkanBuffer)
        {
            delete vulkanBuffer;
            vulkanBuffer = nullptr;
        }
#endif
        return;
    }

    // 后端仍在且是 OpenGL：释放 GL 缓冲（有 GL 上下文）
    if (api && api->IsOpenGL())
    {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ibo);
        glDeleteBuffers(1, &instanceVBO);
    }

    // api == nullptr：后端已关闭（如退出时的静态析构阶段）。
    // 其 GPU 资源（VulkanMeshBuffer / GL 缓冲）已随设备销毁 / 上下文销毁自动回收，
    // 这里不能再调用 vkDestroy*/glDelete*（设备/上下文已失效会导致崩溃）。
}

void Mesh::initialize()
{
    // Vulkan 后端没有 OpenGL 上下文：不创建 GL 缓冲，
    // 顶点/索引数据由 VulkanRenderAPI::SubmitSceneMesh 懒创建 VulkanMeshBuffer。
    RenderAPI *api = RenderAPI::Current();
    if (api && api->IsVulkan())
        return;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v_size * sizeof(float), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, i_size * sizeof(float), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void *)(0 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void *)(6 * sizeof(float)));
}

void Mesh::draw(std::shared_ptr<Shader> shader)
{
    unsigned int count = 1;
    for (int i = 0; i < textures.size(); i++)
    {
        textures[i].bind(i + 1);
        if (textures[i].type == TextureType::DIFFUSE)
        {
            shader->SetUniform1i(("utexture_diffuse" + std::to_string(count)).c_str(), i + 1);
            count++;
        }
    }

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, i_size, GL_UNSIGNED_INT, NULL);
    glBindVertexArray(0);

    for (int i = 0; i < textures.size(); i++)
    {
        textures[i].unbind();
    }
}

void Mesh::drawInstanced(const std::shared_ptr<Shader> &shader,
                         const std::vector<glm::mat4> &modelMatrices)
{
    // 如果未创建 instance buffer，则创建
    if (instanceVBO == 0)
    {
        glGenBuffers(1, &instanceVBO);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, modelMatrices.size() * sizeof(glm::mat4),
                     modelMatrices.data(), GL_DYNAMIC_DRAW);

        // 为 mat4 分配 4 个顶点属性位置
        for (int i = 0; i < 4; i++)
        {
            glEnableVertexAttribArray(3 + i);
            glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE,
                                  sizeof(glm::mat4), (void *)(sizeof(glm::vec4) * i));
            glVertexAttribDivisor(3 + i, 1);
        }

        glBindVertexArray(0);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        // 查询当前分配的大小
        GLint currentSize = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentSize);
        GLsizeiptr requiredSize = modelMatrices.size() * sizeof(glm::mat4);

        if (requiredSize > currentSize)
        {
            // 重新分配（容量不够）
            glDeleteBuffers(1, &instanceVBO);
            glGenBuffers(1, &instanceVBO);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
            glBufferData(GL_ARRAY_BUFFER, requiredSize, modelMatrices.data(), GL_DYNAMIC_DRAW);

            for (int i = 0; i < 4; i++)
            {
                glEnableVertexAttribArray(3 + i);
                glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE,
                                      sizeof(glm::mat4), (void *)(sizeof(glm::vec4) * i));
                glVertexAttribDivisor(3 + i, 1);
            }

            glBindVertexArray(0);
        }
        else
        {
            glBufferSubData(GL_ARRAY_BUFFER, 0, requiredSize, modelMatrices.data());
        }
    }

    glBindVertexArray(vao);
    glDrawElementsInstanced(GL_TRIANGLES, i_size, GL_UNSIGNED_INT, 0, modelMatrices.size());
    glBindVertexArray(0);
}

} // namespace aster

