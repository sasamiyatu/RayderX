#if _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_vulkan.h"
#include "Volk/volk.h"

#include <vector>

#include <stdio.h>

#define VK_CHECK(x)                                         \
	do { 					                                \
		VkResult err = x;                                   \
		if (err != VK_SUCCESS) {	                                        \
			                                                \
				printf("Detected Vulkan error: %d\n", err); \
				abort();                                    \
		}                                                   \
	} while(0)

VkInstance create_instance()
{
	VkInstance instance = VK_NULL_HANDLE;

	VkApplicationInfo application_info{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = nullptr,
		.pApplicationName = "RayderX",
		.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		.pEngineName = "RayderX",
		.engineVersion = VK_MAKE_VERSION(1, 0, 0),
		.apiVersion = VK_API_VERSION_1_3
	};

	
	std::vector<const char*> layers = {
#if _DEBUG
		"VK_LAYER_KHRONOS_validation"
#endif
	};

	std::vector<const char*> extensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#if _DEBUG
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
	};

	VkInstanceCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &application_info,
		.enabledLayerCount = (uint32_t)layers.size(),
		.ppEnabledLayerNames = layers.data(),
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data()
	};

	VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance));
	volkLoadInstance(instance);

	return instance;
}

static VkBool32 debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	if (pCallbackData->pMessage)
		printf("Validation layer: %s\n", pCallbackData->pMessage);
	return VK_FALSE;
}

VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance)
{
	VkDebugUtilsMessengerCreateInfoEXT create_info{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debug_callback
	};

	VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &create_info, nullptr, &debug_messenger));	

	return debug_messenger;
}

VkPhysicalDevice pick_physical_device(VkInstance instance)
{
	uint32_t physical_device_count = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));
	std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data()));
	for (VkPhysicalDevice physical_device : physical_devices)
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			printf("Selecting device: %s\n", properties.deviceName);
			return physical_device;
		}
	}

	return VK_NULL_HANDLE;
}

uint32_t find_queue_family(VkPhysicalDevice physical_device)
{
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
	std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());
	for (uint32_t i = 0; i < queue_family_count; i++)
	{
		if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			return i;
		}
	}

	return VK_QUEUE_FAMILY_IGNORED;
}

VkDevice create_device(VkInstance instance, VkPhysicalDevice physical_device, uint32_t queue_family_index)
{
	float priorities = 1.0f;
	VkDeviceQueueCreateInfo queue_create_info{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queue_family_index,
		.queueCount = 1,
		.pQueuePriorities = &priorities
	};

	std::vector<const char*> extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};


	VkPhysicalDeviceVulkan13Features features13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.synchronization2 = VK_TRUE
	};

	VkDeviceCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features13,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_create_info,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = nullptr,
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data(),
		.pEnabledFeatures = nullptr
	};

	VkDevice device = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDevice(physical_device, &create_info, nullptr, &device));

	return device;
}

VkFence create_fence(VkDevice device)
{
	VkFenceCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
	};
	VkFence fence = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFence(device, &create_info, nullptr, &fence));
	return fence;
}

VkSemaphore create_semaphore(VkDevice device)
{
	VkSemaphoreCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0
	};

	VkSemaphore semaphore = VK_NULL_HANDLE;
	VK_CHECK(vkCreateSemaphore(device, &create_info, nullptr, &semaphore));
	return semaphore;
}

struct Swapchain
{
	VkSwapchainKHR swapchain;
	std::vector<VkImage> images;

	uint32_t width, height;
	uint32_t image_count;
};

VkSurfaceKHR create_surface(VkInstance instance, SDL_Window* window)
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	SDL_Vulkan_CreateSurface(window, instance, &surface);
	return surface;
}

void create_swapchain(Swapchain& swapchain, VkDevice device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t width, uint32_t height)
{
	VkSurfaceCapabilitiesKHR surface_capabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities));
	VkSwapchainCreateInfoKHR create_info{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = nullptr,
		.surface = surface,
		.minImageCount = surface_capabilities.minImageCount,
		.imageFormat = VK_FORMAT_B8G8R8A8_SRGB,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = { width, height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE
	};

	VK_CHECK(vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain.swapchain));

	uint32_t image_count = 0;
	vkGetSwapchainImagesKHR(device, swapchain.swapchain, &swapchain.image_count, nullptr);
	swapchain.images.resize(swapchain.image_count);
	vkGetSwapchainImagesKHR(device, swapchain.swapchain, &swapchain.image_count, swapchain.images.data());

	swapchain.width = width;
	swapchain.height = height;
}

