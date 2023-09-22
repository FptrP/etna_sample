#ifndef SCENE_ABUFFER_RENDERER_HPP_INCLUDED
#define SCENE_ABUFFER_RENDERER_HPP_INCLUDED

#include "SceneRenderer.hpp"

namespace scene
{

constexpr uint32_t ABUFFER_LIST_END = 0xffffffff;

struct FragmentEntry
{
  float depth;
  uint32_t color;
  uint32_t link;
};

struct ABufferResolver
{
  ABufferResolver(const std::string &prog_name, glm::uvec2 resolution);

  void onResolutionChanged(glm::uvec2 resolution);
  
  void dispatch(
    etna::SyncCommandBuffer &cmd,
    const GlobalFrameConstantHandler &gframe, 
    const etna::Image &listHead, 
    const etna::Buffer &listSamples);

  const etna::Image &getTarget() const { return resolveTarget; }

private:
  etna::ComputePipeline pipeline;
  etna::Image resolveTarget;
};

struct ABufferRenderer
{
  ABufferRenderer(const std::string &prog_name, const etna::Image &depthRT);

  void attachToScene(const GLTFScene &scene);

  void render(etna::SyncCommandBuffer &cmd,
    const etna::Image &depthRT, 
    const GlobalFrameConstantHandler &gframe,
    const GLTFScene &scene);

  void onResolutionChanged(uint32_t w, uint32_t h);

  const etna::Image &getListHead() const { return listHead; }
  const etna::Buffer &getListBuffer() const { return fragmentList; }

private:

  uint32_t bindDS(etna::SyncCommandBuffer &cmd, 
    const GlobalFrameConstantHandler &gframe,
    const GLTFScene::Material &material, 
    const GLTFScene &scene);

  etna::GraphicsPipeline pipeline;

  etna::Image listHead;
  etna::Buffer fragmentList;
  SortedScene sceneData;
};

struct TexBlender
{
  TexBlender(const std::string &name, vk::Format dstFmt);
  void blend(etna::SyncCommandBuffer &cmd, const etna::Image &overlay, const etna::Image &dst);

private:
  etna::GraphicsPipeline pipeline;
  vk::UniqueSampler sampler;
};

}

#endif