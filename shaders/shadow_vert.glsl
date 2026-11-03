#version 410

uniform mat4 mvpMatrix;
uniform float blendWeight; // same blend shapes as pbr_vert.glsl, so shadows match the deformed mesh

layout(location = 0) in vec3 position;
layout(location = 4) in vec3 deltaPosition; // (0, 0, 0) for meshes without blend shapes

void main()
{
    gl_Position = mvpMatrix * vec4(position + blendWeight * deltaPosition, 1);
}
