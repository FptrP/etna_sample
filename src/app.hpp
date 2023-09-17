#ifndef APP_HPP_INCLUDED
#define APP_HPP_INCLUDED

#include <etna/Etna.hpp>
#include <etna/EtnaConfig.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/SubmitContext.hpp>
#include <etna/SyncCommandBuffer.hpp>
#include <etna/Sampler.hpp>

#include <GLFW/glfw3.h>

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
  GLFWwindow *getWindow() { return window.get(); }

private:
  struct EtnaDeleter
  {
    ~EtnaDeleter() { etna::shutdown(); }
  };

  struct WindowDeleter
  {
    void operator()(GLFWwindow *window)
    {
      if (window)
        glfwDestroyWindow(window);
    }
  };

  struct GLFWDeleter {
    ~GLFWDeleter()
    {
      glfwTerminate(); 
    }
  };

  GLFWDeleter gltfDeleter;
  std::unique_ptr<GLFWwindow, WindowDeleter> window {nullptr};
  EtnaDeleter etnaDeleter;
  std::unique_ptr<etna::SimpleSubmitContext> submitCtx;  
};



#endif