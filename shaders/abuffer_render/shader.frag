#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../include/GLTFMaterial.glsl"
#include "../include/coords.glsl"
#include "../include/BRDF.glsl"
#include "../include/ABuffer.glsl"

layout(early_fragment_tests) in;

layout (location = 0) in vec2 IN_UV;
layout (location = 1) in vec3 IN_NORM;

layout (set = 0, binding = 0) uniform UboData
{
  GlobalFrameParams gFrame;
}; 

layout (push_constant) uniform PushData
{
  PushConstMaterial pc;
};

layout (set = 0, binding = 1) uniform sampler2D BASE_COLOR_TEX;
layout (set = 0, binding = 2) uniform sampler2D METALLIC_ROUGHNESS_TEX;

layout (set = 0, binding = 3, r32ui) uniform uimage2D LIST_HEAD_TEX;
layout (set = 0, binding = 4, std430) buffer FragmentListBuffer
{
  uint fragmentsCounter;
  FragmentEntry entries[];
} gList;

void main()
{
  uint renderFlags = get_render_flags(pc);
  float metallic = get_metallic(pc);
  float roughness = get_rouhness(pc);

  if ((renderFlags & RF_NO_METALLIC_ROUGHNESS_TEX) == 0)
  {
    vec4 m = texture(METALLIC_ROUGHNESS_TEX, IN_UV);
    metallic = m.b; //!!!!!
    roughness = m.g;
  }

  vec3 baseColor = pc.baseColorFactor.rgb;
  float alpha = pc.baseColorFactor.a;

  if ((renderFlags & RF_NO_BASECOLOR_TEX) == 0)
  {
    vec4 s = texture(BASE_COLOR_TEX, IN_UV).rgba;
    baseColor = s.rgb;
    alpha = s.a;
  }

  vec3 N = normalize(IN_NORM);
  vec3 L = gFrame.sunDirection.xyz;  
  vec3 V = vec3(0, 0, 0);

  {
    vec2 uv = vec2(gl_FragCoord.x/gFrame.viewport.x, gl_FragCoord.y/gFrame.viewport.y);
    vec3 cameraPos = reconstruct_camera_vec(uv, gl_FragCoord.z, gFrame.projectionParams);

    V = normalize(-cameraPos);

  }
  vec3 ambientLight = vec3(0.15, 0.15, 0.15);
  vec3 brdf = BRDF(N, V, L, baseColor, gFrame.sunColor.rgb, ambientLight, metallic, roughness); 
  brdf = pow(brdf, vec3(1/2.2));

  vec4 outColor = vec4(brdf, alpha);

  FragmentEntry entry;
  entry.depth = gl_FragCoord.z;
  entry.packedColor = packUnorm4x8(outColor);
  entry.next = ABUFFER_LIST_END;

  ivec2 pixelPos = ivec2(gl_FragCoord.x, gl_FragCoord.y);

  uint fragmentId = atomicAdd(gList.fragmentsCounter, 1);
  uint previousId = imageAtomicExchange(LIST_HEAD_TEX, pixelPos, fragmentId);
  
  entry.next = previousId;
  gList.entries[fragmentId] = entry;
}