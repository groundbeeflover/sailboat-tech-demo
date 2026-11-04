#version 410
in vec2 fragUV;
out float newFoam;
const int MAX_WAVES = 8;
uniform int waveCount;
uniform vec4 waveDirAmpK[MAX_WAVES];
uniform vec3 waveOmegaPhaseQ[MAX_WAVES];
uniform float time;
uniform float foamThreshold;
uniform sampler2D previousFoam;
uniform vec2 newCenter;
uniform vec2 oldCenter;
uniform float worldSpan;
uniform float decay;
uniform bool havePrevious;

void main()
{
    vec2 world = newCenter + (fragUV - 0.5) * worldSpan;
    // Same horizontal determinant as water_vert.glsl. The foam source follows actual wave crests.
    float dxdx = 1.0, dzdz = 1.0, dxdz = 0.0;
    for (int i = 0; i < MAX_WAVES; ++i) {
        if (i >= waveCount) break;
        vec2 D = waveDirAmpK[i].xy;
        float A = waveDirAmpK[i].z, k = waveDirAmpK[i].w;
        float s = sin(k * dot(D, world) - waveOmegaPhaseQ[i].x * time + waveOmegaPhaseQ[i].y);
        float comp = waveOmegaPhaseQ[i].z * A * k * s;
        dxdx -= comp * D.x * D.x;
        dzdz -= comp * D.y * D.y;
        dxdz -= comp * D.x * D.y;
    }
    float jacobian = dxdx * dzdz - dxdz * dxdz;
    float current = 1.0 - smoothstep(foamThreshold - 0.16, foamThreshold, jacobian);
    float carried = 0.0;
    if (havePrevious) {
        vec2 previousUV = (world - oldCenter) / worldSpan + 0.5;
        if (all(greaterThanEqual(previousUV, vec2(0.0))) && all(lessThanEqual(previousUV, vec2(1.0))))
            carried = texture(previousFoam, previousUV).r * decay;
    }
    newFoam = max(current, carried);
}
