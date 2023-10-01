#version 460 core
#extension GL_GOOGLE_include_directive : enable

layout (push_constant) uniform PushData
{
  mat4 MVP;
  vec4 jitter;
} pc;

layout (location = 0) in vec3 IN_POS;

void main()
{
  vec4 pos = pc.MVP * vec4(IN_POS, 1);
  pos += vec4(pc.jitter.xy, 0, 0) * pos.w;
  gl_Position = pos; 
}