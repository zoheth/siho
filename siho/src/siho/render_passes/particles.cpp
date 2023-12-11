#include "particles.h"

#include <random>

constexpr uint32_t kParticleCount = 4 * 1024;

namespace siho
{
	ParticlesSubpass::ParticlesSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera) : Subpass(render_context, std::move(vertex_shader), std::move(fragment_shader)), camera_(camera)
	{

	}

	void ParticlesSubpass::prepare()
	{
		std::vector<glm::vec3> attractors = {
		glm::vec3(5.0f, 0.0f, 0.0f),
		glm::vec3(-5.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 5.0f),
		glm::vec3(0.0f, 0.0f, -5.0f),
		glm::vec3(0.0f, 4.0f, 0.0f),
		glm::vec3(0.0f, -8.0f, 0.0f),
		};

		uint32_t num_particles = static_cast<uint32_t>(attractors.size()) * kParticleCount;

		std::vector<Particle> particles(num_particles);

		std::default_random_engine rnd_engine;
		std::normal_distribution<float> rnd_distribution(0.0f, 1.0f);

		for (uint32_t i = 0; i < static_cast<uint32_t>(attractors.size()); ++i)
		{
			for (uint32_t j = 0; j < kParticleCount; ++j)
			{
				Particle& particle = particles[i * kParticleCount + j];

				// First particle in group as heavy center of gravity
				if (j == 0)
				{
					particle.pos = glm::vec4(attractors[i] * 1.5f, 90000.0f);
					particle.vel = glm::vec4(glm::vec4(0.0f));
				}
				else
				{
					// Position
					glm::vec3 position(attractors[i] + glm::vec3(rnd_distribution(rnd_engine), rnd_distribution(rnd_engine), rnd_distribution(rnd_engine)) * 0.75f);
					float     len = glm::length(glm::normalize(position - attractors[i]));
					position.y *= 2.0f - (len * len);

					// Velocity
					glm::vec3 angular = glm::vec3(0.5f, 1.5f, 0.5f) * (((i % 2) == 0) ? 1.0f : -1.0f);
					glm::vec3 velocity = glm::cross((position - attractors[i]), angular) + glm::vec3(rnd_distribution(rnd_engine), rnd_distribution(rnd_engine), rnd_distribution(rnd_engine) * 0.025f);

					float mass = (rnd_distribution(rnd_engine) * 0.5f + 0.5f) * 75.0f;
					particle.pos = glm::vec4(position, mass);
					particle.vel = glm::vec4(velocity, 0.0f);
				}

				// Color gradient offset
				particle.vel.w = static_cast<float>(i) * 1.0f / static_cast<uint32_t>(attractors.size());
			}
		}

		compute_uniform_.particle_count = num_particles;

		VkDeviceSize buffer_size = sizeof(Particle) * num_particles;

		vkb::core::Buffer staging_buffer{ render_context.get_device(), buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_COPY };
		staging_buffer.update(particles.data(), buffer_size);

		particle_buffer_ = std::make_unique<vkb::core::Buffer>(render_context.get_device(), buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

		vkb::CommandBuffer& copy_command = render_context.get_device().request_command_buffer();
		copy_command.copy_buffer(staging_buffer, *particle_buffer_, buffer_size);

	}
}
