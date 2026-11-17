#pragma once
#include <array>
#include <vector>
#include <glm/glm.hpp>
#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/GraphicsPipeline.hpp>
class Rocks
{
public:
  void init(const std::vector<float>& heights);
  void setup(const std::vector<vk::Format>& formats);
  void update(const glm::mat4& vp, glm::vec3 camera);
  void draw(vk::CommandBuffer cmd, bool depth);

private:
  struct Instance
  {
    glm::mat4 model;
    glm::vec4 color;
  };
  struct Mesh
  {
    etna::Buffer vertices, indices, instances;
    etna::Image albedo;
    uint32_t count = 0;
    std::vector<Instance> visible;
  };
  std::array<Mesh, 2> meshes;
  etna::Sampler sampler;
  etna::GraphicsPipeline pipeline, depthPipeline;
  std::vector<float> heights;
  struct Push
  {
    glm::mat4 vp;
    glm::vec4 camera;
  } push;
  float density = 0.35f, distance = 350, minSize = 1, maxSize = 5;
};
