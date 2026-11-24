#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform push_constants
{
  mat4 viewProj;
  vec4 chunkOriginAndHalfSize;
  vec4 cameraPosAndMaxTessLevel;
  vec4 lightDirAndHeightScale;
  vec4 terrainParams;
} pc;

layout(location = 0) out vec2 vLocalPos;

void main()
{
  const vec2 corners[4] =
    vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

  vLocalPos = corners[gl_VertexIndex];
  gl_Position = vec4(vLocalPos, 0.0, 1.0);
}
