#pragma once
#include "ctpl_stl.h"
#include "shadow_pass.h"
#include "rendering/subpasses/lighting_subpass.h"

namespace siho
{
	class LightingSubpass : public vkb::LightingSubpass
	{
	public:
		LightingSubpass(vkb::RenderContext& render_context,
			vkb::ShaderSource&& vertex_shader,
			vkb::ShaderSource&& fragment_shader,
			vkb::sg::Camera& camera,
			vkb::sg::Scene& scene,
			siho::ShadowRenderPass& shadow_render_pass);
		void prepare() override;
		void draw(vkb::CommandBuffer& command_buffer) override;
	private:
		std::unique_ptr<vkb::core::Sampler> shadowmap_sampler{};
		ShadowRenderPass& shadow_render_pass_;
	};

	class MainPass
	{
	public:
		MainPass() = default;

		void init(vkb::RenderContext& render_context, vkb::sg::Scene& scene, vkb::sg::Camera& camera, siho::ShadowRenderPass& shadow_render_pass);

		void draw(vkb::CommandBuffer& command_buffer);

		static std::unique_ptr<vkb::RenderTarget> create_render_target(vkb::core::Image&& swapchain_image);
	private:
		void create_render_pipeline(vkb::sg::Camera& camera, vkb::sg::Scene& scene);

		void record_image_memory_barriers(vkb::CommandBuffer& command_buffer);

	private:
		std::unique_ptr<vkb::RenderPipeline> render_pipeline_{};

		vkb::RenderContext* render_context_{};
		ShadowRenderPass* shadow_render_pass_;

		ctpl::thread_pool thread_pool{ 1 };
		uint32_t swapchain_attachment_index{ 0 };
		uint32_t depth_attachment_index{ 1 };

		/*VkFormat          albedo_format{ VK_FORMAT_R8G8B8A8_UNORM };
		VkFormat          normal_format{ VK_FORMAT_A2B10G10R10_UNORM_PACK32 };
		VkImageUsageFlags rt_usage_flags{ VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT };*/
	};
}
