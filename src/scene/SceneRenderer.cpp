#include "SceneRenderer.hpp"

#include <etna/GlobalContext.hpp>

namespace scene
{

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

void GlobalFrameConstantHandler::setViewport(uint32_t width, uint32_t height)
{
  params.viewport = glm::vec4(float(width), float(height), 0.f, 0.f);
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
  params.viewProjection = params.projection * params.view;
  auto ptr = constantUbo.map();
  memcpy(ptr + frameIndex * paramsGpuSize, &params, sizeof(params));
  constantUbo.unmap();
} 

void GlobalFrameConstantHandler::onEndFrame() //next index;
{
  frameIndex = (frameIndex + 1) % numFrames;
}


SceneRenderer::SceneRenderer(const std::string &prog_name, const RenderTargetInfo &rtInfo)
  : program {etna::get_shader_program(prog_name).getId() }
{
  etna::GraphicsPipeline::CreateInfo info {};
  info.vertexShaderInput = scene::Vertex::getDesc();

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

  // 1 sided and 2 sided materials
  // alpha cutoff? 
  pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(prog_name, info);
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
    auto [tex, smp] = scene.getImageSampler(*texid);
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
        
        auto normalMat = transform.normalTransform * glm::transpose(glm::inverse(gframe.getParams().view));

        MaterialPushConstants mpc
        {
          .MVP = gframe.getParams().viewProjection * transform.modelTransform,
          .normalsTransform = normalMat,//glm::transpose(glm::inverse(gframe.getParams().view * transform.modelTransform)),
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