VkCommandPool crate_command_pool(VkDevice device, uint32_t queue_family)
{
	VkCommandPoolCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = queue_family,
	};
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VK_CHECK(vkCreateCommandPool(device, &create_info, nullptr, &command_pool));
	return command_pool;
}

int main(int argc, char** argv)
{
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "1");

	SDL_Window* window = SDL_CreateWindow("RayderX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
	int window_width, window_height;
	SDL_GetWindowSizeInPixels(window, &window_width, &window_height);

	VK_CHECK(volkInitialize());

	VkInstance instance = create_instance();
	VkDebugUtilsMessengerEXT debug_messenger = create_debug_messenger(instance);

	VkPhysicalDevice physical_device = pick_physical_device(instance);
	uint32_t queue_family = find_queue_family(physical_device);
	VkDevice device = create_device(instance, physical_device, queue_family);
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, queue_family, 0, &queue);

	VkFence frame_fence = create_fence(device);
	VkSemaphore acquire_semaphore = create_semaphore(device);
	VkSemaphore release_semaphore = create_semaphore(device);

	VkSurfaceKHR surface = create_surface(instance, window);
	Swapchain swapchain{};
	create_swapchain(swapchain, device, physical_device, surface, window_width, window_height);

	VkCommandPool command_pool = crate_command_pool(device, queue_family);

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo allocate_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};
	VK_CHECK(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer));

    bool running = true;
	while (running)
	{
		uint32_t image_index;
		VK_CHECK(vkAcquireNextImageKHR(device, swapchain.swapchain, UINT64_MAX, acquire_semaphore, VK_NULL_HANDLE, &image_index));

		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
			{
				running = false;
			}
		}

		VK_CHECK(vkResetCommandPool(device, command_pool, 0));
		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

		{
			VkImageMemoryBarrier2 image_memory_barrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.pNext = nullptr,
				.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				.dstAccessMask = 0,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = swapchain.images[image_index],
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			};

			VkDependencyInfo dependency_info{
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
				.memoryBarrierCount = 0,
				.pMemoryBarriers = nullptr,
				.bufferMemoryBarrierCount = 0,
				.pBufferMemoryBarriers = nullptr,
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = &image_memory_barrier
			};

			vkCmdPipelineBarrier2(command_buffer, &dependency_info);
		}

		VkClearColorValue clear_value{ 0.5f, 0.2f, 1.0f, 1.0f };
		VkImageSubresourceRange range{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		};

		vkCmdClearColorImage(command_buffer, swapchain.images[image_index], VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &range);

		{
			VkImageMemoryBarrier2 image_memory_barrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.pNext = nullptr,
				.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				.dstAccessMask = 0,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				.image = swapchain.images[image_index],
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			};

			VkDependencyInfo dependency_info{
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
				.memoryBarrierCount = 0,
				.pMemoryBarriers = nullptr,
				.bufferMemoryBarrierCount = 0,
				.pBufferMemoryBarriers = nullptr,
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = &image_memory_barrier
			};

			vkCmdPipelineBarrier2(command_buffer, &dependency_info);
		}

		vkEndCommandBuffer(command_buffer);

		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &acquire_semaphore,
			.pWaitDstStageMask = &wait_stage,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &release_semaphore
		};

		VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, frame_fence));

		VkPresentInfoKHR present_info{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.pNext = nullptr,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &release_semaphore,
			.swapchainCount = 1,
			.pSwapchains = &swapchain.swapchain,
			.pImageIndices = &image_index,
			.pResults = nullptr
		};
		VK_CHECK(vkQueuePresentKHR(queue, &present_info));

		vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &frame_fence);
	}

	VK_CHECK(vkDeviceWaitIdle(device));

	SDL_DestroyWindow(window);

	vkDestroyCommandPool(device, command_pool, nullptr);
	vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyFence(device, frame_fence, nullptr);
	vkDestroySemaphore(device, acquire_semaphore, nullptr);
	vkDestroySemaphore(device, release_semaphore, nullptr);
	vkDestroyDevice(device, nullptr);
	vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
	vkDestroyInstance(instance, nullptr);

    return 0;
}