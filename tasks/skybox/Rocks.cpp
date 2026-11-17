#include "Rocks.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
namespace
{
struct Vertex
{
  glm::vec4 position, normal, uv;
};
uint32_t hash(uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  return x ^ (x >> 16);
}
float random(uint32_t& seed)
{
  seed = hash(seed);
  return float(seed >> 8) / 16777216.f;
}
}
void Rocks::init(const std::vector<float>& data)
{
  heights = data;
  auto& ctx = etna::get_context();
  auto cmd = ctx.createOneShotCmdMgr();
  etna::BlockingTransferHelper upload({.stagingSize = 16 * 1024 * 1024});
  sampler = etna::Sampler(
    {.filter = vk::Filter::eLinear,
     .addressMode = vk::SamplerAddressMode::eRepeat,
     .name = "rocks"});
  for (int m = 0; m < 2; ++m)
  {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    constexpr int rings = 8, slices = 12;
    for (int y = 0; y <= rings; ++y)
      for (int x = 0; x <= slices; ++x)
      {
        float theta = glm::pi<float>() * y / rings;
        float phi = glm::two_pi<float>() * x / slices;
        glm::vec3 n(std::sin(theta)*std::cos(phi), std::cos(theta),
                    std::sin(theta)*std::sin(phi));
        float radius = 0.45f + 0.06f*std::sin(n.x*11+m)*std::cos(n.z*13+n.y*7);
        glm::vec3 position = n*radius;
        position.y = (position.y+0.5f)*(m == 0 ? 0.8f : 1.2f);
        vertices.push_back({glm::vec4(position,1), glm::vec4(n,0),
                           glm::vec4(float(x)/slices,float(y)/rings,0,0)});
      }
    for (int y = 0; y < rings; ++y)
      for (int x = 0; x < slices; ++x)
      {
        uint32_t a = y*(slices+1)+x, b = a+slices+1;
        indices.insert(indices.end(), {a,a+1,b,a+1,b+1,b});
      }
    auto& mesh = meshes[m];
    auto buffer = [&](size_t size, vk::BufferUsageFlags usage) {
      return ctx.createBuffer(
        {.size = size,
         .bufferUsage = usage | vk::BufferUsageFlagBits::eTransferDst,
         .name = "rocks"});
    };
    mesh.vertices = buffer(
      vertices.size() * sizeof(Vertex),
      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer);
    mesh.indices = buffer(indices.size() * 4, vk::BufferUsageFlagBits::eIndexBuffer);
    mesh.instances = buffer(100000 * sizeof(Instance), vk::BufferUsageFlagBits::eStorageBuffer);
    upload.uploadBuffer<Vertex>(*cmd, mesh.vertices, 0, vertices);
    upload.uploadBuffer<uint32_t>(*cmd, mesh.indices, 0, indices);
    mesh.count = indices.size();
    std::vector<uint8_t> texels(64*64*4);
    uint32_t seed = 137+m;
    for (size_t i=0; i<texels.size(); i+=4)
    {
      uint8_t value = uint8_t(90+60*random(seed));
      texels[i] = value;
      texels[i+1] = value;
      texels[i+2] = value-8;
      texels[i+3] = 255;
    }
    mesh.albedo = ctx.createImage(
      {.extent = vk::Extent3D{64,64,1},
       .name = "rock_albedo",
       .format = vk::Format::eR8G8B8A8Srgb,
       .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst});
    upload.uploadImage(*cmd, mesh.albedo, 0, 0, std::as_bytes(std::span(texels)));
  }
}
void Rocks::setup(const std::vector<vk::Format>& formats)
{
  etna::create_program(
    "rocks",
    {SKYBOX_RENDERER_SHADERS_ROOT "rocks.vert.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "rocks.frag.spv"});
  etna::create_program(
    "rocks_depth",
    {SKYBOX_RENDERER_SHADERS_ROOT "rocks.vert.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "depth.frag.spv"});
  depthPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "rocks_depth",
    etna::GraphicsPipeline::CreateInfo{
      .blendingConfig = {.attachments = {}, .logicOp = vk::LogicOp::eCopy},
      .fragmentShaderOutput = {.depthAttachmentFormat = vk::Format::eD32Sfloat}});
  pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "rocks",
    etna::GraphicsPipeline::CreateInfo{
      .blendingConfig =
        {.attachments = std::vector<vk::PipelineColorBlendAttachmentState>(
           3, vk::PipelineColorBlendAttachmentState{.colorWriteMask = vk::ColorComponentFlags(15)}),
         .logicOp = vk::LogicOp::eCopy},
      .depthConfig =
        {.depthTestEnable = true,
         .depthWriteEnable = false,
         .depthCompareOp = vk::CompareOp::eEqual},
      .fragmentShaderOutput = {
        .colorAttachmentFormats = formats, .depthAttachmentFormat = vk::Format::eD32Sfloat}});
}
void Rocks::update(const glm::mat4& vp, glm::vec3 camera)
{
  maxSize = std::max(minSize, maxSize);
  push = {vp, glm::vec4(camera, 1)};
  auto t = glm::transpose(vp);
  std::array<glm::vec4, 6> planes = {
    t[3] + t[0], t[3] - t[0], t[3] + t[1], t[3] - t[1], t[2], t[3] - t[2]};
  for (auto& p : planes)
    p /= glm::length(glm::vec3(p));
  for (auto& m : meshes)
    m.visible.clear();
  auto height = [&](float x, float z) {
    float u = std::clamp(x + 2048.f - 0.5f, 0.f, 4095.f),
          v = std::clamp(z + 2048.f - 0.5f, 0.f, 4095.f);
    int a = int(u), b = int(v), c = std::min(a + 1, 4095), d = std::min(b + 1, 4095);
    return glm::mix(
      glm::mix(heights[b * 4096 + a], heights[b * 4096 + c], u - a),
      glm::mix(heights[d * 4096 + a], heights[d * 4096 + c], u - a),
      v - b);
  };
  for (int z = int(std::floor((camera.z - distance) / 6));
       z <= int(std::ceil((camera.z + distance) / 6));
       ++z)
    for (int x = int(std::floor((camera.x - distance) / 6));
         x <= int(std::ceil((camera.x + distance) / 6));
         ++x)
    {
      uint32_t seed = hash(uint32_t(x) ^ hash(uint32_t(z) + 12345));
      float px = (x + random(seed)) * 6, pz = (z + random(seed)) * 6;
      if (std::abs(px) > 2048 || std::abs(pz) > 2048)
        continue;
      float h = height(px, pz),
            probability = glm::smoothstep(0.03f, 0.18f, h) * (1 - glm::smoothstep(0.55f, 0.8f, h));
      if (random(seed) > density * probability)
        continue;
      float size = glm::mix(minSize, maxSize, random(seed)), angle = random(seed) * 6.2831853f;
      int model = random(seed) < 0.5f ? 0 : 1;
      float tint = 0.8f + random(seed) * 0.4f;
      glm::vec3 pos(px, h * 140 - size * 0.08f, pz), center = pos + glm::vec3(0, size * 0.5f, 0);
      if (glm::length(center - camera) > distance + size)
        continue;
      bool visible = true;
      for (auto p : planes)
        if (glm::dot(p, glm::vec4(center, 1)) < -size)
          visible = false;
      if (!visible)
        continue;
      meshes[model].visible.push_back(
        {glm::scale(
           glm::rotate(glm::translate(glm::mat4(1), pos), angle, glm::vec3(0, 1, 0)),
           glm::vec3(size)),
         glm::vec4(tint, tint, tint, 1)});
    }


  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
  auto cmd = etna::get_context().createOneShotCmdMgr();
  etna::BlockingTransferHelper upload({.stagingSize = 16 * 1024 * 1024});
  for (auto& m : meshes)
    if (!m.visible.empty())
      upload.uploadBuffer<Instance>(*cmd, m.instances, 0, m.visible);
}
void Rocks::draw(vk::CommandBuffer cmd, bool depth)
{
  auto& selected = depth ? depthPipeline : pipeline;
  auto layout = selected.getVkPipelineLayout();
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, selected.getVkPipeline());
  cmd.pushConstants<Push>(
    layout,
    (depth ? vk::ShaderStageFlags(vk::ShaderStageFlagBits::eVertex)
           : vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
    0,
    {push});
  for (auto& m : meshes)
  {
    if (m.visible.empty())
      continue;
    std::vector<etna::Binding> bindings{
      etna::Binding{0, m.vertices.genBinding()}, etna::Binding{1, m.instances.genBinding()}};
    if (!depth)
      bindings.push_back(
        etna::Binding{
          2, m.albedo.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)});
    auto set = etna::create_descriptor_set(
      etna::get_shader_program(depth ? "rocks_depth" : "rocks").getDescriptorLayoutId(0),
      cmd,
      bindings);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {set.getVkSet()}, {});
    cmd.bindIndexBuffer(m.indices.get(), 0, vk::IndexType::eUint32);
    cmd.drawIndexed(m.count, uint32_t(m.visible.size()), 0, 0, 0);
  }
}
