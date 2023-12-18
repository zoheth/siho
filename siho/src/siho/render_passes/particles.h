#pragma once
#include "camera.h"
#include "rendering/subpass.h"
#include "core/shader_module.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/camera.h"
#include "common/vk_initializers.h"
#include "scene_graph/components/image.h"

namespace siho
{
	//struct Particle
	//{
	//	glm::vec4 pos;        // xyz = position, w = mass
	//	glm::vec4 vel;        // xyz = velocity, w = gradient texture position
	//};

	//class ParticlesSubpass : public vkb::Subpass
	//{
	//public:
	//	ParticlesSubpass(vkb::RenderContext& render_context, vkb::ShaderSource&& vertex_shader, vkb::ShaderSource&& fragment_shader, vkb::sg::Camera& camera);

	//	void prepare() override;

	//	// void draw(vkb::CommandBuffer& command_buffer) override;

	//private:
	//	vkb::sg::Camera& camera_;
	//	std::unique_ptr<vkb::core::Buffer> particle_buffer_{ nullptr };

	//	struct ComputeUniform
	//	{
	//		float   delta_time;        //		Frame delta time
	//		int32_t particle_count;
	//	} compute_uniform_;


	//};

	struct Texture
	{
		std::unique_ptr<vkb::sg::Image> image;
		VkSampler                       sampler;
	};

	Texture load_texture(const std::string& file, vkb::sg::Image::ContentType content_type, const vkb::Device& device);

	class ParticlePass
	{
	public:
		uint32_t num_particles;
		uint32_t work_group_size = 128;
		uint32_t shared_data_size = 1024;

		struct
		{
			Texture particle;
			Texture gradient;
		} textures;

		struct
		{
			std::unique_ptr<vkb::core::Buffer> uniform_buffer;
			VkDescriptorSetLayout descriptor_set_layout;
			VkDescriptorSet descriptor_set;
			VkPipelineLayout pipeline_layout;
			VkPipeline pipeline;
			VkSemaphore semaphore;
			uint32_t queue_family_index;
			struct
			{
				glm::mat4 projection;
				glm::mat4 view;
				glm::vec2 screen_dim;
			} ubo;
		} graphics;

		struct
		{
			std::unique_ptr<vkb::core::Buffer> storage_buffer;
			std::unique_ptr<vkb::core::Buffer> uniform_buffer;

			VkQueue queue;
			VkCommandPool command_pool;
			VkCommandBuffer command_buffer;
			VkSemaphore semaphore;
			VkDescriptorSetLayout descriptor_set_layout;
			VkDescriptorSet descriptor_set;
			VkPipelineLayout pipeline_layout;
			VkPipeline pipeline_calculate;
			VkPipeline pipeline_integrate;
			VkPipeline blur;
			VkPipelineLayout blur_layout;
			VkDescriptorSetLayout blur_descriptor_set_layout;
			VkDescriptorSet blur_descriptor_set;
			uint32_t queue_family_index;
			struct ComputeUbo
			{
				float   delta_time;        //		Frame delta time
				int32_t particle_count;
			} ubo;
		} compute;

		struct Particle
		{
			glm::vec4 pos;        // xyz = position, w = mass
			glm::vec4 vel;        // xyz = velocity, w = gradient texture position
		};

		void init(vkb::RenderContext& render_context, vkb::sg::Camera& camera);
		// std::unique_ptr<vkb::RenderPipeline> graphics_pipeline{};

	private:
		void build_command_buffers();
		void build_compute_command_buffer();

		void prepare_graphics();
		void prepare_storage_buffers();
		void prepare_uniform_buffers();
		void setup_descriptor_set_layout();
		void prepare_pipelines();
		void setup_descriptor_set();


		void prepare_compute();

		void update_compute_uniform_buffers(float delta_time);
		void update_graphics_uniform_buffers();

		void setup_depth_stencil();
		void setup_render_pass();
		void setup_framebuffer();

	private:
		uint32_t width_ = 1280;
		uint32_t height_ = 720;
		vkb::Device* device_;
		vkb::sg::Camera* camera_;

		// vkb::RenderPass render_pass_;

		VkPipelineCache pipeline_cache_;
		VkRenderPass render_pass_ = VK_NULL_HANDLE;
		VkQueue queue_ = VK_NULL_HANDLE;
		VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;

		struct
		{
			VkImage        image;
			VkDeviceMemory mem;
			VkImageView    view;
		} depth_stencil_;
		VkCommandPool cmd_pool_;
		std::vector<VkCommandBuffer> draw_cmd_buffers_;
		std::vector<VkFramebuffer> framebuffers;

	};
}
