#version 410
in vec2 fragUV;
out vec4 fragColor;
uniform sampler2D aoTexture;
void main() { fragColor = vec4(vec3(texture(aoTexture, fragUV).r), 1.0); }
