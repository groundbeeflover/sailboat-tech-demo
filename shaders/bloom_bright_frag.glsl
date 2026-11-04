#version 410

in vec2 fragUV;
uniform sampler2D hdrColor;
uniform float threshold;
uniform float knee;
out vec4 fragColor;

void main()
{
    vec3 color = texture(hdrColor, fragUV).rgb;
    float brightness = max(color.r, max(color.g, color.b));

    // Soft threshold: ramp up smoothly around 'threshold' instead of a hard cutoff, which
    // would produce harsh halos and flicker when a specular highlight moves across the boundary.
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
    fragColor = vec4(color * contribution, 1.0);
}
