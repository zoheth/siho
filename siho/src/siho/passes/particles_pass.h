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

	class FxComputePass
	{
	public:
		uint32_t num_particles;
		uint32_t work_group_size = 128;
	public:
		explicit FxComputePass();
		void init(vkb::RenderContext& render_context);
		void dispatch(vkb::CommandBuffer& command_buffer, float delta_time);

		std::shared_ptr<vkb::core::Buffer> get_storage_buffer();
	private:
		void update_uniform(vkb::CommandBuffer& command_buffer);
		void prepare_storage_buffers();
	private:
		struct ComputeUbo
		{                              // Compute shader uniform block object
			float   delta_time;        //		Frame delta time
			int32_t particle_count;
		} ubo_;

		vkb::RenderContext* render_context_;
		vkb::ShaderSource calculate_shader_;
		vkb::ShaderSource integrate_shader_;

		std::shared_ptr<vkb::core::Buffer> storage_buffer_;

		//vkb::CommandBuffer command_buffer_;
	};

	class FxGraphSubpass : public vkb::Subpass
	{
	public:
		FxGraphSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader, vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera);

		void prepare() override;
		void draw(vkb::CommandBuffer& command_buffer) override;

		void set_storage_buffer(std::shared_ptr<vkb::core::Buffer> storage_buffer);

	protected:
		virtual void update_uniform(vkb::CommandBuffer& command_buffer);

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
