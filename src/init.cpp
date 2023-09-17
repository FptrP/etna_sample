#include "app.hpp"

static void glfw_cb(int err_code, const char *desc)
{
  spdlog::error("gltf error {} : {}", err_code, desc);
}

static std::optional<vk::UniqueSurfaceKHR> create_surface(GLFWwindow *window)
{
  VkSurfaceKHR api_surface {nullptr};
  auto instance = etna::get_context().getInstance();
  if (glfwCreateWindowSurface(instance, window, nullptr, &api_surface) == VK_SUCCESS) {
    return {vk::UniqueSurfaceKHR{api_surface, instance}};
  }

  return {};
}

AppInit::AppInit(uint32_t init_width, uint32_t init_height)
{
  glfwInit();
  ETNA_ASSERT(glfwInit() == GLFW_TRUE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwSetErrorCallback(glfw_cb);
  window.reset(glfwCreateWindow(init_width, init_height, "Vulkan window", nullptr, nullptr));

  etna::InitParams params {};
  params.applicationName = "NoApp";
  params.applicationVersion = VK_MAKE_VERSION(1, 0, 0);

  uint32_t ext_count;
  auto ptr = glfwGetRequiredInstanceExtensions(&ext_count);
  
  std::vector<const char *> instance_extensions; 
  for (uint32_t i = 0; i < ext_count; i++)
    instance_extensions.push_back(ptr[i]);
  
  params.instanceExtensions = std::span{instance_extensions.begin(), instance_extensions.size()};
  
  std::vector<const char*> device_ext {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  params.deviceExtensions = device_ext;

  etna::initialize(params);
  auto surface = create_surface(getWindow()).value();
  submitCtx = etna::create_submit_context(surface.release(), {init_width, init_height});
}

AppInit::~AppInit() {}

void AppInit::mainLoop()
{
  double deltaT = 0.f;
  double time = glfwGetTime();

  auto swapchainRecreateCb = [&]() {
    etna::get_context().getDevice().waitIdle();
    spdlog::warn("Recreating swapchain");
    
    int width = 0, height = 0;
    glfwGetWindowSize(getWindow(), &width, &height);

    uint32_t nWidth = width, nHeight = height;

    submitCtx->recreateSwapchain({.width = nWidth, .height = nHeight});
    etna::get_context().getQueueTrackingState().onWait();

    onResolutionChanged(nWidth, nHeight);
  };

  while (!glfwWindowShouldClose(getWindow()))
  {
    glfwPollEvents();
    deltaT = glfwGetTime() - time;
    time = glfwGetTime();
    update(deltaT);

    auto [backbuffer, state] = submitCtx->acquireBackbuffer();
    if (state != etna::SwapchainState::Ok)
    {
      swapchainRecreateCb();
      continue;
    }
    
    auto &cmd = submitCtx->acquireNextCmd();
    recordRenderCmd(cmd, *backbuffer);

    state = submitCtx->submitCmd(cmd, true);
    if (state != etna::SwapchainState::Ok)
      swapchainRecreateCb();
  }

  etna::get_context().getDevice().waitIdle();
}