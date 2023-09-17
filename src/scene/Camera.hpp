#ifndef SCENE_CAMERA_HPP_INCLUDED
#define SCENE_CAMERA_HPP_INCLUDED

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

struct Camera
{
  Camera(glm::vec3 pos_, glm::vec3 forward_)
    : position {pos_}, front {glm::normalize(forward_)}
  { 
    right = glm::normalize(glm::cross(front, up));
    pitch = glm::degrees(std::asin(front.y));
    yaw = glm::degrees(std::atan2(front.z, front.x));
    rotate(0.f, 0.f);
  }

  glm::mat4 getViewMat() const
  {
    return glm::lookAtRH(position, position + front, up);
  }

  void move(const glm::vec3 &offset)
  {
    position += offset.x * right + offset.y * up + offset.z * front;
  }

  void rotate(float d_yaw, float d_pitch)
  {
    yaw += d_yaw;
    if (yaw < 0.f)
      yaw += 360.f;
    if (yaw > 360.f)
      yaw -= 360.f;

    pitch = glm::clamp<float>(pitch + d_pitch, -89.f, 89.f);

    glm::vec3 direction {
      std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch)),
      std::sin(glm::radians(pitch)),
      std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch))
    };

    front = glm::normalize(direction);
    right = glm::normalize(glm::cross(front, up));
    //up = glm::normalize(glm::cross(right, front));
  }

private:
  float yaw;
  float pitch;

  glm::vec3 position;
  glm::vec3 front;
  glm::vec3 right;
  glm::vec3 up {0, 1, 0};
};

inline glm::mat4 vk_perspective(float fovy, float aspect, float znear, float zfar)
{
  auto mat = glm::perspective(fovy, aspect, znear, zfar);
  mat[1][1] *= -1.f;
  return mat;
}

#endif