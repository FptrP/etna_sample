#version 460 core

layout (location = 0) in vec2 IN_UV;
layout (location = 0) out vec4 OUT_COLOR;

layout (set = 0, binding = 0) uniform sampler2D COLOR_TEX;

void main()
{
  OUT_COLOR = texture(COLOR_TEX, IN_UV); 
}