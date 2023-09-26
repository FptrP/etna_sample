#ifndef RENDERER_IRENDERER_HPP_INCLUDED
#define RENDERER_IRENDERER_HPP_INCLUDED

#include <scene/GLTFScene.hpp>

namespace renderer
{

struct RenderConfig
{
  float resolutionScale = 1.f;
};

//todo: make camera scene's node
//add names to nodes, make scene updatable
//Renderer reads only scene

struct IRenderer
{
  virtual void updateParams(const RenderConfig &new_params) = 0;
  virtual void onResolutionChanged(uint32_t new_width, uint32_t new_height) = 0; 
  
  virtual void attachToScene(scene::GLTFScene &scene) = 0;
  virtual void detachScene() = 0;

  virtual void render(etna::SyncCommandBuffer &cmd, const etna::Image &backbuffer) = 0;
};




} // namespace renderer


#endif