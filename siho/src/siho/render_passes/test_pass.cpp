#include "test_pass.h"

#include <common/vk_initializers.h>


namespace siho
{
	TestSubpass::TestSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader,
		vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera) : Subpass(render_context, std::move(vertex_shader), std::move(fragment_shader)),
		camera_(camera)
	{
	}

	void TestSubpass::prepare()
	{
		auto& resource_cache = render_context.get_device().get_resource_cache();
		resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
		resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());
		vertex_input_state_.bindings = {
			vkb::initializers::vertex_input_binding_description(0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX)
		};
		vertex_input_state_.attributes = {
			vkb::initializers::vertex_input_attribute_description(0,0,VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,position))
		};
		vertex_buffer_ = std::make_unique<vkb::core::Buffer>(render_context.get_device(), sizeof(Vertex) * 3, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		std::vector<Vertex> vertices = {
			Vertex(glm::vec3(-400.5f, 8.5f, -225.0f)),
			Vertex(glm::vec3(-400.5f, 108.5f, -225.0f)),
			Vertex(glm::vec3(-400.0f, 8.5f, 140.0f))
		};
		const uint8_t* vertex_data = reinterpret_cast<const uint8_t*>(vertices.data());
		size_t vertex_data_size = vertices.size() * sizeof(Vertex);
		vertex_buffer_->update(vertex_data, vertex_data_size);

		texture_ = load_texture("textures/particle_rgba.ktx", vkb::sg::Image::Color, render_context.get_device());
	}

	void TestSubpass::draw(vkb::CommandBuffer& command_buffer)
	{
		update_uniform(command_buffer);
		// Get shaders from cache
		auto& resource_cache = command_buffer.get_device().get_resource_cache();
		auto& vert_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
		auto& frag_shader_module = resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());

		std::vector<vkb::ShaderModule*> shader_modules{ &vert_shader_module, &frag_shader_module };

		// Create pipeline layout and bind it
		// The construction of the PipelineLayout class requires a ShaderModule.
		// The ShaderModule is capable of automatically parsing the shader code to extract information about certain resources.
		// Utilizing this information, the PipelineLayout can automatically generate the DescriptorSetLayout.
		// Note that, currently, this process only considers the first descriptor set (set 0) in Vulkan.
		auto& pipeline_layout = resource_cache.request_pipeline_layout(shader_modules);
		command_buffer.bind_pipeline_layout(pipeline_layout);

		// Get image views of the attachments
		/*auto& render_target = get_render_context().get_active_frame().get_render_target();
		auto& target_views = render_target.get_views();
		assert(4 < target_views.size());
		auto& light_view = target_views[4];
		command_buffer.bind_input(light_view, 0, 0, 0);*/

		vkb::RasterizationState rasterization_state;
		rasterization_state.cull_mode = VK_CULL_MODE_NONE;
		command_buffer.set_rasterization_state(rasterization_state);

		command_buffer.set_vertex_input_state(vertex_input_state_);

		std::vector<std::reference_wrapper<const vkb::core::Buffer>> buffers;
		buffers.emplace_back(std::ref(*vertex_buffer_));
		command_buffer.bind_vertex_buffers(0, std::move(buffers), { 0 });

		vkb::DescriptorSetLayout& descriptor_set_layout = pipeline_layout.get_descriptor_set_layout(0);
		if (auto layout_bingding = descriptor_set_layout.get_layout_binding("test_texture"))
		{
			command_buffer.bind_image(texture_.image->get_vk_image_view(),
				*texture_.sampler,
				0, 0, 0);
		}

		command_buffer.draw(3, 1, 0, 0);
	}

	void TestSubpass::update_uniform(vkb::CommandBuffer& command_buffer)
	{
		GlobalUniform global_uniform{};
		
		global_uniform.camera_view_proj = camera_.get_pre_rotation() * vkb::vulkan_style_projection(camera_.get_projection() * camera_.get_view());

		auto& render_frame = get_render_context().get_active_frame();

		auto allocation = render_frame.allocate_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(GlobalUniform));

		// Set the camera's position in world coordinates by inverting the view matrix and extracting its translation component
		global_uniform.camera_position = glm::vec3(glm::inverse(camera_.get_view())[3]);

		global_uniform.model = glm::mat4(1.0f);

		allocation.update(global_uniform);

		command_buffer.bind_buffer(allocation.get_buffer(), allocation.get_offset(), allocation.get_size(), 0, 1, 0);
	}

	inline void upload_image_to_gpu(vkb::CommandBuffer& command_buffer, vkb::core::Buffer& staging_buffer, vkb::sg::Image& image)
	{
		// Clean up the image data, as they are copied in the staging buffer
		image.clear_data();

		{
			vkb::ImageMemoryBarrier memory_barrier{};
			memory_barrier.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			memory_barrier.new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			memory_barrier.src_access_mask = 0;
			memory_barrier.dst_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
			memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_HOST_BIT;
			memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;

			command_buffer.image_memory_barrier(image.get_vk_image_view(), memory_barrier);
		}

		// Create a buffer image copy for every mip level
		auto& mipmaps = image.get_mipmaps();

		std::vector<VkBufferImageCopy> buffer_copy_regions(mipmaps.size());

		for (size_t i = 0; i < mipmaps.size(); ++i)
		{
			auto& mipmap = mipmaps[i];
			auto& copy_region = buffer_copy_regions[i];

			copy_region.bufferOffset = mipmap.offset;
			copy_region.imageSubresource = image.get_vk_image_view().get_subresource_layers();
			// Update miplevel
			copy_region.imageSubresource.mipLevel = mipmap.level;
			copy_region.imageExtent = mipmap.extent;
		}

		command_buffer.copy_buffer_to_image(staging_buffer, image.get_vk_image(), buffer_copy_regions);

		{
			vkb::ImageMemoryBarrier memory_barrier{};
			memory_barrier.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			memory_barrier.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			memory_barrier.src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
			memory_barrier.dst_access_mask = VK_ACCESS_SHADER_READ_BIT;
			memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

			command_buffer.image_memory_barrier(image.get_vk_image_view(), memory_barrier);
		}
	}

	Texture load_texture(const std::string& file, vkb::sg::Image::ContentType content_type, const vkb::Device& device)
	{
		auto image = vkb::sg::Image::load(file, file, content_type);
		image->create_vk_image(device);
		std::vector<vkb::core::Buffer> transient_buffers;

		auto& command_buffer = device.request_command_buffer();

		command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, 0);


		vkb::core::Buffer stage_buffer{ device,
								  image->get_data().size(),
								  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
								  VMA_MEMORY_USAGE_CPU_ONLY };

		stage_buffer.update(image->get_data());

		upload_image_to_gpu(command_buffer, stage_buffer, *image);

		transient_buffers.push_back(std::move(stage_buffer));

		command_buffer.end();

		auto& queue = device.get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0);

		queue.submit(command_buffer, device.request_fence());

		device.get_fence_pool().wait();
		device.get_fence_pool().reset();
		device.get_command_pool().reset_pool();
		device.wait_idle();

		// Remove the staging buffers for the batch we just processed
		transient_buffers.clear();

		Texture texture{};
		texture.image = std::move(image);

		// Create a defaultsampler
		auto& mipmaps = texture.image->get_mipmaps();

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
		texture.sampler = std::make_unique<vkb::core::Sampler>(device, sampler_create_info);
		return texture;
	}
}
