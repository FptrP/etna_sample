#define TINYGLTF_IMPLEMENTATION
#include "GLTFScene.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <etna/GlobalContext.hpp>

#include <glm/gtx/quaternion.hpp>

#include <vulkan/vulkan_format_traits.hpp>

#include <unordered_set>

namespace scene 
{

etna::VertexShaderInputDescription Vertex::getDesc()
{
  etna::VertexByteStreamFormatDescription bufDesc {
    .stride = sizeof(Vertex),
    .attributes {
      {
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(Vertex, pos)
      },
      {
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(Vertex, norm)
      },
      {
        .format = vk::Format::eR32G32Sfloat,
        .offset = offsetof(Vertex, uv)
      }
    }
  };

  etna::VertexShaderInputDescription desc {
    .bindings { 
      etna::VertexShaderInputDescription::Binding {
        .byteStreamDescription = bufDesc,
        .attributeMapping = bufDesc.identityAttributeMapping()
      }
    }
  };

  return desc;
}

static void generate_mips(etna::SyncCommandBuffer &cmd, const etna::Image &image)
{
  uint32_t mipLevels = image.getInfo().mipLevels;
  
  uint32_t srcWidth = image.getInfo().extent.width;
  uint32_t srcHeight = image.getInfo().extent.height;

  for (uint32_t mipDst = 1; mipDst < mipLevels; mipDst++)
  {
    uint32_t dstWidth = std::max(srcWidth/2u, 1u);
    uint32_t dstHeight = std::max(srcHeight/2u, 1u);

    vk::ImageBlit blit {
      .srcSubresource {
        .aspectMask = image.getAspectMaskByFormat(),
        .mipLevel = mipDst - 1,
        .baseArrayLayer = 0,
        .layerCount = 1
      },
      .dstSubresource {
        .aspectMask = image.getAspectMaskByFormat(),
        .mipLevel = mipDst,
        .baseArrayLayer = 0,
        .layerCount = 1
      }
    };
    blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.srcOffsets[1] = vk::Offset3D{int(srcWidth), int(srcHeight), 1};
    blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.dstOffsets[1] = vk::Offset3D{int(dstWidth), int(dstHeight), 1};
    
    cmd.blitImage(
      image, 
      vk::ImageLayout::eTransferSrcOptimal, 
      image, 
      vk::ImageLayout::eTransferDstOptimal,
      {blit},
      vk::Filter::eLinear);
    
    srcWidth = dstWidth;
    srcHeight = dstHeight;
  }
}

static etna::Image load_image(etna::SyncCommandBuffer &cmd, const tinygltf::Image &src,
  std::vector<etna::Buffer> &staging_buffers)
{
  uint32_t stagingMemSize = 0;
  for (auto &buff : staging_buffers)
  {
    stagingMemSize += buff.getSize();
  }

  if (stagingMemSize >= 32 << 20)
  {
    cmd.end();
    cmd.submit();
    etna::get_context().getQueue().waitIdle();
    staging_buffers.clear();
    cmd.reset();
    cmd.begin();
  }


  auto image = etna::get_context().createImage(
    etna::ImageCreateInfo::image2D(src.width, src.height, vk::Format::eR8G8B8A8Unorm));
  
  auto &info = image.getInfo();

  auto stagingBuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo {
    .size = vk::blockSize(info.format) * info.extent.width * info.extent.height,
    .bufferUsage = vk::BufferUsageFlagBits::eTransferSrc,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU
  });

  ETNA_ASSERT(stagingBuf.getSize() == src.image.size() * sizeof(src.image[0]));

  auto ptr = stagingBuf.map();
  std::memcpy(ptr, src.image.data(), src.image.size() * sizeof(src.image[0]));
  stagingBuf.unmap();

  //cmd.reset();
  //cmd.begin();

  cmd.copyBufferToImage(stagingBuf, image, vk::ImageLayout::eTransferDstOptimal, {
    vk::BufferImageCopy {
      .bufferOffset = 0,
      .bufferRowLength = info.extent.width,
      .bufferImageHeight = info.extent.height,
      .imageSubresource {image.getAspectMaskByFormat(), 0, 0, 1},
      .imageOffset {0, 0, 0},
      .imageExtent = info.extent
    }
  });

  generate_mips(cmd, image);
  //cmd.end();
  //cmd.submit();
  //etna::get_context().getQueue().waitIdle();
  
