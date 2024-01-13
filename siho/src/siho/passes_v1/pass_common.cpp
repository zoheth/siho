#include "pass_common.h"

#include <core/device.h>

namespace siho
{
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
