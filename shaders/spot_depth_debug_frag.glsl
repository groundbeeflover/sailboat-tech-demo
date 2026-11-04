#version 410

// Perspective depth strongly bunches up near 1. Convert to distance for a useful debug overlay.
in vec2 fragUV;
uniform sampler2D depthTexture;
uniform float nearPlane;
uniform float farPlane;
out vec4 fragColor;
void main()
{
    float depth = texture(depthTexture, fragUV).r;
    float linearDepth = nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
    float covered = 1.0 - step(0.999999, depth); // clear-depth pixels are empty
    fragColor = vec4(vec3(covered * (1.0 - clamp(linearDepth / farPlane, 0.0, 1.0))), 1.0);
}
