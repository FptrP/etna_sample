#include "ABufferRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/RenderTargetStates.hpp>

namespace scene
{

constexpr uint32_t LIST_MULT = 4;

ABufferResolver::ABufferResolver(const std::string &prog_name, glm::uvec2 resolution)
{
  pipeline = etna::get_context().getPipelineManager().createComputePipeline(prog_name, {});
  onResolutionChanged(resolution);
}

void ABufferResolver::onResolutionChanged(glm::uvec2 resolution)
{
  etna::ImageCreateInfo info {
    .format = vk::Format::eR8G8B8A8Unorm,
    .extent {resolution.x, resolution.y, 1},
    .imageUsage = vk::ImageUsageFlagBits::eStorage
      |vk::ImageUsageFlagBits::eTransferDst
      |vk::ImageUsageFlagBits::eSampled
  };
  resolveTarget = etna::get_context().createImage(std::move(info));
}

void ABufferResolver::dispatch(
  etna::SyncCommandBuffer &cmd,
  const GlobalFrameConstantHandler &gframe, 
  const etna::Image &listHead, 
  const etna::Buffer &listSamples)
{
  auto pipelineInfo = etna::get_shader_program(pipeline.getShaderProgram());

  auto listHeadBinding = listHead.genBinding({}, vk::ImageLayout::eGeneral, listHead.fullRangeView());
  auto listSamplesBinding = listSamples.genBinding();
  auto outputBinding = resolveTarget.genBinding({}, vk::ImageLayout::eGeneral, resolveTarget.fullRangeView());

  std::vector<etna::Binding> bindings {
    {0, gframe.getBinding()},
    {1, listHeadBinding},
    {2, listSamplesBinding},
    {3, outputBinding}
  };

  auto set = etna::create_descriptor_set(pipelineInfo.getDescriptorLayoutId(0), bindings);
  cmd.bindPipeline(pipeline);
  cmd.bindDescriptorSet(vk::PipelineBindPoint::eCompute, pipelineInfo.getPipelineLayout(), 0, set);
  
  uint32_t w = resolveTarget.getInfo().extent.width;
  uint32_t h = resolveTarget.getInfo().extent.height;

  cmd.dispatch((w + 7u)/8, (h + 3u)/4u, 1u);
}


ABufferRenderer::ABufferRenderer(const std::string &prog_name, const etna::Image &depthRT)
{
  etna::GraphicsPipeline::CreateInfo info {};

  info.vertexShaderInput = scene::Vertex::getDesc();
  info.fragmentShaderOutput.colorAttachmentFormats.clear();
  info.fragmentShaderOutput.depthAttachmentFormat = depthRT.getInfo().format;

  info.blendingConfig.attachments.clear();

  info.depthConfig.depthWriteEnable = VK_FALSE;
  info.depthConfig.depthCompareOp = vk::CompareOp::eLess;
  
  pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(prog_name, info);

  onResolutionChanged(depthRT.getInfo().extent.width, depthRT.getInfo().extent.height);
}

void ABufferRenderer::attachToScene(const GLTFScene &scene)
{
  sceneData = scene.queryDrawCalls([](const GLTFScene::Material &material) {
    return material.mode == GLTFScene::MaterialMode::Blend;
  }); 
}

uint32_t ABufferRenderer::bindDS(
  etna::SyncCommandBuffer &cmd, 
  const GlobalFrameConstantHandler &gframe,
  const GLTFScene::Material &material, 
  const GLTFScene &scene)
{
  uint32_t renderFlags = 0;
  
  auto [baseColorTex, baseColorSampler] = scene.getImageSampler(material.baseColorId);
  auto [mrTex, mrSampler] = scene.getImageSampler(material.metallicRoughnessId);

  if (!material.baseColorId.has_value())
    renderFlags |= uint32_t(RenderFlags::NoBaseColorTex);

  if (!material.metallicRoughnessId.has_value())
    renderFlags |= uint32_t(RenderFlags::NoMetallicRougnessTex);

  auto baseColorBinding = baseColorTex->genBinding(
      baseColorSampler, vk::ImageLayout::eShaderReadOnlyOptimal, baseColorTex->fullRangeView());

  auto mrBinding = mrTex->genBinding(
    mrSampler, vk::ImageLayout::eShaderReadOnlyOptimal, mrTex->fullRangeView());

  auto listHeadBinding = listHead.genBinding({}, vk::ImageLayout::eGeneral, listHead.fullRangeView());
  auto fragmentsBinding = fragmentList.genBinding();
  
  std::vector<etna::Binding> bindings {
    etna::Binding {0, gframe.getBinding()},
    etna::Binding {1, baseColorBinding},
    etna::Binding {2, mrBinding},
    etna::Binding {3, listHeadBinding},
    etna::Binding {4, fragmentsBinding}
  };

  const auto &info = etna::get_shader_program(pipeline.getShaderProgram()); 
  auto set = etna::create_descriptor_set(info.getDescriptorLayoutId(0), bindings);
  cmd.bindDescriptorSet(vk::PipelineBindPoint::eGraphics, info.getPipelineLayout(), 0, set);

  return renderFlags;
}

void ABufferRenderer::render(etna::SyncCommandBuffer &cmd,
  const etna::Image &depthRT, 
  const GlobalFrameConstantHandler &gframe,
  const GLTFScene &scene)
{
  vk::ClearColorValue clearVal {};
  clearVal.setUint32({ABUFFER_LIST_END, ABUFFER_LIST_END, ABUFFER_LIST_END, ABUFFER_LIST_END});
  cmd.clearColorImage(listHead, vk::ImageLayout::eGeneral, clearVal, {
    vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
  });

  cmd.fillBuffer(fragmentList, 0, sizeof(uint32_t), 0u);

  if (!sceneData.materialGropus.size())
    return;

  vk::Extent2D extent {
    depthRT.getInfo().extent.width, 
    depthRT.getInfo().extent.height
  };

  etna::RenderingAttachment depthAttachment {
    .view = depthRT.getView({}),
    .layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
    .loadOp = vk::AttachmentLoadOp::eLoad
  };
  
  etna::RenderTargetState rts{cmd, extent, {}, depthAttachment};
  cmd.bindVertexBuffer(0, scene.getVertexBuff(), 0);
  cmd.bindIndexBuffer(scene.getIndexBuff(), 0, vk::IndexType::eUint32);
  cmd.bindPipeline(pipeline);
  auto progInfo = etna::get_shader_program(pipeline.getShaderProgram());
  
  for (auto &group : sceneData.materialGropus)
  {
    auto &material = scene.getMaterial(group.materialIndex);
    auto renderFlags = bindDS(cmd, gframe, material, scene);
    
    for (auto &dc : group.drawCalls)
    {
      for (auto &tId : dc.transformIds)
      {
        auto &transform = scene.getTransform(tId);
        
        //auto normalMat = transform.normalTransform * glm::transpose(glm::inverse(gframe.getParams().view));
        auto normalMat = glm::transpose(glm::inverse(gframe.getParams().view * transform.modelTransform));

        MaterialPushConstants mpc
        {
          .MVP = gframe.getParams().viewProjection * transform.modelTransform,
          .normalsTransform = normalMat,
          .baseColorFactor = material.baseColorFactor,
          .metallic = material.metallicFactor,
          .rougness = material.roughnessFactor,
          .alphaCutoff = material.alphaCutoff,
          .renderFlags = renderFlags
        };

        cmd.pushConstants(pipeline.getShaderProgram(), 0, mpc);
        cmd.drawIndexed(dc.indexCount, 1, dc.firstIndex, dc.vertexOffset, 0);
      }
    }
  }
}

void ABufferRenderer::onResolutionChanged(uint32_t w, uint32_t h)
{
  etna::ImageCreateInfo listHeadInfo {
    .format = vk::Format::eR32Uint,
    .extent {w, h, 1},
    .imageUsage = vk::ImageUsageFlagBits::eStorage
      |vk::ImageUsageFlagBits::eTransferDst
      |vk::ImageUsageFlagBits::eSampled
  };

  listHead = etna::get_context().createImage(std::move(listHeadInfo));

  uint32_t pixels = listHead.getInfo().extent.width * listHead.getInfo().extent.height;

  fragmentList = etna::get_context().createBuffer(etna::Buffer::CreateInfo {
    .size = sizeof(uint32_t) + LIST_MULT * pixels * sizeof(FragmentEntry),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst
  });
}

TexBlender::TexBlender(const std::string &name, vk::Format dstFmt)
{
  vk::SamplerCreateInfo info {
    .magFilter = vk::Filter::eLinear,
    .minFilter = vk::Filter::eLinear,
    .mipmapMode = vk::SamplerMipmapMode::eNearest,
    .addressModeU = vk::SamplerAddressMode::eClampToEdge,
    .addressModeV = vk::SamplerAddressMode::eClampToEdge,
    .addressModeW = vk::SamplerAddressMode::eRepeat,
    .maxLod = VK_LOD_CLAMP_NONE
  };

  sampler = etna::get_context().getDevice().createSamplerUnique(info).value;

  vk::PipelineColorBlendAttachmentState blendState {
    .blendEnable = VK_TRUE,
    .srcColorBlendFactor = vk::BlendFactor::eSrcColor,
    .dstColorBlendFactor = vk::BlendFactor::eDstColor,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR|vk::ColorComponentFlagBits::eG|vk::ColorComponentFlagBits::eB|vk::ColorComponentFlagBits::eA
  };

  etna::GraphicsPipeline::CreateInfo pInfo {};
  pInfo.fragmentShaderOutput.colorAttachmentFormats.push_back(dstFmt);
  pInfo.blendingConfig.attachments[0] = blendState;
  pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(name, pInfo);
}

void TexBlender::blend(etna::SyncCommandBuffer &cmd, const etna::Image &overlay, const etna::Image &dst)
{
  vk::Extent2D ext {dst.getInfo().extent.width, dst.getInfo().extent.height};

  etna::RenderingAttachment dstAttachment {
    .view = dst.getView({}),
    .layout = vk::ImageLayout::eColorAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eLoad
  };

  auto pInfo = etna::get_shader_program(pipeline.getShaderProgram()); 
  auto set = etna::create_descriptor_set(pInfo.getDescriptorLayoutId(0), {
    {0, overlay.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}
  });

  etna::RenderTargetState rts {cmd, ext, {dstAttachment}, {}};
  
  cmd.bindPipeline(pipeline);
  cmd.bindDescriptorSet(vk::PipelineBindPoint::eGraphics, pInfo.getPipelineLayout(), 0, set);
  cmd.draw(3, 1, 0, 0);
}

} // namespace scene