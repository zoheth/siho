#include "particles.h"

#include <random>

constexpr uint32_t kParticleCount = 4 * 1024;

namespace
{
	VkPipelineShaderStageCreateInfo load_shader(const std::string& file, VkShaderStageFlagBits stage, const vkb::Device& device)
	{
		VkPipelineShaderStageCreateInfo shader_stage = {};
		shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shader_stage.stage = stage;
		shader_stage.module = vkb::load_shader(file.c_str(), device.get_handle(), stage);
		shader_stage.pName = "main";
		assert(shader_stage.module != VK_NULL_HANDLE);
		return shader_stage;
	}

	VkDescriptorBufferInfo create_descriptor(const vkb::core::Buffer& buffer, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
	{
		VkDescriptorBufferInfo descriptor_buffer_info{};
		descriptor_buffer_info.buffer = buffer.get_handle();
		descriptor_buffer_info.offset = offset;
		descriptor_buffer_info.range = size;
		return descriptor_buffer_info;
	}

	VkDescriptorImageInfo create_descriptor(const siho::Texture& texture, VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
	{
		VkDescriptorImageInfo descriptor_image_info{};
		descriptor_image_info.sampler = texture.sampler;
		descriptor_image_info.imageView = texture.image->get_vk_image_view().get_handle();

		switch (descriptor_type)
		{
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			if (vkb::is_depth_stencil_format(texture.image->get_vk_image_view().get_format()))
			{
				descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			else
			{
				assert(!vkb::is_depth_format(texture.image->get_vk_image_view().get_format()));
				descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			break;
		case  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			break;
		default:
			descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			break;
		}
		return descriptor_image_info;
	}
}

namespace siho
{
	void ParticlePass::init(vkb::RenderContext& render_context, vkb::sg::Camera& camera)
	{
		vkb::Device& device = render_context.get_device();
		device_ = &device;
		queue_ = device.get_suitable_graphics_queue().get_handle();
		camera_ = &camera;
		width_ = render_context.get_surface_extent().width;
		height_ = render_context.get_surface_extent().height;

		/** Create Pipeline Cache **/
		VkPipelineCacheCreateInfo pipeline_cache_create_info = {};
		pipeline_cache_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK(vkCreatePipelineCache(device.get_handle(), &pipeline_cache_create_info, nullptr, &pipeline_cache_));
		/** Create Pipeline Cache END **/

		/** Create cmd_pool command_buffers **/
		VkCommandPoolCreateInfo command_pool_info = {};
		command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		command_pool_info.queueFamilyIndex = device.get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0).get_family_index();
		VK_CHECK(vkCreateCommandPool(device.get_handle(), &command_pool_info, nullptr, &cmd_pool_));

		draw_cmd_buffers_.resize(render_context.get_render_frames().size());
		VkCommandBufferAllocateInfo allocate_info = 
			vkb::initializers::command_buffer_allocate_info(
				cmd_pool_,
				VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				static_cast<uint32_t>(draw_cmd_buffers_.size()));
		/** Create cmd_pool command_buffers END **/

		graphics.queue_family_index = device.get_queue_family_index(VK_QUEUE_GRAPHICS_BIT);
		compute.queue_family_index = device.get_queue_family_index(VK_QUEUE_COMPUTE_BIT);

		// Not all implementations support a work group size of 256, so we need to check with the device limits
		work_group_size = std::min(static_cast<uint32_t>(256), device.get_gpu().get_properties().limits.maxComputeWorkGroupSize[0]);
		// Same for shared data size for passing data between shader invocations
		shared_data_size = std::min(static_cast<uint32_t>(1024), static_cast<uint32_t>(device.get_gpu().get_properties().limits.maxComputeSharedMemorySize / sizeof(glm::vec4)));

		/** Load assert **/
		textures.particle = load_texture("textures/particle_rgba.ktx", vkb::sg::Image::Color, device);
		textures.gradient = load_texture("textures/particle_gradient_rgba.ktx", vkb::sg::Image::Color, device);

		/** Setup descriptor pool **/
		std::vector<VkDescriptorPoolSize> pool_sizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
		};

		VkDescriptorPoolCreateInfo descriptor_pool_create_info =
			vkb::initializers::descriptor_pool_create_info(
				static_cast<uint32_t>(pool_sizes.size()),
				pool_sizes.data(),
				2);
		VK_CHECK(vkCreateDescriptorPool(device.get_handle(), &descriptor_pool_create_info, nullptr, &descriptor_pool_));

	}

	void ParticlePass::build_command_buffers()
	{
		VkCommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

		VkClearValue clear_values[2];
		clear_values[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
		clear_values[1].depthStencil = { 0.0f, 0 };

		VkRenderPassBeginInfo render_pass_begin_info = vkb::initializers::render_pass_begin_info();
		render_pass_begin_info.renderPass = render_pass_;
		render_pass_begin_info.renderArea.extent.width = width_;
		render_pass_begin_info.renderArea.extent.height = height_;
		render_pass_begin_info.renderArea.offset.x = 0;
		render_pass_begin_info.renderArea.offset.y = 0;
		render_pass_begin_info.clearValueCount = 2;
		render_pass_begin_info.pClearValues = clear_values;

		for(int32_t i =0 ;i<draw_cmd_buffers_.size(), ++i)
		{
			render_pass_begin_info.framebuffer = fram
		}
	}

	void ParticlePass::build_compute_command_buffer()
	{
		VkCommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

		VK_CHECK(vkBeginCommandBuffer(compute.command_buffer, &command_buffer_begin_info));

		// Acquire
		if (graphics.queue_family_index != compute.queue_family_index)
		{
			VkBufferMemoryBarrier buffer_barrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				0,
				VK_ACCESS_SHADER_WRITE_BIT,
				graphics.queue_family_index,
				compute.queue_family_index,
				compute.storage_buffer->get_handle(),
				0,
				compute.storage_buffer->get_size() };

			vkCmdPipelineBarrier(
				compute.command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				1, &buffer_barrier,
				0, nullptr);
		}

		/** 1st pass: Calculate particle movement **/
		vkCmdBindPipeline(compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline_calculate);
		vkCmdBindDescriptorSets(compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline_layout, 0, 1, &compute.descriptor_set, 0, nullptr);
		vkCmdDispatch(compute.command_buffer, num_particles / work_group_size, 1, 1);

		// Ensure all writes to the storage buffer have been completed
		VkBufferMemoryBarrier memory_barrier = vkb::initializers::buffer_memory_barrier();
		memory_barrier.buffer = compute.storage_buffer->get_handle();
		memory_barrier.size = compute.storage_buffer->get_size();
		memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		vkCmdPipelineBarrier(
			compute.command_buffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0, nullptr,
			1, &memory_barrier,
			0, nullptr);
		/** 1st pass END **/

		/** 2nd pass: Integrate particle movement **/
		vkCmdBindPipeline(compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline_integrate);
		vkCmdDispatch(compute.command_buffer, num_particles / work_group_size, 1, 1);
		/** 2nd pass END **/

		// Release
		if (graphics.queue_family_index != compute.queue_family_index)
		{
			VkBufferMemoryBarrier buffer_barrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				VK_ACCESS_SHADER_WRITE_BIT,
				0,
				compute.queue_family_index,
				graphics.queue_family_index,
				compute.storage_buffer->get_handle(),
				0,
				compute.storage_buffer->get_size() };

			vkCmdPipelineBarrier(
				compute.command_buffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				1, &buffer_barrier,
				0, nullptr);
		}

		vkEndCommandBuffer(compute.command_buffer);
	}

	void ParticlePass::prepare_graphics()
	{
		prepare_storage_buffers();
		prepare_uniform_buffers();
		setup_descriptor_set_layout();
		prepare_pipelines();
		setup_descriptor_set();

		// Semaphore for compute & graphics sync
		VkSemaphoreCreateInfo semaphore_create_info = vkb::initializers::semaphore_create_info();
		VK_CHECK(vkCreateSemaphore(device_->get_handle(), &semaphore_create_info, nullptr, &graphics.semaphore));
	}

	void ParticlePass::prepare_storage_buffers()
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

				// Color gradient offset
				particle.vel.w = static_cast<float>(i) * 1.0f / static_cast<uint32_t>(attractors.size());
			}
		}

		compute.ubo.particle_count = num_particles;

		VkDeviceSize storage_buffer_size = particle_buffer.size() * sizeof(Particle);

		vkb::core::Buffer staging_buffer{ *device_, storage_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY };
		staging_buffer.update(particle_buffer.data(), storage_buffer_size);

		compute.storage_buffer = std::make_unique<vkb::core::Buffer>(*device_, storage_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

		VkCommandBuffer copy_command = device_->create_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy    copy_region = {};
		copy_region.size = storage_buffer_size;
		vkCmdCopyBuffer(copy_command, staging_buffer.get_handle(), compute.storage_buffer->get_handle(), 1, &copy_region);
		// Execute a transfer to the compute queue, if necessary
		if (graphics.queue_family_index != compute.queue_family_index)
		{
			VkBufferMemoryBarrier buffer_barrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
				0,
				graphics.queue_family_index,
				compute.queue_family_index,
				compute.storage_buffer->get_handle(),
				0,
				compute.storage_buffer->get_size() };

			vkCmdPipelineBarrier(
				copy_command,
				VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				1, &buffer_barrier,
				0, nullptr);
		}

		device_->flush_command_buffer(copy_command, queue_, true);

	}

	void ParticlePass::prepare_uniform_buffers()
	{
		compute.uniform_buffer = std::make_unique<vkb::core::Buffer>(*device_, sizeof(compute.ubo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		graphics.uniform_buffer = std::make_unique<vkb::core::Buffer>(*device_, sizeof(graphics.ubo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		update_compute_uniform_buffers(1.0f);
		update_graphics_uniform_buffers();
	}

	void ParticlePass::setup_descriptor_set_layout()
	{
		std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings;
		set_layout_bindings = {
			vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,	1),
			vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 2),
		};

		VkDescriptorSetLayoutCreateInfo descriptor_layout =
			vkb::initializers::descriptor_set_layout_create_info(
				set_layout_bindings.data(),
				static_cast<uint32_t>(set_layout_bindings.size()));
		VK_CHECK(vkCreateDescriptorSetLayout(device_->get_handle(), &descriptor_layout, nullptr, &graphics.descriptor_set_layout));

		VkPipelineLayoutCreateInfo pipeline_layout_create_info =
			vkb::initializers::pipeline_layout_create_info(
				&graphics.descriptor_set_layout,
				1);

		VK_CHECK(vkCreatePipelineLayout(device_->get_handle(), &pipeline_layout_create_info, nullptr, &graphics.pipeline_layout));
	}

	void ParticlePass::prepare_pipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo input_assembly_state =
			vkb::initializers::pipeline_input_assembly_state_create_info(
				VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterization_state =
			vkb::initializers::pipeline_rasterization_state_create_info(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_NONE,
				VK_FRONT_FACE_COUNTER_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blend_attachment_state =
			vkb::initializers::pipeline_color_blend_attachment_state(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo color_blend_state =
			vkb::initializers::pipeline_color_blend_state_create_info(
				1,
				&blend_attachment_state);

		VkPipelineDepthStencilStateCreateInfo depth_stencil_state =
			vkb::initializers::pipeline_depth_stencil_state_create_info(
				VK_FALSE,
				VK_FALSE,
				VK_COMPARE_OP_ALWAYS);

		VkPipelineViewportStateCreateInfo viewport_state =
			vkb::initializers::pipeline_viewport_state_create_info(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisample_state =
			vkb::initializers::pipeline_multisample_state_create_info(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamic_state_enables = {
					VK_DYNAMIC_STATE_VIEWPORT,
					VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamic_state =
			vkb::initializers::pipeline_dynamic_state_create_info(
				dynamic_state_enables.data(),
				static_cast<uint32_t>(dynamic_state_enables.size()),
				0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages;

		shader_stages[0] = load_shader("particles/particle.vert", VK_SHADER_STAGE_VERTEX_BIT, *device_);
		shader_stages[1] = load_shader("particles/particle.frag", VK_SHADER_STAGE_FRAGMENT_BIT, *device_);

		const std::vector<VkVertexInputBindingDescription> vertex_input_bindings = {
			vkb::initializers::vertex_input_binding_description(0, sizeof(Particle), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		const std::vector<VkVertexInputAttributeDescription> vertex_input_attributes = {
			vkb::initializers::vertex_input_attribute_description(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, pos)),	// Location 0: Position
			vkb::initializers::vertex_input_attribute_description(0, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, vel)),	// Location 1: Velocity
		};
		VkPipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
		vertex_input_state.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_input_bindings.size());
		vertex_input_state.pVertexBindingDescriptions = vertex_input_bindings.data();
		vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attributes.size());
		vertex_input_state.pVertexAttributeDescriptions = vertex_input_attributes.data();

		VkGraphicsPipelineCreateInfo pipeline_create_info =
			vkb::initializers::pipeline_create_info(
				graphics.pipeline_layout,
				render_pass_,
				0);

		pipeline_create_info.pVertexInputState = &vertex_input_state;
		pipeline_create_info.pInputAssemblyState = &input_assembly_state;
		pipeline_create_info.pRasterizationState = &rasterization_state;
		pipeline_create_info.pColorBlendState = &color_blend_state;
		pipeline_create_info.pMultisampleState = &multisample_state;
		pipeline_create_info.pViewportState = &viewport_state;
		pipeline_create_info.pDepthStencilState = &depth_stencil_state;
		pipeline_create_info.pDynamicState = &dynamic_state;
		pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
		pipeline_create_info.pStages = shader_stages.data();
		pipeline_create_info.renderPass = render_pass_;

		blend_attachment_state.colorWriteMask = 0xF;
		blend_attachment_state.blendEnable = VK_TRUE;
		blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
		blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

		VK_CHECK(vkCreateGraphicsPipelines(device_->get_handle(), pipeline_cache_, 1, &pipeline_create_info, nullptr, &graphics.pipeline));
	}

	void ParticlePass::setup_descriptor_set()
	{
		VkDescriptorSetAllocateInfo alloc_info =
			vkb::initializers::descriptor_set_allocate_info(
				descriptor_pool_,
				&graphics.descriptor_set_layout,
				1);

		VK_CHECK(vkAllocateDescriptorSets(device_->get_handle(), &alloc_info, &graphics.descriptor_set));

		VkDescriptorBufferInfo buffer_descriotor = create_descriptor(*graphics.uniform_buffer);
		VkDescriptorImageInfo particle_image_descriptor = create_descriptor(textures.particle);
		VkDescriptorImageInfo gradient_image_descriptor = create_descriptor(textures.gradient);
		std::vector<VkWriteDescriptorSet> write_descriptor_sets = {
			vkb::initializers::write_descriptor_set(graphics.descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &particle_image_descriptor),
			vkb::initializers::write_descriptor_set(graphics.descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &gradient_image_descriptor),
			vkb::initializers::write_descriptor_set(graphics.descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &buffer_descriotor),
		};
		vkUpdateDescriptorSets(device_->get_handle(), static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
	}

	void ParticlePass::prepare_compute()
	{
		vkGetDeviceQueue(device_->get_handle(), compute.queue_family_index, 0, &compute.queue);

		/** Create compute pipeline **/
		// Compute pipelines are created separate from graphics pipelines even if they use the same queue (family index)

		/** Prepare descriptors **/
		std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
			vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
		};

		VkDescriptorSetLayoutCreateInfo descriptor_layout =
			vkb::initializers::descriptor_set_layout_create_info(
				set_layout_bindings.data(),
				static_cast<uint32_t>(set_layout_bindings.size()));

		VK_CHECK(vkCreateDescriptorSetLayout(device_->get_handle(), &descriptor_layout, nullptr, &compute.descriptor_set_layout));

		VkPipelineLayoutCreateInfo pipeline_layout_create_info =
			vkb::initializers::pipeline_layout_create_info(
				&compute.descriptor_set_layout,
				1);
		VK_CHECK(vkCreatePipelineLayout(device_->get_handle(), &pipeline_layout_create_info, nullptr, &compute.pipeline_layout));

		VkDescriptorSetAllocateInfo alloc_info =
			vkb::initializers::descriptor_set_allocate_info(
				descriptor_pool_,
				&compute.descriptor_set_layout,
				1);

		VK_CHECK(vkAllocateDescriptorSets(device_->get_handle(), &alloc_info, &compute.descriptor_set));

		VkDescriptorBufferInfo storage_buffer_descriptor = create_descriptor(*compute.storage_buffer);
		VkDescriptorBufferInfo uniform_buffer_descriptor = create_descriptor(*compute.uniform_buffer);
		std::vector<VkWriteDescriptorSet> write_descriptor_sets = {
			vkb::initializers::write_descriptor_set(compute.descriptor_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &storage_buffer_descriptor),
			vkb::initializers::write_descriptor_set(compute.descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniform_buffer_descriptor),
		};

		vkUpdateDescriptorSets(device_->get_handle(), static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
		/** Prepare descriptors END **/

		/** Create pipelines **/
		VkComputePipelineCreateInfo compute_pipeline_create_info = vkb::initializers::compute_pipeline_create_info(compute.pipeline_layout, 0);

		/** 1st pass - Particle movement computation **/
		compute_pipeline_create_info.stage = load_shader("particles/particle_calculate.comp", VK_SHADER_STAGE_COMPUTE_BIT, *device_);

		// Set some shader parameters vis specialization constants
		struct SpecializationData
		{
			uint32_t workgroup_size;
			uint32_t shared_data_size;
			float gravity;
			float power;
			float soften;
		} specialization_data;

		std::vector<VkSpecializationMapEntry> specialization_map_entries = {
			vkb::initializers::specialization_map_entry(0, offsetof(SpecializationData, workgroup_size), sizeof(uint32_t)),
			vkb::initializers::specialization_map_entry(1, offsetof(SpecializationData, shared_data_size), sizeof(uint32_t)),
			vkb::initializers::specialization_map_entry(2, offsetof(SpecializationData, gravity), sizeof(float)),
			vkb::initializers::specialization_map_entry(3, offsetof(SpecializationData, power), sizeof(float)),
			vkb::initializers::specialization_map_entry(4, offsetof(SpecializationData, soften), sizeof(float)),
		};

		specialization_data.workgroup_size = work_group_size;
		specialization_data.shared_data_size = shared_data_size;
		specialization_data.gravity = 0.002f;
		specialization_data.power = 0.75f;
		specialization_data.soften = 0.05f;

		VkSpecializationInfo specialization_info =
			vkb::initializers::specialization_info(
				static_cast<uint32_t>(specialization_map_entries.size()),
				specialization_map_entries.data(),
				sizeof(specialization_data),
				&specialization_data);
		compute_pipeline_create_info.stage.pSpecializationInfo = &specialization_info;

		VK_CHECK(vkCreateComputePipelines(device_->get_handle(), pipeline_cache_, 1, &compute_pipeline_create_info, nullptr, &compute.pipeline_calculate));
		/** 1st pass - Particle movement computation END **/

		/** 2nd pass - Particle integration **/
		compute_pipeline_create_info.stage = load_shader("particles/particle_integrate.comp", VK_SHADER_STAGE_COMPUTE_BIT, *device_);

		specialization_map_entries.clear();
		specialization_map_entries.push_back(vkb::initializers::specialization_map_entry(0, 0, sizeof(uint32_t)));
		specialization_info =
			vkb::initializers::specialization_info(
				1,
				specialization_map_entries.data(),
				sizeof(work_group_size),
				&work_group_size);

		compute_pipeline_create_info.stage.pSpecializationInfo = &specialization_info;
		VK_CHECK(vkCreateComputePipelines(device_->get_handle(), pipeline_cache_, 1, &compute_pipeline_create_info, nullptr, &compute.pipeline_integrate));
		/** 2nd pass - Particle integration END **/

		// Separate command pool as queue family for compute may be different than graphics
		VkCommandPoolCreateInfo command_pool_create_info = {};
		command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		command_pool_create_info.queueFamilyIndex = device_->get_queue_family_index(VK_QUEUE_COMPUTE_BIT);
		VK_CHECK(vkCreateCommandPool(device_->get_handle(), &command_pool_create_info, nullptr, &compute.command_pool));

		// Create a command buffer for compute operations
		VkCommandBufferAllocateInfo command_buffer_allocate_info =
			vkb::initializers::command_buffer_allocate_info(
				compute.command_pool,
				VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				1);
		VK_CHECK(vkAllocateCommandBuffers(device_->get_handle(), &command_buffer_allocate_info, &compute.command_buffer));

		// Semaphore for compute & graphics sync
		VkSemaphoreCreateInfo semaphore_create_info = vkb::initializers::semaphore_create_info();
		VK_CHECK(vkCreateSemaphore(device_->get_handle(), &semaphore_create_info, nullptr, &compute.semaphore));

		// Signal the semaphore
		VkSubmitInfo submit_info = vkb::initializers::submit_info();
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &compute.semaphore;
		// todo Why is it queue_ instead of compute.queue here
		VK_CHECK(vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE));
		VK_CHECK(vkQueueWaitIdle(queue_));

		// Build a sigle command buffer containing the compute dispatch commands
		build_compute_command_buffer();

		// If necessary, acquire and immediately release the storage buffer, so that the initial acquire
		// from the graphics command buffers are matched up properly.
		if (graphics.queue_family_index != compute.queue_family_index)
		{
			VkCommandBuffer transfer_command;

			// Create a transient command buffer for setting up the initial buffer transfer state
			VkCommandBufferAllocateInfo command_buffer_allocate_info =
				vkb::initializers::command_buffer_allocate_info(
					compute.command_pool,
					VK_COMMAND_BUFFER_LEVEL_PRIMARY,
					1);

			VK_CHECK(vkAllocateCommandBuffers(device_->get_handle(), &command_buffer_allocate_info, &transfer_command));

			VkCommandBufferBeginInfo command_buffer_info = vkb::initializers::command_buffer_begin_info();
			VK_CHECK(vkBeginCommandBuffer(transfer_command, &command_buffer_info));

			VkBufferMemoryBarrier acquire_buffer_barrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				0,
				VK_ACCESS_SHADER_WRITE_BIT,
				graphics.queue_family_index,
				compute.queue_family_index,
				compute.storage_buffer->get_handle(),
				0,
				compute.storage_buffer->get_size() };
			vkCmdPipelineBarrier(
				transfer_command,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				1, &acquire_buffer_barrier,
				0, nullptr);

			VkBufferMemoryBarrier release_buffer_barrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				VK_ACCESS_SHADER_WRITE_BIT,
				0,
				compute.queue_family_index,
				graphics.queue_family_index,
				compute.storage_buffer->get_handle(),
				0,
				compute.storage_buffer->get_size() };
			vkCmdPipelineBarrier(
				transfer_command,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				1, &release_buffer_barrier,
				0, nullptr);

			// Copied from Device::flush_command_buffer, which we can't use because it would be
			// working with the wrong command pool
			VK_CHECK(vkEndCommandBuffer(transfer_command));

			// Submit compute commands
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &transfer_command;

			// Create fence to ensure that the command buffer has finished executing
			VkFenceCreateInfo fence_info{};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_info.flags = VK_FLAGS_NONE;

			VkFence fence;
			VK_CHECK(vkCreateFence(device_->get_handle(), &fence_info, nullptr, &fence));
			VkResult result = vkQueueSubmit(compute.queue, 1, &submit_info, fence);
			VK_CHECK(vkWaitForFences(device_->get_handle(), 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
			vkDestroyFence(device_->get_handle(), fence, nullptr);

			vkFreeCommandBuffers(device_->get_handle(), compute.command_pool, 1, &transfer_command);
		}
	}

	void ParticlePass::update_compute_uniform_buffers(float delta_time)
	{
		compute.ubo.delta_time = delta_time;
		compute.uniform_buffer->convert_and_update(compute.ubo);
	}

	void ParticlePass::update_graphics_uniform_buffers()
	{
		graphics.ubo.projection = camera_->get_projection();
		graphics.ubo.view = camera_->get_view();
		graphics.ubo.screen_dim = glm::vec2(static_cast<float>(width_), static_cast<float>(height_));
		graphics.uniform_buffer->convert_and_update(graphics.ubo);
	}

	void ParticlePass::setup_render_pass()
	{
		std::array<VkAttachmentDescription, 2> attachments = {};
		// Color attachment
		attachments[0].format = VK_FORMAT_R8G8B8A8_SRGB;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		auto depth_format = vkb::get_suitable_depth_format(device_->get_gpu().get_handle());
		attachments[1].format = depth_format;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference color_reference = {};
		color_reference.attachment = 0;
		color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_reference = {};
		depth_reference.attachment = 1;
		depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass_description = {};
		subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass_description.colorAttachmentCount = 1;
		subpass_description.pColorAttachments = &color_reference;
		subpass_description.pDepthStencilAttachment = &depth_reference;
		subpass_description.inputAttachmentCount = 0;
		subpass_description.pInputAttachments = nullptr;
		subpass_description.preserveAttachmentCount = 0;
		subpass_description.pPreserveAttachments = nullptr;
		subpass_description.pResolveAttachments = nullptr;

		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_NONE_KHR;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo render_pass_create_info = {};
		render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
		render_pass_create_info.pAttachments = attachments.data();
		render_pass_create_info.subpassCount = 1;
		render_pass_create_info.pSubpasses = &subpass_description;
		render_pass_create_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
		render_pass_create_info.pDependencies = dependencies.data();

		VK_CHECK(vkCreateRenderPass(device_->get_handle(), &render_pass_create_info, nullptr, &render_pass_));

	}


	Texture load_texture(const std::string& file, vkb::sg::Image::ContentType content_type, const vkb::Device& device)
	{
		Texture texture{};

		texture.image = vkb::sg::Image::load(file, file, content_type);

		texture.image->create_vk_image(device);

		const auto& queue = device.get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0);

		VkCommandBuffer command_buffer = device.create_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		vkb::core::Buffer staging_buffer{ device, texture.image->get_data().size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY };
		staging_buffer.update(texture.image->get_data());

		std::vector<VkBufferImageCopy> buffer_copy_regions;

		auto& mipmaps = texture.image->get_mipmaps();

		for (size_t i = 0; i < mipmaps.size(); ++i)
		{
			VkBufferImageCopy buffer_copy_region{};
			buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			buffer_copy_region.imageSubresource.mipLevel = vkb::to_u32(i);
			buffer_copy_region.imageSubresource.baseArrayLayer = 0;
			buffer_copy_region.imageSubresource.layerCount = 1;
			buffer_copy_region.imageExtent.width = texture.image->get_extent().width >> i;
			buffer_copy_region.imageExtent.height = texture.image->get_extent().height >> i;
			buffer_copy_region.imageExtent.depth = 1;
			buffer_copy_region.bufferOffset = mipmaps[i].offset;

			buffer_copy_regions.push_back(buffer_copy_region);
		}

		VkImageSubresourceRange subresource_range{};
		subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresource_range.baseMipLevel = 0;
		subresource_range.levelCount = vkb::to_u32(mipmaps.size());
		subresource_range.layerCount = 1;

		vkb::image_layout_transition(command_buffer,
			texture.image->get_vk_image().get_handle(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			subresource_range);

		vkCmdCopyBufferToImage(command_buffer,
			staging_buffer.get_handle(),
			texture.image->get_vk_image().get_handle(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			vkb::to_u32(buffer_copy_regions.size()),
			buffer_copy_regions.data());

		vkb::image_layout_transition(command_buffer,
			texture.image->get_vk_image().get_handle(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			subresource_range);

		device.flush_command_buffer(command_buffer, queue.get_handle());

		// Create a defaultsampler
		VkSamplerCreateInfo sampler_create_info = {};
		sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_create_info.magFilter = VK_FILTER_LINEAR;
		sampler_create_info.minFilter = VK_FILTER_LINEAR;
		sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.mipLodBias = 0.0f;
		sampler_create_info.compareOp = VK_COMPARE_OP_NEVER;
		sampler_create_info.minLod = 0.0f;
		// Max level-of-detail should match mip level count
		sampler_create_info.maxLod = static_cast<float>(mipmaps.size());
		// Only enable anisotropic filtering if enabled on the device
		// Note that for simplicity, we will always be using max. available anisotropy level for the current device
		// This may have an impact on performance, esp. on lower-specced devices
		// In a real-world scenario the level of anisotropy should be a user setting or e.g. lowered for mobile devices by default
		sampler_create_info.maxAnisotropy = device.get_gpu().get_features().samplerAnisotropy ? (device.get_gpu().get_properties().limits.maxSamplerAnisotropy) : 1.0f;
		sampler_create_info.anisotropyEnable = device.get_gpu().get_features().samplerAnisotropy;
		sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK(vkCreateSampler(device.get_handle(), &sampler_create_info, nullptr, &texture.sampler));

		return texture;
	}
}
