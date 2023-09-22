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

#include "scene/Camera.hpp"
#include "scene/GLTFScene.hpp"
#include "scene/SceneRenderer.hpp"
#include "scene/ABufferRenderer.hpp"
#include "app.hpp"


static void update_camera(GLFWwindow *window, Camera &camera, float dt)
{
  static std::optional<double> cursorX = std::nullopt;
  static std::optional<double> cursorY = std::nullopt;
  static bool isCursorCaptured = false;


  glm::vec3 movement{0.f, 0.f, 0.f};
  if (glfwGetKey(window, GLFW_KEY_W))
    movement.z += 0.35;
  if (glfwGetKey(window, GLFW_KEY_S))
    movement.z -= 0.35;
  if (glfwGetKey(window, GLFW_KEY_D))
    movement.x += 0.35;
  if (glfwGetKey(window, GLFW_KEY_A))
    movement.x -= 0.35;

  if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS)
    isCursorCaptured = !isCursorCaptured;

  camera.move(movement * dt);

  double nX, nY;
  glfwGetCursorPos(window, &nX, &nY);

  if (isCursorCaptured)
  {
    if (cursorX.has_value() && cursorY.has_value())
      camera.rotate(0.1 * (nX - *cursorX), -0.1 * (nY - *cursorY));
  }

  cursorX = nX;
  cursorY = nY;
}

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

    opaqueRenderer = std::make_unique<scene::SceneRenderer>("gltf_opaque_forward", rtInfo);
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
    gFrameConsts.makeProjection(glm::radians(90.f), float(new_width)/new_height, 0.01f, 1000.f);
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
    
    etna::RenderingAttachment colorAttachment {
      .view = backbuffer.getView({}),
      .layout = vk::ImageLayout::eColorAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear
    };
    
    etna::RenderingAttachment depthAttachment {
      .view = depthRT.getView({}),
      .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .clearValue = vk::ClearDepthStencilValue{.depth = 1.f}
    };
    
    vk::Viewport viewport {
      .width = (float)resolution.width, 
      .height = (float)resolution.height, 
      .minDepth = 0.f, 
      .maxDepth = 1.f
    };

    vk::Rect2D renderArea {
      {0, 0},
      {resolution.width, resolution.height}
    };

    cmd.begin();
    cmd.beginRendering(renderArea, {colorAttachment}, depthAttachment);
    cmd.setViewport(0, {viewport});
    cmd.setScissor(0, renderArea);
    cmd.bindVertexBuffer(0, scene->getVertexBuff(), 0);
    cmd.bindIndexBuffer(scene->getIndexBuff(), 0, vk::IndexType::eUint32);
    
    opaqueRenderer->render(cmd, gFrameConsts, *scene);
    
    cmd.endRendering();

    abufferRenderer->render(cmd, depthRT, gFrameConsts, *scene);
    abufferResolver->dispatch(cmd, gFrameConsts, abufferRenderer->getListHead(), abufferRenderer->getListBuffer());

    texBlender->blend(cmd, abufferResolver->getTarget(), backbuffer);

    cmd.transformLayout(backbuffer, vk::ImageLayout::ePresentSrcKHR, {
      vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

    cmd.end();
    gFrameConsts.onEndFrame();
  }

  void update(float dt) override
  {
    update_camera(getWindow(), camera, dt);
    gFrameConsts.setViewMatrix(camera.getViewMat());
    
    gFrameConsts.setSunColor(glm::vec3 {5.f, 5.f, 5.f});
    gFrameConsts.setSunDirection(glm::vec3 {2.f, 1.f, 0.1f});
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
};

int main(int argc, char **argv)
{
  EtnaSampleApp etnaApp {1024, 768};
  //etnaApp.loadScene("assets/ABeautifulGame/ABeautifulGame.gltf");
  etnaApp.loadScene("assets/FlightHelmet/FlightHelmet.gltf");
  etnaApp.mainLoop();
  return 0;
}