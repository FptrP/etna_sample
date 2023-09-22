#version 460 core

layout (location = 0) in vec2 IN_UV;
layout (location = 0) out vec4 OUT_COLOR;

layout (set = 0, binding = 0) uniform sampler2D TEXTURE;

void main()
{
  OUT_COLOR = texture(TEXTURE, IN_UV);
}