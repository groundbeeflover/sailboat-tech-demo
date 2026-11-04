#version 410
layout(location = 0) in vec3 conePosition;
layout(location = 1) in float shell;
uniform mat4 viewProjection;
uniform mat4 beamToWorld;
uniform float beamLength;
uniform float beamRadius;
out float distanceAlong;
out float shellRadius;
void main()
{
    distanceAlong = conePosition.z;
    shellRadius = shell;
    vec3 local = vec3(conePosition.xy * beamRadius, conePosition.z * beamLength);
    gl_Position = viewProjection * beamToWorld * vec4(local, 1.0);
}
