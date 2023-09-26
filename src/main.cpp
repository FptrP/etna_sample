#include <iostream>

#include <etna/Etna.hpp>
#include <etna/EtnaConfig.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/SubmitContext.hpp>
#include <etna/SyncCommandBuffer.hpp>
#include <etna/Sampler.hpp>
#include <etna/RenderTargetStates.hpp>

#include <span>
#include <optional>
#include <ranges>

#include "scene/Camera.hpp"
#include "scene/GLTFScene.hpp"
#include "scene/SceneRenderer.hpp"
#include "scene/ABufferRenderer.hpp"
#include "app.hpp"
#include "events/events.hpp"
#include "CameraEvents.hpp"

struct GlobalParamsSystem
{
  GlobalParamsSystem() 
    : cb {event::make_handle()}
  {
    ETNA_ASSERT(std::type_index(typeid(GuiRenderEvent)) != std::type_index(typeid(MouseMoveEvent)));
    cb.addCallback<GuiRenderEvent>(std::bind_front(&GlobalParamsSystem::renderUI, this));
  }

  void renderUI(const GuiRenderEvent &)
  {
    ImGui::BeginChild("Global params");
    ImGui::SliderFloat3("Sun radiance", sunRadiance.data(), 0.f, 5.f);
    ImGui::SliderFloat3("Sun direction", sunDir.data(), -1.f, 1.f);
    ImGui::SliderFloat("FOV", &fov, 60.f, 120.f);
    ImGui::EndChild();
  }

  void updateParams(scene::GlobalFrameConstantHandler &handler)
  {
    handler.setSunColor({sunRadiance[0], sunRadiance[1], sunRadiance[2]});
    handler.setSunDirection({sunDir[0], sunDir[1], sunDir[2]});
    handler.updateFov(glm::radians(fov));
  }

private:
  event::CallbackHandle cb;

  std::array<float, 3> sunDir {0.f, 1.f, 0.f};
  std::array<float, 3> sunRadiance {3.f, 3.f, 3.f};
  float fov = 90.f;
};

struct EtnaSampleApp : AppInit 
{

  EtnaSampleApp(uint32_t w, uint32_t h) 
    : AppInit{w, h}, camera{{0.f, 0.5f, 0.f}, {0.f, 0.f, 1.f}}
  {
    depthRT = etna::get_context().createImage(
      etna::ImageCreateInfo::depthRT(w, h, vk::Format::eD24UnormS8Uint));
    
    gFrameConsts.makeProjection(glm::radians(90.f), float(w)/h, 0.01f, 1000.f);
    gFrameConsts.setViewport(w, h);
  }

  void loadScene(const std::string &path)
  {
    scene::RenderTargetInfo rtInfo {
      {getSubmitCtx().getSwapchainFmt()},
      depthRT.getInfo().format
    };
    
    etna::create_program("gltf_opaque_forward", {
      "shaders/gltf_opaque_forward/shader.vert.spv",
      "shaders/gltf_opaque_forward/shader.frag.spv"
    });

    etna::create_program("gltf_depth_prepass", {
      "shaders/depth_prepass/shader.vert.spv",
    });

    etna::create_program("abuffer_render", {
      "shaders/abuffer_render/shader.vert.spv",
      "shaders/abuffer_render/shader.frag.spv"
    });

    etna::create_program("abuffer_resolve", {
      "shaders/abuffer_resolve/shader.comp.spv"
    });

    etna::create_program("fullscreen_blend", {
      "shaders/fullscreen_blend/shader.vert.spv",
      "shaders/fullscreen_blend/shader.frag.spv"
    });
    
    glm::uvec2 resolution {depthRT.getInfo().extent.width, depthRT.getInfo().extent.height};

    opaqueRenderer = std::make_unique<scene::SceneRenderer>("gltf_opaque_forward", "gltf_depth_prepass", rtInfo);
    abufferRenderer = std::make_unique<scene::ABufferRenderer>("abuffer_render", depthRT);
    abufferResolver = std::make_unique<scene::ABufferResolver>("abuffer_resolve", resolution);

    utilCmd.emplace(getSubmitCtx().getCommandPool());

    scene = scene::load_scene(path, *utilCmd);
    opaqueRenderer->attachToScene(*scene);
    abufferRenderer->attachToScene(*scene);
    
    texBlender = std::make_unique<scene::TexBlender>("fullscreen_blend", rtInfo.colorRT[0]);
  }

