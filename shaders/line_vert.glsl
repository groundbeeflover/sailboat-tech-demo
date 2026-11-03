#version 410

uniform mat4 viewProjection;
uniform float pointSize;

layout(location = 0) in vec3 position;

void main()
{
    gl_Position = viewProjection * vec4(position, 1.0);
    gl_PointSize = pointSize;
}
