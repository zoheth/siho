#version 450

layout (location = 0) in vec3 inPos;

void main () 
{	
//    vec3 vertices[3] = vec3[](vec3(0.0, -0.5, 1.0),  
//                              vec3(-0.5, 0.5, 1.0),
//                              vec3(0.5, 0.5, 1.0)); 
//
//    gl_Position = vec4(vertices[gl_VertexIndex], 1.0);
	gl_Position = vec4(inPos, 1.0);
}