#include <iostream>

#include <etna/Etna.hpp>
#include <etna/EtnaConfig.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/SubmitContext.hpp>
#include <etna/SyncCommandBuffer.hpp>
#include <etna/Sampler.hpp>

#include <GLFW/glfw3.h>

#include <span>
#include <optional>
#include <ranges>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static std::optional<vk::UniqueSurfaceKHR> create_surface(GLFWwindow *window)
{
  VkSurfaceKHR api_surface {nullptr};
  auto instance = etna::get_context().getInstance();
  if (glfwCreateWindowSurface(instance, window, nullptr, &api_surface) == VK_SUCCESS) {
    return {vk::UniqueSurfaceKHR{api_surface, instance}};
  }

  return {};
}

etna::Image load_image(etna::SyncCommandBuffer &cmd, std::string_view path)
{
  int x, y, c;
  auto pixels = stbi_load(path.begin(), &x, &y, &c, 4);

  etna::ImageCreateInfo info {
    .extent = {uint32_t(x), uint32_t(y), 1},
  };

  info.imageUsage = etna::ImageCreateInfo::imageUsageFromFmt(info.format, false);

  auto image = etna::create_image_from_bytes(info, cmd, pixels);

  stbi_image_free(pixels);
  return image;
}

static etna::GraphicsPipeline create_pipeline(vk::Format swapchain_fmt)
{
  etna::create_program("triangle", {
    "shaders/triangle_texture/shader.vert.spv",
    "shaders/triangle_texture/shader.frag.spv"
  });

  etna::GraphicsPipeline::CreateInfo info {};
  info.fragmentShaderOutput.colorAttachmentFormats.push_back(swapchain_fmt);
  return etna::get_context().getPipelineManager().createGraphicsPipeline("triangle", info);
}

void write_commands(etna::SyncCommandBuffer &cmd, const etna::Image &backbuffer, 
  const etna::GraphicsPipeline &pipeline, const etna::Image &texture, vk::Sampler sampler)
{
  auto resolution = backbuffer.getInfo().extent;

  auto progInfo = etna::get_shader_program(pipeline.getShaderProgram());
  auto set = etna::create_descriptor_set(progInfo.getDescriptorLayoutId(0), {
    etna::Binding {0, texture.genBinding(sampler, vk::ImageLayout::eShaderReadOnlyOptimal)}
  });
  
  cmd.beginRendering({{0, 0}, {resolution.width, resolution.height}}, {
    etna::RenderingAttachment {
      .view = backbuffer.getView({}),
      .layout = vk::ImageLayout::eColorAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear
    }
  });

  cmd.bindPipeline(pipeline);
  cmd.bindDescriptorSet(vk::PipelineBindPoint::eGraphics, progInfo.getPipelineLayout(), 0, set);

  cmd.setViewport(0, {
    vk::Viewport {
      .width = (float)resolution.width, 
      .height = (float)resolution.height, 
      .minDepth = 0.f, 
      .maxDepth = 1.f
    }
  });

  cmd.setScissor(0, {vk::Rect2D{{0, 0}, {resolution.width, resolution.height}}});
  cmd.draw(3, 1, 0, 0);
  cmd.endRendering();

  cmd.transformLayout(backbuffer, vk::ImageLayout::ePresentSrcKHR, {
    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
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
  auto pipeline = create_pipeline(submitCtx->getSwapchainFmt());

  std::optional<etna::SyncCommandBuffer> cmd {submitCtx->getCommandPool()};
  
  auto texture = load_image(*cmd, "assets/brick.png"); 
  auto sampler = etna::Sampler{etna::Sampler::CreateInfo {.filter = vk::Filter::eLinear }};

  auto swapchainRecreateCb = [&](const char *where) {
    etna::get_context().getDevice().waitIdle();
    spdlog::warn("Recreating swapchain {}", where);
    int width = 0, height = 0;
    glfwGetWindowSize(window, &width, &height);
    submitCtx->recreateSwapchain({.width = uint32_t(width), .height = uint32_t(height)});
    etna::get_context().getQueueTrackingState().onWait();
  };

  while(!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    auto [backbuffer, state] = submitCtx->acquireBackbuffer();
    if (state != etna::SwapchainState::Ok)
    {
      swapchainRecreateCb("acquireBackbuffer");
      continue;
    }

    auto &cmd = submitCtx->acquireNextCmd();
    cmd.begin();
    write_commands(cmd, *backbuffer, pipeline, texture, sampler.get());
    ETNA_ASSERT(cmd.end() == vk::Result::eSuccess);
    
    state = submitCtx->submitCmd(cmd, true);
    if (state != etna::SwapchainState::Ok)
      swapchainRecreateCb("submitCmd");
  }

  etna::get_context().getDevice().waitIdle();
  
  {
    auto tmp = std::move(sampler);
  }
  
  texture.reset();
  cmd.reset();
  submitCtx.reset();
  etna::shutdown();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}