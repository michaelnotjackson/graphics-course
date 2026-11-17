add_executable(skybox_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  HeightMap.cpp
  Rocks.cpp
)

target_link_libraries(skybox_renderer
  PRIVATE glfw etna glm::glm wsi gui)

target_add_shaders(skybox_renderer
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
)

