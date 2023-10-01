#ifndef SCENE_RENDERER_HPP_INCLUDED
#define SCENE_RENDERER_HPP_INCLUDED

#include <etna/Etna.hpp>
#include <array>
#include "GLTFScene.hpp"

namespace scene
{


struct GlobalFrameConstants
{
  glm::mat4 view; // TODO: inverseView
  glm::mat4 projection;
  glm::mat4 viewProjection;
  glm::mat4 prevViewProjection;
  glm::vec4 projectionParams; //tg(fovy/2), aspect, znear, zfar
  glm::vec4 sunDirection; // camera space
  glm::vec4 sunColor;
  glm::vec4 viewport; //width, height, .. ...
  glm::vec4 jitter; //xy - current, zw - previous
};

struct GlobalFrameConstantHandler
{
  GlobalFrameConstantHandler();
  
  void setViewMatrix(const glm::mat4 &view);
  void makeProjection(float fovy, float aspect, float znear, float zfar);
  void setViewport(uint32_t width, uint32_t height);
  void setSunColor(glm::vec3 color);
  void setSunDirection(glm::vec3 dir);
  
  void updateFov(float fovy);
  void updateAspect(float aspect);
  
  void onBeginFrame(); //write data to const buffer
  void onEndFrame(); //next index;

  void setJitterState(bool enable)
  {
    if (enable && !enableJitter)
      invalidateHistory = true;
    enableJitter = enable;
  }

  const GlobalFrameConstants &getParams() const { return params; }

  const etna::Buffer &getUBO() const {
    return constantUbo;
  }

  uint32_t getUBOoffset() const {
    return frameIndex * paramsGpuSize;
  }
  
  void reset() { constantUbo.reset(); }

  etna::BufferBinding getBinding() const
  {
    return constantUbo.genBinding(getUBOoffset(), paramsGpuSize);
  }

  bool getInvalidateHistory() const { return invalidateHistory; }

private:
  GlobalFrameConstants params;

  uint32_t numFrames;
  uint32_t frameIndex = 0;

  static constexpr uint32_t JITTER_COUNT = 8u;
  uint32_t jitterIndex = 0;
  bool enableJitter = true;

  bool invalidateHistory = true;

  uint32_t paramsGpuSize;

  etna::Buffer constantUbo; 
};

enum class RenderFlags : uint32_t
{
  NoBaseColorTex = 1,
  NoMetallicRougnessTex = 2
};

struct RenderTargetInfo
{
  std::vector<vk::Format> colorRT;
  vk::Format depthRT;
};

struct RenderTargetState
{
  RenderTargetState(uint32_t w, uint32_t h);

  void onResolutionChanged(uint32_t new_w, uint32_t new_h);

  const etna::Image &getDepth() const { return depth[historyIndex]; }
  const etna::Image &getColor() const { return currentColor; }
  const etna::Image &getVelocity() const { return velocity; }
  const etna::Image &getTAATarget() const {return color[historyIndex]; }

  const etna::Image &getDepthHistory() const { return depth[(historyIndex + 1) % 2]; }
  const etna::Image &getColorHistory() const { return color[(historyIndex + 1) % 2]; }

  vk::Format getDepthFmt() const { return depth[historyIndex].getInfo().format; }
  vk::Format getColorFmt() const { return color[historyIndex].getInfo().format; }
  vk::Format getVelocityFmt() const { return velocity.getInfo().format; }

  static constexpr vk::Format baseColorFmt = vk::Format::eR16G16B16A16Sfloat;

  void nextFrame() { historyIndex = (historyIndex + 1) % 2; }
  
private:
  uint32_t historyIndex = 0;
  std::array<etna::Image, 2> depth;
  std::array<etna::Image, 2> color;
  etna::Image velocity;
  etna::Image currentColor;
};

struct MaterialPushConstants
{
  glm::mat4 MVP;
  glm::mat4 prevMVP;
  glm::mat4 normalsTransform;
  glm::vec4 baseColorFactor;
  float metallic;
  float rougness;
  float alphaCutoff;
  uint32_t renderFlags;
};

static_assert(sizeof(MaterialPushConstants) <= 256);

//base layout

struct SceneRenderer
{
  SceneRenderer(const std::string &prog_name, 
    const std::string &depth_prog_name,
    const RenderTargetInfo &rtInfo);

  void attachToScene(const GLTFScene &scene);
  
  void depthPrepass(etna::SyncCommandBuffer &cmd, 
    const GlobalFrameConstantHandler &gframe, const GLTFScene &scene);

  void render(etna::SyncCommandBuffer &cmd, 
    const GlobalFrameConstantHandler &gframe, const GLTFScene &scene);

private:

  uint32_t bindDS(etna::SyncCommandBuffer &cmd, 
    const GlobalFrameConstantHandler &gframe,
    const GLTFScene::Material &material, 
    const GLTFScene &scene);

  etna::ShaderProgramId program;
  etna::GraphicsPipeline depthPipeline;
  etna::GraphicsPipeline pipeline;
  SortedScene sceneData;
};



} // namespace scene

#endif