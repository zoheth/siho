#pragma once

#include "rendering/subpass.h"

#include "scene_graph/components/image.h"

namespace siho
{
	struct Texture
	{
		std::unique_ptr<vkb::sg::Image> image;
		std::unique_ptr<vkb::core::Sampler> sampler;
		// VkSampler                       sampler;
	};

	inline void upload_image_to_gpu(vkb::CommandBuffer& command_buffer, vkb::core::Buffer& staging_buffer, vkb::sg::Image& image);
	Texture load_texture(const std::string& file, vkb::sg::Image::ContentType content_type, const vkb::Device& device);

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

	private:
		vkb::VertexInputState vertex_input_state_;
		std::unique_ptr<vkb::core::Buffer> vertex_buffer_;
		Texture texture_;
	};
}