#version 450
layout(push_constant) uniform Push {
  vec4 sun;
  vec4 color;
  vec4 clouds;
  vec4 layer;
  ivec4 steps;
} pc;
layout(location=0) out vec4 result;

float hash(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}
float noise(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f*f*(3-2*f);
  return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),
                 mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
             mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),
                 mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float density(vec3 p) {
  float h = (p.y-pc.clouds.w)/(pc.layer.x-pc.clouds.w);
  if (h <= 0 || h >= 1 || pc.clouds.x <= 0) return 0;
  vec3 q = p * pc.clouds.z;
  float n = 0, a = 0.533333;
  for (int i=0; i<4; ++i) {
    n += a*noise(q);
    q = q*2.03 + vec3(17,31,47);
    a *= 0.5;
  }
  float shape = smoothstep(1-pc.clouds.x, 1-pc.clouds.x+0.18, n);
  return shape*smoothstep(0,0.15,h)*(1-smoothstep(0.75,1,h))*pc.clouds.y;
}
vec3 direction(vec2 p) {
  if (pc.steps.z == 0) return normalize(vec3(1,-p.y,-p.x));
  if (pc.steps.z == 1) return normalize(vec3(-1,-p.y,p.x));
  if (pc.steps.z == 2) return normalize(vec3(p.x,1,p.y));
  if (pc.steps.z == 3) return normalize(vec3(p.x,-1,-p.y));
  if (pc.steps.z == 4) return normalize(vec3(p.x,-p.y,1));
  return normalize(vec3(-p.x,-p.y,-1));
}
void main() {
  vec3 ray = direction(gl_FragCoord.xy/2048.0*2-1);
  vec3 sunDir = normalize(pc.sun.xyz);
  vec3 sunlight = pc.color.rgb*pc.sun.w;
  vec3 sky = mix(vec3(0.65,0.8,1.0), vec3(0.06,0.18,0.48),
                 clamp(ray.y*0.5+0.5,0,1));
  float theta = acos(clamp(dot(ray,sunDir),-1,1));
  sky += sunlight*(exp(-theta*theta*9000)+exp(-theta*42));
  float transmittance = 1;
  vec3 scattering = vec3(0);
  if (ray.y > 0.001) {
    float start = pc.clouds.w/ray.y;
    float finish = min(pc.layer.x/ray.y, 80000.0);
    if (finish > start) {
      float ds = (finish-start)/float(pc.steps.x);
      for (int i=0; i<pc.steps.x; ++i) {
        vec3 p = ray*(start+(float(i)+0.5)*ds);
        float d = density(p);
        if (d > 0) {
          float shadowDistance = (pc.layer.x-p.y)/sunDir.y;
          float shadowStep = shadowDistance/float(pc.steps.y);
          float opticalDepth = 0;
          for (int j=0; j<pc.steps.y; ++j)
            opticalDepth += density(p+sunDir*((float(j)+0.5)*shadowStep))*shadowStep;
          float alpha = 1-exp(-d*ds);
          vec3 illumination = vec3(0.22,0.28,0.38)+sunlight*exp(-opticalDepth)*0.65;
          scattering += transmittance*alpha*illumination;
          transmittance *= 1-alpha;
          if (transmittance < 0.001) break;
        }
      }
    }
  }
  result = vec4(scattering+transmittance*sky,transmittance);
}
