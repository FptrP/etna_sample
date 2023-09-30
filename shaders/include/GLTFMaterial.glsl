#ifndef GLTF_MATERIAL_GLSL_INCLUDED
#define GLTF_MATERIAL_GLSL_INCLUDED

struct PushConstMaterial
{
  mat4 MVP;
  mat4 prevMVP;
  mat4 normalTransform;
  vec4 baseColorFactor;
  vec4 metallic_roughness_alphaCutoff_flags;
};

struct GlobalFrameParams
{
  mat4 view;
  mat4 projection;
  mat4 viewProjection;
  mat4 prevViewProjection;
  vec4 projectionParams; //tg(fovy/2), aspect, znear, zfar
  vec4 sunDirection;
  vec4 sunColor;
  vec4 viewport;
};

#define RF_NO_BASECOLOR_TEX 1u
#define RF_NO_METALLIC_ROUGHNESS_TEX 2u

float get_metallic(in PushConstMaterial mat)
{
  return mat.metallic_roughness_alphaCutoff_flags.x;
}

float get_rouhness(in PushConstMaterial mat)
{
  return mat.metallic_roughness_alphaCutoff_flags.y;
}

float get_alpha_cutoff(in PushConstMaterial mat)
{
  return mat.metallic_roughness_alphaCutoff_flags.z;
}

uint get_render_flags(in PushConstMaterial mat)
{
  return floatBitsToUint(mat.metallic_roughness_alphaCutoff_flags.w);
}



#endif