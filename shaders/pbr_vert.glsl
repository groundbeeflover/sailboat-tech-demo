#version 410

uniform mat4 modelMatrix;
uniform mat3 normalMatrix; // inverse transpose of the model matrix (needed for the sail's non-uniform scale)
uniform mat4 viewProjection;
uniform float blendWeight; // blend-shape weight, 0 = base shape

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 tangent; // xyz tangent, w handedness
// Blend-shape deltas (target - base). Meshes that do not provide these arrays read the default
// attribute value (0, 0, 0), so for them the blend has no effect.
layout(location = 4) in vec3 deltaPosition;
layout(location = 5) in vec3 deltaNormal;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec4 fragTangent;

void main()
{
    vec3 blendedPosition = position + blendWeight * deltaPosition;
    vec3 blendedNormal = normal + blendWeight * deltaNormal;

    vec4 world = modelMatrix * vec4(blendedPosition, 1.0);
    fragPosition = world.xyz;
    fragNormal = normalMatrix * blendedNormal;
    // A tangent lies in the surface, so it transforms like a position difference (model matrix),
    // not like the normal.
    fragTangent = vec4(mat3(modelMatrix) * tangent.xyz, tangent.w);
    fragTexCoord = texCoord;
    gl_Position = viewProjection * world;
}
