#ifndef IMGUI_CTX_HPP_INCLUDED
#define IMGUI_CTX_HPP_INCLUDED

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL2/SDL.h>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>

struct ImguiInitilizer
{
  ImguiInitilizer(SDL_Window *window, vk::Format swapchain_fmt, uint32_t swapchain_images)
  {
    vk::DescriptorPoolSize poolSizes [] {
      {vk::DescriptorType::eSampledImage, 100},
      {vk::DescriptorType::eSampler, 100},
      {vk::DescriptorType::eUniformBuffer, 100},
      {vk::DescriptorType::eCombinedImageSampler, 100},
    };
    
    vk::DescriptorPoolCreateInfo descInfo {
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = 100
    };

    descInfo.setPoolSizes(poolSizes);

    imguiPool = etna::get_context().getDevice().createDescriptorPoolUnique(descInfo).value;

    ETNA_ASSERT(loadVkFunc("vkCmdBeginRenderingKHR", nullptr) != nullptr);
    ETNA_ASSERT(loadVkFunc("vkCmdEndRenderingKHR", nullptr) != nullptr);

    ImGui::CreateContext();
	  ImGui_ImplSDL2_InitForVulkan(window);
    ImGui_ImplVulkan_LoadFunctions(loadVkFunc, nullptr);

    ImGui_ImplVulkan_InitInfo imguiInitInfo {
      .Instance = etna::get_context().getInstance(),
      .PhysicalDevice = etna::get_context().getPhysicalDevice(),
      .Device = etna::get_context().getDevice(),
      .QueueFamily = etna::get_context().getQueueFamilyIdx(),
      .Queue = etna::get_context().getQueue(),
      .PipelineCache = nullptr,
      .DescriptorPool = imguiPool.get(),
      .Subpass = 0,
      .MinImageCount = swapchain_images,
      .ImageCount = swapchain_images,
      .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
      .UseDynamicRendering = true,
      .ColorAttachmentFormat = VkFormat(swapchain_fmt),
      .Allocator = nullptr,
      .CheckVkResultFn = nullptr
    };

    bool ok = ImGui_ImplVulkan_Init(&imguiInitInfo, nullptr);
    ETNA_ASSERT(ok);
  }

  void uploadFonts(etna::SyncCommandBuffer &cmd)
  {
    cmd.reset();
    cmd.begin();
    ImGui_ImplVulkan_CreateFontsTexture(cmd.get());
    cmd.end();
    cmd.submit();

    auto res = etna::get_context().getQueue().waitIdle();
    ETNA_ASSERT(res == vk::Result::eSuccess);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }

  ~ImguiInitilizer()
  {
    ImGui_ImplVulkan_Shutdown();
  }


private:

  static PFN_vkVoidFunction loadVkFunc(const char *func_name, void */*user_data*/)
  {
    //Imgui is too stupid to load this...
    if (!strcmp(func_name, "vkCmdBeginRenderingKHR"))
    {
      return PFN_vkVoidFunction(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginRendering);
    }

    if (!strcmp(func_name, "vkCmdEndRenderingKHR"))
    {
      return PFN_vkVoidFunction(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndRendering);
    }

    return etna::get_context().getLoader().getProcAddress<PFN_vkVoidFunction>(func_name);
  }

  vk::UniqueDescriptorPool imguiPool;
};

#endif