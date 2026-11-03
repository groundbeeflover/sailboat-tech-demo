#version 410

// Full-screen triangle on the far plane; the fragment shader looks up the cubemap along the view ray.
uniform mat4 inverseViewProjection; // inverse of (projection * view without translation)

out vec3 fragDirection;

void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec2 ndc = corner * 2.0 - 1.0;
    vec4 world = inverseViewProjection * vec4(ndc, 1.0, 1.0);
    fragDirection = world.xyz / world.w;
    gl_Position = vec4(ndc, 1.0, 1.0); // depth 1: only visible where nothing else was drawn
}
