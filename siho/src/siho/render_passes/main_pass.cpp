#include "main_pass.h"

namespace 
{
	VkFormat          albedo_format{ VK_FORMAT_R8G8B8A8_UNORM };
	VkFormat          normal_format{ VK_FORMAT_A2B10G10R10_UNORM_PACK32 };
	VkImageUsageFlags rt_usage_flags{ VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT };
}
namespace siho
{
	LightingSubpass::LightingSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera, vkb::sg::Scene& scene,
		ShadowRenderPass& shadow_render_pass)

		:vkb::LightingSubpass(render_context, std::move(vertex_shader), std::move(fragment_shader), camera, scene),
		shadow_render_pass_(shadow_render_pass)
	{
	}

	void LightingSubpass::prepare()
	{
		vkb::LightingSubpass::prepare();
		shadowmap_sampler = ShadowRenderPass::create_shadowmap_sampler(get_render_context());
	}

	void LightingSubpass::draw(vkb::CommandBuffer& command_buffer)
	{
		command_buffer.push_constants(ShadowRenderPass::get_cascade_splits(dynamic_cast<const vkb::sg::PerspectiveCamera&>(camera)));


		command_buffer.bind_image(shadow_render_pass_.get_shadowmaps_view(), *shadowmap_sampler, 0, 5, 0);

		auto& render_frame = get_render_context().get_active_frame();
		vkb::BufferAllocation shadow_buffer = render_frame.allocate_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(ShadowUniform));
		shadow_buffer.update(shadow_render_pass_.get_shadow_uniform());
		command_buffer.bind_buffer(shadow_buffer.get_buffer(), shadow_buffer.get_offset(), shadow_buffer.get_size(), 0, 6, 0);
		vkb::LightingSubpass::draw(command_buffer);
	}

	void MainPass::init(vkb::RenderContext& render_context, vkb::sg::Scene& scene, vkb::sg::Camera& camera,
		siho::ShadowRenderPass& shadow_render_pass)
	{
		render_context_ = &render_context;
		shadow_render_pass_ = &shadow_render_pass;
		create_render_pipeline(camera, scene);
	}

	void MainPass::draw(vkb::CommandBuffer& command_buffer)
	{
		auto& render_target = render_context_->get_active_frame().get_render_target();
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

		record_image_memory_barriers(command_buffer);
		render_pipeline_->draw(command_buffer, render_target);
	}

	std::unique_ptr<vkb::RenderTarget> MainPass::create_render_target(vkb::core::Image&& swapchain_image)
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

		vkb::core::Image light_image{ device,extent,
			albedo_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
			VMA_MEMORY_USAGE_GPU_ONLY
		};

		vkb::core::Image depth_image2{ device,extent,
			vkb::get_suitable_depth_format(swapchain_image.get_device().get_gpu().get_handle()),
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rt_usage_flags,
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

	void MainPass::create_render_pipeline(vkb::sg::Camera& camera, vkb::sg::Scene& scene)
	{
		// Geometry subpass
		auto geometry_vs = vkb::ShaderSource{ "deferred/geometry.vert" };
		auto geometry_fs = vkb::ShaderSource{ "deferred/geometry.frag" };
		auto scene_subpass = std::make_unique<vkb::GeometrySubpass>(*render_context_, std::move(geometry_vs), std::move(geometry_fs), scene, camera);

		// Outputs are depth, albedo, normal
		scene_subpass->set_output_attachments({ 1, 2, 3 });

		auto lighting_vs = vkb::ShaderSource{ "deferred/lighting.vert" };
		auto lighting_fs = vkb::ShaderSource{ "deferred/lighting.frag" };
		auto lighting_subpass = std::make_unique<LightingSubpass>(*render_context_, std::move(lighting_vs), std::move(lighting_fs), camera, scene, *shadow_render_pass_);

		lighting_subpass->set_input_attachments({ 1, 2, 3 });

		auto test_vs = vkb::ShaderSource{ "tests/test.vert" };
		auto test_fs = vkb::ShaderSource("tests/test.frag");
		auto test_subpass = std::make_unique<TestSubpass>(*render_context_, std::move(test_vs), std::move(test_fs));

		std::vector<std::unique_ptr<vkb::Subpass>> subpasses{};
		//subpasses.push_back(std::move(scene_subpass));
		//subpasses.push_back(std::move(lighting_subpass));
		subpasses.push_back(std::move(test_subpass));

		render_pipeline_ = std::make_unique<vkb::RenderPipeline>(std::move(subpasses));

		render_pipeline_->set_load_store(vkb::gbuffer::get_clear_all_store_swapchain());
		render_pipeline_->set_clear_value(vkb::gbuffer::get_clear_value());
	}

	void MainPass::record_image_memory_barriers(vkb::CommandBuffer& command_buffer)
	{
		auto& views = render_context_->get_active_frame().get_render_target().get_views();
		{
			vkb::ImageMemoryBarrier memory_barrier{};
			memory_barrier.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			memory_barrier.new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			memory_barrier.src_access_mask = 0;
			memory_barrier.dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

			assert(swapchain_attachment_index < views.size());
			command_buffer.image_memory_barrier(views[swapchain_attachment_index], memory_barrier);
			// Skip 1 as it is handled later as a depth-stencil attachment
			for (size_t i = 2; i < views.size(); ++i)
			{
				command_buffer.image_memory_barrier(views[i], memory_barrier);
			}
		}

		{
			vkb::ImageMemoryBarrier memory_barrier{};
			memory_barrier.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			memory_barrier.new_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			memory_barrier.src_access_mask = 0;
			memory_barrier.dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

			assert(depth_attachment_index < views.size());
			command_buffer.image_memory_barrier(views[depth_attachment_index], memory_barrier);
		}

		{

			auto& shadowmap_views0 = shadow_render_pass_->get_shadow_render_targets(0)[render_context_->get_active_frame_index()]->get_views();
			auto& shadowmap_views1 = shadow_render_pass_->get_shadow_render_targets(1)[render_context_->get_active_frame_index()]->get_views();
			auto& shadowmap_views2 = shadow_render_pass_->get_shadow_render_targets(2)[render_context_->get_active_frame_index()]->get_views();

			vkb::ImageMemoryBarrier memory_barrier{};
			memory_barrier.old_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			memory_barrier.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			memory_barrier.src_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			memory_barrier.dst_access_mask = VK_ACCESS_SHADER_READ_BIT;
			memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

			for (const auto& shadowmap : shadowmap_views0)
			{
				command_buffer.image_memory_barrier(shadowmap, memory_barrier);
			}

			for (const auto& shadowmap : shadowmap_views1)
			{
				command_buffer.image_memory_barrier(shadowmap, memory_barrier);
			}

			for (const auto& shadowmap : shadowmap_views2)
			{
				command_buffer.image_memory_barrier(shadowmap, memory_barrier);
			}
		}
	}
}
