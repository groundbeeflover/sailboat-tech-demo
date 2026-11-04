#version 410
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 4) in vec3 deltaPosition;
layout(location = 5) in vec3 deltaNormal;
uniform mat4 modelMatrix;
uniform mat4 viewProjection;
uniform mat3 normalViewMatrix;
uniform float blendWeight;
out vec3 viewNormal;
void main()
{
    gl_Position = viewProjection * modelMatrix * vec4(position + blendWeight * deltaPosition, 1.0);
    viewNormal = normalize(normalViewMatrix * (normal + blendWeight * deltaNormal));
}
