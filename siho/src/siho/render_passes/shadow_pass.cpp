#include "shadow_pass.h"

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
			(f * n) / ((n - f) * c);
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
		rasterization_state.depth_clamp_enable = VK_TRUE;

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

	ShadowRenderPass::ShadowRenderPass()
		= default;

	void ShadowRenderPass::init(vkb::RenderContext& render_context, vkb::sg::Scene& scene,
		vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light)
	{
		render_context_ = &render_context;
		main_camera_ = &camera;
		create_render_targets();

		create_light_camera(camera, light, scene);

		create_shadow_render_pipelines(scene);

	}

	void ShadowRenderPass::update()
	{
		for (uint32_t i = 0; i < cascades_.size(); i++)
		{
			update_light_camera(*cascades_[i].light_camera, *main_camera_, i);
		}
	}

	void ShadowRenderPass::draw(vkb::CommandBuffer& command_buffer)
	{
		for (auto& cascade : cascades_)
		{
			auto& render_target = *cascade.shadow_render_targets[render_context_->get_active_frame_index()];
			auto& extent = render_target.get_extent();

			VkViewport viewport{};
			viewport.width = static_cast<float>(extent.width);
			viewport.height = static_cast<float>(extent.height);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			command_buffer.set_viewport(0, { viewport });

			VkRect2D scissor{};
			scissor.extent = extent;
			command_buffer.set_scissor(0, { scissor });

			record_image_memory_barriers(command_buffer, render_target);
			cascade.shadow_render_pipeline->draw(command_buffer, render_target);
			command_buffer.end_render_pass();
		}
	}

	ShadowUniform ShadowRenderPass::get_shadow_uniform() const
	{
		ShadowUniform uniform;
		for(uint32_t i = 0; i < kCascadeCount; i++)
		{
			uniform.shadowmap_projection_matrix[i] = vkb::vulkan_style_projection(cascades_[i].light_camera->get_projection()) * cascades_[i].light_camera->get_view();
		}
		return uniform;
	}

	const vkb::core::ImageView& ShadowRenderPass::get_shadowmaps_view() const
	{
		return *shadowmap_array_image_views_[render_context_->get_active_frame_index()];
	}

	std::unique_ptr<vkb::core::Sampler> ShadowRenderPass::create_shadowmap_sampler(vkb::RenderContext& render_context)
	{
		VkSamplerCreateInfo shadowmap_sampler_create_info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		shadowmap_sampler_create_info.minFilter = VK_FILTER_LINEAR;
		shadowmap_sampler_create_info.magFilter = VK_FILTER_LINEAR;
		shadowmap_sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		shadowmap_sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		shadowmap_sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		shadowmap_sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		shadowmap_sampler_create_info.compareEnable = VK_TRUE;
		shadowmap_sampler_create_info.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
		return std::make_unique<vkb::core::Sampler>(render_context.get_device(), shadowmap_sampler_create_info);
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
		vkb::sg::PerspectiveCamera& camera, uint32_t cascade_index)
	{
		assert(cascade_index < kCascadeCount && cascade_index >= 0);
		glm::mat4 inverse_view_projection = glm::inverse(vkb::vulkan_style_projection(camera.get_projection()) * camera.get_view());
		std::vector<glm::vec3> corners(8);
		for (uint32_t i = 0; i < 8; i++)
		{
			glm::vec4 homogenous_corner = glm::vec4(
				(i & 1) ? 1.0f : -1.0f,
				(i & 2) ? 1.0f : -1.0f,
				(i & 4) ? calculate_cascade_split_depth(cascade_index, kCascadeCount, camera) : calculate_cascade_split_depth(cascade_index + 1, kCascadeCount, camera),
				//(i & 4) ? 1.0f : 0.0f,
				1.0f);

			glm::vec4 world_corner = inverse_view_projection * homogenous_corner;
			corners[i] = glm::vec3(world_corner) / world_corner.w;
		}

		glm::mat4 invert_y = glm::mat4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, -1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);

		glm::mat4 light_view_mat = invert_y * light_camera.get_view();

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

	void ShadowRenderPass::create_render_targets()
	{
		auto& device = render_context_->get_device();

		for(uint32_t i = 0; i < kCascadeCount; i++)
		{
			cascades_[i].shadow_render_targets.resize(render_context_->get_render_frames().size());
		}

		VkExtent3D extent{ shadowmap_resolution_, shadowmap_resolution_, 1 };

		for(uint32_t j = 0; j < render_context_->get_render_frames().size(); j++)
		{
			// For every frame
			// Create a depth image for each frame, used for shadow mapping.
			// One image for all cascades
			shadowmap_array_images_.push_back(std::make_unique<vkb::core::Image>(device,
				extent,
				vkb::get_suitable_depth_format(device.get_gpu().get_handle()),
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY,
				VK_SAMPLE_COUNT_1_BIT,
				1,
				kCascadeCount));

			// Create an image view representing the entire image array for shader binding.
			// sampler2DArray in shader
			shadowmap_array_image_views_.push_back(std::make_unique<vkb::core::ImageView>(*shadowmap_array_images_[j], VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_FORMAT_UNDEFINED, 0, 0, 1, kCascadeCount));

			for (uint32_t i = 0; i < kCascadeCount; i++)
			{
				// Create image views for each cascade. These are part of the render target and 
				// allow pipeline results to be written specifically to sections of the image.
				vkb::core::ImageView cascade_image_view{ *shadowmap_array_images_[j], VK_IMAGE_VIEW_TYPE_2D_ARRAY ,VK_FORMAT_UNDEFINED,0,i,1,1 };
				std::vector<vkb::core::ImageView> image_views;
				image_views.push_back(std::move(cascade_image_view));

				// The render targets use these cascade-specific views, sharing the same base image.
				// This allows the shader to access results written by all the render target views.
				cascades_[i].shadow_render_targets[j] = std::make_unique<vkb::RenderTarget>(std::move(image_views));
			}
		}

	}

	void ShadowRenderPass::create_light_camera(vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light, vkb::sg::Scene& scene)
	{
		for (uint32_t i = 0; i < cascades_.size(); i++)
		{
			auto light_camera_ptr = std::make_unique<vkb::sg::OrthographicCamera>("shadowmap_camera");
			light_camera_ptr->set_node(*light.get_node());
			update_light_camera(*light_camera_ptr, camera, i);
			cascades_[i].light_camera = light_camera_ptr.get();
			light.get_node()->set_component(*light_camera_ptr);
			scene.add_component(std::move(light_camera_ptr));
		}
	}

	void ShadowRenderPass::create_shadow_render_pipelines(vkb::sg::Scene& scene)
	{
		for (auto& cascade : cascades_)
		{

			auto shadowmap_vs = vkb::ShaderSource{ "shadows/shadowmap.vert" };
			auto shadowmap_fs = vkb::ShaderSource{ "shadows/shadowmap.frag" };
			auto scene_subpass = std::make_unique<ShadowSubpass>(*render_context_, std::move(shadowmap_vs), std::move(shadowmap_fs), scene, *
				cascade.light_camera);

			cascade.shadow_subpass = scene_subpass.get();

			auto shadowmap_render_pipeline = std::make_unique<vkb::RenderPipeline>();
			shadowmap_render_pipeline->add_subpass(std::move(scene_subpass));
			cascade.shadow_render_pipeline = std::move(shadowmap_render_pipeline);
		}
	}

	void ShadowRenderPass::record_image_memory_barriers(vkb::CommandBuffer& command_buffer, vkb::RenderTarget& render_target)
	{
		auto& shadowmap_views = render_target.get_views();

		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		memory_barrier.new_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		memory_barrier.src_access_mask = 0;
		memory_barrier.dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		for (const auto& shadowmap : shadowmap_views)
		{
			command_buffer.image_memory_barrier(shadowmap, memory_barrier);
		}

	}
}
