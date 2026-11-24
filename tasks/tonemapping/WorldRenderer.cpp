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
#include "Lut.hpp"


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

  hdrImage = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = {resolution.x, resolution.y, 1},
      .name = "hdr_image",
      .format = vk::Format::eB10G11R11UfloatPack32,
      .imageUsage =
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
    });
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

  lutSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eClampToEdge,
      .name = "lut_sampler"});

  lutImage = buildColorLut3D(
    TONEMAPPING_ASSETS_ROOT "lut_source.png", TONEMAPPING_ASSETS_ROOT "lut_graded.png");

  statsBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = 2 * sizeof(uint32_t),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .name = "luminance_stats",
    });

  histogramBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = kHistogramBins * sizeof(uint32_t),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .name = "luminance_histogram",
    });

  exposureBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = 2 * sizeof(float),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .name = "exposure_state",
    });

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

  const std::array<float, 2> initialExposure{0.0f, 1.0f};
  transferHelper.uploadBuffer<float>(*cmdMgr, exposureBuffer, 0, initialExposure);
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "terrain",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.frag.spv"});
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
    {TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "depth.frag.spv"});
  config.fragmentShaderOutput.colorAttachmentFormats.clear();
  config.blendingConfig.attachments.clear();
  config.depthConfig.depthWriteEnable = true;
  config.depthConfig.depthCompareOp = vk::CompareOp::eLessOrEqual;
  depthPipeline = pipelineManager.createGraphicsPipeline("terrain_depth", config);

  etna::create_program(
    "lighting",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "screen.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "lighting.frag.spv"});
  config = etna::GraphicsPipeline::CreateInfo{};
  config.depthConfig.depthTestEnable = false;
  config.depthConfig.depthWriteEnable = false;
  config.fragmentShaderOutput.colorAttachmentFormats = {vk::Format::eB10G11R11UfloatPack32};
  lightingPipeline = pipelineManager.createGraphicsPipeline("lighting", config);

  etna::create_program(
    "sky",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "screen.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "sky.frag.spv"});
  auto skyConfig = config;
  skyConfig.fragmentShaderOutput.colorAttachmentFormats = {vk::Format::eR16G16B16A16Sfloat};
  skyPipeline = pipelineManager.createGraphicsPipeline("sky", skyConfig);

  etna::create_program(
    "postprocess",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "screen.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "postprocess.frag.spv"});
  auto postConfig = config;
  postConfig.fragmentShaderOutput.colorAttachmentFormats = {swapchain_format};
  postprocessPipeline = pipelineManager.createGraphicsPipeline("postprocess", postConfig);

  etna::create_program("minmax_shared", {TONEMAPPING_RENDERER_SHADERS_ROOT "minmax_shared.comp.spv"});
  etna::create_program("minmax_global", {TONEMAPPING_RENDERER_SHADERS_ROOT "minmax_global.comp.spv"});
  etna::create_program("histogram_shared", {TONEMAPPING_RENDERER_SHADERS_ROOT "histogram_shared.comp.spv"});
  etna::create_program("histogram_global", {TONEMAPPING_RENDERER_SHADERS_ROOT "histogram_global.comp.spv"});
  etna::create_program("exposure", {TONEMAPPING_RENDERER_SHADERS_ROOT "exposure.comp.spv"});

  minMaxSharedPipeline = pipelineManager.createComputePipeline("minmax_shared", {});
  minMaxGlobalPipeline = pipelineManager.createComputePipeline("minmax_global", {});
  histogramSharedPipeline = pipelineManager.createComputePipeline("histogram_shared", {});
  histogramGlobalPipeline = pipelineManager.createComputePipeline("histogram_global", {});
  exposurePipeline = pipelineManager.createComputePipeline("exposure", {});
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;
  frameDt = packet.deltaTime;

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
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

  ImGui::SetNextWindowPos(ImVec2(10, 320), ImGuiCond_FirstUseEver);
  ImGui::Begin("Post-processing");
  ImGui::Checkbox("Tonemapping", &enableTonemap);
  ImGui::Checkbox("Color grading (3D LUT)", &enableLut);
  ImGui::Checkbox("Use shared memory", &useSharedMemory);
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
  {
    ETNA_PROFILE_GPU(cmd, lighting);
    std::vector<etna::Binding> bindings{
      {0, gbuffer[0].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      {1, gbuffer[1].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      {2, gbuffer[2].genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      {3, mainViewDepth.genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      {4, skyMap.genBinding(skySampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal,
                           {.type = vk::ImageViewType::eCube})}};
    auto set = etna::create_descriptor_set(
      etna::get_shader_program("lighting").getDescriptorLayoutId(0), cmd, bindings);
    etna::RenderTargetState rt(
      cmd, rect, {{.image = hdrImage.get(), .view = hdrImage.getView({})}}, {});
    auto layout = lightingPipeline.getVkPipelineLayout();
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, lightingPipeline.getVkPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {set.getVkSet()}, {});
    cmd.pushConstants<ScreenPush>(layout, vk::ShaderStageFlagBits::eFragment, 0, {screenPush});
    cmd.draw(3, 1, 0, 0);
  }

  if (enableTonemap)
  {
    const uint32_t groupsX = (resolution.x + 15u) / 16u;
    const uint32_t groupsY = (resolution.y + 15u) / 16u;

    etna::set_state(
      cmd,
      statsBuffer.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite,
      ForceSetState::eTrue);
    etna::set_state(
      cmd,
      histogramBuffer.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite,
      ForceSetState::eTrue);
    etna::flush_barriers(cmd);

    cmd.fillBuffer(statsBuffer.get(), 0, sizeof(uint32_t), 0x7F7FFFFFu);
    cmd.fillBuffer(statsBuffer.get(), sizeof(uint32_t), sizeof(uint32_t), 0u);
    cmd.fillBuffer(histogramBuffer.get(), 0, kHistogramBins * sizeof(uint32_t), 0u);

    {
      ETNA_PROFILE_GPU(cmd, lumMinMax);
      const char* progName = useSharedMemory ? "minmax_shared" : "minmax_global";
      auto& pipe = useSharedMemory ? minMaxSharedPipeline : minMaxGlobalPipeline;
      auto set = etna::create_descriptor_set(
        etna::get_shader_program(progName).getDescriptorLayoutId(0),
        cmd,
        {
          {0, hdrImage.genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
          {1, statsBuffer.genBinding()},
        });
      etna::set_state(
        cmd,
        statsBuffer.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        ForceSetState::eTrue);
      etna::flush_barriers(cmd);

      cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipe.getVkPipeline());
      cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, pipe.getVkPipelineLayout(), 0, {set.getVkSet()}, {});
      cmd.dispatch(groupsX, groupsY, 1);
    }

    {
      ETNA_PROFILE_GPU(cmd, lumHistogram);
      const char* progName = useSharedMemory ? "histogram_shared" : "histogram_global";
      auto& pipe = useSharedMemory ? histogramSharedPipeline : histogramGlobalPipeline;
      auto set = etna::create_descriptor_set(
        etna::get_shader_program(progName).getDescriptorLayoutId(0),
        cmd,
        {
          {0, hdrImage.genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
          {1, statsBuffer.genBinding()},
          {2, histogramBuffer.genBinding()},
        });
      etna::set_state(
        cmd,
        statsBuffer.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead,
        ForceSetState::eTrue);
      etna::set_state(
        cmd,
        histogramBuffer.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        ForceSetState::eTrue);
      etna::flush_barriers(cmd);

      cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipe.getVkPipeline());
      cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, pipe.getVkPipelineLayout(), 0, {set.getVkSet()}, {});
      cmd.dispatch(groupsX, groupsY, 1);
    }

    {
      ETNA_PROFILE_GPU(cmd, lumExposure);
      auto set = etna::create_descriptor_set(
        etna::get_shader_program("exposure").getDescriptorLayoutId(0),
        cmd,
        {
          {0, statsBuffer.genBinding()},
          {1, histogramBuffer.genBinding()},
          {2, exposureBuffer.genBinding()},
        });
      etna::set_state(
        cmd,
        histogramBuffer.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead,
        ForceSetState::eTrue);
      etna::set_state(
        cmd,
        exposureBuffer.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        ForceSetState::eTrue);
      etna::flush_barriers(cmd);

      const ExposurePush expPush{frameDt, resolution.x * resolution.y};
      auto layout = exposurePipeline.getVkPipelineLayout();
      cmd.bindPipeline(vk::PipelineBindPoint::eCompute, exposurePipeline.getVkPipeline());
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 0, {set.getVkSet()}, {});
      cmd.pushConstants<ExposurePush>(layout, vk::ShaderStageFlagBits::eCompute, 0, {expPush});
      cmd.dispatch(1, 1, 1);
    }
  }

  {
    ETNA_PROFILE_GPU(cmd, postprocess);
    auto set = etna::create_descriptor_set(
      etna::get_shader_program("postprocess").getDescriptorLayoutId(0),
      cmd,
      {
        {0, hdrImage.genBinding(screenSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        {1, exposureBuffer.genBinding()},
        {2, lutImage.genBinding(lutSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      });
    etna::set_state(
      cmd,
      exposureBuffer.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderStorageRead,
      ForceSetState::eTrue);
    etna::RenderTargetState rt(cmd, rect, {{.image = target, .view = view}}, {});
    const PostPush postPush{enableTonemap ? 1u : 0u, enableLut ? 1u : 0u};
    auto layout = postprocessPipeline.getVkPipelineLayout();
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, postprocessPipeline.getVkPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {set.getVkSet()}, {});
    cmd.pushConstants<PostPush>(layout, vk::ShaderStageFlagBits::eFragment, 0, {postPush});
    cmd.draw(3, 1, 0, 0);
  }
}
