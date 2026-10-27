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

layout(location = 0) in TES_OUT
{
  vec3 worldPos;
  vec3 normal;
  float height;
} vIn;

layout(location = 0) out vec4 outColor;

void main()
{
  const vec3 normal = normalize(vIn.normal);
  const vec3 lightDir = normalize(pc.lightDirAndHeightScale.xyz);

  const float diffuse = max(dot(normal, lightDir), 0.0);
  const float slope = 1.0 - clamp(normal.y, 0.0, 1.0);

  const vec3 grassColor = vec3(0.20, 0.38, 0.12);
  const vec3 rockColor = vec3(0.38, 0.34, 0.28);
  const vec3 snowColor = vec3(0.88, 0.90, 0.94);

  vec3 albedo = mix(grassColor, rockColor, smoothstep(0.25, 0.65, slope));

  const float heightScale = max(pc.lightDirAndHeightScale.w, 1.0);
  albedo = mix(albedo, snowColor, smoothstep(0.62, 0.85, vIn.height / heightScale));

  const vec3 sunColor = vec3(1.0, 0.96, 0.88);
  const vec3 ambientColor = vec3(0.16, 0.20, 0.26);

  outColor = vec4(albedo * (ambientColor + sunColor * diffuse), 1.0);
}
