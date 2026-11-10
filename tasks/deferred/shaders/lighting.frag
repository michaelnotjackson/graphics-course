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
layout(location=0) out vec4 result;
void main() {
 vec2 uv=gl_FragCoord.xy/vec2(textureSize(depthMap,0));
 float depth=texture(depthMap,uv).r;
 if(depth>=1) {result=vec4(.45,.62,.85,1); return;}
 vec3 albedo=texture(albedoMap,uv).rgb, material=texture(materialMap,uv).rgb;
 vec3 N=decode(texture(normalMap,uv).rg), V=normalize(pc.camera.xyz-worldPosition(uv,depth));
 vec3 L=normalize(vec3(.45,.8,.35)), H=normalize(V+L);
 float nl=max(dot(N,L),0),nv=max(dot(N,V),.001),nh=max(dot(N,H),0),vh=max(dot(V,H),0);
 float r=max(material.r,.045),a=r*r,a2=a*a,k=(r+1)*(r+1)/8;
 float D=a2/(3.14159265*pow(nh*nh*(a2-1)+1,2));
 float G=nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
 vec3 f0=mix(vec3(.04),albedo,material.g),F=f0+(1-f0)*pow(1-vh,5);
 vec3 radiance=((1-F)*(1-material.g)*albedo/3.14159265+D*G*F/max(4*nv*nl,.001))*nl*vec3(3,2.88,2.64);
 radiance+=albedo*vec3(.16,.20,.26)*material.b;
 result=vec4(radiance/(1+radiance),1);
}
