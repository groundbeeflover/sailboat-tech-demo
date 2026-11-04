#version 410

in vec2 fragUV;
uniform sampler2D source;
uniform vec2 direction; // horizontal or vertical offset of one texel, in UV coordinates
out vec4 fragColor;

void main()
{
    // Separable 9-tap Gaussian (radius 4). With GL_LINEAR, two neighbouring taps are
    // combined into one off-centre lookup: only 5 texture fetches per fragment, not 9.
    // Weights sum to ~1, preserving the energy of the extracted highlights.
    vec3 sum = texture(source, fragUV).rgb * 0.2270270;
    sum += texture(source, fragUV + 1.3846154 * direction).rgb * 0.3162162;
    sum += texture(source, fragUV - 1.3846154 * direction).rgb * 0.3162162;
    sum += texture(source, fragUV + 3.2307692 * direction).rgb * 0.0702703;
    sum += texture(source, fragUV - 3.2307692 * direction).rgb * 0.0702703;
    fragColor = vec4(sum, 1.0);
}
