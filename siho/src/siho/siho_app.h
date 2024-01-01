#pragma once

#include "ctpl_stl.h"
#include "platform/application.h"
#include "vulkan_sample.h"
#include "scene_graph/components/perspective_camera.h"
#include "rendering/subpasses/lighting_subpass.h"

#include "siho/passes/shadow_pass.h"
#include "siho/passes/main_pass.h"
#include "siho/passes/particles_pass.h"
#include "scene_graph/components/orthographic_camera.h"

namespace siho
{

	class SihoApplication : public vkb::VulkanSample
	{
	public:
		SihoApplication() = default;
		
		bool prepare(const vkb::ApplicationOptions &options) override;
		void update(float delta_time) override;
		void draw_gui() override;
		virtual ~SihoApplication() = default;
	private:
		void prepare_render_context() override;
		// void draw_renderpass(vkb::CommandBuffer& command_buffer, vkb::RenderTarget& render_target) override;

		std::vector<vkb::CommandBuffer*> record_command_buffers(vkb::CommandBuffer& main_command_buffer);

		void record_present_image_memory_barriers(vkb::CommandBuffer& command_buffer);
	private:
		vkb::sg::PerspectiveCamera* camera{};

		ctpl::thread_pool thread_pool{ 1 };
		uint32_t swapchain_attachment_index{ 0 };

		ShadowRenderPass shadow_render_pass_;
		MainPass main_pass_;
		FxComputePass fx_compute_pass_;

		vkb::sg::Light* directional_light_;

	};
} // namespace siho