  //cmd.reset();
  staging_buffers.emplace_back(std::move(stagingBuf));
  return image;
}

static etna::Image create_stub_rexture(etna::SyncCommandBuffer &cmd)
{
  auto createInfo = etna::ImageCreateInfo::image2D(1, 1, vk::Format::eR8G8B8A8Unorm);
  auto image = etna::get_context().createImage(std::move(createInfo));

  vk::ClearColorValue color{1.f, 0.f, 0.f, 0.f};

  cmd.begin();
  cmd.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, color, {
    vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
  });
  cmd.end();
  cmd.submit();
  etna::get_context().getQueue().waitIdle();
  cmd.reset();

  return image;
}

static vk::SamplerAddressMode remap_wrap_mode(int wrap)
{
  switch (wrap)
  {
    case TINYGLTF_TEXTURE_WRAP_REPEAT:
      return vk::SamplerAddressMode::eRepeat;
    case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
      return vk::SamplerAddressMode::eClampToBorder;  
    case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: 
      return vk::SamplerAddressMode::eMirroredRepeat;
  }
  return vk::SamplerAddressMode::eClampToBorder;
}

static std::tuple<vk::Filter, vk::SamplerMipmapMode>
remap_min_filter(int val)
{
  switch (val)
  {
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
      return {vk::Filter::eNearest, vk::SamplerMipmapMode::eLinear};
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
      return {vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest};
    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
      return {vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest};
    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
      return {vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear};
  }

  return {vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear};
}

static vk::UniqueSampler create_sampler(const tinygltf::Sampler &desc)
{
  auto magFilter = (desc.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)? 
    vk::Filter::eNearest : vk::Filter::eLinear; 
  
  auto [minFilter, mipMode] = remap_min_filter(desc.minFilter);

  vk::SamplerCreateInfo info {
    .magFilter = magFilter,
    .minFilter = minFilter,
    .mipmapMode = mipMode,
    .addressModeU = remap_wrap_mode(desc.wrapS),
    .addressModeV = remap_wrap_mode(desc.wrapT),
    .addressModeW = vk::SamplerAddressMode::eRepeat,
    .maxLod = VK_LOD_CLAMP_NONE
  };

  return etna::get_context().getDevice().createSamplerUnique(info).value;
}

std::span<const float> get_span(const tinygltf::Model &model, const tinygltf::Accessor &accessor)
{
  ETNA_ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
  ETNA_ASSERT(accessor.sparse.isSparse == false);
  ETNA_ASSERT(accessor.bufferView >= 0);

  auto &bufferView = model.bufferViews[accessor.bufferView];
  auto &buffer = model.buffers[bufferView.buffer];
  
  uint32_t components = tinygltf::GetNumComponentsInType(uint32_t(accessor.type));
  uint32_t floatsCount = accessor.count * components;
  
  uint32_t byteOffset = accessor.byteOffset + bufferView.byteOffset;
  const uint8_t *ptr = buffer.data.data();

  return std::span{reinterpret_cast<const float*>(ptr + byteOffset), floatsCount};
}

static std::variant<std::span<const uint16_t>, std::span<const uint32_t>>
get_indicies(const tinygltf::Model &model, const tinygltf::Accessor &accessor)
{
  ETNA_ASSERT(accessor.sparse.isSparse == false);
  ETNA_ASSERT(accessor.bufferView >= 0);

  auto &bufferView = model.bufferViews[accessor.bufferView];
  auto &buffer = model.buffers[bufferView.buffer];
  
  uint32_t components = tinygltf::GetNumComponentsInType(uint32_t(accessor.type));
  ETNA_ASSERT(components == 1);
  uint32_t compSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
  ETNA_ASSERT(compSize == 2 || compSize == 4);  
  uint32_t byteOffset = accessor.byteOffset + bufferView.byteOffset;
  const uint8_t *ptr = buffer.data.data();

  if (compSize == 2)
  {
    return std::span{reinterpret_cast<const uint16_t*>(ptr + byteOffset), accessor.count};
  }
  
  return std::span{reinterpret_cast<const uint32_t*>(ptr + byteOffset), accessor.count};
}

