#include "WorldRenderer.hpp"

#include <array>
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

  mainViewDepth = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "terrain_depth",
      .format = vk::Format::eD32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
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
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "terrain",
    {INSTANCING_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     INSTANCING_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     INSTANCING_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     INSTANCING_RENDERER_SHADERS_ROOT "terrain.frag.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  rocks.setup(swapchain_format);
  auto& pipelineManager = etna::get_context().getPipelineManager();

  terrainPipeline = pipelineManager.createGraphicsPipeline(
    "terrain",
    etna::GraphicsPipeline::CreateInfo{
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
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

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

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);


  const vk::MemoryBarrier2 uploadBarrier{
    .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
    .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
    .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eIndexInput,
    .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eIndexRead,
  };
  cmd_buf.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &uploadBarrier});

  auto programInfo = etna::get_shader_program("terrain");
  auto descriptorSet = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
      0, heightMap.genBinding(heightMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  etna::RenderTargetState renderTargets(
    cmd_buf,
    {{0, 0}, {resolution.x, resolution.y}},
    {{.image = target_image,
      .view = target_image_view,
      .clearColorValue = std::array<float, 4>{0.45f, 0.62f, 0.85f, 1.0f}}},
    {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

  const auto pipelineLayout = terrainPipeline.getVkPipelineLayout();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());
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
          vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eFragment,
        0,
        {pushConstants});

      cmd_buf.drawIndexed(4, 1, 0, 0, 0);
    }
  }
  rocks.draw(cmd_buf);
}
