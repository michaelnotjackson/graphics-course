#version 450
layout(binding=2) uniform sampler2D albedoMap;
layout(push_constant) uniform Push { mat4 vp; vec4 camera; } pc;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 color;
layout(location=0) out vec4 outColor;
void main() {
 const float PI=3.14159265;
 vec3 N=normalize(normal),V=normalize(pc.camera.xyz-position),L=normalize(vec3(.45,.8,.35)),H=normalize(V+L);
 vec3 albedo=texture(albedoMap,uv).rgb*color;
 float nl=max(dot(N,L),0),nv=max(dot(N,V),.001),nh=max(dot(N,H),0),vh=max(dot(V,H),0);


 float roughness=.85,a=roughness*roughness,a2=a*a,k=(roughness+1)*(roughness+1)/8;
 float D=a2/(PI*pow(nh*nh*(a2-1)+1,2));
 float G=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
 vec3 F=vec3(.04)+.96*pow(1-vh,5);
 vec3 radiance=((1-F)*albedo/PI+D*G*F/max(4*nv*nl,.001))*nl*vec3(3,2.88,2.64)+albedo*vec3(.16,.20,.26);
 outColor=vec4(radiance/(1+radiance),1);
}
