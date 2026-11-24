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

layout(quads, equal_spacing, ccw) in;

layout(location = 0) in vec2 tcLocalPos[];

layout(location = 0) out TES_OUT
{
  vec3 worldPos;
  vec3 normal;
  float height;
} vOut;

layout(binding = 0) uniform sampler2D heightMap;

void main()
{
  const vec2 uv = gl_TessCoord.xy;

  const vec2 localPos = mix(
    mix(tcLocalPos[0], tcLocalPos[1], uv.x),
    mix(tcLocalPos[3], tcLocalPos[2], uv.x),
    uv.y);

  const float halfSize = pc.chunkOriginAndHalfSize.w;
  const vec2 worldXZ = pc.chunkOriginAndHalfSize.xz + localPos * halfSize;

  const float terrainSize = pc.terrainParams.x;
  const float heightScale = pc.lightDirAndHeightScale.w;

  const vec2 texCoord = (worldXZ + vec2(terrainSize * 0.5)) / terrainSize;
  const vec2 texelSize = 1.0 / vec2(textureSize(heightMap, 0));

  const float height = textureLod(heightMap, texCoord, 0.0).r;

  const float heightLeft = textureLod(heightMap, texCoord - vec2(texelSize.x, 0.0), 0.0).r;
  const float heightRight = textureLod(heightMap, texCoord + vec2(texelSize.x, 0.0), 0.0).r;
  const float heightDown = textureLod(heightMap, texCoord - vec2(0.0, texelSize.y), 0.0).r;
  const float heightUp = textureLod(heightMap, texCoord + vec2(0.0, texelSize.y), 0.0).r;

  const float stepWorld = 2.0 * terrainSize * texelSize.x;
  const float dHeightX = (heightRight - heightLeft) * heightScale;
  const float dHeightZ = (heightUp - heightDown) * heightScale;

  vOut.normal = normalize(vec3(-dHeightX / stepWorld, 1.0, -dHeightZ / stepWorld));
  vOut.worldPos = vec3(worldXZ.x, height * heightScale, worldXZ.y);
  vOut.height = height * heightScale;

  gl_Position = pc.viewProj * vec4(vOut.worldPos, 1.0);
}
