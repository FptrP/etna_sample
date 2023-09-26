#ifndef SCENE_RENDERER_HPP_INCLUDED
#define SCENE_RENDERER_HPP_INCLUDED

#include <etna/Etna.hpp>

#include "GLTFScene.hpp"

namespace scene
{


struct GlobalFrameConstants
{
  glm::mat4 view; // TODO: inverseView
  glm::mat4 projection;
  glm::mat4 viewProjection;
  glm::vec4 projectionParams; //tg(fovy/2), aspect, znear, zfar
  glm::vec4 sunDirection; // camera space
  glm::vec4 sunColor;
  glm::vec4 viewport; //width, height, .. ...
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

private:
  GlobalFrameConstants params;

  uint32_t numFrames;
  uint32_t frameIndex = 0;

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

struct MaterialPushConstants
{
  glm::mat4 MVP;
  glm::mat4 normalsTransform;
  glm::vec4 baseColorFactor;
  float metallic;
  float rougness;
  float alphaCutoff;
  uint32_t renderFlags;
};

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