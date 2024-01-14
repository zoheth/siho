#pragma once

#include "rendering/render_pipeline.h"
#include "rendering/subpass.h"
#include "scene_graph/components/camera.h"
#include "scene_graph/scene.h"

VKBP_DISABLE_WARNINGS()
#include "common/glm_common.h"
VKBP_ENABLE_WARNINGS()

constexpr auto kMaxDeferredLightCount = 32;


namespace siho
{
	/**
	 * @brief Light uniform structure for lighting shader
	 * Inverse view projection matrix and inverse resolution vector are used
	 * in lighting pass to reconstruct position from depth and frag coord
	 */
	struct alignas(16) LightUniform
	{
		glm::mat4 inv_view_proj;
		glm::vec2 inv_resolution;
	};

	struct alignas(16) DeferredLights
	{
		vkb::sg::Light directional_lights[kMaxDeferredLightCount];
		vkb::sg::Light point_lights[kMaxDeferredLightCount];
		vkb::sg::Light spot_lights[kMaxDeferredLightCount];
	};

	class LightingSubpass : public vkb::Subpass
	{
	public:
		LightingSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader, vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera, vkb::sg::Scene& scene);

		void prepare() override;
		void draw(vkb::CommandBuffer& command_buffer) override;

	private:
		vkb::sg::Camera& camera_;
		vkb::sg::Scene& scene_;
		vkb::ShaderVariant lighting_variant_;


	};

	class MasterPass
	{
	public:
		MasterPass(vkb::RenderContext& render_context, vkb::sg::Camera& camera, vkb::sg::Scene& scene);

		void draw(vkb::CommandBuffer& command_buffer);

		/**
		 * Creates a RenderTarget from a swapchain image. This function is essential for the MasterPass
		 * as it needs to provide RenderTargets for RenderFrames. Other passes do not require this function.
		 *
		 * @param swapchain_image A movable vkb::core::Image object representing the swapchain image.
		 */
		static std::unique_ptr<vkb::RenderTarget> create_render_target(vkb::core::Image&& swapchain_image);
	private:
		void create_render_pipeline(vkb::sg::Camera& camera, vkb::sg::Scene& scene);

	private:
		std::unique_ptr<vkb::RenderPipeline> render_pipeline_{};

		vkb::RenderContext* render_context_{};
	};
}

