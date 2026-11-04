#version 410

// Gerstner wave displacement of the endless water grid.
// Keep gerstner() in sync with Waves::displacement / Waves::normal in src/waves.cpp,
// which the boat uses to float on this exact surface.

const int MAX_WAVES = 8;
uniform int waveCount;
uniform vec4 waveDirAmpK[MAX_WAVES]; // xy = direction of travel (xz plane), z = amplitude A, w = wavenumber k
uniform vec3 waveOmegaPhaseQ[MAX_WAVES]; // x = angular frequency omega, y = phase, z = steepness Q
uniform float time;
uniform float foamThreshold; // maximum horizontal area compression to begin forming foam

uniform vec2 gridOffset; // camera position snapped to the grid spacing
uniform mat4 viewProjection;

layout(location = 0) in vec2 gridPosition; // grid vertex relative to the camera, in metres

out vec3 fragPosition;
out vec3 fragNormal;
out float fragHeight;
out float fragFoam;

vec3 gerstner(vec2 p, out vec3 normal, out float jacobian)
{
    vec3 displacement = vec3(0.0);
    vec3 n = vec3(0.0, 1.0, 0.0);
    // Horizontal deformation gradient: d(x + displacement.x, z + displacement.z) / d(x,z).
    // Determinant < 1 means compression; near 0 indicates a nearly folding crest.
    float dx_dx = 1.0;
    float dz_dz = 1.0;
    float dx_dz = 0.0;
    for (int i = 0; i < MAX_WAVES; ++i) {
        if (i >= waveCount)
            break;
        vec2 D = waveDirAmpK[i].xy;
        float A = waveDirAmpK[i].z;
        float k = waveDirAmpK[i].w;
        float omega = waveOmegaPhaseQ[i].x;
        float phase = waveOmegaPhaseQ[i].y;
        float Q = waveOmegaPhaseQ[i].z;

        float theta = k * dot(D, p) - omega * time + phase;
        float c = cos(theta);
        float s = sin(theta);
        displacement.x += Q * A * D.x * c;
        displacement.z += Q * A * D.y * c;
        displacement.y += A * s;
        // Analytic normal of the displaced surface (Finch 2004, eq. 12).
        n.x -= D.x * k * A * c;
        n.z -= D.y * k * A * c;
        n.y -= Q * k * A * s;
        float compression = Q * A * k * s;
        dx_dx -= compression * D.x * D.x;
        dz_dz -= compression * D.y * D.y;
        dx_dz -= compression * D.x * D.y;
    }
    jacobian = dx_dx * dz_dz - dx_dz * dx_dz;
    normal = normalize(n);
    return displacement;
}

void main()
{
    // Waves are evaluated at the world-space rest position, so the pattern stays fixed in the world
    // while the grid itself travels with the camera.
    vec2 rest = gridPosition + gridOffset;
    vec3 normal;
    float jacobian;
    vec3 d = gerstner(rest, normal, jacobian);
    vec3 world = vec3(rest.x + d.x, d.y, rest.y + d.z);

    fragPosition = world;
    fragNormal = normal;
    fragHeight = d.y;
    // Soft onset avoids a harsh binary cutoff as the waves move.
    // A calm/flat sea has jacobian = 1 and therefore produces no foam.
    fragFoam = 1.0 - smoothstep(foamThreshold - 0.16, foamThreshold, jacobian);
    gl_Position = viewProjection * vec4(world, 1.0);
}
