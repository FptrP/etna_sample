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
#include "renderer/TAA.hpp"

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
    ImGui::Checkbox("Enable jitter", &enableJitter);
    ImGui::EndChild();
  }

  void updateParams(scene::GlobalFrameConstantHandler &handler)
  {
    handler.setSunColor({sunRadiance[0], sunRadiance[1], sunRadiance[2]});
    handler.setSunDirection({sunDir[0], sunDir[1], sunDir[2]});
    handler.setJitterState(enableJitter);

    if (std::abs(prevFov - fov) > 1e-3)
    {
      handler.updateFov(glm::radians(fov));
      fov = prevFov; 
    }
  }

private:
  event::CallbackHandle cb;

  std::array<float, 3> sunDir {0.f, 1.f, 0.f};
  std::array<float, 3> sunRadiance {3.f, 3.f, 3.f};
  float fov = 90.f;
  float prevFov = -1.f;
  bool enableJitter = true;
};

struct EtnaSampleApp : AppInit 
{

  EtnaSampleApp(uint32_t w, uint32_t h, float render_scale = 1.f) 
    : AppInit{w, h}, camera{{0.f, 0.5f, 0.f}, {0.f, 0.f, 1.f}}, renderScale {render_scale}
  {
    auto res = getRenderResolution(w, h);
    rts = std::make_unique<scene::RenderTargetState>(res.x, res.y);

    gFrameConsts.makeProjection(glm::radians(90.f), float(w)/h, 0.01f, 1000.f);
    gFrameConsts.setViewport(res.x, res.y);
  }

  glm::uvec2 getRenderResolution(uint32_t w, uint32_t h) const
  {
    return {uint32_t(w * renderScale), uint32_t(h * renderScale)};
  }

  void loadScene(const std::string &path)
  {
    scene::RenderTargetInfo rtInfo {
      {rts->getColorFmt(), rts->getVelocityFmt()},
      rts->getDepthFmt()
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
    
    etna::create_program("taa", {
      "shaders/TAA/shader.comp.spv",
    });

    auto srcRes = rts->getColor().getExtent2D();

    glm::uvec2 resolution {srcRes.width, srcRes.height};

    opaqueRenderer = std::make_unique<scene::SceneRenderer>("gltf_opaque_forward", "gltf_depth_prepass", rtInfo);
    abufferRenderer = std::make_unique<scene::ABufferRenderer>("abuffer_render", rts->getDepth());
    abufferResolver = std::make_unique<scene::ABufferResolver>("abuffer_resolve", resolution);

    utilCmd.emplace(getSubmitCtx().getCommandPool());

    scene = scene::load_scene(path, *utilCmd);
    opaqueRenderer->attachToScene(*scene);
    abufferRenderer->attachToScene(*scene);
    
    texBlender = std::make_unique<scene::TexBlender>("fullscreen_blend", rtInfo.colorRT[0]);
    taaPass = std::make_unique<renderer::TAA>("taa");
  }

  void onResolutionChanged(uint32_t new_width, uint32_t new_height) override
  {
    auto res = getRenderResolution(new_width, new_height);

    gFrameConsts.updateAspect(float(res.x)/res.y);
    gFrameConsts.setViewport(res.x, res.y);

    rts->onResolutionChanged(res.x, res.y);
    abufferRenderer->onResolutionChanged(res.x, res.y);
    abufferResolver->onResolutionChanged({res.x, res.y});
  }
  
  void recordRenderCmd(etna::SyncCommandBuffer &cmd, const etna::Image &backbuffer) override
  {
    gFrameConsts.onBeginFrame();
    rts->nextFrame(); // swap history  
    auto resolution = rts->getColor().getExtent2D();
    
    vk::Rect2D renderArea {
      {0, 0},
      {resolution.width, resolution.height}
    };

    { // depth prepass
      etna::RenderingAttachment depthAttachment {
        .view = rts->getDepth().getView({}),
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
        .view = rts->getColor().getView({}),
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear
      };

      etna::RenderingAttachment velocityAttachment {
        .view = rts->getVelocity().getView({}),
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .clearValue = vk::ClearColorValue {0.f, 0.f, 0.f, 0.f}
      };

      etna::RenderingAttachment depthAttachment {
        .view = rts->getDepth().getView({}),
        .layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad
      };

      etna::RenderTargetState rts{cmd, renderArea.extent, 
        {colorAttachment, velocityAttachment}, depthAttachment};

      cmd.bindVertexBuffer(0, scene->getVertexBuff(), 0);
      cmd.bindIndexBuffer(scene->getIndexBuff(), 0, vk::IndexType::eUint32);
      opaqueRenderer->render(cmd, gFrameConsts, *scene);
    }
    
    { //apply taa 
      taaPass->dispatch(cmd, *rts, gFrameConsts, gFrameConsts.getInvalidateHistory());
    }

    abufferRenderer->render(cmd, rts->getDepth(), gFrameConsts, *scene);
    abufferResolver->dispatch(cmd, gFrameConsts, abufferRenderer->getListHead(), abufferRenderer->getListBuffer());

    texBlender->blend(cmd, abufferResolver->getTarget(), rts->getColor());

    {
      //blit to backbuffer
      auto srcRes = rts->getColor().getExtent2D();
      auto dstRes = backbuffer.getExtent2D();

      vk::ImageBlit region {
        .srcSubresource {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstSubresource {vk::ImageAspectFlagBits::eColor, 0, 0, 1}
      };

      region.srcOffsets[0] = vk::Offset3D{0, 0, 0};
      region.srcOffsets[1] = vk::Offset3D{int32_t(srcRes.width), int32_t(srcRes.height), 1};
      region.dstOffsets[0] = vk::Offset3D{0, 0, 0};
      region.dstOffsets[1] = vk::Offset3D{int32_t(dstRes.width), int32_t(dstRes.height), 1};

      cmd.blitImage(rts->getColor(), 
        vk::ImageLayout::eTransferSrcOptimal,
        backbuffer,
        vk::ImageLayout::eTransferDstOptimal,
        {region}, 
        vk::Filter::eLinear);
    }

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
  const float renderScale = 1.f;

  scene::GlobalFrameConstantHandler gFrameConsts;

  std::unique_ptr<scene::RenderTargetState> rts;

  std::optional<etna::SyncCommandBuffer> utilCmd;

  std::unique_ptr<scene::GLTFScene> scene;
  std::unique_ptr<scene::SceneRenderer> opaqueRenderer;
  std::unique_ptr<scene::ABufferRenderer> abufferRenderer;
  std::unique_ptr<scene::ABufferResolver> abufferResolver;
  std::unique_ptr<scene::TexBlender> texBlender;
  std::unique_ptr<renderer::TAA> taaPass;

  Camera camera;
  CameraSystem cameraUpdater {1.0f, 0.3f};
  GlobalParamsSystem gFrameConstsUpdater;  
};

int main(int argc, char **argv)
{
  EtnaSampleApp etnaApp {1920, 1080, 0.7};
  //etnaApp.loadScene("assets/FlightHelmet/FlightHelmet.gltf");
  etnaApp.loadScene("assets/ABeautifulGame/ABeautifulGame_transperent.gltf");
  etnaApp.mainLoop();
  return 0;
}