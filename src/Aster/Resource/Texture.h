#pragma once
#include <string>

namespace aster
{

enum TextureType
{
    DIFFUSE,
    SPECULAR,
    AMBIENT
};

class Texture
{
public:
    unsigned int texid;
    TextureType type;

    Texture() = default;
    Texture(const std::string &dict, const std::string &file, TextureType type);
    void bind(unsigned int channel);
    void unbind();
};

} // namespace aster
