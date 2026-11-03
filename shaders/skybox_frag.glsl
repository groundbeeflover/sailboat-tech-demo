#version 410

in vec3 fragDirection;
uniform samplerCube environmentMap;
out vec4 fragColor;

void main()
{
    fragColor = vec4(texture(environmentMap, normalize(fragDirection)).rgb, 1.0);
}
