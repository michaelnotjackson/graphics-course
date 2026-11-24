#version 450
layout(binding = 0) uniform sampler2D hdrMap;
layout(std430, binding = 1) readonly buffer Exposure {
  float adaptedLum;
  float exposure;
} expData;
layout(binding = 2) uniform sampler3D lutMap;

layout(push_constant) uniform Push {
  uint enableTonemap;
  uint enableLut;
} pc;

layout(location = 0) out vec4 result;

vec3 aces(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c) {
  vec3 lo = c * 12.92;
  vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
  return mix(lo, hi, greaterThan(c, vec3(0.0031308)));
}

void main() {
  vec3 color = texelFetch(hdrMap, ivec2(gl_FragCoord.xy), 0).rgb;
  if (pc.enableTonemap != 0u) {
    color = aces(color * expData.exposure);
  } else {
    color = clamp(color, 0.0, 1.0);
  }
  color = linearToSrgb(color);
  if (pc.enableLut != 0u) {
    vec3 uv = (color * 31.0 + 0.5) / 32.0;
    color = texture(lutMap, uv).rgb;
  }
  result = vec4(color, 1.0);
}
