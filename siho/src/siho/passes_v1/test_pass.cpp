#include "test_pass.h"

#include <common/vk_initializers.h>


namespace siho
{
	TestSubpass::TestSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera) : Subpass(render_context, std::move(vertex_shader), std::move(fragment_shader)),
		camera_(camera)
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
		vertex_buffer_ = std::make_unique<vkb::core::Buffer>(render_context.get_device(), sizeof(Vertex) * 3, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		std::vector<Vertex> vertices = {
			Vertex(glm::vec3(-400.5f, 8.5f, -225.0f)),
			Vertex(glm::vec3(-400.5f, 108.5f, -225.0f)),
			Vertex(glm::vec3(-400.0f, 8.5f, 140.0f))
		};
		const uint8_t* vertex_data = reinterpret_cast<const uint8_t*>(vertices.data());
		size_t vertex_data_size = vertices.size() * sizeof(Vertex);
		vertex_buffer_->update(vertex_data, vertex_data_size);

		texture_ = load_texture("textures/particle_rgba.ktx", vkb::sg::Image::Color, render_context.get_device());
	}

	void TestSubpass::draw(vkb::CommandBuffer& command_buffer)
	{
		update_uniform(command_buffer);
		// Get shaders from cache
		auto& resource_cache = command_buffer.get_device().get_resource_cache();
		auto& vert_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
		auto& frag_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());

		std::vector<vkb::ShaderModule*> shader_modules{ &vert_shader_module, &frag_shader_module };

		// Create pipeline layout and bind it
		// The construction of the PipelineLayout class requires a ShaderModule.
		// The ShaderModule is capable of automatically parsing the shader code to extract information about certain resources.
		// Utilizing this information, the PipelineLayout can automatically generate the DescriptorSetLayout.
		// Note that, currently, this process only considers the first descriptor set (set 0) in Vulkan.
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

		vkb::DescriptorSetLayout& descriptor_set_layout = pipeline_layout.get_descriptor_set_layout(0);
		if (auto layout_bingding = descriptor_set_layout.get_layout_binding("test_texture"))
		{
			command_buffer.bind_image(texture_.image->get_vk_image_view(),
				*texture_.sampler,
				0, 0, 0);
		}

		command_buffer.draw(3, 1, 0, 0);
	}

	void TestSubpass::update_uniform(vkb::CommandBuffer& command_buffer)
	{
		GlobalUniform global_uniform{};
		
		global_uniform.camera_view_proj = camera_.get_pre_rotation() * vkb::vulkan_style_projection(camera_.get_projection()) * camera_.get_view();

		auto& render_frame = get_render_context().get_active_frame();

		auto allocation = render_frame.allocate_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(GlobalUniform));

		// Set the camera's position in world coordinates by inverting the view matrix and extracting its translation component
		global_uniform.camera_position = glm::vec3(glm::inverse(camera_.get_view())[3]);

		global_uniform.model = glm::mat4(1.0f);

		allocation.update(global_uniform);

		command_buffer.bind_buffer(allocation.get_buffer(), allocation.get_offset(), allocation.get_size(), 0, 1, 0);
	}
}
