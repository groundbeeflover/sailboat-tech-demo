#version 410

// B3 scripted-capture overlay. The minimap is already tone-mapped to sRGB-like RGBA8;
// displaying it through post_frag.glsl again would incorrectly tone-map and gamma it twice.
in vec2 fragUV;
uniform sampler2D minimapColor;
out vec4 fragColor;

void main()
{
    fragColor = texture(minimapColor, fragUV);
}
