#include "WorldRenderer.hpp"

#include <array>
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


void WorldRenderer::resize(glm::uvec2 size)
{
  resolution = size;
  auto& ctx = etna::get_context();
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
  for (int i = 0; i < 3; ++i)
    gbuffer[i] = ctx.createImage(
        etna::Image::CreateInfo{
          .extent = {resolution.x, resolution.y, 1},
          .name = "gbuffer",
          .format = formats[i],
          .imageUsage =
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
}
 
void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();
  skyMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = {2048, 2048, 1},
    .name = "sky_cubemap",
    .format = vk::Format::eR16G16B16A16Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
    .layers = 6,
    .flags = vk::ImageCreateFlagBits::eCubeCompatible});
  skySampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToEdge,
    .name = "sky_sampler"});

  resize(swapchain_resolution);
  screenSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eNearest,
      .addressMode = vk::SamplerAddressMode::eClampToEdge,
      .name = "gbuffer_sampler"});
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

  const std::array<uint32_t, 4> chunkIndices{0, 1, 2, 3};
  transferHelper.uploadBuffer<uint32_t>(*cmdMgr, chunkIndexBuffer, 0, chunkIndices);
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "terrain",
    {SKYBOX_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "terrain.frag.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  std::vector formats{
    vk::Format::eR8G8B8A8Unorm, vk::Format::eR16G16Snorm, vk::Format::eR8G8B8A8Unorm};
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
    {SKYBOX_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "depth.frag.spv"});
  config.fragmentShaderOutput.colorAttachmentFormats.clear();
  config.blendingConfig.attachments.clear();
  config.depthConfig.depthWriteEnable = true;
  config.depthConfig.depthCompareOp = vk::CompareOp::eLessOrEqual;
  depthPipeline = pipelineManager.createGraphicsPipeline("terrain_depth", config);
  etna::create_program("lighting",
    {SKYBOX_RENDERER_SHADERS_ROOT "screen.vert.spv",
     SKYBOX_RENDERER_SHADERS_ROOT "lighting.frag.spv"});
  config = etna::GraphicsPipeline::CreateInfo{};
  config.depthConfig.depthTestEnable = false;
  config.depthConfig.depthWriteEnable = false;
  config.fragmentShaderOutput.colorAttachmentFormats = {swapchain_format};
  etna::create_program("sky", {SKYBOX_RENDERER_SHADERS_ROOT "screen.vert.spv",
                               SKYBOX_RENDERER_SHADERS_ROOT "sky.frag.spv"});
  auto skyConfig = config;
  skyConfig.fragmentShaderOutput.colorAttachmentFormats = {vk::Format::eR16G16B16A16Sfloat};
  skyPipeline = pipelineManager.createGraphicsPipeline("sky", skyConfig);
  lightingPipeline = pipelineManager.createGraphicsPipeline("lighting", config);

}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;
  ImGui::SetNextWindowSizeConstraints(ImVec2(380, 300), ImVec2(1000, 1000));
  ImGui::Begin("Sky");
  ImGui::PushItemWidth(200);
  skyDirty |= ImGui::SliderFloat("Sun azimuth", &sunAzimuth, -180, 180);
  skyDirty |= ImGui::SliderFloat("Sun elevation", &sunElevation, 1, 89);
  skyDirty |= ImGui::ColorEdit3("Sun color", &skyPush.color.x);
  skyDirty |= ImGui::SliderFloat("Sun intensity", &skyPush.sun.w, 0, 20);
  skyDirty |= ImGui::SliderFloat("Cloud coverage", &skyPush.clouds.x, 0, 1);
  skyDirty |= ImGui::SliderFloat("Cloud density", &skyPush.clouds.y, 0, 0.04f);
  skyDirty |= ImGui::SliderFloat("Cloud scale", &skyPush.clouds.z, 0.0002f, 0.01f, "%.4f");
  skyDirty |= ImGui::SliderFloat("Layer bottom", &skyPush.clouds.w, 100, 2000);
  skyDirty |= ImGui::SliderFloat("Layer top", &skyPush.layer.x, 200, 4000);
  skyPush.layer.x = std::max(skyPush.layer.x, skyPush.clouds.w + 50);
  skyDirty |= ImGui::SliderInt("Ray steps", &skyPush.steps.x, 8, 128);
  skyDirty |= ImGui::SliderInt("Shadow steps", &skyPush.steps.y, 1, 16);
  ImGui::PopItemWidth();
  ImGui::End();
  float az = glm::radians(sunAzimuth), el = glm::radians(sunElevation);
  skyPush.sun = glm::vec4(std::cos(el)*std::cos(az), std::sin(el),
                         std::cos(el)*std::sin(az), skyPush.sun.w);

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

void WorldRenderer::bakeSky(vk::CommandBuffer cmd)
{
  if (!skyDirty || (skyBaked && ImGui::IsAnyItemActive()))
    return;
  for (uint32_t face = 0; face < 6; ++face)
  {
    etna::RenderTargetState rt(cmd, {{0, 0}, {2048, 2048}},
      {{.image = skyMap.get(),
        .view = skyMap.getView({.baseLayer = face, .layerCount = 1,
                               .type = vk::ImageViewType::e2D})}}, {});
    skyPush.steps.z = int(face);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, skyPipeline.getVkPipeline());
    cmd.pushConstants<SkyPush>(skyPipeline.getVkPipelineLayout(),
      vk::ShaderStageFlagBits::eFragment, 0, {skyPush});
    cmd.draw(3, 1, 0, 0);
  }
  skyDirty = false;
  skyBaked = true;
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd, vk::Image target, vk::ImageView view)
{
  bakeSky(cmd);
  ETNA_PROFILE_GPU(cmd, deferred);
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
    skyPush.sun, skyPush.color};
  std::vector<etna::Binding> bindings{
    {0, gbuffer[0].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    {1, gbuffer[1].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    {2, gbuffer[2].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    {3, mainViewDepth.genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    {4, skyMap.genBinding(skySampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal,
                         {.type = vk::ImageViewType::eCube})}};
  auto set = etna::create_descriptor_set(
    etna::get_shader_program("lighting").getDescriptorLayoutId(0), cmd, bindings);
  etna::RenderTargetState rt(cmd, rect, {{.image = target, .view = view}}, {});
  auto layout = lightingPipeline.getVkPipelineLayout();
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, lightingPipeline.getVkPipeline());
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {set.getVkSet()}, {});
  cmd.pushConstants<ScreenPush>(layout, vk::ShaderStageFlagBits::eFragment, 0, {screenPush});
  cmd.draw(3, 1, 0, 0);
}
