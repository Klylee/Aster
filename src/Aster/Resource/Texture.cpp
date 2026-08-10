#include "Texture.h"

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include <stb_image.h>
#include <iostream>
#include <GL/glew.h>

namespace aster
{

Texture::Texture(const std::string &dict, const std::string &file, TextureType type) : texid(0), type(type)
{
    std::string filepath = dict + "/" + file;
    int w, h, channels;
    unsigned char *img = stbi_load(filepath.c_str(), &w, &h, &channels, 0);
    if (img)
    {
        GLenum format = GL_RED;
        if (channels == 1)
            format = GL_RED;
        else if (channels == 3)
            format = GL_RGB;
        else if (channels == 4)
            format = GL_RGBA;

        glGenTextures(1, &texid);
        glBindTexture(GL_TEXTURE_2D, texid);
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, img);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(img);
    }
    else
    {
        std::cout << "can not load texture file" << std::endl;
    }
}

void Texture::bind(unsigned int channel)
{
    /*glBindTexture(GL_TEXTURE_2D, 0);*/
    glActiveTexture(GL_TEXTURE0 + channel);
    glBindTexture(GL_TEXTURE_2D, texid);
}
void Texture::unbind()
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace aster
