#ifndef CAMERA_EVENTS_HPP_INCLUDED
#define CAMERA_EVENTS_HPP_INCLUDED

#include "events/events.hpp"
#include "app_events.hpp"
#include "scene/Camera.hpp"

struct CameraSystem
{
  CameraSystem(float move_speed = 1.f, float mouse_speed = 1.f)
    : moveSpeed {move_speed}, mouseSpeed {mouse_speed}
  {
    cb = event::make_handle();
    cb.addCallback<KeyPressedEvent>([this](const KeyPressedEvent &evt)
    {
      updateMovement(evt.key, true);
      if (evt.key == SDLK_f && !evt.repeat)
      {
        cursorHidden = !cursorHidden;
        SDL_SetRelativeMouseMode(SDL_bool(cursorHidden));
      }
    });

    cb.addCallback<KeyReleasedEvent>([this](const KeyReleasedEvent &evt)
    {
      updateMovement(evt.key, false);
    });

    cb.addCallback<MouseMoveEvent>([this](const MouseMoveEvent &evt)
    {
      if (cursorHidden)
        mouseMovement += evt.positionDelta;
    });
  }
  
  void update(Camera &camera, float dt)
  {
    glm::vec3 keyMovement {0.f, 0.f, 0.f};
    keyMovement.x = movementX.x - movementX.y;
    keyMovement.y = movementY.x - movementY.y;
    keyMovement.z = movementZ.x - movementZ.y;

    camera.move(keyMovement * dt);
    camera.rotate(mouseSpeed * mouseMovement.x, mouseSpeed * (-mouseMovement.y));
    
    keyMovement = glm::vec3{0.f, 0.f, 0.f};
    mouseMovement = glm::vec2{0.f, 0.f};
  }

private:
  void updateMovement(SDL_Keycode key, bool is_pressed)
  {
    float value = is_pressed? 1.f : 0.f;
    switch (key)
    {
      case SDLK_w:
        movementZ.x = value;
        break;
      case SDLK_s:
        movementZ.y = value;
        break;
      case SDLK_a:
        movementX.y = value;
        break;
      case SDLK_d:
        movementX.x = value;
        break;
      case SDLK_e:
        movementY.x = value;
        break;
      case SDLK_q:
        movementY.y = value;
        break;
      default:
        break;
    }
  }

  event::CallbackHandle cb;

  float moveSpeed = 1.f;
  float mouseSpeed = 1.f;

  bool cursorHidden = false;

  glm::vec2 movementX {0.f, 0.f};
  glm::vec2 movementY {0.f, 0.f};
  glm::vec2 movementZ {0.f, 0.f};

  glm::vec2 mouseMovement {0.f, 0.f};
};


#endif