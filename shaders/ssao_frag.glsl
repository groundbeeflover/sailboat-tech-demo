#version 410
in vec2 fragUV;
out float ambientVisibility;
uniform sampler2D depthTexture;
uniform sampler2D normalTexture;
uniform mat4 projection;
uniform mat4 inverseProjection;
uniform float radius;
uniform float strength;
const float PI2 = 6.2831853;
const int SAMPLES = 16;

vec3 viewPosition(vec2 uv, float depth)
{
    vec4 h = inverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return h.xyz / max(abs(h.w), 1e-5) * sign(h.w);
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main()
{
    float depth = texture(depthTexture, fragUV).r;
    if (depth >= 0.999999) { ambientVisibility = 1.0; return; }
    vec3 P = viewPosition(fragUV, depth);
    vec3 N = normalize(texture(normalTexture, fragUV).rgb * 2.0 - 1.0);
    // Per-pixel angular dither removes a fixed star-shaped banding pattern.
    float randomAngle = hash(floor(gl_FragCoord.xy)) * PI2;
    vec3 helper = abs(N.z) < 0.95 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(helper, N));
    vec3 bitangent = cross(N, tangent);
    float occluded = 0.0;
    float validSamples = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        float fi = float(i) + 0.5;
        float a = PI2 * fract(fi * 0.618034) + randomAngle;
        float z = 0.12 + 0.83 * fract(fi * 0.754878);
        float radial = sqrt(max(1.0 - z * z, 0.0));
        vec3 direction = tangent * cos(a) * radial + bitangent * sin(a) * radial + N * z;
        float scale = mix(0.15, 1.0, pow(fi / float(SAMPLES), 2.0));
        vec3 testP = P + direction * radius * scale;
        vec4 clip = projection * vec4(testP, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0)))) continue;
        float neighbourDepth = texture(depthTexture, sampleUV).r;
        if (neighbourDepth >= 0.999999) continue;
        vec3 neighbour = viewPosition(sampleUV, neighbourDepth);
        float inRange = 1.0 - smoothstep(radius * 0.5, radius * 1.75, abs(P.z - neighbour.z));
        occluded += step(testP.z + 0.035, neighbour.z) * inRange;
        validSamples += 1.0;
    }
    ambientVisibility = clamp(1.0 - strength * occluded / max(validSamples, 1.0), 0.0, 1.0);
}
