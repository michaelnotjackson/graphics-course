add_executable(instancing_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  HeightMap.cpp
  Rocks.cpp
)

target_link_libraries(instancing_renderer
  PRIVATE glfw etna glm::glm wsi gui tinygltf)

target_add_shaders(instancing_renderer
  shaders/rocks.vert
  shaders/rocks.frag
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
)

target_compile_definitions(instancing_renderer PRIVATE ROCK_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
