#pragma once
#include "rendering/subpass.h"
#include "core/shader_module.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/camera.h"

namespace siho
{
	struct Particle
	{
		glm::vec4 pos;        // xyz = position, w = mass
		glm::vec4 vel;        // xyz = velocity, w = gradient texture position
	};

	class ParticlesSubpass : public vkb::Subpass
	{
	public:
		ParticlesSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader, vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera);

		void prepare() override;

		// void draw(vkb::CommandBuffer& command_buffer) override;

	private:
		vkb::sg::Camera& camera_;
		std::unique_ptr<vkb::core::Buffer> particle_buffer_{ nullptr };

		struct ComputeUniform
		{
			float   delta_time;        //		Frame delta time
			int32_t particle_count;
		} compute_uniform_;


	};

	class ParticlePass
	{
		std::unique_ptr<vkb::RenderPipeline> graphics_pipeline{};

	};
}
