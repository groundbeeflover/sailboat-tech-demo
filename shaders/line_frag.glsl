#version 410

uniform vec3 color;
uniform bool roundPoints;
out vec4 fragColor;

void main()
{
    if (roundPoints && length(gl_PointCoord - 0.5) > 0.5)
        discard;
    fragColor = vec4(color, 1.0);
}
