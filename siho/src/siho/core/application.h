#pragma once

#include <platform/application.h>
#include <vulkan_sample.h>
#include <scene_graph/components/perspective_camera.h>

namespace siho
{
	class Application : public vkb::VulkanSample
	{
	public:
		Application() = default;
		
		bool prepare(const vkb::ApplicationOptions &options) override;

		virtual ~Application() = default;
	private:
		void prepare_render_context() override;
		void draw_renderpass(vkb::CommandBuffer& command_buffer, vkb::RenderTarget& render_target) override;

		std::unique_ptr<vkb::RenderTarget> create_render_target(vkb::core::Image&& swapchain_image) const;
		std::unique_ptr<vkb::RenderPipeline> create_render_pipeline();
	private:
		std::unique_ptr<vkb::RenderPipeline> render_pipeline{};
		vkb::sg::PerspectiveCamera* camera{};

		VkFormat          albedo_format{ VK_FORMAT_R8G8B8A8_UNORM };
		VkFormat          normal_format{ VK_FORMAT_A2B10G10R10_UNORM_PACK32 };
		VkImageUsageFlags rt_usage_flags{ VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT };
	};
} // namespace siho