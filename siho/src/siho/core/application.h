#pragma once

#include <vulkan/vulkan.hpp>

#include "platform/application.h"

namespace siho
{

	class Application : public vkb::Application
	{
	public:
		Application() = default;
		
		virtual ~Application() override;


	};
} // namespace siho