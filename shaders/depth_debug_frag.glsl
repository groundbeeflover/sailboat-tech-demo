#version 410

// Shows the shadow map in a corner of the screen (debug view and report screenshots).
// The light projection is orthographic, so depth is linear in distance: near = dark, far = light.

in vec2 fragUV;
uniform sampler2D depthTexture; // compare mode is switched off while this is drawn
out vec4 fragColor;

void main()
{
    float depth = texture(depthTexture, fragUV).r;
    // The boat only spans a narrow band around the middle of the depth range; stretch that band.
    float v = clamp((depth - 0.38) / 0.24, 0.0, 1.0);
    fragColor = vec4(vec3(v), 1.0);
}
