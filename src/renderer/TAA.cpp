#include "TAA.hpp"

#include <etna/GlobalContext.hpp>

namespace renderer
{

TAA::TAA(const std::string &prog_name)
{
  etna::ComputePipeline::CreateInfo info {};
  pipeline = etna::get_context().getPipelineManager().createComputePipeline(prog_name, info);

  vk::SamplerCreateInfo sinfo {
    .magFilter = vk::Filter::eLinear,
    .minFilter = vk::Filter::eLinear,
    .mipmapMode = vk::SamplerMipmapMode::eNearest,
    .addressModeU = vk::SamplerAddressMode::eClampToEdge,
    .addressModeV = vk::SamplerAddressMode::eClampToEdge,
    .addressModeW = vk::SamplerAddressMode::eRepeat,
    .maxLod = VK_LOD_CLAMP_NONE
  };

  sampler = etna::get_context().getDevice().createSamplerUnique(sinfo).value;
}

struct TAAPushConsts
{
  glm::vec4 nop;
};

void TAA::dispatch(etna::SyncCommandBuffer &cmd,
  const scene::RenderTargetState &rts, 
  const scene::GlobalFrameConstantHandler &g_frame,
  bool invalidate)
{
  cmd.bindPipeline(pipeline);
  auto pipelineInfo = etna::get_shader_program(pipeline.getShaderProgram());

  etna::Image::ViewParams depthView {};
  depthView.aspect = vk::ImageAspectFlagBits::eDepth;

  auto color = rts.getColor()
    .genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal, {});
  auto depth = rts.getDepth()
    .genBinding(sampler.get(), vk::ImageLayout::eDepthStencilReadOnlyOptimal, depthView);
  auto velocity = rts.getVelocity()
    .genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal, {});
  
  auto historyColor = (invalidate? rts.getColor() : rts.getColorHistory())
    .genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal, {});
  auto historyDepth = (invalidate? rts.getDepth() : rts.getDepthHistory())
    .genBinding(sampler.get(), vk::ImageLayout::eDepthStencilReadOnlyOptimal, depthView); 
  
  auto output = rts.getTAATarget()
    .genBinding(nullptr, vk::ImageLayout::eGeneral, {}); 

  auto set = etna::create_descriptor_set(pipelineInfo.getDescriptorLayoutId(0), {
    etna::Binding {0, color},
    etna::Binding {1, depth},
    etna::Binding {2, velocity},
    etna::Binding {3, historyColor},
    etna::Binding {4, historyDepth},
    etna::Binding {5, output}
  });

  cmd.bindDescriptorSet(vk::PipelineBindPoint::eCompute, pipelineInfo.getPipelineLayout(), 0, {set});
  
  auto res = rts.getTAATarget().getExtent2D();

  cmd.dispatch((res.width + 7)/8, (res.height + 3)/4, 1);

  // copy TAA target to current target
  auto srcRes = rts.getTAATarget().getExtent2D();
  auto dstRes = rts.getColor().getExtent2D();

  vk::ImageBlit region {
    .srcSubresource {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
    .dstSubresource {vk::ImageAspectFlagBits::eColor, 0, 0, 1}
  };

  region.srcOffsets[0] = vk::Offset3D{0, 0, 0};
  region.srcOffsets[1] = vk::Offset3D{int32_t(srcRes.width), int32_t(srcRes.height), 1};
  region.dstOffsets[0] = vk::Offset3D{0, 0, 0};
  region.dstOffsets[1] = vk::Offset3D{int32_t(dstRes.width), int32_t(dstRes.height), 1};

  cmd.blitImage(
    rts.getTAATarget(),
    vk::ImageLayout::eTransferSrcOptimal,
    rts.getColor(),
    vk::ImageLayout::eTransferDstOptimal,
    {region},
    vk::Filter::eLinear);
}



} // namespace renderer