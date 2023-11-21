#pragma once

#include "rendering/subpasses/geometry_subpass.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/orthographic_camera.h"
#include "scene_graph/components/perspective_camera.h"

namespace siho
{
	class ShadowSubpass : public vkb::GeometrySubpass
	{
	public:
		ShadowSubpass(vkb::RenderContext& render_context,
			vkb::ShaderSource&& vertex_source,
			vkb::ShaderSource&& fragment_source,
			vkb::sg::Scene& scene,
			vkb::sg::Camera& camera);
	protected:
		void prepare_pipeline_state(vkb::CommandBuffer& command_buffer, VkFrontFace front_face, bool double_sided_material) override;

		vkb::PipelineLayout& prepare_pipeline_layout(vkb::CommandBuffer& command_buffer, const std::vector<vkb::ShaderModule*>& shader_modules) override;

		void prepare_push_constants(vkb::CommandBuffer& command_buffer, vkb::sg::SubMesh& sub_mesh) override;
	};

	class ShadowRenderPass
	{
	public:
		void init(vkb::RenderContext& render_context, vkb::sg::Scene& scene, vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light);

		vkb::sg::OrthographicCamera* get_light_camera() const
		{
			return light_camera_;
		}

		static glm::vec4 get_cascade_splits(const vkb::sg::PerspectiveCamera& camera);

	private:
		std::unique_ptr<vkb::sg::OrthographicCamera> create_light_camera(vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light);

	private:
		vkb::sg::OrthographicCamera* light_camera_{};
	};
}
