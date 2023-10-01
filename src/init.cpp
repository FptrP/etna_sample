#include "app.hpp"
#include <events/events.hpp>

#include <cstdlib>
#include <SDL2/SDL_vulkan.h>

#include <etna/RenderTargetStates.hpp>
#include "scene/SceneRenderer.hpp"
#include "app_events.hpp"

struct GuiSystem
{
  GuiSystem()
  {
    cb = event::make_handle();
    cb.addCallback<KeyPressedEvent>([this](const KeyPressedEvent &evt) {
      if (evt.key == SDLK_g && !evt.repeat)
        mainWindowEnabled = !mainWindowEnabled;
    });
  }

  void drawUI()
  {
    if (!mainWindowEnabled)
      return;

    ImGui::Begin("Main window");
    event::send_event_immediate(GuiRenderEvent{});
    ImGui::End();
  }

private:
  event::CallbackHandle cb;
  bool mainWindowEnabled = false;
};


static std::optional<vk::UniqueSurfaceKHR> create_surface(SDL_Window *window)
{
  VkSurfaceKHR api_surface {nullptr};
  auto instance = etna::get_context().getInstance();

  if (!SDL_Vulkan_CreateSurface(window, instance, &api_surface))
  {
    ETNA_ASSERT(0);
    return {};
  }

  return {vk::UniqueSurfaceKHR{api_surface, instance}};
}

AppInit::AppInit(uint32_t init_width, uint32_t init_height)
{
  setenv("SDL_VIDEODRIVER", "x11", 1); // nvidia hates wayland...
  SDL_Init(SDL_INIT_VIDEO);
  
  auto raw_window = SDL_CreateWindow("Vulkan window", 
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED, 
    init_width, init_height, 
    SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  
  window.reset(raw_window);
  
  etna::InitParams params {};
  params.applicationName = "NoApp";
  params.applicationVersion = VK_MAKE_VERSION(1, 0, 0);

  uint32_t ext_count;
  SDL_Vulkan_GetInstanceExtensions(window.get(), &ext_count, nullptr);
  
  std::vector<const char *> instance_extensions;
  instance_extensions.resize(ext_count, nullptr);

  SDL_Vulkan_GetInstanceExtensions(window.get(), &ext_count, instance_extensions.data());
  
  params.instanceExtensions = std::span{instance_extensions.begin(), instance_extensions.size()};
  
  std::vector<const char*> device_ext {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  params.deviceExtensions = device_ext;
  params.features.features.fragmentStoresAndAtomics = VK_TRUE;

  etna::initialize(params);
  auto surface = create_surface(getWindow()).value();
  submitCtx = etna::create_submit_context(surface.release(), {init_width, init_height}, true);

  imguiCtx = std::make_unique<ImguiInitilizer>(
    window.get(), 
    submitCtx->getSwapchainFmt(),
    submitCtx->getBackbuffersCount());
  
  auto uploadCmd = submitCtx->getCommandPool().allocate();
  imguiCtx->uploadFonts(uploadCmd);
}

AppInit::~AppInit() {}

void AppInit::mainLoop()
{
  GuiSystem gui {};
  double deltaT = 0.f;
  double time = SDL_GetTicks64()/1000.0;

  auto swapchainRecreateCb = [&]() {
    etna::get_context().getDevice().waitIdle();
    
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window.get(), &width, &height);
    uint32_t nWidth = width, nHeight = height;
    
    auto newExtent = submitCtx->recreateSwapchain({.width = nWidth, .height = nHeight});
    spdlog::warn("Recreating swapchain : w={}, h={}", newExtent.width, newExtent.height);
    etna::get_context().getQueueTrackingState().onWait();
    onResolutionChanged(newExtent.width, newExtent.height);
  };

  bool quit = false;

  while (!quit)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_QUIT)
        quit = true;
      ImGui_ImplSDL2_ProcessEvent(&event);
      event::dispatch_sdl_events(getWindow(), event);
    }

    ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(window.get());
		ImGui::NewFrame();

    gui.drawUI();

    double deltaT = SDL_GetTicks64()/1000.0 - time;
    time = SDL_GetTicks64()/1000.0;
    update(deltaT);

    auto [backbuffer, state] = submitCtx->acquireBackbuffer();
    if (state != etna::SwapchainState::Ok)
    {
      swapchainRecreateCb();
      continue;
    }

    ImGui::Render();
    auto &cmd = submitCtx->acquireNextCmd();
    
    cmd.begin();
    recordRenderCmd(cmd, *backbuffer);
    cmd.end();

    state = submitCtx->submitCmd(cmd, true);
    if (state != etna::SwapchainState::Ok)
      swapchainRecreateCb();
  }

  etna::get_context().getDevice().waitIdle();
}

void AppInit::drawImGui(etna::SyncCommandBuffer &cmd, const etna::Image &backbuffer)
{
  vk::Extent2D extent {
    backbuffer.getInfo().extent.width,
    backbuffer.getInfo().extent.height
  };

  etna::RenderingAttachment colorAttachment {
    .view = backbuffer.getView({}),
    .layout = vk::ImageLayout::eColorAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eLoad
  };

  etna::RenderTargetState rts{cmd, extent, {colorAttachment}, {}};
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), VkCommandBuffer(cmd.getRenderCmd()));
}