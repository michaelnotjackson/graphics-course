#version 450
layout(binding=0) uniform sampler2D albedoMap;
layout(binding=1) uniform sampler2D normalMap;
layout(binding=2) uniform sampler2D materialMap;
layout(binding=3) uniform sampler2D depthMap;
layout(binding=4) uniform sampler2D decalMap;
layout(push_constant) uniform Push {mat4 inverseVP; vec4 camera; vec4 settings;} pc;
vec2 encode(vec3 n) {
 n /= abs(n.x)+abs(n.y)+abs(n.z);
 return n.z >= 0 ? n.xy : (1-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));
}
vec3 decode(vec2 e) {
 vec3 n=vec3(e,1-abs(e.x)-abs(e.y));
 if(n.z<0) n.xy=(1-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));
 return normalize(n);
}
vec3 worldPosition(vec2 uv,float d) {
 vec4 p=pc.inverseVP*vec4(uv*2-1,d,1);
 return p.xyz/p.w;
}
layout(location=0) out vec4 albedo;
layout(location=1) out vec2 normal;
layout(location=2) out vec4 material;
void main() {
 vec2 uv=gl_FragCoord.xy/vec2(textureSize(depthMap,0));
 albedo=texture(albedoMap,uv); normal=texture(normalMap,uv).rg; material=texture(materialMap,uv);
 float depth=texture(depthMap,uv).r;
 if(depth>=1) return;
 vec3 world=worldPosition(uv,depth), n=decode(normal);
 for(int i=0;i<4;i++) {
   vec3 center=vec3(float(i%2)*100-50,70,float(i/2)*100-50);
   vec3 local=(world-center)/vec3(40,100,40);
   if(any(greaterThan(abs(local),vec3(1)))) continue;
   vec4 texel=texture(decalMap,local.xz*.5+.5);
   float alpha=texel.a*pc.settings.x;
   albedo.rgb=mix(albedo.rgb,vec3(.25,.08,.025)*(.6+texel.b),alpha);
   vec2 tilt=(texel.rg*2-1)*pc.settings.y;
   n=normalize(mix(n,normalize(n+vec3(tilt.x,0,tilt.y)),alpha));
   material.r=mix(material.r,texel.b,alpha*pc.settings.z);
 }
 normal=encode(n);
}
