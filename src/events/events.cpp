#include "events.hpp"
#include "app_events.hpp"
#include <iostream>

namespace event
{

EventDispatcher g_event_dispatcher;

CallbackHandle::~CallbackHandle()
{
  for (auto id : ids)
    owner->removeCallback(id);
}

void EventDispatcher::dispatchEvent(const EventDispatcher::EventStorageBase &event) const
{
  if (!eventHandlers.contains(event.eventType))
    return;
     
  auto buckedId = eventHandlers.bucket(event.eventType);

  for (auto it = eventHandlers.begin(buckedId); it != eventHandlers.end(buckedId); it++)
    if (it->first == event.eventType)
      it->second->cb(event);
}

void EventDispatcher::processEvents()
{
  if (!eventHandlers.bucket_count())
  {
    eventQueue.clear();
    return;
  }

  while (eventQueue.size())
  {
    auto &storage = eventQueue.front();
    dispatchEvent(*storage);
    eventQueue.pop_front();
  }
}

void EventDispatcher::removeCallback(uint32_t id)
{
  auto it = std::find_if(eventHandlers.begin(), eventHandlers.end(), 
    [&](const auto &val){
      return val.second->id == id;
    }
  );

  if (it != eventHandlers.end())
    eventHandlers.erase(it);
}

void dispatch_sdl_events(SDL_Window *window, const SDL_Event &event)
{
  if (event.type == SDL_KEYDOWN)
  {
    KeyPressedEvent keyEvent {
      .key = event.key.keysym.sym,
      .repeat = event.key.repeat
    };

    g_event_dispatcher.sendEventImmediate(std::move(keyEvent));
  }
  else if (event.type == SDL_KEYUP)
  {
    KeyReleasedEvent keyEvent {.key = event.key.keysym.sym };
    g_event_dispatcher.sendEventImmediate(std::move(keyEvent));
  }
  else if (event.type == SDL_MOUSEMOTION)
  {
    auto &motion = event.motion;
    MouseMoveEvent moveEvent {
      .position = glm::vec2{motion.x, motion.y},
      .positionDelta = glm::vec2 {motion.xrel, motion.yrel}
    };
    g_event_dispatcher.sendEventImmediate(std::move(moveEvent));
  }
}

} // namespace event