#pragma once

#include "rendering/subpass.h"

namespace siho
{
	struct Vertex {
		glm::vec3 position;
		Vertex(glm::vec3 position)
			: position(position) {}
	};

	class TestSubpass : public vkb::Subpass
	{
	public:
		TestSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader, vkb::ShaderSource&& fragment_shader);

		void prepare() override;
		void draw(vkb::CommandBuffer& command_buffer) override;
	};
}