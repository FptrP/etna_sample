#ifndef EVENTS_HPP_INCLUDED
#define EVENTS_HPP_INCLUDED

#include <scene/Camera.hpp>

#include <deque>
#include <variant>
#include <functional>
#include <optional>
#include <typeinfo>
#include <typeindex>
#include <memory>

#include <SDL2/SDL.h>

namespace event
{ 

struct EventDispatcher;

struct CallbackHandle
{
  CallbackHandle(const CallbackHandle &) = delete;
  CallbackHandle &operator=(const CallbackHandle &) = delete;

  CallbackHandle(){}
  CallbackHandle(CallbackHandle &&rhs) 
  {
    std::swap(owner, rhs.owner);
    std::swap(ids, rhs.ids);
  }

  CallbackHandle &operator=(CallbackHandle &&rhs)
  {
    std::swap(owner, rhs.owner);
    std::swap(ids, rhs.ids);
    return *this;
  }

  template <typename EventT, typename Func>
  void addCallback(Func &&func);

  ~CallbackHandle();

private:
  CallbackHandle(EventDispatcher *dispatcher)
    : owner {dispatcher} {}

  EventDispatcher *owner = nullptr;
  std::vector<uint32_t> ids;

  friend EventDispatcher;
};

struct EventDispatcher
{

  CallbackHandle createHandle()
  {
    return {this};
  }

  template <typename EventT>
  void sendEvent(EventT &&event)
  {
    std::unique_ptr<EventStorageBase> ptr;
    ptr.reset(new EventStorage<EventT>{std::move(event)});
    eventQueue.push_back(std::move(ptr));
  }

  template <typename EventT>
  void sendEventImmediate(EventT &&event)
  {
    EventStorage<EventT> eventStorage{std::move(event)};
    dispatchEvent(eventStorage);
  }

  template <typename EventT, typename Func>
  uint32_t registerCallback(Func &&cb)
  {
    uint32_t cbId = handlersId; 
    handlersId++;

    std::function<void(const EventStorageBase &evt)> funcWrapper = 
      [cb=std::move(cb)](const EventStorageBase &evt) {
        auto &v = reinterpret_cast<const EventStorage<EventT>&>(evt);
        cb(v.data);
      };

    auto cbWrapper = std::make_unique<EventSubscriber>(cbId, typeid(EventT), std::move(funcWrapper));
    auto eventType = cbWrapper->eventType;
    eventHandlers.emplace(eventType, std::move(cbWrapper));
    return cbId;
  }

  void processEvents();
  void removeCallback(uint32_t id);

private:

  struct EventStorageBase
  {
    EventStorageBase(std::type_index i) : eventType {i} {}
    virtual ~EventStorageBase() {}
    std::type_index eventType;
  };

  void dispatchEvent(const EventStorageBase &event) const;

  template <typename EventT>
  struct EventStorage : EventStorageBase
  {
    EventStorage(EventT &&e) : EventStorageBase{typeid(EventT)}, data {std::move(e)} {}
    EventStorage(const EventT &e) : EventStorageBase{typeid(EventT)}, data {e} {}

    ~EventStorage() {}

    EventT data;
  };

  struct EventSubscriber
  {
    uint32_t id;
    std::type_index eventType;
    std::function<void(const EventStorageBase &evt)> cb;
  };

  std::deque<std::unique_ptr<EventStorageBase>> eventQueue;

  uint32_t handlersId = 0;
  std::unordered_multimap<std::type_index, std::unique_ptr<EventSubscriber>> eventHandlers;
};

template <typename EventT, typename Func>
void CallbackHandle::addCallback(Func &&func)
{
  auto id = owner->registerCallback<EventT>(std::move(func));
  ids.push_back(id);
}


extern EventDispatcher g_event_dispatcher;

void dispatch_sdl_events(SDL_Window *window, const SDL_Event &event);
  
template <typename EventT>
void send_event_immediate(EventT &&event)
{
  g_event_dispatcher.sendEventImmediate(std::move(event));
}

template <typename EventT>
void send_event(EventT &&event)
{
  g_event_dispatcher.sendEvent(std::move(event));
}

inline void process_events()
{
  g_event_dispatcher.processEvents();
}

template <typename EventT, typename Func>
CallbackHandle register_callback(Func &&func)
{
  return g_event_dispatcher.registerCallback<EventT>(std::move(func));
}

inline CallbackHandle make_handle()
{
  return g_event_dispatcher.createHandle();
}

} // namespace event

#endif