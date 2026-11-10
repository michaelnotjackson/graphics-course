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

layout(vertices = 4) out;

layout(location = 0) in vec2 vLocalPos[];
layout(location = 0) out vec2 tcLocalPos[];

float edgeTessLevel(vec3 a, vec3 b)
{
  const float falloff = pc.terrainParams.z;
  const float maxLevel = pc.cameraPosAndMaxTessLevel.w;

  const float dist = distance(pc.cameraPosAndMaxTessLevel.xyz, 0.5 * (a + b));
  const float target = maxLevel * falloff * falloff / (falloff * falloff + dist * dist);

  return clamp(floor(target + 0.5), 1.0, maxLevel);
}

void main()
{
  tcLocalPos[gl_InvocationID] = vLocalPos[gl_InvocationID];

  if (gl_InvocationID == 0)
  {
    const float halfSize = pc.chunkOriginAndHalfSize.w;
    const vec2 origin = pc.chunkOriginAndHalfSize.xz;

    vec3 corners[4];
    for (int i = 0; i < 4; ++i)
    {
      corners[i] = vec3(
        origin.x + vLocalPos[i].x * halfSize,
        0.0,
        origin.y + vLocalPos[i].y * halfSize);
    }

    const float left = edgeTessLevel(corners[0], corners[3]);
    const float bottom = edgeTessLevel(corners[0], corners[1]);
    const float right = edgeTessLevel(corners[1], corners[2]);
    const float top = edgeTessLevel(corners[2], corners[3]);

    gl_TessLevelOuter[0] = left;
    gl_TessLevelOuter[1] = bottom;
    gl_TessLevelOuter[2] = right;
    gl_TessLevelOuter[3] = top;

    const float inner = max(max(left, bottom), max(right, top));
    gl_TessLevelInner[0] = inner;
    gl_TessLevelInner[1] = inner;
  }
}
