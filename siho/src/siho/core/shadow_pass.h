#pragma once

#include "rendering/subpasses/geometry_subpass.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/orthographic_camera.h"
#include "scene_graph/components/perspective_camera.h"

constexpr uint32_t kCascadeCount = 3;

namespace siho
{
	struct alignas(16) ShadowUniform
	{
		glm::mat4 shadowmap_projection_matrix;        // Projection matrix used to render shadowmap
	};

	class ShadowSubpass : public vkb::GeometrySubpass
	{
	public:
		ShadowSubpass(vkb::RenderContext& render_context,
			vkb::ShaderSource&& vertex_source,
			vkb::ShaderSource&& fragment_source,
			vkb::sg::Scene& scene,
			vkb::sg::Camera& camera);
	protected:
		void prepare_pipeline_state(vkb::CommandBuffer& command_buffer, VkFrontFace front_face, bool double_sided_material) override;

		vkb::PipelineLayout& prepare_pipeline_layout(vkb::CommandBuffer& command_buffer, const std::vector<vkb::ShaderModule*>& shader_modules) override;

		void prepare_push_constants(vkb::CommandBuffer& command_buffer, vkb::sg::SubMesh& sub_mesh) override;
	};

	struct Cascade
	{
		vkb::sg::OrthographicCamera* light_camera{};
		std::vector<std::unique_ptr<vkb::RenderTarget>> shadow_render_targets;
		ShadowSubpass* shadow_subpass{};
		std::unique_ptr<vkb::RenderPipeline> shadow_render_pipeline{};
	};

	class ShadowRenderPass
	{
	public:
		ShadowRenderPass();

		void init(vkb::RenderContext& render_context, vkb::sg::Scene& scene, vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light);

		void update();

		void draw(vkb::CommandBuffer& command_buffer);

		vkb::sg::Camera& get_light_camera(uint32_t cascade_index) const
		{
			return *cascades_[cascade_index].light_camera;
		}

		std::vector<std::unique_ptr<vkb::RenderTarget>>& get_shadow_render_targets(uint32_t cascade_index)
		{
			return cascades_[cascade_index].shadow_render_targets;
		}

		ShadowUniform get_shadow_uniform(uint32_t cascade_index) const;

		const vkb::core::ImageView& get_shadowmaps_view() const;

		static std::unique_ptr<vkb::core::Sampler> create_shadowmap_sampler(vkb::RenderContext& render_context);

		static glm::vec4 get_cascade_splits(const vkb::sg::PerspectiveCamera& camera);
	private:
		static void update_light_camera(vkb::sg::OrthographicCamera& light_camera, vkb::sg::PerspectiveCamera& camera, uint32_t cascade_index);
		void create_render_targets();
		void create_light_camera(vkb::sg::PerspectiveCamera& camera, vkb::sg::Light& light, vkb::sg::Scene& scene);
		void create_shadow_render_pipelines(vkb::sg::Scene& scene);

		void record_image_memory_barriers(vkb::CommandBuffer& command_buffer, vkb::RenderTarget& render_target);
	private:
		glm::vec4 cascade_splits_;
		vkb::RenderContext* render_context_{};
		vkb::sg::PerspectiveCamera* main_camera_{};

		std::vector < std::unique_ptr<vkb::core::Image>> shadowmap_array_images_;

		std::vector<std::unique_ptr<vkb::core::ImageView>> shadowmap_array_image_views_;

		std::unique_ptr<vkb::RenderPipeline> shadow_render_pipeline_{};
		const uint32_t shadowmap_resolution_{ 2048 };
		std::array<Cascade, kCascadeCount> cascades_;
	};
}
