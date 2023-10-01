#include "SceneRenderer.hpp"

#include <etna/GlobalContext.hpp>

namespace scene
{

static float halton_sequence(uint32_t i, uint32_t b)
{
  float f = 1.0f;
  float r = 0.0f;
 
  while (i > 0)
  {
    f /= static_cast<float>(b);
    r = r + f * static_cast<float>(i % b);
    i = static_cast<uint32_t>(floorf(static_cast<float>(i) / static_cast<float>(b)));
  }
 
  return r;
}

GlobalFrameConstantHandler::GlobalFrameConstantHandler()
  : numFrames {etna::get_context().getNumFramesInFlight()}
{
  auto physicalDevice = etna::get_context().getPhysicalDevice();
  auto offsetAlignment = physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;

  paramsGpuSize = ((sizeof(params) + offsetAlignment - 1)/offsetAlignment)*offsetAlignment;
  
  constantUbo = etna::get_context().createBuffer(etna::Buffer::CreateInfo {
    .size = numFrames * paramsGpuSize,
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU
  });
}
  
void GlobalFrameConstantHandler::setViewMatrix(const glm::mat4 &view)
{
  params.view = view;
}

void GlobalFrameConstantHandler::makeProjection(float fovy, float aspect, float znear, float zfar)
{
  params.projectionParams = glm::vec4{std::tan(fovy/2.f), aspect, znear, zfar};
  params.projection = vk_perspective(fovy, aspect, znear, zfar);
}

void GlobalFrameConstantHandler::updateFov(float fovy)
{
  invalidateHistory = true;
  params.projectionParams.x = std::tan(fovy/2.f);
  params.projection = vk_perspective(
    params.projectionParams.x,
    params.projectionParams.y,
    params.projectionParams.z,
    params.projectionParams.w);
}

void GlobalFrameConstantHandler::updateAspect(float aspect)
{
  invalidateHistory = true;
  params.projectionParams.y = aspect;
  params.projection = vk_perspective(
    params.projectionParams.x,
    params.projectionParams.y,
    params.projectionParams.z,
    params.projectionParams.w);
}

void GlobalFrameConstantHandler::setViewport(uint32_t width, uint32_t height)
{
  params.viewport = glm::vec4(float(width), float(height), 0.f, 0.f);
  invalidateHistory = true;
}

void GlobalFrameConstantHandler::setSunColor(glm::vec3 color)
{
  params.sunColor = glm::vec4{color.x, color.y, color.z, 1.f};
}

void GlobalFrameConstantHandler::setSunDirection(glm::vec3 dir)
{
  dir = glm::normalize(dir);
  params.sunDirection = params.view * glm::vec4{dir.x, dir.y, dir.z, 0.f};
}

void GlobalFrameConstantHandler::onBeginFrame() //write data to const buffer
{
  uint32_t prevJitterIndex = (jitterIndex + JITTER_COUNT - 1) % JITTER_COUNT; 

  params.jitter.x = 2.f * halton_sequence(jitterIndex + 1, 2) - 1.f;
  params.jitter.y = 2.f * halton_sequence(jitterIndex + 1, 3) - 1.f;
  params.jitter.z = 2.f * halton_sequence(prevJitterIndex + 1, 2) - 1.f;
  params.jitter.w = 2.f * halton_sequence(prevJitterIndex + 1, 3) - 1.f;

  params.viewProjection = params.projection * params.view;

  if (invalidateHistory)
  {
    params.prevViewProjection = params.viewProjection;
    params.jitter.z = params.jitter.x;
    params.jitter.w = params.jitter.y;
  }

  params.jitter /= glm::vec4{params.viewport.x, params.viewport.y, params.viewport.x, params.viewport.y};
  
  if (!enableJitter)
    params.jitter = glm::zero<glm::vec4>();

  auto ptr = constantUbo.map();
  memcpy(ptr + frameIndex * paramsGpuSize, &params, sizeof(params));
  constantUbo.unmap();
} 

void GlobalFrameConstantHandler::onEndFrame() //next index;
{
  params.prevViewProjection = params.viewProjection;
  frameIndex = (frameIndex + 1) % numFrames;
  if (invalidateHistory)
    spdlog::warn("History invalidate");
    
  invalidateHistory = false;
  jitterIndex = (jitterIndex + 1) % JITTER_COUNT;
}


RenderTargetState::RenderTargetState(uint32_t w, uint32_t h)
{
  onResolutionChanged(w, h);
}

void RenderTargetState::onResolutionChanged(uint32_t new_w, uint32_t new_h)
{
  for (uint32_t i = 0; i < 2; i++)
  {
    depth[i] = etna::get_context().createImage(
    etna::ImageCreateInfo::depthRT(new_w, new_h, vk::Format::eD24UnormS8Uint));
    color[i] = etna::get_context().createImage(
      etna::ImageCreateInfo::colorRT(new_w, new_h, baseColorFmt));
  }

  currentColor = etna::get_context().createImage(
      etna::ImageCreateInfo::colorRT(new_w, new_h, baseColorFmt));

  velocity = etna::get_context().createImage(
    etna::ImageCreateInfo::colorRT(new_w, new_h, vk::Format::eR16G16Sfloat));  
}

SceneRenderer::SceneRenderer(const std::string &prog_name,
  const std::string &depth_prog_name,
  const RenderTargetInfo &rtInfo)
  : program {etna::get_shader_program(prog_name).getId() }
{
  etna::GraphicsPipeline::CreateInfo info {};
  info.vertexShaderInput = scene::Vertex::getDesc();
  info.depthConfig.depthCompareOp = vk::CompareOp::eEqual;
  info.depthConfig.depthWriteEnable = VK_FALSE;
  info.blendingConfig.attachments.clear();

  vk::PipelineColorBlendAttachmentState blendState {
    .blendEnable = VK_FALSE,
    .colorWriteMask = vk::ColorComponentFlagBits::eA
      |vk::ColorComponentFlagBits::eR|vk::ColorComponentFlagBits::eG|vk::ColorComponentFlagBits::eB
  };

  for (uint32_t i = 0; i < rtInfo.colorRT.size(); i++)
  {
    info.fragmentShaderOutput.colorAttachmentFormats.push_back(rtInfo.colorRT[i]);
    info.blendingConfig.attachments.push_back(blendState);
  }

  info.fragmentShaderOutput.depthAttachmentFormat = rtInfo.depthRT;

  pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(prog_name, info);

  //depth prepass
  info.vertexShaderInput = scene::Vertex::getDescPosOnly();
  info.blendingConfig.attachments.clear();
  info.fragmentShaderOutput.colorAttachmentFormats.clear();
  info.depthConfig.depthWriteEnable = VK_TRUE;
  info.depthConfig.depthCompareOp = vk::CompareOp::eLessOrEqual;

  depthPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(depth_prog_name, info);
}

void SceneRenderer::attachToScene(const GLTFScene &scene)
{
  sceneData = scene.queryDrawCalls([](const GLTFScene::Material &material) {
    return material.mode == GLTFScene::MaterialMode::Opaque;
  }); 
}

static std::tuple<const etna::Image*, vk::Sampler>
get_image_texture(const GLTFScene &scene, std::optional<uint32_t> texid)
{
  const etna::Image *image = &scene.getStubTexture();
  vk::Sampler sampler = scene.getSampler(0); //TODO: default sampler

  if (texid.has_value())
  {
    auto [tex, smp] = scene.getImageSamplerId(*texid);
    image = &scene.getImage(tex);
    sampler = scene.getSampler(smp);
  }

  return {image, sampler};
}

static float castUint32ToFloat(uint32_t i)
{
  return *reinterpret_cast<const float*>(&i);
}

uint32_t SceneRenderer::bindDS(
  etna::SyncCommandBuffer &cmd, 
  const GlobalFrameConstantHandler &gframe,
  const GLTFScene::Material &material, 
  const GLTFScene &scene)
{
  uint32_t renderFlags = 0;
  
  auto [baseColorTex, baseColorSampler] = get_image_texture(scene, material.baseColorId);
  auto [mrTex, mrSampler] = get_image_texture(scene, material.metallicRoughnessId);

  if (!material.baseColorId.has_value())
    renderFlags |= uint32_t(RenderFlags::NoBaseColorTex);

  if (!material.metallicRoughnessId.has_value())
    renderFlags |= uint32_t(RenderFlags::NoMetallicRougnessTex);

  auto baseColorBinding = baseColorTex->genBinding(
      baseColorSampler, vk::ImageLayout::eShaderReadOnlyOptimal, baseColorTex->fullRangeView());

  auto mrBinding = mrTex->genBinding(
    mrSampler, vk::ImageLayout::eShaderReadOnlyOptimal, mrTex->fullRangeView());

  std::vector<etna::Binding> bindings {
    etna::Binding {0, gframe.getBinding()},
    etna::Binding {1, baseColorBinding},
    etna::Binding {2, mrBinding}
  };

  const auto &info = etna::get_shader_program(pipeline.getShaderProgram()); 
  auto set = etna::create_descriptor_set(info.getDescriptorLayoutId(0), bindings);
  cmd.bindDescriptorSet(vk::PipelineBindPoint::eGraphics, info.getPipelineLayout(), 0, set);

  return renderFlags;
}

struct DepthPushConstants
{
  glm::mat4 MVP;
  glm::vec4 jitter;
};

void SceneRenderer::depthPrepass(etna::SyncCommandBuffer &cmd, 
  const GlobalFrameConstantHandler &gframe, const GLTFScene &scene)
{
  //expect cmd in render state, binded scene vertex/index buffers
  cmd.bindPipeline(depthPipeline);
  auto progInfo = etna::get_shader_program(pipeline.getShaderProgram());

  for (auto &group : sceneData.materialGropus)
  {    
    for (auto &dc : group.drawCalls)
    {
      for (auto &tId : dc.transformIds)
      {
        DepthPushConstants dPC;

        auto &transform = scene.getTransform(tId);
        auto normalMat = glm::transpose(glm::inverse(gframe.getParams().view * transform.modelTransform));
        auto MVP = gframe.getParams().viewProjection * transform.modelTransform;
        
        dPC.MVP = MVP;
        dPC.jitter = gframe.getParams().jitter;

        cmd.pushConstants(pipeline.getShaderProgram(), 0, dPC);
        cmd.drawIndexed(dc.indexCount, 1, dc.firstIndex, dc.vertexOffset, 0);
      }
    }
  }

}

void SceneRenderer::render(etna::SyncCommandBuffer &cmd, 
  const GlobalFrameConstantHandler &gframe, const GLTFScene &scene)
{
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
          .prevMVP = gframe.getParams().prevViewProjection * transform.modelTransform,
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

} // namespace scene