#include "particles_pass.h"

#include <random>
#include <utility>
#include <common/vk_initializers.h>

constexpr uint32_t kParticleCount = 4 * 1024;

namespace siho
{
	FxComputePass::FxComputePass()
	{
		calculate_shader_ = vkb::ShaderSource{ "particles/particle_calculate.comp" };
		integrate_shader_ = vkb::ShaderSource{ "particles/particle_integrate.comp" };
		num_particles = 6 * kParticleCount;
		ubo_.particle_count = num_particles;
		ubo_.delta_time = 0.0f;
	}

	void FxComputePass::init(vkb::RenderContext& render_context)
	{
		render_context_ = &render_context;
		auto& resource_cache = render_context_->get_device().get_resource_cache();
		auto& calculate_module = resource_cache.request_shader_module(VK_SHADER_STAGE_COMPUTE_BIT, calculate_shader_);
		auto& integrate_module = resource_cache.request_shader_module(VK_SHADER_STAGE_COMPUTE_BIT, integrate_shader_);
		prepare_storage_buffers();
	}

	void FxComputePass::dispatch(vkb::CommandBuffer& command_buffer, float delta_time)
	{
		ubo_.delta_time = delta_time;
		auto& render_frame = render_context_->get_active_frame();
		auto allocation = render_frame.allocate_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(ubo_));
		allocation.update(ubo_);
		command_buffer.bind_buffer(allocation.get_buffer(), allocation.get_offset(), allocation.get_size(), 0, 1, 0);

		vkb::BufferMemoryBarrier buffer_barrier{};
		buffer_barrier.src_access_mask = 0;
		buffer_barrier.dst_access_mask = VK_ACCESS_SHADER_WRITE_BIT;
		buffer_barrier.src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		buffer_barrier.dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		command_buffer.buffer_memory_barrier(*storage_buffer_, 0, storage_buffer_->get_size(), buffer_barrier);

		auto& resource_cache = render_context_->get_device().get_resource_cache();
		auto& calculate_module = resource_cache.request_shader_module(VK_SHADER_STAGE_COMPUTE_BIT, calculate_shader_);
		auto& integrate_module = resource_cache.request_shader_module(VK_SHADER_STAGE_COMPUTE_BIT, integrate_shader_);

		{
			const std::vector<vkb::ShaderModule*> shader_modules{ &calculate_module };

			auto& pipeline_layout = resource_cache.request_pipeline_layout(shader_modules);

			command_buffer.bind_pipeline_layout(pipeline_layout);

			command_buffer.set_specialization_constant(0, work_group_size);

			const vkb::DescriptorSetLayout& descriptor_set_layout = pipeline_layout.get_descriptor_set_layout(0);

			if (const auto layout_bingding = descriptor_set_layout.get_layout_binding("Pos"))
			{
				command_buffer.bind_buffer(*storage_buffer_, 0, storage_buffer_->get_size(), 0, layout_bingding->binding, 0);
			}
			command_buffer.dispatch(num_particles / work_group_size, 1, 1);
		}

		buffer_barrier.src_access_mask = VK_ACCESS_SHADER_WRITE_BIT;
		buffer_barrier.dst_access_mask = VK_ACCESS_SHADER_READ_BIT;
		buffer_barrier.src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		buffer_barrier.dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		command_buffer.buffer_memory_barrier(*storage_buffer_, 0, storage_buffer_->get_size(), buffer_barrier);

		{
			const std::vector<vkb::ShaderModule*> shader_modules{ &integrate_module };

			auto& pipeline_layout = resource_cache.request_pipeline_layout(shader_modules);

			command_buffer.bind_pipeline_layout(pipeline_layout);

			command_buffer.set_specialization_constant(0, work_group_size);

			const vkb::DescriptorSetLayout& descriptor_set_layout = pipeline_layout.get_descriptor_set_layout(0);

			/*if (const auto layout_bingding = descriptor_set_layout.get_layout_binding("Pos"))
			{
				command_buffer.bind_buffer(*storage_buffer_, 0, storage_buffer_->get_size(), 0, layout_bingding->binding, 0);
			}*/
			command_buffer.dispatch(num_particles / work_group_size, 1, 1);
		}

