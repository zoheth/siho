#pragma once

#include <rendering/subpass.h>
#include <scene_graph/components/camera.h>

#include "pass_common.h"

namespace siho
{
	// SSBO particle declaration
	struct Particle
	{
		glm::vec4 pos;        // xyz = position, w = mass
		glm::vec4 vel;        // xyz = velocity, w = gradient texture position
	};

	class FxGraphSubpass : public vkb::Subpass
	{
	public:
		FxGraphSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader, vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera);

		void prepare() override;
		void draw(vkb::CommandBuffer& command_buffer) override;

	protected:
		virtual void update_uniform(vkb::CommandBuffer& command_buffer);

	private:
		void prepare_storage_buffers();

	private:
		struct alignas(16) GlobalUniform
		{
			glm::mat4 camera_view;
			glm::mat4 camera_proj;
			glm::vec2 screen_dim;
		};

		struct
		{
			Texture particle;
			Texture gradient;
		} textures_;

		uint32_t num_particles_;

		vkb::sg::Camera& camera_;

		std::shared_ptr<vkb::core::Buffer> storage_buffer_;

		vkb::VertexInputState vertex_input_state_;

	};
}
