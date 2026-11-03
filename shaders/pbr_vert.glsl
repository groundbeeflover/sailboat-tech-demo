#version 410

uniform mat4 modelMatrix;
uniform mat3 normalMatrix; // inverse transpose of the model matrix (needed for the sail's non-uniform scale)
uniform mat4 viewProjection;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 tangent; // xyz tangent, w handedness

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec4 fragTangent;

void main()
{
    vec4 world = modelMatrix * vec4(position, 1.0);
    fragPosition = world.xyz;
    fragNormal = normalMatrix * normal;
    // A tangent lies in the surface, so it transforms like a position difference (model matrix),
    // not like the normal.
    fragTangent = vec4(mat3(modelMatrix) * tangent.xyz, tangent.w);
    fragTexCoord = texCoord;
    gl_Position = viewProjection * world;
}