  void onResolutionChanged(uint32_t new_width, uint32_t new_height) override
  {
    gFrameConsts.updateAspect(float(new_width)/new_height);
    gFrameConsts.setViewport(new_width, new_height);
    depthRT = etna::get_context().createImage(
      etna::ImageCreateInfo::depthRT(new_width, new_height, vk::Format::eD24UnormS8Uint)  
    );

    abufferRenderer->onResolutionChanged(new_width, new_height);
    abufferResolver->onResolutionChanged({new_width, new_height});
  }
  
  void recordRenderCmd(etna::SyncCommandBuffer &cmd, const etna::Image &backbuffer) override
  {
    gFrameConsts.onBeginFrame();
    auto resolution = backbuffer.getInfo().extent;
    
    vk::Rect2D renderArea {
      {0, 0},
      {resolution.width, resolution.height}
    };

    { // depth prepass
      etna::RenderingAttachment depthAttachment {
        .view = depthRT.getView({}),
        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .clearValue = vk::ClearDepthStencilValue{.depth = 1.f}
      };

      etna::RenderTargetState rts{cmd, renderArea.extent, {}, depthAttachment};
      cmd.bindVertexBuffer(0, scene->getVertexBuff(), 0);
      cmd.bindIndexBuffer(scene->getIndexBuff(), 0, vk::IndexType::eUint32);
      opaqueRenderer->depthPrepass(cmd, gFrameConsts, *scene);
    }

    { // color pass
      etna::RenderingAttachment colorAttachment {
        .view = backbuffer.getView({}),
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear
      };
    
      etna::RenderingAttachment depthAttachment {
        .view = depthRT.getView({}),
        .layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad
      };

      etna::RenderTargetState rts{cmd, renderArea.extent, {colorAttachment}, depthAttachment};
      cmd.bindVertexBuffer(0, scene->getVertexBuff(), 0);
      cmd.bindIndexBuffer(scene->getIndexBuff(), 0, vk::IndexType::eUint32);
      opaqueRenderer->render(cmd, gFrameConsts, *scene);
    }
    
    abufferRenderer->render(cmd, depthRT, gFrameConsts, *scene);
    abufferResolver->dispatch(cmd, gFrameConsts, abufferRenderer->getListHead(), abufferRenderer->getListBuffer());

    texBlender->blend(cmd, abufferResolver->getTarget(), backbuffer);

    drawImGui(cmd, backbuffer);

    cmd.transformLayout(backbuffer, vk::ImageLayout::ePresentSrcKHR, {
      vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

    gFrameConsts.onEndFrame();
  }

  void update(float dt) override
  {
    event::process_events();
    cameraUpdater.update(camera, dt);
    gFrameConsts.setViewMatrix(camera.getViewMat());
    gFrameConstsUpdater.updateParams(gFrameConsts);
  }

private:
  scene::GlobalFrameConstantHandler gFrameConsts;
  etna::Image depthRT;

  std::optional<etna::SyncCommandBuffer> utilCmd;

  std::unique_ptr<scene::GLTFScene> scene;
  std::unique_ptr<scene::SceneRenderer> opaqueRenderer;
  std::unique_ptr<scene::ABufferRenderer> abufferRenderer;
  std::unique_ptr<scene::ABufferResolver> abufferResolver;
  std::unique_ptr<scene::TexBlender> texBlender;
  
  Camera camera;
  CameraSystem cameraUpdater {1.0f, 0.3f};
  GlobalParamsSystem gFrameConstsUpdater;  
};

int main(int argc, char **argv)
{
  EtnaSampleApp etnaApp {1920, 1080};
  etnaApp.loadScene("assets/FlightHelmet/FlightHelmet.gltf");
  etnaApp.mainLoop();
  return 0;
}