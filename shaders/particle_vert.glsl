#version 410

// Spray and wake particles: one instance per particle, 4 corners per instance (see particles.cpp).

uniform mat4 viewProjection;
uniform vec3 cameraRight; // camera axes in world space (columns 0 and 1 of inverse(view))
uniform vec3 cameraUp;
uniform vec3 cameraPosition;
uniform vec3 lightDirection; // towards the light: sun by day, moon at night

layout(location = 0) in vec2 corner; // per vertex: quad corner in [-1, 1]^2, the same for every particle
layout(location = 1) in vec4 positionSize; // per instance: centre (world) and half-size (metres)
layout(location = 2) in vec3 alphaRotationFlat; // per instance: opacity, texture rotation, 1 = lies on the water

out vec2 fragCorner; // position on the quad: round mask and fake sphere normal
out vec2 fragUV;
out vec3 fragLightLocal; // light direction in the quad's frame (right, up, facing)
out float fragScatter;
out float fragAlpha;

void main()
{
    float flatness = alphaRotationFlat.z;
    vec3 toCamera = normalize(cameraPosition - positionSize.xyz);
    // Spray faces the camera (billboard); wake foam lies flat on the water. Both frames are
    // right-handed: cross(right, up) = facing.
    vec3 right = mix(cameraRight, vec3(1.0, 0.0, 0.0), flatness);
    vec3 up = mix(cameraUp, vec3(0.0, 0.0, -1.0), flatness);
    vec3 facing = mix(toCamera, vec3(0.0, 1.0, 0.0), flatness);

    vec3 world = positionSize.xyz + (corner.x * right + corner.y * up) * positionSize.w;
    gl_Position = viewProjection * vec4(world, 1.0);

    // Rotate the texture, not the quad, so the puffs don't all look alike.
    float c = cos(alphaRotationFlat.y);
    float s = sin(alphaRotationFlat.y);
    fragUV = 0.5 + 0.5 * (mat2(c, s, -s, c) * corner);
    fragCorner = corner;

    fragLightLocal = vec3(dot(lightDirection, right), dot(lightDirection, up), dot(lightDirection, facing));
    // Droplets scatter light mostly forwards: spray between the camera and the sun lights up.
    fragScatter = pow(max(dot(-toCamera, lightDirection), 0.0), 8.0);
    // Fade out quads very close to the lens (deck camera): there one puff would cover half the screen,
    // and the straight line where it cuts through the deck would show.
    fragAlpha = alphaRotationFlat.x * smoothstep(1.0, 4.0, distance(cameraPosition, positionSize.xyz));
}
