#version 460 core

const vec2 VERTS[3] = vec2[](
  vec2(0.f, 1.f),
  vec2(1.f, -1.f),
  vec2(-1.f, -1.f)
);

void main() {
  gl_Position = vec4(VERTS[gl_VertexIndex], 0.f, 1.f);
}