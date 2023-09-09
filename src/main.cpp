#include <iostream>

#include <etna/Etna.hpp>
#include <etna/EtnaConfig.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/SubmitContext.hpp>
#include <etna/SyncCommandBuffer.hpp>

#include <GLFW/glfw3.h>

#include <span>
#include <optional>
#include <ranges>

static std::optional<vk::UniqueSurfaceKHR> create_surface(GLFWwindow *window)
{
  VkSurfaceKHR api_surface {nullptr};
  auto instance = etna::get_context().getInstance();
  if (glfwCreateWindowSurface(instance, window, nullptr, &api_surface) == VK_SUCCESS) {
    return {vk::UniqueSurfaceKHR{api_surface, instance}};
  }

  return {};
}

void write_commands(vk::CommandBuffer cmd, etna::CmdBufferTrackingState &cmd_tracker, const etna::Image &backbuffer)
{
  vk::ImageSubresourceRange range {
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1
  };
  
  etna::CmdBarrier barrier;

  cmd_tracker.requestState(backbuffer, 0, 0, etna::ImageSubresState {
    .activeStages = vk::PipelineStageFlagBits2::eTransfer,
    .activeAccesses = vk::AccessFlagBits2::eTransferWrite,
    .layout = vk::ImageLayout::eTransferDstOptimal
  });

  cmd_tracker.flushBarrier(barrier);
  barrier.flush(cmd);

  vk::ClearColorValue clear_val {};
  clear_val.setFloat32({1.f, 0.f, 0.f, 0.f});
  cmd.clearColorImage(backbuffer.get(), vk::ImageLayout::eTransferDstOptimal, clear_val, {range});

  cmd_tracker.requestState(backbuffer, 0, 0, etna::ImageSubresState {
    .activeStages = vk::PipelineStageFlags2{},
    .activeAccesses = vk::AccessFlags2{},
    .layout = vk::ImageLayout::ePresentSrcKHR
  });

  cmd_tracker.flushBarrier(barrier);
  barrier.flush(cmd);
}

int main()
{
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1024, 768, "Vulkan window", nullptr, nullptr);

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
  auto surface = create_surface(window).value();
  auto submitCtx = etna::create_submit_context(surface.release(), {1024, 768});
  auto cmdTracker = etna::CmdBufferTrackingState{};

  auto swapchainRecreateCb = [&](const char *where) {
    etna::get_context().getDevice().waitIdle();
    spdlog::warn("Recreating swapchain {}", where);
    int width = 0, height = 0;
    glfwGetWindowSize(window, &width, &height);
    submitCtx->recreateSwapchain({.width = uint32_t(width), .height = uint32_t(height)});
    cmdTracker.onSync();
  };

  while(!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    auto [backbuffer, state] = submitCtx->acquireBackbuffer();
    if (state != etna::SwapchainState::Ok)
    {
      swapchainRecreateCb("acquireBackbuffer");
      continue;
    }

    auto cmd = submitCtx->acquireNextCmd();
    cmd.begin(vk::CommandBufferBeginInfo {});
    write_commands(cmd, cmdTracker, *backbuffer);
    ETNA_ASSERT(cmd.end() == vk::Result::eSuccess);
    
    state = submitCtx->submitCmd(cmd, true);
    if (state != etna::SwapchainState::Ok)
      swapchainRecreateCb("submitCmd");
  }

  etna::get_context().getDevice().waitIdle();
  submitCtx.reset();
  etna::shutdown();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}