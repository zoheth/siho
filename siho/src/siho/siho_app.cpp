#include "siho_app.h"
#include "common/vk_common.h"
#include "glm/gtc/type_ptr.hpp"

#include "rendering/pipeline_state.h"
#include "rendering/render_context.h"
#include "rendering/render_pipeline.h"
#include "rendering/subpasses/geometry_subpass.h"
#include "rendering/subpasses/lighting_subpass.h"
#include "scene_graph/node.h"

namespace siho
{

	bool SihoApplication::prepare(const vkb::ApplicationOptions& options)
	{
		if (!VulkanSample::prepare(options))
		{
			return false;
		}

		std::set<VkImageUsageFlagBits> usage = { VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ,
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT };
		get_render_context().update_swapchain(usage);

		load_scene("scenes/sponza/Sponza01.gltf");

		scene->clear_components<vkb::sg::Light>();

		// vkb::sg::Node sun_node{ -1, "sun node" };
		auto& directional_light = vkb::add_directional_light(*scene, glm::quat({ glm::radians(-43.0f), glm::radians(-89.0f), glm::radians(-43.0f) }));
		directional_light_ = &directional_light;

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

		auto& camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
		camera = dynamic_cast<vkb::sg::PerspectiveCamera*>(&camera_node.get_component<vkb::sg::Camera>());

		shadow_render_pass_.init(get_render_context(), *scene, *camera, directional_light);

		fx_compute_pass_.init(get_render_context());

		main_pass_.init(get_render_context(), *scene, *camera, shadow_render_pass_, fx_compute_pass_);

		stats->request_stats({ vkb::StatIndex::frame_times });

		gui = std::make_unique<vkb::Gui>(*this, *window, stats.get());

		return true;

	}

	void SihoApplication::update(float delta_time)
	{
		update_scene(delta_time);
		update_stats(delta_time);
		update_gui(delta_time);

		shadow_render_pass_.update();

		auto& main_command_buffer = render_context->begin();

		auto command_buffers = record_command_buffers(main_command_buffer);


		const auto& queue = device->get_queue_by_flags(VK_QUEUE_COMPUTE_BIT, 0);
		auto& compute_command_buffer = render_context->get_active_frame().request_command_buffer(queue);

		compute_command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		fx_compute_pass_.dispatch(compute_command_buffer, delta_time);
		compute_command_buffer.end();

		queue.submit(compute_command_buffer, VK_NULL_HANDLE);

		render_context->submit(command_buffers);
		
	}

	void SihoApplication::draw_gui()
	{
		const bool landscape = camera->get_aspect_ratio() > 1.0f;
		uint32_t lines = 2;


		gui->show_options_window(
			[this]() {
				ImGui::AlignTextToFramePadding();
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.4f);

				auto directional_light = scene->get_components<vkb::sg::Light>()[0];
				auto& transform = directional_light->get_node()->get_transform();
				glm::quat rotation = transform.get_rotation();

				float angle = glm::degrees(glm::angle(rotation));
				glm::vec3 axis = glm::axis(rotation);

				if (ImGui::DragFloat3("Axis", glm::value_ptr(axis), 0.01f, -1.0f, 1.0f) ||
					ImGui::DragFloat("Angle", &angle, 0.1f, -360.0f, 360.0f)) {
					angle = glm::radians(angle);
					rotation = glm::angleAxis(angle, glm::normalize(axis));
					transform.set_rotation(rotation);
				}

				ImGui::PopItemWidth();
			},
			lines);
	}

	void SihoApplication::prepare_render_context()
	{
		get_render_context().prepare(2, [this](vkb::core::Image&& swapchain_image)
			{
				return MainPass::create_render_target(std::move(swapchain_image));
			});
	}

	std::vector<vkb::CommandBuffer*> SihoApplication::record_command_buffers(vkb::CommandBuffer& main_command_buffer)
	{
		/*auto reset_mode = vkb::CommandBuffer::ResetMode::ResetPool;
		const auto& queue = device->get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0);*/

		std::vector<vkb::CommandBuffer*> command_buffers;
		// shadow_subpass->set_thread_index(1);

		// auto& scene_command_buffer = render_context->get_active_frame().request_command_buffer(queue, reset_mode, VK_COMMAND_BUFFER_LEVEL_SECONDARY, 0);
		// auto& shadow_command_buffer = render_context->get_active_frame().request_command_buffer(queue, reset_mode, VK_COMMAND_BUFFER_LEVEL_SECONDARY, 1);

		main_command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		shadow_render_pass_.draw(main_command_buffer);
		// draw_main_pass(main_command_buffer);
		main_pass_.draw(main_command_buffer);
		if (gui)
		{
			gui->draw(main_command_buffer);
		}
		// todo: This shouldn't be here
		main_command_buffer.end_render_pass();
		record_present_image_memory_barriers(main_command_buffer);
		main_command_buffer.end();
		command_buffers.push_back(&main_command_buffer);

		return command_buffers;
	}

	void SihoApplication::record_present_image_memory_barriers(vkb::CommandBuffer& command_buffer)
	{
		auto& views = render_context->get_active_frame().get_render_target().get_views();
		{
			vkb::ImageMemoryBarrier memory_barrier{};
			memory_barrier.old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			memory_barrier.new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			memory_barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

			assert(swapchain_attachment_index < views.size());
			command_buffer.image_memory_barrier(views[swapchain_attachment_index], memory_barrier);
		}
	}
}

std::unique_ptr<vkb::Application> create_application()
{
	return std::make_unique<siho::SihoApplication>();
}