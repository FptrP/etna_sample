#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../include/GLTFMaterial.glsl"

layout (set = 0, binding = 0) uniform UboData
{
  GlobalFrameParams gFrame;
}; 

layout (push_constant) uniform PushData
{
  PushConstMaterial pc;
};

layout (location = 0) in vec3 IN_POS;
layout (location = 1) in vec3 IN_NORM;
layout (location = 2) in vec2 IN_UV;


layout (location = 0) out vec2 OUT_UV;
layout (location = 1) out vec3 OUT_NORM;

void main()
{
  gl_Position = pc.MVP * vec4(IN_POS, 1); 
  OUT_UV = IN_UV;
  OUT_NORM = vec3(pc.normalTransform * vec4(IN_NORM, 0));
}