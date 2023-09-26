#ifndef APP_EVENTS_HPP_INCLUDED
#define APP_EVENTS_HPP_INCLUDED

#include <SDL2/SDL.h>

#include "scene/Camera.hpp"

struct MouseMoveEvent
{
  glm::vec2 position;
  glm::vec2 positionDelta;
};

struct KeyPressedEvent
{
  SDL_Keycode key;
  bool repeat;
};

struct KeyReleasedEvent
{
  SDL_Keycode key;
};

struct ResolutionChangedEvent
{
  glm::uvec2 newResolution;
};

struct GuiRenderEvent
{
  //nope
};

#endif