#include "application.h"
#include "common/vk_common.h"

#include "rendering/pipeline_state.h"
#include "rendering/render_context.h"
#include "rendering/render_pipeline.h"
#include "rendering/subpasses/geometry_subpass.h"
#include "rendering/subpasses/lighting_subpass.h"
#include "scene_graph/node.h"

namespace siho
{

	LightingSubpass::LightingSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera, vkb::sg::Scene& scene,
		vkb::sg::Camera& shadowmap_camera, std::vector<std::unique_ptr<vkb::RenderTarget>>& shadow_render_targets)

		:vkb::LightingSubpass(render_context, std::move(vertex_shader), std::move(fragment_shader), camera, scene),
		shadowmap_camera(shadowmap_camera), shadow_render_targets(shadow_render_targets)
	{
	}

	void LightingSubpass::prepare()
	{
		vkb::LightingSubpass::prepare();
		VkSamplerCreateInfo shadowmap_sampler_create_info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		shadowmap_sampler_create_info.minFilter = VK_FILTER_LINEAR;
		shadowmap_sampler_create_info.magFilter = VK_FILTER_LINEAR;
		shadowmap_sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		shadowmap_sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		shadowmap_sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		shadowmap_sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		shadowmap_sampler_create_info.compareEnable = VK_TRUE;
		shadowmap_sampler_create_info.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
		shadowmap_sampler = std::make_unique<vkb::core::Sampler>(get_render_context().get_device(), shadowmap_sampler_create_info);
	}

	void LightingSubpass::draw(vkb::CommandBuffer& command_buffer)
	{
		ShadowUniform shadow_uniform;
		shadow_uniform.shadowmap_projection_matrix = vkb::vulkan_style_projection(shadowmap_camera.get_projection()) * shadowmap_camera.get_view();

		auto& shadow_render_target = *shadow_render_targets[get_render_context().get_active_frame_index()];
		assert(!shadow_render_target.get_views().empty());
		command_buffer.bind_image(shadow_render_target.get_views().at(0), *shadowmap_sampler, 0, 5, 0);

		auto& render_frame = get_render_context().get_active_frame();
		vkb::BufferAllocation shadow_buffer = render_frame.allocate_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(glm::mat4));
		command_buffer.bind_buffer(shadow_buffer.get_buffer(), shadow_buffer.get_offset(), shadow_buffer.get_size(), 0, 6, 0);
		vkb::LightingSubpass::draw(command_buffer);
	}


	bool Application::prepare(const vkb::ApplicationOptions& options)
	{
		if (!VulkanSample::prepare(options))
		{
			return false;
		}

		std::set<VkImageUsageFlagBits> usage = { VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ,
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT };
		get_render_context().update_swapchain(usage);

		load_scene("scenes/sponza/Sponza01.gltf");

		scene->clear_components<vkb::sg::Light>();

		auto light_pos = glm::vec3(0.0f, 128.0f, -225.0f);
		auto light_color = glm::vec3(1.0, 1.0, 1.0);

		for (int i = -1; i < 4; ++i)
		{
			for (int j = 0; j < 2; ++j)
			{
				glm::vec3 pos = light_pos;
				pos.x += i * 400.0f;
				pos.z += j * (225 + 140);
				pos.y = 8;

				for (int k = 0; k < 3; ++k)
				{
					pos.y = pos.y + (k * 100);

					light_color.x = static_cast<float>(rand()) / (RAND_MAX);
					light_color.y = static_cast<float>(rand()) / (RAND_MAX);
					light_color.z = static_cast<float>(rand()) / (RAND_MAX);

					vkb::sg::LightProperties props;
					props.color = light_color;
					props.intensity = 0.2f;

					vkb::add_point_light(*scene, pos, props);
				}
			}
		}

		auto& camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
		camera = dynamic_cast<vkb::sg::PerspectiveCamera*>(&camera_node.get_component<vkb::sg::Camera>());

		render_pipeline = create_render_pipeline();

		stats->request_stats({ vkb::StatIndex::frame_times });

		gui = std::make_unique<vkb::Gui>(*this, *window, stats.get());

		return true;

	}

	void Application::prepare_render_context()
	{
		get_render_context().prepare(1, [this](vkb::core::Image&& swapchain_image)
			{
				return create_render_target(std::move(swapchain_image));
			});
	}

	void Application::draw_renderpass(vkb::CommandBuffer& command_buffer, vkb::RenderTarget& render_target)
	{
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

		render_pipeline->draw(command_buffer, render_target);

		if (gui)
		{
			gui->draw(command_buffer);
		}

		command_buffer.end_render_pass();
	}

	std::unique_ptr<vkb::RenderTarget> Application::create_render_target(vkb::core::Image&& swapchain_image) const
	{
		auto& device = swapchain_image.get_device();
		auto& extent = swapchain_image.get_extent();
		// G-Buffer should fit 128-bit budget for buffer color storage
		// in order to enable subpasses merging by the driver
		// Light (swapchain_image) RGBA8_UNORM   (32-bit)
		// Albedo                  RGBA8_UNORM   (32-bit)
		// Normal                  RGB10A2_UNORM (32-bit)
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

	std::unique_ptr<vkb::RenderPipeline> Application::create_render_pipeline()
	{
		// Geometry subpass
		auto geometry_vs = vkb::ShaderSource{ "deferred/geometry.vert" };
		auto geometry_fs = vkb::ShaderSource{ "deferred/geometry.frag" };
		auto scene_subpass = std::make_unique<vkb::GeometrySubpass>(get_render_context(), std::move(geometry_vs), std::move(geometry_fs), *scene, *camera);

		// Outputs are depth, albedo, normal
		scene_subpass->set_output_attachments({ 1, 2, 3 });

		auto lighting_vs = vkb::ShaderSource{ "deferred/lighting.vert" };
		auto lighting_fs = vkb::ShaderSource{ "deferred/lighting.frag" };
		auto lighting_subpass = std::make_unique<vkb::LightingSubpass>(get_render_context(), std::move(lighting_vs), std::move(lighting_fs), *camera, *scene);

		lighting_subpass->set_input_attachments({ 1, 2, 3 });

		std::vector<std::unique_ptr<vkb::Subpass>> subpasses{};
		subpasses.push_back(std::move(scene_subpass));
		subpasses.push_back(std::move(lighting_subpass));

		auto render_pipeline = std::make_unique<vkb::RenderPipeline>(std::move(subpasses));

		render_pipeline->set_load_store(vkb::gbuffer::get_clear_all_store_swapchain());
		render_pipeline->set_clear_value(vkb::gbuffer::get_clear_value());

		return render_pipeline;
	}
}

std::unique_ptr<vkb::Application> create_application()
{
	return std::make_unique<siho::Application>();
}