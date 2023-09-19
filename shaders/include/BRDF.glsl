#ifndef BRDF_GLSL_INCLUDED
#define BRDF_GLSL_INCLUDED

//https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation
//https://github.com/KhronosGroup/glTF-Sample-Viewer/blob/main/source/Renderer/shaders/brdf.glsl

#define M_PI 3.141592653589793f

vec3 F_Schlick(vec3 f0, vec3 f90, float VdotH)
{
  return f0 + (f90 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;

  float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
  float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);

  float GGX = GGXV + GGXL;
  if (GGX > 0.0)
  {
    return 0.5 / GGX;
  }
  return 0.0;
}

float D_GGX(float NdotH, float alphaRoughness)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;
  float f = (NdotH * NdotH) * (alphaRoughnessSq - 1.0) + 1.0;
  return alphaRoughnessSq / (M_PI * f * f);
}

vec3 BRDF_specularGGX(vec3 f0, vec3 f90, float alphaRoughness, float specularWeight, 
                      float VdotH, float NdotL, float NdotV, float NdotH)
{
  vec3 F = F_Schlick(f0, f90, VdotH);
  float Vis = V_GGX(NdotL, NdotV, alphaRoughness);
  float D = D_GGX(NdotH, alphaRoughness);
  return specularWeight * F * Vis * D;
}

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#acknowledgments AppendixB
vec3 BRDF_lambertian(vec3 f0, vec3 f90, vec3 diffuseColor, float specularWeight, float VdotH)
{
  // see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
  return (1.0 - specularWeight * F_Schlick(f0, f90, VdotH)) * (diffuseColor / M_PI);
}


vec3 BRDF(vec3 N, vec3 V, vec3 L, vec3 baseColor, vec3 lightColor, vec3 ambientLight, float metallic, float roughness)
{
  vec3 H = normalize(L + V);
  vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
  vec3 F90 = vec3(1, 1, 1);

  float NdotL = max(dot(N, L), 0);
  float NdotV = max(dot(N, V), 0);
  float NdotH = max(dot(N, H), 0);
  float VdotH = max(dot(V, H), 0);

  vec3 lightIntensity = lightColor * NdotL;

  float specularWeight = 1.f;
  vec3 cDiff = mix(baseColor, vec3(0, 0, 0), metallic);

  vec3 brdfSpecular = BRDF_specularGGX(F0, F90, roughness, metallic,
   VdotH, NdotL, NdotV, NdotH);
  
  vec3 brdfLambertian = BRDF_lambertian(F0, F90, cDiff, specularWeight, VdotH);

  return lightIntensity * (brdfSpecular + brdfLambertian) + ambientLight * cDiff;
}

#endif