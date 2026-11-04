#version 410
in vec2 fragUV;
out float blurredVisibility;
uniform sampler2D aoTexture;
uniform sampler2D depthTexture;
uniform vec2 texel;
void main()
{
    float centerDepth = texture(depthTexture, fragUV).r;
    float total = 0.0, weights = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = fragUV + vec2(x, y) * texel;
            float depth = texture(depthTexture, uv).r;
            float spatial = exp(-0.32 * float(x * x + y * y));
            float edge = exp(-750.0 * abs(centerDepth - depth));
            float weight = spatial * edge;
            total += texture(aoTexture, uv).r * weight;
            weights += weight;
        }
    }
    blurredVisibility = total / max(weights, 1e-5);
}
