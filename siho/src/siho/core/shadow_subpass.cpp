#include "shadow_subpass.h"

#include "scene_graph/components/orthographic_camera.h"
#include "rendering/subpass.h"

namespace 
{
	float calculate_cascade_split_depth(unsigned int cascade_index, unsigned int total_cascades, const vkb::sg::PerspectiveCamera& camera, float lambda = 0.5) {
		float n = camera.get_near_plane();
		float f = camera.get_far_plane();
		float i = static_cast<float>(cascade_index);
		float N = static_cast<float>(total_cascades);

		// Calculate the logarithmic and linear depth
		float c_log = n * std::pow((f / n), i / N);
		float c_lin = n + (i / N) * (f - n);

		// Interpolate between logarithmic and linear depth using lambda
		float c = lambda * c_log + (1 - lambda) * c_lin;

		// Convert view space depth to clip space depth for Vulkan's [0, 1] range.
		// The near and far planes are inverted in the projection matrix for precision reasons,
		// so we invert them here as well to match that convention.
		return n / (n - f) -
			( f * n) / ((n - f) * c);
	}
}

namespace siho
{
	ShadowSubpass::ShadowSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_source,
		vkb::ShaderSource&& fragment_source, vkb::sg::Scene& scene, vkb::sg::Camera& camera)
		:vkb::GeometrySubpass{ render_context, std::move(vertex_source), std::move(fragment_source), scene, camera }
	{
	}

	void ShadowSubpass::prepare_pipeline_state(vkb::CommandBuffer& command_buffer, VkFrontFace front_face,
		bool double_sided_material)
	{
		vkb::RasterizationState rasterization_state{};
		rasterization_state.front_face = front_face;
		rasterization_state.depth_bias_enable = VK_TRUE;

		if (double_sided_material)
		{
			rasterization_state.cull_mode = VK_CULL_MODE_NONE;
		}

		command_buffer.set_rasterization_state(rasterization_state);
		command_buffer.set_depth_bias(-1.4f, 0.0f, -1.7f);

		vkb::MultisampleState multisample_state{};
		multisample_state.rasterization_samples = sample_count;
		command_buffer.set_multisample_state(multisample_state);
	}

	vkb::PipelineLayout& ShadowSubpass::prepare_pipeline_layout(vkb::CommandBuffer& command_buffer,
		const std::vector<vkb::ShaderModule*>& shader_modules)
	{
		// Only vertex shader is needed in the shadow subpass
		assert(!shader_modules.empty());
		auto vertex_shader_module = shader_modules[0];

		vertex_shader_module->set_resource_mode("GlobalUniform", vkb::ShaderResourceMode::Dynamic);

		return command_buffer.get_device().get_resource_cache().request_pipeline_layout({ vertex_shader_module });
	}

	void ShadowSubpass::prepare_push_constants(vkb::CommandBuffer& command_buffer, vkb::sg::SubMesh& sub_mesh)
	{
		// No push constants are used the in shadow pass
		return;
	}

	void ShadowRenderPass::init(vkb::RenderContext& render_context, vkb::sg::Scene& scene,
		vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light)
	{
		auto light_camera_ptr = std::make_unique<vkb::sg::OrthographicCamera>("shadowmap_camera");
		update_light_camera(*light_camera_ptr, camera, light);
		light_camera_ptr->set_node(*light.get_node());
		light_camera_ = light_camera_ptr.get();
		light.get_node()->set_component(*light_camera_ptr);
		scene.add_component(std::move(light_camera_ptr));
	}

	glm::vec4 ShadowRenderPass::get_cascade_splits(const vkb::sg::PerspectiveCamera& camera)
	{
		glm::vec4 splits;
		splits.x = calculate_cascade_split_depth(1, 3, camera);
		splits.y = calculate_cascade_split_depth(2, 3, camera);
		splits.z = calculate_cascade_split_depth(3, 3, camera);

		return splits;
	}

	void ShadowRenderPass::update_light_camera(vkb::sg::OrthographicCamera& light_camera,
		vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light)
	{
		glm::mat4 inverse_view_projection = glm::inverse(vkb::vulkan_style_projection(camera.get_projection()) * camera.get_view());
		std::vector<glm::vec3> corners(8);
		for (uint32_t i = 0; i < 8; i++)
		{
			glm::vec4 homogenous_corner = glm::vec4(
				(i & 1) ? 1.0f : -1.0f,
				(i & 2) ? 1.0f : -1.0f,
				(i & 4) ? 1.0f : calculate_cascade_split_depth(1, 3, camera),
				1.0f);

			glm::vec4 world_corner = inverse_view_projection * homogenous_corner;
			corners[i] = glm::vec3(world_corner) / world_corner.w;
		}

		auto& light_transform = light.get_node()->get_transform();

		glm::mat4 light_view_mat = glm::inverse(light_transform.get_world_matrix());

		for (auto& corner : corners)
		{
			corner = light_view_mat * glm::vec4(corner, 1.0f);
		}

		glm::vec3 min_bounds = glm::vec3(FLT_MAX), max_bounds(-FLT_MAX);

		for (const auto& corner : corners)
		{
			min_bounds = glm::min(min_bounds, corner);
			max_bounds = glm::max(max_bounds, corner);
		}

		light_camera.set_left(min_bounds.x);
		light_camera.set_right(max_bounds.x);
		light_camera.set_bottom(min_bounds.y);
		light_camera.set_top(max_bounds.y);
		light_camera.set_near_plane(min_bounds.z);
		light_camera.set_far_plane(max_bounds.z);

	}
}
