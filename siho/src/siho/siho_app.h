#pragma once

#include "platform/application.h"
#include "vulkan_sample.h"
#include "scene_graph/components/camera.h"
#include "scene_graph/components/perspective_camera.h"

#include "siho/passes/master_pass.h"

namespace siho
{

	class SihoApp : public vkb::VulkanSample
	{
	public:
		SihoApp() = default;

		virtual ~SihoApp() = default;

		virtual bool prepare(const vkb::ApplicationOptions& options) override;

		virtual void update(float delta_time) override;

		virtual void finish() override;

	private:
		void prepare_render_context() override;


		vkb::sg::PerspectiveCamera* camera_{ nullptr };

		vkb::sg::Light* directional_light_;
	};
}
