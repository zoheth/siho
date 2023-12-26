#version 450
#extension GL_KHR_vulkan_glsl : enable

layout (location = 0) out vec4 outFragColor;

//layout(set = 0 ,input_attachment_index = 0, binding = 0) uniform subpassInput i_light;

void main () 
{
	//vec4 light = subpassLoad(i_light);
	outFragColor = vec4(1.0, 1.0,1.0,0.5);
}