		buffer_barrier.src_access_mask = VK_ACCESS_SHADER_WRITE_BIT;
		buffer_barrier.dst_access_mask = 0;
		buffer_barrier.src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		buffer_barrier.dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		command_buffer.buffer_memory_barrier(*storage_buffer_, 0, storage_buffer_->get_size(), buffer_barrier);
	}

	void FxComputePass::update_uniform(vkb::CommandBuffer& command_buffer)
	{

	}

	FxGraphSubpass::FxGraphSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera)
		: Subpass(render_context, std::move(vertex_shader), std::move(fragment_shader)),
		camera_(camera)
	{
	}

	void FxGraphSubpass::prepare()
	{
		num_particles_ = 6* kParticleCount;
		auto& resource_cache = render_context.get_device().get_resource_cache();
		resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
		resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());
		vertex_input_state_.bindings = {
			vkb::initializers::vertex_input_binding_description(0,sizeof(Particle),VK_VERTEX_INPUT_RATE_VERTEX)
		};
		vertex_input_state_.attributes = {
			vkb::initializers::vertex_input_attribute_description(0,0,VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle,pos)),
			vkb::initializers::vertex_input_attribute_description(0, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, vel))
		};

		textures_.gradient = load_texture("textures/particle_gradient_rgba.ktx", vkb::sg::Image::Color, render_context.get_device());
		textures_.particle = load_texture("textures/particle_rgba.ktx", vkb::sg::Image::Color, render_context.get_device());
	}

	void FxGraphSubpass::draw(vkb::CommandBuffer& command_buffer)
	{
		vkb::BufferMemoryBarrier buffer_barrier{};
		buffer_barrier.src_access_mask = 0;
		buffer_barrier.dst_access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		buffer_barrier.src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		buffer_barrier.dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		//command_buffer.buffer_memory_barrier(*storage_buffer_, 0, storage_buffer_->get_size(), buffer_barrier);

		update_uniform(command_buffer);

		auto& resource_cache = command_buffer.get_device().get_resource_cache();
		auto& vert_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
		auto& frag_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());

		std::vector<vkb::ShaderModule*> shader_modules{ &vert_shader_module, &frag_shader_module };

		auto& pipeline_layout = resource_cache.request_pipeline_layout(shader_modules);
		command_buffer.bind_pipeline_layout(pipeline_layout);

		vkb::InputAssemblyState input_assembly_state;
		input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		command_buffer.set_input_assembly_state(input_assembly_state);

		vkb::RasterizationState rasterization_state;
		rasterization_state.cull_mode = VK_CULL_MODE_NONE;
		command_buffer.set_rasterization_state(rasterization_state);

		vkb::DepthStencilState depth_stencil_state;
		//depth_stencil_state.depth_test_enable = VK_FALSE;
		depth_stencil_state.depth_write_enable = VK_FALSE;
		//depth_stencil_state.depth_compare_op = VK_COMPARE_OP_ALWAYS;
		command_buffer.set_depth_stencil_state(depth_stencil_state);

		vkb::ColorBlendAttachmentState color_blend_attachment_state;
		color_blend_attachment_state.blend_enable = VK_TRUE;
		color_blend_attachment_state.color_write_mask = 0xf;
		color_blend_attachment_state.color_blend_op = VK_BLEND_OP_ADD;
		color_blend_attachment_state.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
		color_blend_attachment_state.dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
		color_blend_attachment_state.src_alpha_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA;
		color_blend_attachment_state.dst_alpha_blend_factor = VK_BLEND_FACTOR_DST_ALPHA;
		vkb::ColorBlendState color_blend_state;
		color_blend_state.attachments = { color_blend_attachment_state };
		command_buffer.set_color_blend_state(color_blend_state);

		command_buffer.set_vertex_input_state(vertex_input_state_);

		std::vector<std::reference_wrapper<const vkb::core::Buffer>> buffers;
		buffers.emplace_back(std::ref(*storage_buffer_));
		command_buffer.bind_vertex_buffers(0, std::move(buffers), { 0 });

		vkb::DescriptorSetLayout& descriptor_set_layout = pipeline_layout.get_descriptor_set_layout(0);
		if (auto layout_bingding = descriptor_set_layout.get_layout_binding("samplerColorMap"))
		{
			command_buffer.bind_image(textures_.particle.image->get_vk_image_view(),
				*textures_.particle.sampler,
				0, layout_bingding->binding, 0);
		}
		if (auto layout_bingding = descriptor_set_layout.get_layout_binding("samplerGradientRamp"))
		{
			command_buffer.bind_image(textures_.gradient.image->get_vk_image_view(),
				*textures_.gradient.sampler,
				0, layout_bingding->binding, 0);
		}

		command_buffer.draw(num_particles_, 1, 0, 0);

		input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		command_buffer.set_input_assembly_state(input_assembly_state);

		buffer_barrier.src_access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		buffer_barrier.dst_access_mask = 0;
		buffer_barrier.src_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		buffer_barrier.dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		//command_buffer.buffer_memory_barrier(*storage_buffer_, 0, storage_buffer_->get_size(), buffer_barrier);

	}

	void FxGraphSubpass::set_storage_buffer(std::shared_ptr<vkb::core::Buffer> storage_buffer)
	{
		storage_buffer_ = std::move(storage_buffer);
	}

	void FxGraphSubpass::update_uniform(vkb::CommandBuffer& command_buffer)
	{
		GlobalUniform global_uniform{};

		global_uniform.camera_view = camera_.get_view();
		global_uniform.camera_proj = camera_.get_pre_rotation() * vkb::vulkan_style_projection(camera_.get_projection());

		auto& render_frame = get_render_context().get_active_frame();

		auto extent = render_frame.get_render_target().get_extent();
		global_uniform.screen_dim = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));

		auto allocation = render_frame.allocate_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(GlobalUniform));

		// Set the camera's position in world coordinates by inverting the view matrix and extracting its translation component

		allocation.update(global_uniform);

		command_buffer.bind_buffer(allocation.get_buffer(), allocation.get_offset(), allocation.get_size(), 0, 2, 0);
	}

	std::shared_ptr<vkb::core::Buffer> FxComputePass::get_storage_buffer()
	{
		return storage_buffer_;
	}

	void FxComputePass::prepare_storage_buffers()
	{
		std::vector<glm::vec3> attractors = {
		glm::vec3(5.0f, 0.0f, 0.0f),
		glm::vec3(-5.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 5.0f),
		glm::vec3(0.0f, 0.0f, -5.0f),
		glm::vec3(0.0f, 4.0f, 0.0f),
		glm::vec3(0.0f, -8.0f, 0.0f),
		};

		num_particles = static_cast<uint32_t>(attractors.size()) * kParticleCount;

		size_t vertex_data_size = num_particles * sizeof(Particle);

		storage_buffer_ = std::make_shared<vkb::core::Buffer>(render_context_->get_device(), vertex_data_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		// Initial particle positions
		std::vector<Particle> particle_buffer(num_particles);

		std::default_random_engine      rnd_engine(static_cast<unsigned>(time(nullptr)));
		std::normal_distribution<float> rnd_distribution(0.0f, 1.0f);

		for (uint32_t i = 0; i < static_cast<uint32_t>(attractors.size()); i++)
		{
			for (uint32_t j = 0; j < kParticleCount; j++)
			{
				Particle& particle = particle_buffer[i * kParticleCount + j];

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
				particle.pos = glm::mat4(150.0f) * particle.pos;

				// Color gradient offset
				particle.vel.w = static_cast<float>(i) * 1.0f / static_cast<uint32_t>(attractors.size());
			}
		}

		const uint8_t* vertex_data = reinterpret_cast<const uint8_t*>(particle_buffer.data());
		storage_buffer_->update(vertex_data, vertex_data_size);
	}
}
