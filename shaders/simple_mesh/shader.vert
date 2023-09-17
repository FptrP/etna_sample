#version 460 core

layout (location = 0) in vec3 IN_POS;
layout (location = 1) in vec3 IN_NORM;
layout (location = 2) in vec2 IN_UV;

layout (push_constant) uniform PushConstants
{
  mat4 MVP;
};

layout (location = 0) out vec2 OUT_UV;

void main() {
  gl_Position = MVP * vec4(IN_POS, 1);
  OUT_UV = IN_UV;
}