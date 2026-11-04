#version 410
in float distanceAlong;
in float shellRadius;
uniform vec3 beamColor;
uniform float beamOpacity;
out vec4 fragColor;
void main()
{
    // The beam dies off with travelled distance and has a very soft tip.
    // Individual thin shells avoid drawing a solid opaque cone over the ocean.
    float fade = pow(1.0 - clamp(distanceAlong, 0.0, 1.0), 1.7);
    fade *= smoothstep(0.0, 0.065, distanceAlong);
    float shellWeight = shellRadius < 0.5 ? 0.38 : (shellRadius < 0.8 ? 0.45 : 0.28);
    fragColor = vec4(beamColor, beamOpacity * fade * shellWeight * 0.09);
}
