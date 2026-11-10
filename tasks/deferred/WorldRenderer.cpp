#include "WorldRenderer.hpp"

#include <array>
#include <cstdlib>
#include <imgui.h>
#include <span>
#include <vector>

#include <etna/BlockingTransferHelper.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>

#include "HeightMap.hpp"


void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();
  timer = etna::unwrap_vk_result(ctx.getDevice().createQueryPoolUnique(
    vk::QueryPoolCreateInfo{.queryType = vk::QueryType::eTimestamp, .queryCount = 2}));

  mainViewDepth = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "terrain_depth",
      .format = vk::Format::eD32Sfloat,
      .imageUsage =
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
    });

  auto formats =
    std::array{vk::Format::eR8G8B8A8Unorm, vk::Format::eR16G16Snorm, vk::Format::eR8G8B8A8Unorm};
  if (std::getenv("DEFERRED_WIDE_GBUFFER"))
    formats.fill(vk::Format::eR32G32B32A32Sfloat);
  for (int i = 0; i < 3; ++i)
    for (auto* buffers : {&gbuffer, &decalBuffer})
      (*buffers)[i] = ctx.createImage(
        etna::Image::CreateInfo{
          .extent = {resolution.x, resolution.y, 1},
          .name = "gbuffer",
          .format = formats[i],
          .imageUsage =
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  screenSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eNearest,
      .addressMode = vk::SamplerAddressMode::eClampToEdge,
      .name = "gbuffer_sampler"});
  decalTexture = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = {64, 64, 1},
      .name = "decal_material",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst});
  heightMap = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{kHeightMapResolution, kHeightMapResolution, 1},
      .name = "terrain_height_map",
      .format = vk::Format::eR32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });

  heightMapSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eClampToEdge,
      .mipmapMode = vk::SamplerMipmapMode::eNearest,
      .name = "terrain_height_map_sampler",
    });

  chunkIndexBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = 4 * sizeof(uint32_t),
      .bufferUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .name = "terrain_chunk_indices",
    });

  std::vector<float> heights(static_cast<std::size_t>(kHeightMapResolution) * kHeightMapResolution);
  generateHeightMap(heights.data(), kHeightMapResolution);

  rocks.init(heights);

  auto cmdMgr = ctx.createOneShotCmdMgr();
  etna::BlockingTransferHelper transferHelper({.stagingSize = 16 * 1024 * 1024});

  transferHelper.uploadImage(
    *cmdMgr,
    heightMap,
    0,
    0,
    std::as_bytes(std::span<const float>{heights.data(), heights.size()}));

  std::vector<uint8_t> texels(64 * 64 * 4);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
    {
      float u = (x + 0.5f) / 32 - 1, v = (y + 0.5f) / 32 - 1;
      int i = (y * 64 + x) * 4;
      texels[i] = uint8_t(128 + 60 * u);
      texels[i + 1] = uint8_t(128 + 60 * v);
      texels[i + 2] = uint8_t(100 + 60 * std::sin(x * 0.4f) * std::sin(y * 0.4f));
      texels[i + 3] = uint8_t(255 * glm::clamp((1 - u * u - v * v) * 5, 0.f, 1.f));
    }
  transferHelper.uploadImage(*cmdMgr, decalTexture, 0, 0, std::as_bytes(std::span{texels}));
  const std::array<uint32_t, 4> chunkIndices{0, 1, 2, 3};
  transferHelper.uploadBuffer<uint32_t>(*cmdMgr, chunkIndexBuffer, 0, chunkIndices);
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "terrain",
    {DEFERRED_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "terrain.frag.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  std::vector formats{
    vk::Format::eR8G8B8A8Unorm, vk::Format::eR16G16Snorm, vk::Format::eR8G8B8A8Unorm};
  if (std::getenv("DEFERRED_WIDE_GBUFFER"))
    std::fill(formats.begin(), formats.end(), vk::Format::eR32G32B32A32Sfloat);
  rocks.setup(formats);
  auto& pipelineManager = etna::get_context().getPipelineManager();

  auto config = etna::GraphicsPipeline::CreateInfo{
    .inputAssemblyConfig =
      vk::PipelineInputAssemblyStateCreateInfo{
        .topology = vk::PrimitiveTopology::ePatchList,
      },
    .tessellationConfig =
      vk::PipelineTessellationStateCreateInfo{
        .patchControlPoints = 4,
      },
    .rasterizationConfig =
      vk::PipelineRasterizationStateCreateInfo{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.f,
      },
    .fragmentShaderOutput =
      {
        .colorAttachmentFormats = formats,
        .depthAttachmentFormat = vk::Format::eD32Sfloat,
      },
  };
  config.blendingConfig.attachments.resize(3, config.blendingConfig.attachments[0]);
  config.depthConfig.depthWriteEnable = false;
  config.depthConfig.depthCompareOp = vk::CompareOp::eEqual;
  terrainPipeline = pipelineManager.createGraphicsPipeline("terrain", config);
  etna::create_program(
    "terrain_depth",
    {DEFERRED_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "depth.frag.spv"});
  config.fragmentShaderOutput.colorAttachmentFormats.clear();
  config.blendingConfig.attachments.clear();
  config.depthConfig.depthWriteEnable = true;
  config.depthConfig.depthCompareOp = vk::CompareOp::eLessOrEqual;
  depthPipeline = pipelineManager.createGraphicsPipeline("terrain_depth", config);
  for (auto name : {"lighting", "decal"})
    etna::create_program(
      name,
      {DEFERRED_RENDERER_SHADERS_ROOT "screen.vert.spv",
       std::string(DEFERRED_RENDERER_SHADERS_ROOT) + name + ".frag.spv"});
  config = etna::GraphicsPipeline::CreateInfo{};
  config.depthConfig.depthTestEnable = false;
  config.depthConfig.depthWriteEnable = false;
  config.fragmentShaderOutput.colorAttachmentFormats = {swapchain_format};
  lightingPipeline = pipelineManager.createGraphicsPipeline("lighting", config);
  config.fragmentShaderOutput.colorAttachmentFormats = formats;
  config.blendingConfig.attachments.resize(3, config.blendingConfig.attachments[0]);
  decalPipeline = pipelineManager.createGraphicsPipeline("decal", config);
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;
  ImGui::Begin("Deferred");
  ImGui::SliderFloat("Decal opacity", &opacity, 0, 1);
  ImGui::SliderFloat("Decal normals", &normalStrength, 0, 1);
  ImGui::SliderFloat("Decal roughness", &roughnessStrength, 0, 1);
  ImGui::End();

  const float aspect = float(resolution.x) / float(resolution.y);
  worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
  cameraPos = packet.mainCam.position;
  rocks.update(worldViewProj, cameraPos);

  pushConstants.viewProj = worldViewProj;
  pushConstants.cameraPosAndMaxTessLevel = glm::vec4{cameraPos, kMaxTessLevel};
  pushConstants.lightDirAndHeightScale =
    glm::vec4{glm::normalize(glm::vec3{0.45f, 0.8f, 0.35f}), kHeightScale};
  pushConstants.terrainParams =
    glm::vec4{kTerrainSize, float(kHeightMapResolution), kTessellationFalloff, 0.0f};
}

