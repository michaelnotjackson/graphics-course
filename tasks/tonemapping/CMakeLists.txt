add_executable(tonemapping_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  HeightMap.cpp
  Rocks.cpp
  Lut.cpp
)

target_link_libraries(tonemapping_renderer
  PRIVATE glfw etna glm::glm wsi gui tinygltf)

target_compile_definitions(tonemapping_renderer
  PRIVATE TONEMAPPING_ASSETS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets/")

target_add_shaders(tonemapping_renderer
  shaders/sky.frag
  shaders/screen.vert
  shaders/depth.frag
  shaders/lighting.frag
  shaders/rocks.vert
  shaders/rocks.frag
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
  shaders/minmax_shared.comp
  shaders/minmax_global.comp
  shaders/histogram_shared.comp
  shaders/histogram_global.comp
  shaders/exposure.comp
  shaders/postprocess.frag
)
