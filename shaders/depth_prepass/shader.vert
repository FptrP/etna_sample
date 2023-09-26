#version 460 core
#extension GL_GOOGLE_include_directive : enable

layout (push_constant) uniform PushData
{
  mat4 MVP;
} pc;

layout (location = 0) in vec3 IN_POS;

void main()
{
  gl_Position = pc.MVP * vec4(IN_POS, 1); 
}