#version 410

// One triangle that covers the whole screen, generated from gl_VertexID (no vertex buffer needed;
// draw with glDrawArrays(GL_TRIANGLES, 0, 3) and any VAO bound).
out vec2 fragUV;

void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); // (0,0), (2,0), (0,2)
    fragUV = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
