#ifndef RENDERER_TAA_INCLUDED
#define RENDERER_TAA_INCLUDED

#include "scene/SceneRenderer.hpp"

namespace renderer
{

struct TAA
{
  TAA(const std::string &prog_name);

  void dispatch(etna::SyncCommandBuffer &cmd,
    const scene::RenderTargetState &rts, 
    const scene::GlobalFrameConstantHandler &g_frame,
    bool invalidate = false);

private:
  etna::ComputePipeline pipeline;
  vk::UniqueSampler sampler;
};


} // namespace renderer

#endif