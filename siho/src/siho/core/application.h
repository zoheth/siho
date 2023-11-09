#pragma once

#include "ctpl_stl.h"
#include "platform/application.h"
#include "vulkan_sample.h"
#include "scene_graph/components/perspective_camera.h"
#include "rendering/subpasses/lighting_subpass.h"

#include "shadow_subpass.h"

namespace siho
{
	struct alignas(16) ShadowUniform
	{
		glm::mat4 shadowmap_projection_matrix;        // Projection matrix used to render shadowmap
	};

	class LightingSubpass : public vkb::LightingSubpass
	{
	public:
		LightingSubpass(vkb::RenderContext& render_context, 
			vkb::ShaderSource&& vertex_shader, 
			vkb::ShaderSource&& fragment_shader, 
			vkb::sg::Camera& camera, 
			vkb::sg::Scene& scene, 
			vkb::sg::Camera& shadowmap_camera,
			std::vector<std::unique_ptr<vkb::RenderTarget>>& shadow_render_targets);
		void prepare() override;
		void draw(vkb::CommandBuffer& command_buffer) override;
	private:
		std::unique_ptr<vkb::core::Sampler> shadowmap_sampler{};
		vkb::sg::Camera& shadowmap_camera;
		std::vector<std::unique_ptr<vkb::RenderTarget>>& shadow_render_targets;
	};

	class Application : public vkb::VulkanSample
	{
	public:
		Application() = default;
		
		bool prepare(const vkb::ApplicationOptions &options) override;
		void update(float delta_time) override;

		virtual ~Application() = default;
	private:
		void prepare_render_context() override;
		// void draw_renderpass(vkb::CommandBuffer& command_buffer, vkb::RenderTarget& render_target) override;

		std::unique_ptr<vkb::RenderTarget> create_shadow_render_target(uint32_t size) const;
		std::unique_ptr<vkb::RenderPipeline> create_shadow_renderpass();

		std::unique_ptr<vkb::RenderTarget> create_render_target(vkb::core::Image&& swapchain_image) const;
		std::unique_ptr<vkb::RenderPipeline> create_render_pipeline();

		void draw_shadow_pass(vkb::CommandBuffer& command_buffer);
		void draw_main_pass(vkb::CommandBuffer& command_buffer);

		std::vector<vkb::CommandBuffer*> record_command_buffers(vkb::CommandBuffer& main_command_buffer);

		void record_main_pass_image_memory_barriers(vkb::CommandBuffer& command_buffer);
		void record_shadow_pass_image_memory_barriers(vkb::CommandBuffer& command_buffer);
		void record_present_image_memory_barriers(vkb::CommandBuffer& command_buffer);
	private:
		std::unique_ptr<vkb::RenderPipeline> render_pipeline{};
		vkb::sg::PerspectiveCamera* camera{};

		ctpl::thread_pool thread_pool{ 1 };
		uint32_t swapchain_attachment_index{ 0 };
		uint32_t depth_attachment_index{ 1 };
		uint32_t shadowmap_attachment_index{ 0 };
		// shadow
		const uint32_t SHADOWMAP_RESOLUTION{ 1024 };

		std::vector<std::unique_ptr<vkb::RenderTarget>> shadow_render_targets;
		std::unique_ptr<vkb::RenderPipeline> shadow_render_pipeline{};
		ShadowSubpass* shadow_subpass{};
		vkb::sg::Camera* shadowmap_camera{};


		VkFormat          albedo_format{ VK_FORMAT_R8G8B8A8_UNORM };
		VkFormat          normal_format{ VK_FORMAT_A2B10G10R10_UNORM_PACK32 };
		VkImageUsageFlags rt_usage_flags{ VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT };
	};
} // namespace siho