void WorldRenderer::drawGeometry(vk::CommandBuffer cmd_buf, bool depth)
{
  auto programInfo = etna::get_shader_program(depth ? "terrain_depth" : "terrain");
  auto descriptorSet = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
      0, heightMap.genBinding(heightMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  auto& pipeline = depth ? depthPipeline : terrainPipeline;
  const auto pipelineLayout = pipeline.getVkPipelineLayout();
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, {descriptorSet.getVkSet()}, {});
  cmd_buf.bindIndexBuffer(chunkIndexBuffer.get(), 0, vk::IndexType::eUint32);

  const float halfSize = kChunkSize * 0.5f;
  const float terrainOrigin = -kTerrainSize * 0.5f;

  for (uint32_t z = 0; z < kChunksPerSide; ++z)
  {
    for (uint32_t x = 0; x < kChunksPerSide; ++x)
    {
      const float chunkX = terrainOrigin + (static_cast<float>(x) + 0.5f) * kChunkSize;
      const float chunkZ = terrainOrigin + (static_cast<float>(z) + 0.5f) * kChunkSize;

      pushConstants.chunkOriginAndHalfSize = glm::vec4{chunkX, 0.0f, chunkZ, halfSize};

      cmd_buf.pushConstants<PushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
          vk::ShaderStageFlagBits::eTessellationEvaluation |
          (depth ? vk::ShaderStageFlags{} : vk::ShaderStageFlagBits::eFragment),
        0,
        {pushConstants});

      cmd_buf.drawIndexed(4, 1, 0, 0, 0);
    }
  }
  rocks.draw(cmd_buf, depth);
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd, vk::Image target, vk::ImageView view)
{
  ETNA_PROFILE_GPU(cmd, deferred);
  auto& ctx = etna::get_context();
  if (timerReady)
  {
    std::array<uint64_t, 2> ticks{};
    auto result = ctx.getDevice().getQueryPoolResults(
      timer.get(),
      0,
      2,
      sizeof(ticks),
      ticks.data(),
      sizeof(uint64_t),
      vk::QueryResultFlagBits::e64);
    if (result == vk::Result::eSuccess)
    {
      double ms = double(ticks[1] - ticks[0]) *
        ctx.getPhysicalDevice().getProperties().limits.timestampPeriod / 1e6;
      if (++gpuFrames > 5)
        gpuTotal += ms;
      if (gpuFrames == 35)
        spdlog::info("Deferred GPU average (30 frames): {} ms", gpuTotal / 30);
    }
  }
  cmd.resetQueryPool(timer.get(), 0, 2);
  cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, timer.get(), 0);
  vk::Rect2D rect{{0, 0}, {resolution.x, resolution.y}};
  {
    ETNA_PROFILE_GPU(cmd, depthPrepass);
    etna::RenderTargetState rt(
      cmd, rect, {}, {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});
    drawGeometry(cmd, true);
  }
  auto attachments = [](auto& buffers) {
    std::vector<etna::RenderTargetState::AttachmentParams> result;
    for (auto& image : buffers)
      result.push_back({.image = image.get(), .view = image.getView({})});
    return result;
  };
  {
    ETNA_PROFILE_GPU(cmd, fillGbuffer);
    etna::RenderTargetState rt(
      cmd,
      rect,
      attachments(gbuffer),
      {.image = mainViewDepth.get(),
       .view = mainViewDepth.getView({}),
       .loadOp = vk::AttachmentLoadOp::eLoad});
    drawGeometry(cmd, false);
  }
  screenPush = {
    glm::inverse(worldViewProj),
    glm::vec4(cameraPos, 1),
    glm::vec4(opacity, normalStrength, roughnessStrength, 0)};
  auto screen = [&](const char* name, auto& pipeline, auto& source, auto targets) {
    std::vector<etna::Binding> bindings{
      etna::Binding{
        0, source[0].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{
        1, source[1].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{
        2, source[2].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{
        3, mainViewDepth.genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{
        4, decalTexture.genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}};
    auto set = etna::create_descriptor_set(
      etna::get_shader_program(name).getDescriptorLayoutId(0), cmd, bindings);
    etna::RenderTargetState rt(cmd, rect, targets, {});
    auto layout = pipeline.getVkPipelineLayout();
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {set.getVkSet()}, {});
    cmd.pushConstants<ScreenPush>(layout, vk::ShaderStageFlagBits::eFragment, 0, {screenPush});
    cmd.draw(3, 1, 0, 0);
  };
  {
    ETNA_PROFILE_GPU(cmd, decals);
    screen("decal", decalPipeline, gbuffer, attachments(decalBuffer));
  }
  {
    ETNA_PROFILE_GPU(cmd, lighting);
    screen(
      "lighting",
      lightingPipeline,
      decalBuffer,
      std::vector<etna::RenderTargetState::AttachmentParams>{{.image = target, .view = view}});
  }
  cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timer.get(), 1);
  timerReady = true;
}
