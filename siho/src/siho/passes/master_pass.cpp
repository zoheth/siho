#include "master_pass.h"

#include "rendering/render_pipeline.h"
#include "rendering/subpasses/geometry_subpass.h"

namespace siho
{
	LightingSubpass::LightingSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera, vkb::sg::Scene& scene) :
		vkb::Subpass(render_context, std::move(vertex_shader), std::move(fragment_shader)),
		camera_{ camera },
		scene_{ scene }
	{
	}

	void LightingSubpass::prepare()
	{
		lighting_variant_.add_definitions({ "MAX_LIGHT_COUNT " + std::to_string(kMaxDeferredLightCount) });

		lighting_variant_.add_definitions(vkb::light_type_definitions);
		// Build all shaders upfront
		auto& resource_cache = render_context.get_device().get_resource_cache();
		resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader(), lighting_variant_);
		resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader(), lighting_variant_);
	}

	void LightingSubpass::draw(vkb::CommandBuffer& command_buffer)
	{
		allocate_lights<DeferredLights>(scene_.get_components<vkb::sg::Light>(), kMaxDeferredLightCount);
		command_buffer.bind_lighting(get_lighting_state(), 0, 4);

		// Get shaders from cache
		auto& resource_cache = command_buffer.get_device().get_resource_cache();
		auto& vert_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader(), lighting_variant_);
		auto& frag_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader(), lighting_variant_);

		std::vector<vkb::ShaderModule*> shader_modules{ &vert_shader_module, &frag_shader_module };

		// Create pipeline layout and bind it
		auto& pipeline_layout = resource_cache.request_pipeline_layout(shader_modules);
		command_buffer.bind_pipeline_layout(pipeline_layout);

		// Get image views of the attachments
		auto& render_target = get_render_context().get_active_frame().get_render_target();
		auto& target_views = render_target.get_views();
		assert(3 < target_views.size());

		// Bind depth, albedo, and normal as input attachments
		auto& depth_view = target_views[1];
		command_buffer.bind_input(depth_view, 0, 0, 0);

		auto& albedo_view = target_views[2];
		command_buffer.bind_input(albedo_view, 0, 1, 0);

		auto& normal_view = target_views[3];
		command_buffer.bind_input(normal_view, 0, 2, 0);

		// Set cull mode to front as full screen triangle is clock-wise
		vkb::RasterizationState rasterization_state;
		rasterization_state.cull_mode = VK_CULL_MODE_FRONT_BIT;
		command_buffer.set_rasterization_state(rasterization_state);

		// Populate uniform values
		LightUniform light_uniform;

		// Inverse resolution
		light_uniform.inv_resolution.x = 1.0f / render_target.get_extent().width;
		light_uniform.inv_resolution.y = 1.0f / render_target.get_extent().height;

		// Inverse view projection
		light_uniform.inv_view_proj = glm::inverse(vkb::vulkan_style_projection(camera_.get_projection()) * camera_.get_view());

		// Allocate a buffer using the buffer pool from the active frame to store uniform values and bind it
		auto& render_frame = get_render_context().get_active_frame();
		auto  allocation = render_frame.allocate_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(LightUniform));
		allocation.update(light_uniform);
		command_buffer.bind_buffer(allocation.get_buffer(), allocation.get_offset(), allocation.get_size(), 0, 3, 0);



		// Draw full screen triangle
		command_buffer.draw(3, 1, 0, 0);
	}

	MasterPass::MasterPass(vkb::RenderContext& render_context, vkb::sg::Camera& camera, vkb::sg::Scene& scene)
	{

	}

	std::unique_ptr<vkb::RenderTarget> MasterPass::create_render_target(vkb::core::Image&& swapchain_image)
	{
		auto& device = swapchain_image.get_device();
		auto& extent = swapchain_image.get_extent();

		// G-Buffer should fit 128-bit budget for buffer color storage
		// in order to enable subpasses merging by the driver
		// Light (swapchain_image) RGBA8_UNORM   (32-bit)
		// Albedo                  RGBA8_UNORM   (32-bit)
		// Normal                  RGB10A2_UNORM (32-bit)

		VkFormat          albedo_format{ VK_FORMAT_R8G8B8A8_UNORM };
		VkFormat          normal_format{ VK_FORMAT_A2B10G10R10_UNORM_PACK32 };
		VkImageUsageFlags rt_usage_flags{ VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT };

		vkb::core::Image depth_image{ device,extent,
			vkb::get_suitable_depth_format(swapchain_image.get_device().get_gpu().get_handle()),
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rt_usage_flags,
			VMA_MEMORY_USAGE_GPU_ONLY
		};

		vkb::core::Image albedo_image{ device,extent,
			albedo_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
			VMA_MEMORY_USAGE_GPU_ONLY
		};

		vkb::core::Image normal_image{ device,extent,
			normal_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
			VMA_MEMORY_USAGE_GPU_ONLY
		};

		vkb::core::Image light_image{ device,extent,
			albedo_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
			VMA_MEMORY_USAGE_GPU_ONLY
		};

		std::vector<vkb::core::Image> images;

		// Attachment 0
		images.push_back(std::move(swapchain_image));

		// Attachment 1
		images.push_back(std::move(depth_image));

		// Attachment 2
		images.push_back(std::move(albedo_image));

		// Attachment 3
		images.push_back(std::move(normal_image));

		return std::make_unique<vkb::RenderTarget>(std::move(images));
	}

	void MasterPass::create_render_pipeline(vkb::sg::Camera& camera, vkb::sg::Scene& scene)
	{
		// Geometry subpass
		auto geometry_vs = vkb::ShaderSource{ "deferred/geometry.vert" };
		auto geometry_fs = vkb::ShaderSource{ "deferred/geometry.frag" };
		auto scene_subpass = std::make_unique<vkb::GeometrySubpass>(*render_context_, std::move(geometry_vs), std::move(geometry_fs), scene, camera);

		// Outputs are depth, albedo, normal
		scene_subpass->set_output_attachments({ 1, 2, 3 });

		auto lighting_vs = vkb::ShaderSource{ "deferred/lighting.vert" };
		auto lighting_fs = vkb::ShaderSource{ "deferred/lighting.frag" };
		auto lighting_subpass = std::make_unique<LightingSubpass>(*render_context_, std::move(lighting_vs), std::move(lighting_fs), camera, scene);
		lighting_subpass->set_disable_depth_stencil_attachment(true);
		lighting_subpass->set_input_attachments({ 1, 2, 3 });


		std::vector<std::unique_ptr<vkb::Subpass>> subpasses{};
		subpasses.push_back(std::move(scene_subpass));
		subpasses.push_back(std::move(lighting_subpass));

		render_pipeline_ = std::make_unique<vkb::RenderPipeline>(std::move(subpasses));

		render_pipeline_->set_load_store(vkb::gbuffer::get_clear_all_store_swapchain());
		render_pipeline_->set_clear_value(vkb::gbuffer::get_clear_value());
	}
}
