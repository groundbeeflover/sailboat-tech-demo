#version 410

// Final pass: HDR scene colour -> exposure -> ACES filmic tone curve -> sRGB gamma.
// Bloom will be added here (bright pass + blur, then add before tone mapping).

in vec2 fragUV;
uniform sampler2D hdrColor;
uniform float exposure;
out vec4 fragColor;

// Narkowicz 2016, fit of the ACES reference rendering transform.
vec3 acesFilm(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(hdrColor, fragUV).rgb * exposure;
    vec3 ldr = acesFilm(hdr);
    fragColor = vec4(pow(ldr, vec3(1.0 / 2.2)), 1.0);
}
