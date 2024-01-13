#pragma once

#include <memory>

#include <scene_graph/components/image.h>
#include <core/sampler.h>
#include <core/command_buffer.h>

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
}