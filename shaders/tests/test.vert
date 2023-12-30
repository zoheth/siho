#version 450
#extension GL_KHR_vulkan_glsl : enable

layout (location = 0) in vec3 position;

layout(set = 0, binding = 1) uniform GlobalUniform {
    mat4 model;
    mat4 view_proj;
    vec3 camera_position;
} global_uniform;

layout (location = 0) out vec4 o_pos;
layout (location = 1) out vec2 o_uv;

void main () 
{	
//    vec3 vertices[3] = vec3[](vec3(0.0, -0.5, 1.0),  
//                              vec3(-0.5, 0.5, 1.0),
//                              vec3(0.5, 0.5, 1.0)); 
//
//    gl_Position = vec4(vertices[gl_VertexIndex], 1.0);
	o_pos = vec4(position, 1.0);
    o_uv = vec2(gl_VertexIndex & 1, (gl_VertexIndex>>1) & 1);
    
    gl_Position = global_uniform.view_proj * o_pos;
	//gl_Position = global_uniform.view_proj * global_uniform.model * vec4(inPos, 1.0);
}