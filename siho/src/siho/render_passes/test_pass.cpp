#include "test_pass.h"

#include <common/vk_initializers.h>

namespace siho
{
	TestSubpass::TestSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader) : Subpass(render_context, std::move(vertex_shader), std::move(fragment_shader))
	{
	}

	void TestSubpass::prepare()
	{
		auto& resource_cache = render_context.get_device().get_resource_cache();
		resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
		resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());
	}

	void TestSubpass::draw(vkb::CommandBuffer& command_buffer)
	{
		// Get shaders from cache
		auto& resource_cache = command_buffer.get_device().get_resource_cache();
		auto& vert_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
		auto& frag_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());

		std::vector<vkb::ShaderModule*> shader_modules{ &vert_shader_module, &frag_shader_module };

		// Create pipeline layout and bind it
		auto& pipeline_layout = resource_cache.request_pipeline_layout(shader_modules);
		command_buffer.bind_pipeline_layout(pipeline_layout);

		vkb::RasterizationState rasterization_state;
		rasterization_state.cull_mode = VK_CULL_MODE_FRONT_BIT;
		command_buffer.set_rasterization_state(rasterization_state);

		vkb::VertexInputState vertex_input_state;

		vertex_input_state.bindings = {
			vkb::initializers::vertex_input_binding_description(0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX)
		};
		vertex_input_state.attributes = {
			vkb::initializers::vertex_input_attribute_description(0,0,VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex,position))
		};
		vkb::core::Buffer vertex_buffer = vkb::core::Buffer{ command_buffer.get_device(), sizeof(Vertex) * 3, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU };

		std::vector<Vertex> vertices = {
			Vertex(glm::vec3(-0.5f, -0.5f, 0.0f)),
			Vertex(glm::vec3(0.5f, -0.5f, 0.0f)),
			Vertex(glm::vec3(0.0f, 0.5f, 0.0f))
		};
		vertex_buffer.convert_and_update(vertices);

		command_buffer.set_vertex_input_state(vertex_input_state);

		std::vector<std::reference_wrapper<const vkb::core::Buffer>> buffers;
		buffers.emplace_back(std::ref(vertex_buffer));
		command_buffer.bind_vertex_buffers(0, std::move(buffers), { 0 });

		command_buffer.draw(3, 1, 0, 0);
	}
}
