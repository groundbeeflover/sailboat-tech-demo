#include "texture.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <framework/image.h>

#include <iostream>
#include <utility>

Texture::Texture(std::filesystem::path filePath, bool srgb)
{
    // Load image from disk to CPU memory.
    // Image class is defined in <framework/image.h>
    Image cpuTexture { filePath };

    // Create a texture on the GPU and bind it for parameter setting
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    // Define GPU texture parameters and upload corresponding data based on number of image channels
    switch (cpuTexture.channels) {
        case 1:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, cpuTexture.width, cpuTexture.height, 0, GL_RED, GL_UNSIGNED_BYTE, cpuTexture.get_data());
            break;
        case 3:
            glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8 : GL_RGB8, cpuTexture.width, cpuTexture.height, 0, GL_RGB, GL_UNSIGNED_BYTE, cpuTexture.get_data());
            break;
        case 4:
            glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, cpuTexture.width, cpuTexture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, cpuTexture.get_data());
            break;
        default:
            std::cerr << "Number of channels read for texture is not supported" << std::endl;
            throw std::exception();
    }

    setSamplingAndMipmaps();
}

Texture::Texture(int width, int height, const std::vector<uint8_t>& rgba, bool srgb)
{
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    setSamplingAndMipmaps();
}

void Texture::setSamplingAndMipmaps()
{
    // Set behavior for when texture coordinates are outside the [0, 1] range (wrap around).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Set interpolation for texture sampling (bilinear interpolation across mip-maps).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Generate mip-maps
    glGenerateMipmap(GL_TEXTURE_2D);
}

Texture::Texture(Texture&& other)
    : m_texture(other.m_texture)
{
    other.m_texture = INVALID;
}

Texture& Texture::operator=(Texture&& other)
{
    // The defaulted move assignment would leave other.m_texture set, so both destructors would delete it.
    std::swap(m_texture, other.m_texture);
    return *this;
}

Texture::~Texture()
{
    if (m_texture != INVALID)
        glDeleteTextures(1, &m_texture);
}

void Texture::bind(GLint textureSlot)
{
    glActiveTexture(static_cast<GLenum>(textureSlot));
    glBindTexture(GL_TEXTURE_2D, m_texture);
}
