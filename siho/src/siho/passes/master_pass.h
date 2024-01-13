#pragma once

#include <rendering/subpass.h>
#include <scene_graph/components/camera.h>
#include <scene_graph/scene.h>

VKBP_DISABLE_WARNINGS()
#include <common/glm_common.h>
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
	};
}

