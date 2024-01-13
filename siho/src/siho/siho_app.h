#pragma once

#include "platform/application.h"
#include "vulkan_sample.h"
#include "scene_graph/components/camera.h"
#include "scene_graph/components/perspective_camera.h"

namespace siho
{
	class MainPass
	{
	public:
		MainPass() = default;

	};
	;
	class ShadowPass
	{
	public:
		ShadowPass() = default;

	};
	class ParticlesPass
	{
	public:
		ParticlesPass() = default;
	};
	class SihoApp : public vkb::VulkanSample
	{
	public:
		SihoApp() = default;

		virtual ~SihoApp() = default;

		virtual bool prepare(const vkb::ApplicationOptions& options) override;

		virtual void update(float delta_time) override;

		virtual void finish() override;

	private:
		virtual void draw_gui() override;


		vkb::sg::PerspectiveCamera* camera_{ nullptr };
		ShadowPass shadow_pass_;
		MainPass main_pass_;
		ParticlesPass particles_pass_;

		vkb::sg::Light* directional_light_;
	};
}
