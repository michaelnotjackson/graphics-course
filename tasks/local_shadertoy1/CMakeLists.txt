
add_executable(local_shadertoy1
  main.cpp
  App.cpp
)

include(FetchContent)

FetchContent_Declare(
  etna
  GIT_REPOSITORY https://github.com/alexandrShcherbakov/etna.git
  GIT_TAG v1.10.0
)

FetchContent_MakeAvailable(etna)

target_link_libraries(local_shadertoy1
  PRIVATE glfw etna glm::glm wsi gui)

target_compile_definitions(local_shadertoy1
  PRIVATE LOCAL_SHADERTOY1_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/shaders/")

target_add_shaders(local_shadertoy1
  shaders/toy.comp
)
