#version 450
layout(binding=2) uniform sampler2D albedoMap;
layout(push_constant) uniform Push { mat4 vp; vec4 camera; } pc;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 color;
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
void main() {
 outColor=vec4(texture(albedoMap,uv).rgb*color,1);
 outNormal=encode(normalize(normal));
 outMaterial=vec4(.85,0,1,1);
}
