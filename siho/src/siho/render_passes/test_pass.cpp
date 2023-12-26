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
		vertex_input_state_.bindings = {
			vkb::initializers::vertex_input_binding_description(0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX)
		};
		vertex_input_state_.attributes = {
			vkb::initializers::vertex_input_attribute_description(0,0,VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,position))
		};
		vertex_buffer_ = std::make_unique<vkb::core::Buffer>( render_context.get_device(), sizeof(Vertex) * 3, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU );

		std::vector<Vertex> vertices = {
			Vertex(glm::vec3(-0.5f, -0.5f, 1.0f)),
			Vertex(glm::vec3(0.5f, -0.5f, 1.0f)),
			Vertex(glm::vec3(0.0f, 0.5f, 1.0f))
		};
		const uint8_t* vertex_data = reinterpret_cast<const uint8_t*>(vertices.data());
		size_t vertex_data_size = vertices.size() * sizeof(Vertex);
		vertex_buffer_->update(vertex_data, vertex_data_size);
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

		// Get image views of the attachments
		/*auto& render_target = get_render_context().get_active_frame().get_render_target();
		auto& target_views = render_target.get_views();
		assert(4 < target_views.size());
		auto& light_view = target_views[4];
		command_buffer.bind_input(light_view, 0, 0, 0);*/

		vkb::RasterizationState rasterization_state;
		rasterization_state.cull_mode = VK_CULL_MODE_NONE;
		command_buffer.set_rasterization_state(rasterization_state);

		command_buffer.set_vertex_input_state(vertex_input_state_);

		std::vector<std::reference_wrapper<const vkb::core::Buffer>> buffers;
		buffers.emplace_back(std::ref(*vertex_buffer_));
		command_buffer.bind_vertex_buffers(0, std::move(buffers), { 0 });

		command_buffer.draw(3, 1, 0, 0);
	}
}
