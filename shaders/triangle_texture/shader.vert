#version 460 core

const vec2 VERTS[3] = vec2[](
  vec2(0.f, 1.f),
  vec2(1.f, -1.f),
  vec2(-1.f, -1.f)
);

const vec2 UV[3] = vec2[](
  vec2(0, 0),
  vec2(1, 0),
  vec2(1, 1)
);

layout (push_constant) uniform PushConstants
{
  mat4 MVP;
};

layout (location = 0) out vec2 OUT_UV;

void main() {
  vec4 pos = vec4(VERTS[gl_VertexIndex], 0.f, 1.f);
  gl_Position = MVP * pos;
  OUT_UV = UV[gl_VertexIndex];
}