static std::tuple<etna::Buffer, etna::Buffer> load_verts_data(
  etna::SyncCommandBuffer &cmd, 
  const std::vector<Vertex> &vertsData,
  const std::vector<uint32_t> &indexData)
{
  auto vertBuff = etna::get_context().createBuffer(etna::Buffer::CreateInfo {
    .size = sizeof(Vertex) * vertsData.size(),
    .bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eTransferDst
  });

  auto indexBuff = etna::get_context().createBuffer(etna::Buffer::CreateInfo {
    .size = sizeof(uint32_t) * indexData.size(),
    .bufferUsage = vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eTransferDst
  });

  auto stagingVerts = etna::get_context().createBuffer(etna::Buffer::CreateInfo {
    .size = vertBuff.getSize(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferSrc,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU
  });

  auto stagingIndex = etna::get_context().createBuffer(etna::Buffer::CreateInfo {
    .size = indexBuff.getSize(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferSrc,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU
  });

  auto mem = stagingVerts.map();
  std::memcpy(mem, vertsData.data(), stagingVerts.getSize());
  stagingVerts.unmap();

  mem = stagingIndex.map();
  std::memcpy(mem, indexData.data(), indexBuff.getSize());
  stagingIndex.unmap();

  cmd.reset();
  cmd.begin();
  cmd.copyBuffer(stagingVerts, vertBuff, {vk::BufferCopy {.size = vertBuff.getSize()}});
  cmd.copyBuffer(stagingIndex, indexBuff, {vk::BufferCopy{.size = indexBuff.getSize()}});
  cmd.end();
  cmd.submit();

  etna::get_context().getQueue().waitIdle();
  cmd.reset();

  return {std::move(vertBuff), std::move(indexBuff)};
}

static GLTFScene::Mesh::DrawCall process_prim(
  const tinygltf::Model &model,
  const tinygltf::Primitive &primitive, 
  std::vector<Vertex> &vertexData,
  std::vector<uint32_t> &indexData)
{
  auto posAccessorIt = primitive.attributes.find("POSITION");
  auto uvAccessorIt = primitive.attributes.find("TEXCOORD_0");
  auto normAccessorIt = primitive.attributes.find("NORMAL");

  ETNA_ASSERT(posAccessorIt != primitive.attributes.end());
  
  std::optional<tinygltf::Accessor> pos = model.accessors[posAccessorIt->second];
  std::optional<tinygltf::Accessor> norm;
  std::optional<tinygltf::Accessor> uv;

  if (normAccessorIt != primitive.attributes.end())
  {
    norm = model.accessors[normAccessorIt->second];
    ETNA_ASSERT(norm->sparse.isSparse == false);
  }

  if (uvAccessorIt != primitive.attributes.end())
  {
    uv = model.accessors[uvAccessorIt->second];
    ETNA_ASSERT(uv->sparse.isSparse == false);
  }

  ETNA_ASSERT(pos->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
  ETNA_ASSERT(norm->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
  ETNA_ASSERT(uv->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

  std::span<const float> posData{};
  std::span<const float> normData{};
  std::span<const float> uvData{};

  if (pos.has_value())
    posData = get_span(model, *pos);
  if (norm.has_value())
    normData = get_span(model, *norm);
  if (uv.has_value())
    uvData = get_span(model, *uv);

  uint32_t vertexOffset = vertexData.size();
  uint32_t vertexCount = pos->count;

  for (uint32_t vertId = 0; vertId < vertexCount; vertId++)
  {
    Vertex vert {
      .pos {0.f, 0.f, 0.f},
      .norm {0.f, 0.f, 0.f},
      .uv {0.f, 0.f}
    };

    if (!posData.empty())
      vert.pos = glm::vec3{posData[3*vertId], posData[3*vertId + 1], posData[3*vertId + 2]};
    if (!normData.empty())
      vert.norm = glm::vec3{normData[3*vertId], normData[3*vertId + 1], normData[3*vertId + 2]};
    if (!uvData.empty())
      vert.uv = glm::vec2{uvData[2*vertId], uvData[2*vertId + 1]};

    vertexData.push_back(vert);
  }

  uint32_t firstIndex = indexData.size();
  
  ETNA_ASSERT(primitive.indices >= 0);
  const auto &indices = model.accessors[primitive.indices];
  ETNA_ASSERT(indices.sparse.isSparse == false);

  uint32_t indexCount = indices.count;
  auto indexInput = get_indicies(model, indices);

  std::visit([&](auto &&span){
    for (uint32_t indexI = 0; indexI < indexCount; indexI++)
      indexData.push_back(span[indexI]);
  }, indexInput);

  return GLTFScene::Mesh::DrawCall {firstIndex, indexCount, vertexOffset, 0}; 
}

static std::vector<GLTFScene::Material> load_materials(const tinygltf::Model &model)
{
  std::vector<GLTFScene::Material> materials;

  materials.reserve(model.materials.size());
  for (auto &src : model.materials)
  {
    GLTFScene::Material mat;
    if (src.pbrMetallicRoughness.baseColorTexture.index >= 0)
      mat.baseColorId = uint32_t(src.pbrMetallicRoughness.baseColorTexture.index);
    if (src.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
      mat.metallicRoughnessId = src.pbrMetallicRoughness.metallicRoughnessTexture.index;
    if (src.normalTexture.index >= 0) // TODO: normal texture parameters
      mat.normalTexId = uint32_t(src.normalTexture.index);

    if (src.alphaMode == "OPAQUE")
      mat.mode = GLTFScene::MaterialMode::Opaque;
    if (src.alphaMode == "BLEND")    
      mat.mode = GLTFScene::MaterialMode::Blend;
    if (src.alphaMode == "MASK")
      mat.mode = GLTFScene::MaterialMode::Mask;
    mat.alphaCutoff = src.alphaCutoff;

    mat.metallicFactor = src.pbrMetallicRoughness.metallicFactor;
    mat.roughnessFactor = src.pbrMetallicRoughness.roughnessFactor;
    
    auto &baseColor = src.pbrMetallicRoughness.baseColorFactor;
    if (baseColor.size() == 4)
      mat.baseColorFactor = glm::vec4 {
        float(baseColor[0]), 
        float(baseColor[1]), 
        float(baseColor[2]), 
        float(baseColor[3])
      }; 
    
    materials.push_back(std::move(mat));
  }
  return materials;
}

std::unique_ptr<GLTFScene> load_scene(const std::string &path, etna::SyncCommandBuffer &cmd)
{
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  
  std::string err;
  std::string warn;

  auto ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);

  if (!err.empty())
    ETNA_ASSERTF(false, "GLTF error : {}", err);

  if (!warn.empty())
    spdlog::warn("GLTF warings : {}", warn);

  ETNA_ASSERT(ret);
  
  std::vector<Vertex> vertexData;
  std::vector<uint32_t> indexData;
  std::vector<GLTFScene::Mesh> sceneMeshes;

  for (const auto &mesh : model.meshes)
  {
    GLTFScene::Mesh sceneMesh;
    for (const auto &prim : mesh.primitives)
    {
      auto dc = process_prim(model, prim, vertexData, indexData);
      ETNA_ASSERT(prim.material >= 0);
      dc.materialId = prim.material;
      sceneMesh.drawCalls.push_back(dc);
    }
    sceneMeshes.push_back(std::move(sceneMesh));
  }

  auto [vertsBuff, indexBuff] = load_verts_data(cmd, vertexData, indexData);

  std::vector<GLTFScene::Node> sceneNodes;
  sceneNodes.reserve(model.nodes.size());
  
  for (const auto &node : model.nodes)
  {
    GLTFScene::Node sceneNode {};
    sceneNode.childNodes.reserve(node.children.size());
    for (auto childId : node.children)
      sceneNode.childNodes.push_back(childId);
    if (node.mesh >= 0)
      sceneNode.meshIndex = node.mesh;

    sceneNode.transform = glm::identity<glm::mat4>();

    if (node.matrix.size())
    {
      for (uint32_t i = 0; i < 4; i++)
        for (uint32_t j = 0; j < 4; j++)
          sceneNode.transform[j][i] = node.matrix[i * 4 + j];
    }
    else if (node.scale.size() || node.rotation.size() || node.translation.size())
    {
      glm::mat4 S = glm::identity<glm::mat4>();
      glm::mat4 R = glm::identity<glm::mat4>();
      glm::mat4 T = glm::identity<glm::mat4>();

      if (node.scale.size() == 3)
        S = glm::scale(glm::identity<glm::mat4>(), 
          glm::vec3(node.scale[0], node.scale[1], node.scale[2]));

      if (node.rotation.size() == 4)
        R = glm::toMat4(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], 
          node.rotation[2]));      
      
      if (node.translation.size() == 3)
        T = glm::translate(glm::identity<glm::mat4>(), 
          glm::vec3(node.translation[0], node.translation[1], node.translation[2]));

      sceneNode.transform = T * R * S;
    }

    sceneNodes.push_back(std::move(sceneNode));  
  }
  

  std::unique_ptr<GLTFScene> scene{new GLTFScene{}};
  scene->indexBuffer = std::move(indexBuff);
  scene->vertexBuffer = std::move(vertsBuff);
  scene->meshes = std::move(sceneMeshes);
  scene->nodes = std::move(sceneNodes);

  if (model.scenes.size())
  {
    scene->rootNodes.reserve(model.scenes[0].nodes.size());
    
    scene->rootNodes.insert(scene->rootNodes.begin(), 
      model.scenes[0].nodes.begin(),
      model.scenes[0].nodes.end());
  }

  if (model.images.size())
  {
    cmd.reset();
    cmd.begin();

    std::vector<etna::Buffer> stagingBuffers;
    scene->images.reserve(model.images.size());
    for (auto &src : model.images)
      scene->images.emplace_back(load_image(cmd, src, stagingBuffers));
    
    cmd.end();
    cmd.submit();
    etna::get_context().getQueue().waitIdle();
    stagingBuffers.clear();
    cmd.reset();
  }

  scene->stubTexture = create_stub_rexture(cmd);

  if (model.samplers.size())
  {
    scene->samplers.reserve(model.samplers.size());
    for (auto &src : model.samplers)
      scene->samplers.emplace_back(create_sampler(src));
  }

  if (model.textures.size())
  {
    scene->imageSamplers.reserve(model.textures.size());
    for (auto &src : model.textures)
      scene->imageSamplers.push_back({src.source, src.sampler});
  }

  scene->materials = load_materials(model);
  scene->initTransforms();

  return scene;
}

void GLTFScene::initTransforms()
{
  worldTransforms.clear();

  auto runNodesCb = [&](uint32_t index, Node &node, glm::mat4 transform, auto &&cb) -> void
  {
    glm::mat4 nodeTransform = transform * node.transform;
    node.worldTransformIndex = std::nullopt;
    
    if (node.meshIndex.has_value())
    {
      node.worldTransformIndex = worldTransforms.size();  
      worldTransforms.push_back(Transform {
        nodeTransform,
        glm::transpose(glm::inverse(nodeTransform))
      });
    }

    for (auto childId : node.childNodes)
      cb(childId, nodes.at(childId), nodeTransform, cb);
  };

  for (auto rootId : rootNodes)
    runNodesCb(rootId, nodes.at(rootId), glm::identity<glm::mat4>(), runNodesCb);
}

SortedScene GLTFScene::buildSortedScene(const std::unordered_set<uint32_t> &queriedMaterials) const
{
  SortedScene sortedScene;
  sortedScene.materialGropus.reserve(queriedMaterials.size());

  std::unordered_map<uint32_t, uint32_t> materialToGroup;

  for (auto mId : queriedMaterials)
  {
    SortedScene::MaterialGroup group;
    group.materialIndex = mId;
    materialToGroup.emplace(mId, sortedScene.materialGropus.size());
    sortedScene.materialGropus.push_back(std::move(group));
  }

  auto scanCb = [&](const Node &node)
  {
    if (!node.meshIndex.has_value())
      return;
    
    ETNA_ASSERT(node.worldTransformIndex.has_value());
    auto &srcMesh = getMeshes().at(*node.meshIndex);

    for (const auto &dc : srcMesh.drawCalls)
    {
      if (!queriedMaterials.contains(dc.materialId))
        continue;

      uint32_t dstGroup = materialToGroup.at(dc.materialId);
      auto &dstDrawCalls = sortedScene.materialGropus.at(dstGroup).drawCalls;

      auto it = std::find_if(dstDrawCalls.begin(), dstDrawCalls.end(), 
        [&](const SortedScene::DrawCall &sortedDc) -> bool {
          return sortedDc.firstIndex == dc.firstIndex
              && sortedDc.indexCount == dc.indexCount
              && sortedDc.vertexOffset == dc.vertexOffset;
        }
      );

      if (it != dstDrawCalls.end())
        it->transformIds.push_back(*node.worldTransformIndex);
      else
        dstDrawCalls.push_back(
          SortedScene::DrawCall {
            .firstIndex = dc.firstIndex,
            .indexCount = dc.indexCount,
            .vertexOffset = dc.vertexOffset,
            .transformIds {*node.worldTransformIndex}
          }
        ); 
    }
  };

  for (auto &node : nodes)
    scanCb(node);

  return sortedScene;
}

} // namespace scene