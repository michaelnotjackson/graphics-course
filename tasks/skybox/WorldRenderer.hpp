#pragma once

#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <glm/glm.hpp>
#include "Rocks.hpp"

#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"


class WorldRenderer
{
public:
  WorldRenderer() = default;

  void loadShaders();
  void invalidateSky() { skyDirty = true; }
  void resize(glm::uvec2 size);
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  static constexpr uint32_t kHeightMapResolution = 4096;
  static constexpr float kTerrainSize = 4096.0f;
  static constexpr float kChunkSize = 128.0f;
  static constexpr uint32_t kChunksPerSide = 32;
  static constexpr float kHeightScale = 140.0f;
  static constexpr float kMaxTessLevel = 64.0f;
  static constexpr float kTessellationFalloff = 400.0f;

  struct PushConstants
  {
    glm::mat4x4 viewProj;
    glm::vec4 chunkOriginAndHalfSize;
    glm::vec4 cameraPosAndMaxTessLevel;
    glm::vec4 lightDirAndHeightScale;
    glm::vec4 terrainParams;
  };

  struct SkyPush
  {
    glm::vec4 sun{0.45f, 0.8f, 0.35f, 3.0f};
    glm::vec4 color{1.0f, 0.96f, 0.88f, 0.0f};
    glm::vec4 clouds{0.55f, 0.008f, 0.002f, 600.0f};
    glm::vec4 layer{1400.0f, 0.0f, 0.0f, 0.0f};
    glm::ivec4 steps{48, 6, 0, 0};
  } skyPush;
  etna::Image skyMap;
  etna::Sampler skySampler;
  etna::GraphicsPipeline skyPipeline;
  bool skyDirty = true;
  bool skyBaked = false;
  float sunAzimuth = 52.0f, sunElevation = 55.0f;
  void bakeSky(vk::CommandBuffer cmd);
  void drawGeometry(vk::CommandBuffer cmd_buf, bool depth);
  std::array<etna::Image, 3> gbuffer;
  etna::Sampler screenSampler;
  etna::GraphicsPipeline depthPipeline, lightingPipeline;
  struct ScreenPush
  {
    glm::mat4 inverseVP;
    glm::vec4 camera;
    glm::vec4 sun;
    glm::vec4 sunColor;
  } screenPush;
  Rocks rocks;
  etna::Image mainViewDepth;
  etna::Image heightMap;
  etna::Sampler heightMapSampler;
  etna::Buffer chunkIndexBuffer;

  etna::GraphicsPipeline terrainPipeline{};

  PushConstants pushConstants{};
  glm::mat4x4 worldViewProj{1.0f};
  glm::vec3 cameraPos{0.0f};

  glm::uvec2 resolution{};
};
