#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <cstdint>
#include <exception>
#include <filesystem>
#include <vector>
#include <framework/opengl_includes.h>

struct ImageLoadingException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Texture {
public:
    // srgb = true for colour (albedo) maps, so the GPU converts them to linear when sampling.
    // Normal and roughness maps hold linear data and must use srgb = false.
    Texture(std::filesystem::path filePath, bool srgb = false);
    // From raw 8-bit RGBA pixels (row 0 = v 0), e.g. procedurally generated textures.
    Texture(int width, int height, const std::vector<uint8_t>& rgba, bool srgb);
    Texture(const Texture&) = delete;
    Texture(Texture&&);
    ~Texture();

    Texture& operator=(const Texture&) = delete;
    Texture& operator=(Texture&&);

    void bind(GLint textureSlot);
    GLuint id() const { return m_texture; }

private:
    void setSamplingAndMipmaps();

    static constexpr GLuint INVALID = 0xFFFFFFFF;
    GLuint m_texture { INVALID };
};
