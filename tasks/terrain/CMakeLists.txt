add_executable(terrain_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  HeightMap.cpp
)

target_link_libraries(terrain_renderer
  PRIVATE glfw etna glm::glm wsi)

target_add_shaders(terrain_renderer
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
)
