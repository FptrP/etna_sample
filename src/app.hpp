#ifndef APP_HPP_INCLUDED
#define APP_HPP_INCLUDED

#include <etna/Etna.hpp>
#include <etna/EtnaConfig.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/SubmitContext.hpp>
#include <etna/SyncCommandBuffer.hpp>
#include <etna/Sampler.hpp>

#include <SDL2/SDL.h>

#include "imgui_ctx.hpp"

struct AppInit
{
  AppInit(uint32_t init_width, uint32_t init_height);
  virtual ~AppInit();

  void mainLoop();

  virtual void onResolutionChanged(uint32_t new_width, uint32_t new_height) {}
  virtual void recordRenderCmd(etna::SyncCommandBuffer &cmd, const etna::Image &backbuffer) {}
  virtual void update(float dt) {}

protected:
  etna::SimpleSubmitContext &getSubmitCtx() { return *submitCtx; }
  SDL_Window *getWindow() { return window.get(); }

  void drawImGui(etna::SyncCommandBuffer &cmd, const etna::Image &backbuffer);

private:
  struct EtnaDeleter
  {
    ~EtnaDeleter() { etna::shutdown(); }
  };

  struct WindowDeleter
  {
    void operator()(SDL_Window *window)
    {
      if (window)
        SDL_DestroyWindow(window);
    }
  };

  struct SDLDeleter
  {
    ~SDLDeleter() {
      SDL_Quit();
    }
  };

  SDLDeleter sdlDeleter;
  std::unique_ptr<SDL_Window, WindowDeleter> window {nullptr};
  EtnaDeleter etnaDeleter;
  std::unique_ptr<etna::SimpleSubmitContext> submitCtx;  
  std::unique_ptr<ImguiInitilizer> imguiCtx;
};



#endif