#version 450
struct Vertex { vec4 position; vec4 normal; vec4 uv; };
struct Instance { mat4 model; vec4 color; };
layout(std430,binding=0) readonly buffer Vertices { Vertex vertices[]; };
layout(std430,binding=1) readonly buffer Instances { Instance instances[]; };
layout(push_constant) uniform Push { mat4 vp; vec4 camera; } pc;
layout(location=0) out vec3 position;
layout(location=1) out vec3 normal;
layout(location=2) out vec2 uv;
layout(location=3) out vec3 color;
void main() {
 Vertex v=vertices[gl_VertexIndex];Instance i=instances[gl_InstanceIndex];
 position=(i.model*v.position).xyz;normal=normalize(mat3(i.model)*v.normal.xyz);uv=v.uv.xy;color=i.color.rgb;gl_Position=pc.vp*vec4(position,1);
}
