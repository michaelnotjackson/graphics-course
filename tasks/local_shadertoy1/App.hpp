#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>

#include "wsi/OsWindowingManager.hpp"

#include <chrono>


class App
{
public:
  App();
  ~App();

  void run();

private:
  void drawFrame();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  etna::ComputePipeline pipeline;
  etna::Image image;

  void updateInput();
  void reloadToyShader();

  struct ShaderConstants
  {
    float resolutionTime[4]{};
    float mouse[4]{};
  };

  static_assert(sizeof(ShaderConstants) == 32);

  ShaderConstants shaderConstants{};

  std::chrono::steady_clock::time_point startTime{};
  bool reloadWasDown = false;
};
