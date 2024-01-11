#include "siho_app_v2.h"

namespace siho
{
	bool SihoApp::prepare(const vkb::ApplicationOptions& options)
	{
		if (!VulkanSample::prepare(options))
		{
			return false;
		}
		std::set<VkImageUsageFlagBits> usage = { VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ,
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT };
		get_render_context().update_swapchain(usage);

		scene->clear_components<vkb::sg::Light>();

		auto& directional_light = vkb::add_directional_light(*scene, glm::quat({ glm::radians(-43.0f), glm::radians(-89.0f), glm::radians(-43.0f) }));
		directional_light_ = &directional_light;
		{
			auto light_pos = glm::vec3(0.0f, 128.0f, -225.0f);
			auto light_color = glm::vec3(1.0, 1.0, 1.0);

			for (int i = -1; i < 4; ++i)
			{
				for (int j = 0; j < 2; ++j)
				{
					glm::vec3 pos = light_pos;
					pos.x += i * 400.0f;
					pos.z += j * (225 + 140);
					pos.y = 8;

					for (int k = 0; k < 3; ++k)
					{
						pos.y = pos.y + (k * 100);

						light_color.x = static_cast<float>(rand()) / (RAND_MAX);
						light_color.y = static_cast<float>(rand()) / (RAND_MAX);
						light_color.z = static_cast<float>(rand()) / (RAND_MAX);

						vkb::sg::LightProperties props;
						props.color = light_color;
						props.intensity = 0.2f;

						vkb::add_point_light(*scene, pos, props);
					}
				}
			}
		}

		auto& camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
		camera_ = dynamic_cast<vkb::sg::PerspectiveCamera*>(&camera_node.get_component<vkb::sg::Camera>());
	}
}
