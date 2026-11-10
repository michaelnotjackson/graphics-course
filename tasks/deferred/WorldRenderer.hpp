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

  void drawGeometry(vk::CommandBuffer cmd_buf, bool depth);
  std::array<etna::Image, 3> gbuffer, decalBuffer;
  etna::Image decalTexture;
  etna::Sampler screenSampler;
  etna::GraphicsPipeline depthPipeline, lightingPipeline, decalPipeline;
  struct ScreenPush
  {
    glm::mat4 inverseVP;
    glm::vec4 camera;
    glm::vec4 settings;
  } screenPush;
  float opacity = 0.8f, normalStrength = 0.5f, roughnessStrength = 0.8f;
  vk::UniqueQueryPool timer;
  bool timerReady = false;
  double gpuTotal = 0;
  unsigned gpuFrames = 0;
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
