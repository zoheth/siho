#pragma once

#include "rendering/subpasses/geometry_subpass.h"

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

		vkb::PipelineLayout &prepare_pipeline_layout(vkb::CommandBuffer& command_buffer, const std::vector<vkb::ShaderModule*>& shader_modules) override;

		void prepare_push_constants(vkb::CommandBuffer& command_buffer, vkb::sg::SubMesh& sub_mesh) override;
	};
}
