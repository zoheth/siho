#include "vulkan_sample.h"
#include "rendering/subpasses/forward_subpass.h"
#include "scene_graph/components/camera.h"

class SwapchainImages : public vkb::VulkanSample
{
  public:
	SwapchainImages();

	virtual ~SwapchainImages() = default;

	virtual bool prepare(const vkb::ApplicationOptions &options) override;

	virtual void update(float delta_time) override;

  private:
	vkb::sg::Camera *camera{nullptr};

	virtual void draw_gui() override;

	int swapchain_image_count{3};

	int last_swapchain_image_count{3};
};

SwapchainImages::SwapchainImages()
{
	auto &config = get_configuration();

	config.insert<vkb::IntSetting>(0, swapchain_image_count, 3);
	config.insert<vkb::IntSetting>(1, swapchain_image_count, 2);
}

bool SwapchainImages::prepare(const vkb::ApplicationOptions &options)
{
	if (!VulkanSample::prepare(options))
	{
		return false;
	}

	load_scene("scenes/sponza/Sponza01.gltf");

	auto &camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
	camera            = &camera_node.get_component<vkb::sg::Camera>();

	vkb::ShaderSource vert_shader("base.vert");
	vkb::ShaderSource frag_shader("base.frag");
	auto              scene_subpass = std::make_unique<vkb::ForwardSubpass>(get_render_context(), std::move(vert_shader), std::move(frag_shader), *scene, *camera);

	auto render_pipeline = vkb::RenderPipeline();
	render_pipeline.add_subpass(std::move(scene_subpass));

	set_render_pipeline(std::move(render_pipeline));

	stats->request_stats({vkb::StatIndex::frame_times});
	gui = std::make_unique<vkb::Gui>(*this, *window, stats.get());

	return true;
}

void SwapchainImages::update(float delta_time)
{
	// Process GUI input
	if (swapchain_image_count != last_swapchain_image_count)
	{
		get_device().wait_idle();

		// Create a new swapchain with a new swapchain image count
		render_context->update_swapchain(swapchain_image_count);

		last_swapchain_image_count = swapchain_image_count;
	}

	VulkanSample::update(delta_time);
}

void SwapchainImages::draw_gui()
{
	gui->show_options_window(
	    /* body = */ [this]() {
		    ImGui::RadioButton("Double buffering", &swapchain_image_count, 2);
		    ImGui::SameLine();
		    ImGui::RadioButton("Triple buffering", &swapchain_image_count, 3);
		    ImGui::SameLine();
	    },
	    /* lines = */ 1);
}

std::unique_ptr<vkb::Application> create_application()
{
	return std::make_unique<SwapchainImages>();
}