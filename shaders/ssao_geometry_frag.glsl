#version 410
in vec3 viewNormal;
out vec4 normalColor;
void main()
{
    normalColor = vec4(normalize(viewNormal) * 0.5 + 0.5, 1.0);
}
