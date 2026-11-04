#version 410

uniform sampler2D puffTexture; // white, the shape is in alpha (generateSprayPuff)
uniform vec3 lightColor;
uniform vec3 ambientColor;

in vec2 fragCorner;
in vec2 fragUV;
in vec3 fragLightLocal;
in float fragScatter;
in float fragAlpha;

out vec4 fragColor;

const float PI = 3.14159265;
const vec3 SprayAlbedo = vec3(0.9, 0.94, 1.0);

void main()
{
    float r2 = dot(fragCorner, fragCorner);
    if (r2 > 1.0)
        discard; // outside the inscribed disc the rotated texture coordinates would wrap around
    float alpha = fragAlpha * texture(puffTexture, fragUV).a;

    // Light the flat quad as if it were a sphere: n = (x, y, sqrt(1 - x^2 - y^2)) in the quad's frame.
    vec3 n = vec3(fragCorner, sqrt(1.0 - r2));
    // Wrapped diffuse: in a cloud of droplets light also reaches the side turned away from it.
    float diffuse = max((dot(n, fragLightLocal) + 0.5) / 1.5, 0.0);
    vec3 color = SprayAlbedo * (lightColor * (diffuse / PI + 0.5 * fragScatter) + ambientColor);

    fragColor = vec4(color * alpha, alpha); // premultiplied alpha, see glBlendFunc in particles.cpp
}
