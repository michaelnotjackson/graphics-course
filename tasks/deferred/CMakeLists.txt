add_executable(deferred_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  HeightMap.cpp
  Rocks.cpp
)

target_link_libraries(deferred_renderer
  PRIVATE glfw etna glm::glm wsi gui tinygltf)

target_add_shaders(deferred_renderer
  shaders/screen.vert
  shaders/depth.frag
  shaders/lighting.frag
  shaders/decal.frag
  shaders/rocks.vert
  shaders/rocks.frag
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
)

target_compile_definitions(deferred_renderer PRIVATE ROCK_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/../instancing/assets/")
