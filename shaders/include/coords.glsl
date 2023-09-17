#ifndef COORDS_GLSL_INCLUDED
#define COORDS_GLSL_INCLUDED

float linearize_depth(float depth, float znear, float zfar)
{
  float p = zfar/(znear - zfar);
  float q = znear * zfar/(znear - zfar); 
  return -q/(p + depth);
}

vec3 reconstruct_camera_vec(vec2 uv, float depth, vec4 project_params)
{
  float xn = 2.f * (uv.x - 0.5f);
  float yn = 2.f * (uv.y - 0.5f);

  float z = linearize_depth(depth, project_params.z, project_params.w);

  float x = -xn * z * project_params.x * project_params.y;
  float y =  yn * z * project_params.x;  

  return vec3(x, y, z);
}

vec3 reconstruct_camera_NDC(float xn, float yn, float depth, vec4 project_params)
{
  float z = linearize_depth(depth, project_params.z, project_params.w);

  float x = -xn * z * project_params.x * project_params.y;
  float y = -yn * z * project_params.x;  
  return vec3(x, y, z);
}


#endif