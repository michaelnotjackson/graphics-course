#include "Rocks.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <tiny_gltf.h>
#include <imgui.h>
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
  const char* names[] = {"boulder_01", "rock_09"};
  for (int m = 0; m < 2; ++m)
  {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    std::string path = std::string(ROCK_ASSETS) + names[m] + "/" + names[m] + ".gltf";
    if (!loader.LoadASCIIFromFile(&model, &err, &warn, path))
      throw std::runtime_error(path + ": " + err);
    const auto& primitive = model.meshes.at(0).primitives.at(0);
    auto read = [&](int accessor, size_t index, void* dst, size_t size) {
      const auto& a = model.accessors.at(accessor);
      const auto& v = model.bufferViews.at(a.bufferView);
      const auto& b = model.buffers.at(v.buffer).data;
      size_t offset = v.byteOffset + a.byteOffset + index * a.ByteStride(v);
      if (offset + size > b.size())
        throw std::runtime_error("Invalid glTF accessor");
      std::memcpy(dst, b.data() + offset, size);
    };
    int pos = primitive.attributes.at("POSITION"), normal = primitive.attributes.at("NORMAL"),
        uv = primitive.attributes.at("TEXCOORD_0");
    std::vector<Vertex> vertices(model.accessors.at(pos).count);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (size_t i = 0; i < vertices.size(); ++i)
    {
      auto& v = vertices[i];
      read(pos, i, &v.position, 12);
      read(normal, i, &v.normal, 12);
      read(uv, i, &v.uv, 8);
      lo = glm::min(lo, glm::vec3(v.position));
      hi = glm::max(hi, glm::vec3(v.position));
    }

    float scale = 1.f / glm::length(hi - lo);
    glm::vec3 center((lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2);
    for (auto& v : vertices)
      v.position = glm::vec4((glm::vec3(v.position) - center) * scale, 1);
    auto& mesh = meshes[m];
    const auto& ia = model.accessors.at(primitive.indices);
    std::vector<uint32_t> indices(ia.count);
    for (size_t i = 0; i < indices.size(); ++i)
    {
      uint32_t value = 0;
      size_t bytes = ia.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? 4
        : ia.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT          ? 2
                                                                              : 1;
      read(primitive.indices, i, &value, bytes);
      indices[i] = value;
    }
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
    const auto& material = model.materials.at(primitive.material);
    const auto& image = model.images.at(
      model.textures.at(material.pbrMetallicRoughness.baseColorTexture.index).source);
    mesh.albedo = ctx.createImage(
      {.extent = vk::Extent3D{uint32_t(image.width), uint32_t(image.height), 1},
       .name = "rock_albedo",
       .format = vk::Format::eR8G8B8A8Srgb,
       .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst});
    upload.uploadImage(*cmd, mesh.albedo, 0, 0, std::as_bytes(std::span(image.image)));
  }
}
void Rocks::setup(const std::vector<vk::Format>& formats)
{
  etna::create_program(
    "rocks",
    {DEFERRED_RENDERER_SHADERS_ROOT "rocks.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "rocks.frag.spv"});
  etna::create_program(
    "rocks_depth",
    {DEFERRED_RENDERER_SHADERS_ROOT "rocks.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "depth.frag.spv"});
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
  ImGui::Begin("Rock instancing");
  ImGui::SliderFloat("Density", &density, 0, 1);
  ImGui::SliderFloat("Distance", &distance, 20, 600);
  ImGui::SliderFloat("Min size", &minSize, 0.2f, 10);
  ImGui::SliderFloat("Max size", &maxSize, minSize, 15);
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
  ImGui::Text("Visible: %zu + %zu (2 draws)", meshes[0].visible.size(), meshes[1].visible.size());
  ImGui::End();


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
