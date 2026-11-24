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

layout(location=0) out vec4 outColor;
layout(location=1) out vec2 outNormal;
layout(location=2) out vec4 outMaterial;
vec2 encode(vec3 n) {
 n /= abs(n.x)+abs(n.y)+abs(n.z);
 return n.z >= 0 ? n.xy : (1-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));
}
vec3 decode(vec2 e) {
 vec3 n=vec3(e,1-abs(e.x)-abs(e.y));
 if(n.z<0) n.xy=(1-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));
 return normalize(n);
}


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

  outColor=vec4(albedo,1);
  outNormal=encode(normal);
  outMaterial=vec4(.9,0,1,1);
}
