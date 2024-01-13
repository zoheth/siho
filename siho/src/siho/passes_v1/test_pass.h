#pragma once
#include <rendering/subpass.h>
#include <scene_graph/components/camera.h>

#include "pass_common.h"

namespace siho
{
	class TestSubpass : public vkb::Subpass
	{
	public:
		TestSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader, vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera);

		void prepare() override;
		void draw(vkb::CommandBuffer& command_buffer) override;

	protected:
		virtual void update_uniform(vkb::CommandBuffer& command_buffer);
	private:
		struct alignas(16) GlobalUniform
		{
			glm::mat4 model;

			glm::mat4 camera_view_proj;

			glm::vec3 camera_position;
		};

		struct Vertex {
			glm::vec3 position;
			Vertex(glm::vec3 position)
				: position(position) {}
		};

		vkb::sg::Camera& camera_;

		vkb::VertexInputState vertex_input_state_;
		std::unique_ptr<vkb::core::Buffer> vertex_buffer_;
		Texture texture_;
	};
}