#include "shadow_subpass.h"

namespace siho
{
	ShadowSubpass::ShadowSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_source,
		vkb::ShaderSource&& fragment_source, vkb::sg::Scene& scene, vkb::sg::Camera& camera)
			:vkb::GeometrySubpass{render_context, std::move(vertex_source), std::move(fragment_source), scene, camera}
	{
	}

	void ShadowSubpass::prepare_pipeline_state(vkb::CommandBuffer& command_buffer, VkFrontFace front_face,
	                                           bool double_sided_material)
	{
		vkb::RasterizationState rasterization_state{};
		rasterization_state.front_face = front_face;
		rasterization_state.depth_bias_enable = VK_TRUE;

		if (double_sided_material)
		{
			rasterization_state.cull_mode = VK_CULL_MODE_NONE;
		}

		command_buffer.set_rasterization_state(rasterization_state);
		command_buffer.set_depth_bias(-1.4f, 0.0f, -1.7f);

		vkb::MultisampleState multisample_state{};
		multisample_state.rasterization_samples = sample_count;
		command_buffer.set_multisample_state(multisample_state);
	}

	vkb::PipelineLayout& ShadowSubpass::prepare_pipeline_layout(vkb::CommandBuffer& command_buffer,
		const std::vector<vkb::ShaderModule*>& shader_modules)
	{
		// Only vertex shader is needed in the shadow subpass
		assert(!shader_modules.empty());
		auto vertex_shader_module = shader_modules[0];

		vertex_shader_module->set_resource_mode("GlobalUniform", vkb::ShaderResourceMode::Dynamic);

		return command_buffer.get_device().get_resource_cache().request_pipeline_layout({ vertex_shader_module });
	}

	void ShadowSubpass::prepare_push_constants(vkb::CommandBuffer& command_buffer, vkb::sg::SubMesh& sub_mesh)
	{
		// No push constants are used the in shadow pass
		return;
	}
}
