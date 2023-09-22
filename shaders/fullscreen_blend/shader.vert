#version 460 core

const vec2 SCREEN_POS[] = vec2[](
  vec2(-1, -1),
  vec2(-1, 3),
  vec2(3, -1)
);

const vec2 SCREEN_UV[] = vec2[](
  vec2(0, 0),
  vec2(0, 2),
  vec2(2, 0)
);

layout (location = 0) out vec2 OUT_UV;

void main()
{
  gl_Position = vec4(SCREEN_POS[gl_VertexIndex], 0, 1);
  OUT_UV = SCREEN_UV[gl_VertexIndex];
}