#include "Material.h"
#include <GL/glew.h>

namespace aster
{

void Material::ApplyUniforms()
{
    if (!shader)
        return; // 无 shader 的材质（如 Vulkan 单色材质）无需上传 uniform
    shader->SetUniforms(uniforms);
}

void Material::ApplyRenderState()
{
    if (renderState.depthTest)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
        glDisable(GL_DEPTH_TEST);

    glDepthFunc(renderState.depthFunc);
    glDepthMask(renderState.depthWrite ? GL_TRUE : GL_FALSE);

    if (renderState.blend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);

    if (renderState.cullFace)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);

    glBlendFunc(renderState.blendSrc, renderState.blendDst);
}

} // namespace aster

