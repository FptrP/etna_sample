#ifndef GLTF_SCENE_HPP_INCLUDED
#define GLTF_SCENE_HPP_INCLUDED

#include <tiny_gltf.h>

#include "Camera.hpp"

#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/SyncCommandBuffer.hpp>
#include <etna/Sampler.hpp>

#include <unordered_set>

namespace scene
{

struct Vertex
{
  glm::vec3 pos;
  glm::vec3 norm;
  glm::vec2 uv;

  static etna::VertexShaderInputDescription getDesc();
};

struct SortedScene
{
  struct DrawCall
  {
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t vertexOffset;

    std::vector<uint32_t> transformIds;
  };

  struct MaterialGroup
  {
    uint32_t materialIndex;
    std::vector<DrawCall> drawCalls;
  };

  std::vector<MaterialGroup> materialGropus;
};

//static_assert(sizeof(Vertex) > 10);

struct GLTFScene
{
  enum MaterialMode
  {
    Opaque,
    Blend,
    Mask
  };

  struct Mesh
  {
    struct DrawCall
    {
      uint32_t firstIndex;
      uint32_t indexCount;
      uint32_t vertexOffset;
      uint32_t materialId;
    };

    std::vector<DrawCall> drawCalls;
  };
  
  struct Node
  {
    glm::mat4 transform;
    std::optional<uint32_t> meshIndex;
    std::optional<uint32_t> worldTransformIndex; // index to store global transform
    std::vector<uint32_t> childNodes;
  };

  struct Material
  {
    std::optional<uint32_t> baseColorId;
    std::optional<uint32_t> metallicRoughnessId;
    std::optional<uint32_t> normalTexId;
    std::optional<uint32_t> occlusionTexId;

    MaterialMode mode = MaterialMode::Opaque;
    
    glm::vec4 baseColorFactor {0.f, 0.f, 0.f, 1.f};
    float metallicFactor = 0.1f;
    float roughnessFactor = 0.9f; 
    float alphaCutoff = 0.5f;
  };

  struct Transform
  {
    glm::mat4 modelTransform;
    glm::mat4 normalTransform;
  };

  const etna::Buffer &getVertexBuff() const { return vertexBuffer; }
  const etna::Buffer &getIndexBuff() const { return indexBuffer; }
  const std::vector<Mesh> &getMeshes() const { return meshes; }
  const Transform &getTransform(uint32_t index) const { return worldTransforms.at(index); }

  void initTransforms();

  template <typename F>
  void traverseNodes(F cb) const
  {
    traverseNodes(cb, glm::identity<glm::mat4>(), rootNodes);
  }

  template <typename F>
  SortedScene queryDrawCalls(F &&cb) const
  {
    std::unordered_set<uint32_t> queryMaterials;

    for (uint32_t mId = 0; mId < materials.size(); mId++)
    {
      if (cb(materials[mId]))
        queryMaterials.emplace(mId);
    }

    return buildSortedScene(queryMaterials);
  }

  const Material &getMaterial(uint32_t id) const {
    return materials.at(id);
  }

  std::tuple<uint32_t, uint32_t> getImageSampler(uint32_t id) const {
    return imageSamplers.at(id);
  }

  const etna::Image &getImage(uint32_t id) const {
    return images.at(id);
  }

  vk::Sampler getSampler(uint32_t id) const {
    return samplers.at(id).get();
  }

  const etna::Image &getStubTexture() const {
    return *stubTexture;
  }

private:
  SortedScene buildSortedScene(const std::unordered_set<uint32_t> &queriedMaterials) const;

  template <typename F>
  void traverseNodes(F cb, const glm::mat4 &transform, const std::vector<uint32_t> &nodeIds) const
  {
    for (auto nodeId : nodeIds)
    {
      const auto &node = nodes[nodeId];
      glm::mat4 nodeTransform = transform * node.transform;
      if (node.meshIndex.has_value())
      {
        const auto &mesh = meshes[*node.meshIndex];
        for (auto &drawCall : mesh.drawCalls)
        {
          cb(nodeTransform, drawCall, materials[drawCall.materialId]); //todo - material
        }
      }
      
      if (node.childNodes.size())
        traverseNodes(cb, nodeTransform, node.childNodes);
    }
    
  }

  std::vector<Node> nodes;
  std::vector<Transform> worldTransforms;
  
  std::vector<Mesh> meshes;

  std::vector<uint32_t> rootNodes;

  std::vector<Material> materials;

  std::vector<etna::Image> images;
  std::vector<vk::UniqueSampler> samplers;
  std::vector<std::tuple<uint32_t, uint32_t>> imageSamplers; 
  std::optional<etna::Image> stubTexture;
  
  etna::Buffer vertexBuffer;
  etna::Buffer indexBuffer;

  friend std::unique_ptr<GLTFScene> load_scene(const std::string &path, etna::SyncCommandBuffer &cmd);
};

std::unique_ptr<GLTFScene> load_scene(const std::string &path, etna::SyncCommandBuffer &cmd);

//SortedScene 

} // namespace scene


#endif