#if _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "common.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_vulkan.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#include <vector>
#include <stdio.h>
#include <filesystem>

#include "dds.h"
#include "resources.h"
#include "scene.h"
#include "sdkmesh.h"
#include "shaders.h"

static constexpr VkPhysicalDeviceType PREFERRED_GPU_TYPE = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
static constexpr VkFormat LINEAR_DEPTH_FORMAT = VK_FORMAT_R32_SFLOAT;
static constexpr VkFormat RENDER_TARGET_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
static constexpr uint32_t SHADOWMAP_SIZE = 2048;
static constexpr VkSampleCountFlagBits MSAA = VK_SAMPLE_COUNT_4_BIT;

static constexpr float ENVIRONMENT_INTENSITY = 0.55f;
static constexpr float AMBIENT_INTENSITY = 0.61f;

static constexpr float SSS_TRANSLUCENCY = 0.83f;
static constexpr float SSS_WIDTH = 0.012f;

static constexpr uint32_t N_BLOOM_PASSES = 6;
static constexpr float BLOOM_THRESHOLD = 0.63f;
static constexpr float BLOOM_WIDTH = 1.0f;
static constexpr float BLOOM_INTENSITY = 1.0f;
static constexpr float BLOOM_DEFOCUS = 0.2f;

static constexpr float DOF_FOCUS_DISTANCE = 2.76f;
static constexpr float DOF_FOCUS_RANGE = 0.253552526f;
static constexpr glm::vec2 DOF_FOCUS_FALLOFF = glm::vec2(15.0f);
static constexpr float DOF_BLUR_WIDTH = 2.5f;

static constexpr float FILM_GRAIN_NOISE_INTENSITY = 1.0f;

#define DISABLE_POST_PROCESSING 0

#if DISABLE_POST_PROCESSING == 1
static constexpr bool SSS_ENABLED = false;
static constexpr bool BLOOM_ENABLED = false;
static constexpr bool DOF_ENABLED = false;
static constexpr bool FILM_GRAIN_ENABLED = false;
static constexpr float EXPOSURE = 1.0f;
#else
static constexpr bool SSS_ENABLED = true;
static constexpr bool BLOOM_ENABLED = true;
static constexpr bool DOF_ENABLED = true;
static constexpr bool FILM_GRAIN_ENABLED = true;
static constexpr float EXPOSURE = 2.0f;
#endif

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
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
	};

#if _DEBUG
	std::vector<VkValidationFeatureEnableEXT>  validation_feature_enables = { VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT };

	VkValidationFeaturesEXT validation_features{
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.enabledValidationFeatureCount = (uint32_t)validation_feature_enables.size(),
		.pEnabledValidationFeatures = validation_feature_enables.data(),
	};
#endif

	VkInstanceCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#if _DEBUG
		.pNext = &validation_features,
#endif
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

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		assert(false);
	}
	return VK_FALSE;
}

VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance)
{
	VkDebugUtilsMessengerCreateInfoEXT create_info{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | 
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
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
		if (properties.deviceType == PREFERRED_GPU_TYPE)
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
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
		VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
	};

	VkPhysicalDeviceVulkan12Features features12{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.scalarBlockLayout = VK_TRUE,
	};

	VkPhysicalDeviceVulkan13Features features13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &features12,
		.shaderDemoteToHelperInvocation = VK_TRUE,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE,
	};

	VkPhysicalDeviceMaintenance5Features maintenance5_features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES,
		.pNext = &features13,
		.maintenance5 = VK_TRUE,
	};


	VkPhysicalDeviceVulkan14Features features14{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = &features13,
		.maintenance5 = VK_TRUE,
	};

	VkPhysicalDeviceFeatures features{
		.sampleRateShading = VK_TRUE,
		.samplerAnisotropy = VK_TRUE,
	};

	VkDeviceCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &maintenance5_features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_create_info,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = nullptr,
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data(),
		.pEnabledFeatures = &features
	};

	VkDevice device = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDevice(physical_device, &create_info, nullptr, &device));

	//volkLoadDevice(device);

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
	VkFormat format;
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

VkFormat get_swapchain_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
	uint32_t count = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, 0));
	std::vector<VkSurfaceFormatKHR> formats(count);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data()));

	for (uint32_t i = 0; i < count; ++i)
		if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB || formats[i].format == VK_FORMAT_B8G8R8A8_SRGB)
			return formats[i].format;

	return formats[0].format;
}

void create_swapchain(Swapchain& swapchain, VkDevice device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t width, uint32_t height)
{
	VkFormat format = get_swapchain_format(physical_device, surface);
	VkSurfaceCapabilitiesKHR surface_capabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities));
	VkSwapchainCreateInfoKHR create_info{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = nullptr,
		.surface = surface,
		.minImageCount = surface_capabilities.minImageCount,
		.imageFormat = format,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = { width, height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT /* | VK_IMAGE_USAGE_STORAGE_BIT*/,
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

	swapchain.format = format;
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

VmaAllocator create_allocator(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device)
{

	VmaVulkanFunctions funcs{
		.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
		.vkGetDeviceProcAddr = vkGetDeviceProcAddr
	};

	VmaAllocatorCreateInfo create_info{
		.physicalDevice = physical_device,
		.device = device,
		.pVulkanFunctions = &funcs,
		.instance = instance
	};

	VmaAllocator allocator = VK_NULL_HANDLE;
	VK_CHECK(vmaCreateAllocator(&create_info, &allocator));
	return allocator;
}

VkPipeline create_shadowmap_pipeline(VkDevice device, std::initializer_list<Shader> shaders, VkPipelineLayout layout, VkFormat depth_format)
{
	std::vector<VkPipelineShaderStageCreateInfo> shader_stages(shaders.size());
	std::vector<VkShaderModuleCreateInfo> module_info(shaders.size());
	for (size_t i = 0; i < shaders.size(); ++i)
	{
		const Shader& shader = *(shaders.begin() + i);
		module_info[i] = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = shader.spirv.size(),
			.pCode = (uint32_t*)shader.spirv.data()
		};

		shader_stages[i] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = &module_info[i],
			.stage = shader.stage,
			.pName = shader.entry_point.c_str(),
		};
	}

	VkPipelineVertexInputStateCreateInfo vertex_input_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};


	VkPipelineInputAssemblyStateCreateInfo input_assembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	VkPipelineTessellationStateCreateInfo tessellation_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
	};

	VkPipelineViewportStateCreateInfo viewport_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo rasterization_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	std::vector<VkDynamicState> dynamic_states = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamic_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = (uint32_t)dynamic_states.size(),
		.pDynamicStates = dynamic_states.data()
	};

	VkPipelineDepthStencilStateCreateInfo depth_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	VkPipelineRenderingCreateInfo rendering_create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.depthAttachmentFormat = depth_format,
	};

	VkGraphicsPipelineCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &rendering_create_info,
		.stageCount = (uint32_t)shaders.size(),
		.pStages = shader_stages.data(),
		.pVertexInputState = &vertex_input_state,
		.pInputAssemblyState = &input_assembly,
		.pTessellationState = &tessellation_state,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization_state,
		.pMultisampleState = &multisample_state,
		.pDepthStencilState = &depth_info,
		.pDynamicState = &dynamic_state,
		.layout = layout,
	};

	VkPipeline pipeline = VK_NULL_HANDLE;
	VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));
	return pipeline;
}

VkPipeline create_pipeline(VkDevice device, std::initializer_list<Shader> shaders, VkPipelineLayout layout, std::initializer_list<VkFormat> color_attachment_formats, 
	VkFormat depth_format = VK_FORMAT_UNDEFINED, 
	VkPipelineMultisampleStateCreateInfo multisample_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	},
	VkPipelineDepthStencilStateCreateInfo depth_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	},
	VkPipelineColorBlendAttachmentState blend_state = {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	},
	VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT
	)
{
	std::vector<VkPipelineShaderStageCreateInfo> shader_stages(shaders.size());
	std::vector<VkShaderModuleCreateInfo> module_info(shaders.size());
	for (size_t i = 0; i < shaders.size(); ++i)
	{
		const Shader& shader = *(shaders.begin() + i);
		module_info[i] = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = shader.spirv.size(),
			.pCode = (uint32_t*)shader.spirv.data()
		};

		shader_stages[i] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = &module_info[i],
			.stage = shader.stage,
			.pName = shader.entry_point.c_str(),
		};
	}

	VkPipelineVertexInputStateCreateInfo vertex_input_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};


	VkPipelineInputAssemblyStateCreateInfo input_assembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	VkPipelineTessellationStateCreateInfo tessellation_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
	};

	VkPipelineViewportStateCreateInfo viewport_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo rasterization_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = cull_mode,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	std::vector<VkDynamicState> dynamic_states = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamic_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = (uint32_t)dynamic_states.size(),
		.pDynamicStates = dynamic_states.data()
	};

	VkPipelineRenderingCreateInfo rendering_create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = (uint32_t)color_attachment_formats.size(),
		.pColorAttachmentFormats = color_attachment_formats.begin(),
		.depthAttachmentFormat = depth_format,
	};

	std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments(color_attachment_formats.size());
	for (size_t i = 0; i < color_attachment_formats.size(); ++i)
	{
		color_blend_attachments[i] = blend_state;
	}

	VkPipelineColorBlendStateCreateInfo color_blend_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = (uint32_t)color_blend_attachments.size(),
		.pAttachments = color_blend_attachments.data()
	};

	VkGraphicsPipelineCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &rendering_create_info,
		.stageCount = (uint32_t)shaders.size(),
		.pStages = shader_stages.data(),
		.pVertexInputState = &vertex_input_state,
		.pInputAssemblyState = &input_assembly,
		.pTessellationState = &tessellation_state,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization_state,
		.pMultisampleState = &multisample_state,
		.pDepthStencilState = &depth_info,
		.pColorBlendState = &color_blend_state,
		.pDynamicState = &dynamic_state,
		.layout = layout,
	};

	VkPipeline pipeline = VK_NULL_HANDLE;
	VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));
	return pipeline;
}


VkPipeline create_compute_pipeline(VkDevice device, const Shader& shader, VkPipelineLayout layout)
{
	VkShaderModuleCreateInfo module_info{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shader.spirv.size(),
		.pCode = (uint32_t*)shader.spirv.data(),
	};

	VkComputePipelineCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = &module_info,
			.stage = shader.stage,
			.pName = shader.entry_point.c_str()
		},
		.layout = layout
	};

	VkPipeline pipeline = 0;
	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));
	return pipeline;
}

bool init_imgui(SDL_Window* window, VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, VkQueue queue, uint32_t min_image_count, uint32_t image_count, VkFormat format)
{
	ImGui_ImplVulkan_InitInfo info{};
	info.Instance = instance;
	info.PhysicalDevice = physical_device;
	info.Device = device;
	info.QueueFamily = queue_family;
	info.Queue = queue;
	info.MinImageCount = min_image_count;
	info.ImageCount = image_count;
	info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	info.UseDynamicRendering = true;
	info.DescriptorPoolSize = 8;

	VkFormat color_attachment_format = format;
	info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_attachment_format;
	info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	info.PipelineRenderingCreateInfo.depthAttachmentFormat = DEPTH_FORMAT;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigWindowsMoveFromTitleBarOnly = true;

	auto* addr = vkGetInstanceProcAddr(instance, "vkCmdBeginRenderingKHR");
	ImGui_ImplSDL2_InitForVulkan(window);
	ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void* userdata) { return vkGetInstanceProcAddr((VkInstance)userdata, function_name); }, (void*)instance);

	bool success = ImGui_ImplVulkan_Init(&info);
	return success;
}

void set_viewport_and_scissor(VkCommandBuffer cmd, uint32_t width, uint32_t height)
{
	VkViewport viewport{
		.x = 0.0f,
		.y = (float)height,
		.width = (float)width,
		.height = -(float)height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{
		.offset = { 0, 0 },
		.extent = { width, height }
	};

	vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void begin_rendering(VkCommandBuffer cmd, uint32_t width, uint32_t height, std::initializer_list<VkRenderingAttachmentInfo> color_attachments, std::optional<VkRenderingAttachmentInfo> depth_attachment = {})
{
	VkRenderingInfo rendering_info{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { 0, 0, width, height },
		.layerCount = 1,
		.colorAttachmentCount = (uint32_t)color_attachments.size(),
		.pColorAttachments = color_attachments.begin(),
		.pDepthAttachment = depth_attachment.has_value() ? &*depth_attachment : nullptr,
	};

	vkCmdBeginRendering(cmd, &rendering_info);
}

template <typename T>
void draw_with_pipeline_and_program(VkCommandBuffer cmd, const Mesh mesh, const Program& program, VkPipeline pipeline, const T& push_constants, DescriptorInfo* descriptor_info)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdPushDescriptorSetWithTemplateKHR(cmd, program.descriptor_update_template, program.pipeline_layout, 0, descriptor_info);
	vkCmdPushConstants(cmd, program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants), &push_constants);
	vkCmdDrawIndexed(cmd, mesh.index_count, 1, mesh.first_index, mesh.first_vertex, 0);
}

struct OrbitCamera
{
	float distance = 2.0f;
	float fov = glm::radians(45.0f);
	float near_plane = 0.1f;
	float far_plane = 10.0f;
	glm::vec2 angles = glm::vec2(0.0f, 0.0f);
	glm::vec2 pan_pos = glm::vec2(0.0f, 0.0f);
	glm::mat4 projection = glm::perspectiveRH_ZO(fov, 1280.0f / 720.0f, near_plane, far_plane);

	glm::mat4 compute_view() const
	{
		glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-pan_pos.x, -pan_pos.y, distance));
		glm::mat4 rx = glm::rotate(glm::mat4(1.0f), angles.y, glm::vec3(1.0f, 0.0f, 0.0f));
		view = view * rx;

		glm::mat4 ry = glm::rotate(glm::mat4(1.0f), angles.x, glm::vec3(0.0f, 1.0f, 0.0f));
		view = view * ry;

		glm::mat4 view_inverse = glm::inverse(view);
		glm::vec3 look_at_pos = glm::vec3(view_inverse * glm::vec4(0.0f, 0.0f, distance, 1.0f));
		glm::vec3 eyepos = glm::vec3(view_inverse * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

		eyepos.z = -eyepos.z;
		look_at_pos.z = -look_at_pos.z;
		view = glm::lookAt(eyepos, look_at_pos, glm::vec3(0.0f, 1.0f, 0.0f));
		return view;
	}
};

struct GPULight {
    glm::vec3 position;
    glm::vec3 direction;
    float falloff_start;
    float falloff_width;
    glm::vec3 color;
	float attenuation;
    float far_plane;
    float bias;
    glm::mat4 view_projection;
};

struct Light
{
	OrbitCamera orbit_camera;
	float falloff_width = 0.05f;
	float bias = -0.1f;
	float attenuation = 1.0f / 128.0f;
	glm::vec3 color = glm::vec3(0.0f);
	Texture shadowmap;
};

struct Lights
{
	std::vector<GPULight> gpu_lights;
	std::vector<Light> lights;
	Buffer buffer;
};

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("Usage: %s <scene file>\n", argv[0]);
		return 1;
	}
    
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");

	SDL_Window* window = SDL_CreateWindow("RayderX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
	int window_width, window_height;
	SDL_GetWindowSizeInPixels(window, &window_width, &window_height);

	VK_CHECK(volkInitialize());

	VkInstance instance = create_instance();

#if _DEBUG
	VkDebugUtilsMessengerEXT debug_messenger = create_debug_messenger(instance);
#endif

	VkPhysicalDevice physical_device = pick_physical_device(instance);
	uint32_t queue_family = find_queue_family(physical_device);
	VkDevice device = create_device(instance, physical_device, queue_family);
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, queue_family, 0, &queue);

	VmaAllocator allocator = create_allocator(instance, physical_device, device);

	ShaderCompiler compiler{};
	if (!create_shader_compiler(compiler))
	{
		printf("Failed to create shader compiler!\n");
		return EXIT_FAILURE;
	}

	VkFence frame_fence = create_fence(device);
	VkSemaphore acquire_semaphore = create_semaphore(device);
	VkSemaphore release_semaphore = create_semaphore(device);

	VkSurfaceKHR surface = create_surface(instance, window);
	Swapchain swapchain{};
	create_swapchain(swapchain, device, physical_device, surface, window_width, window_height);
	std::vector<VkImageView> views(swapchain.image_count);
	for (size_t i = 0; i < swapchain.image_count; ++i)
		views[i] = create_image_view(device, swapchain.images[i], VK_IMAGE_VIEW_TYPE_2D, swapchain.format);

	VkCommandPool command_pool = crate_command_pool(device, queue_family);

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo allocate_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};
	VK_CHECK(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer));

#if 0
	if (!init_imgui(window, instance, physical_device, device, queue_family, queue, 2, 2, swapchain.format))
	{
		printf("Failed to initialize imgui!\n");
		return EXIT_FAILURE;
	}
#endif

	Buffer scratch_buffer = create_buffer(allocator, 1024 * 1024 * 128, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	std::vector<Mesh> meshes;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<MeshDraw> mesh_draws;
	std::vector<Material> materials;
	std::vector<Texture> textures;

	Texture beckmann_lut;
	if (!load_texture(beckmann_lut, "data/BeckmannMap.dds", device, allocator, command_pool, command_buffer, queue, scratch_buffer, false))
	{
		printf("Failed to load beckmann lut!\n");
		return EXIT_FAILURE;
	}

	bool scene_is_gltf = false;
	std::filesystem::path ext = std::filesystem::path(argv[1]).extension();
	if (ext == ".glb" || ext == ".gltf")
	{
		if (!load_scene(argv[1], meshes, materials, textures, vertices, indices, mesh_draws, device, allocator, command_pool, command_buffer, queue, scratch_buffer))
		{
			printf("Failed to load scene!\n");
			return 1;
		}

		scene_is_gltf = true;
	}
	else if (ext == ".sdkmesh")
	{
		std::vector<uint8_t> sdkmesh;
		if (!read_binary_file(argv[1], sdkmesh))
		{
			printf("Failed to load sdkmesh\n");
			return EXIT_FAILURE;
		}

		uint8_t* data = sdkmesh.data();
		SDKMESH_HEADER* header = (SDKMESH_HEADER*)data;
		SDKMESH_VERTEX_BUFFER_HEADER* vertex_buffer_array = (SDKMESH_VERTEX_BUFFER_HEADER*)(data +
			header->VertexStreamHeadersOffset);
		SDKMESH_INDEX_BUFFER_HEADER* index_buffer_array = (SDKMESH_INDEX_BUFFER_HEADER*)(data +
			header->IndexStreamHeadersOffset);
		SDKMESH_MESH* mesh = (SDKMESH_MESH*)(data + header->MeshDataOffset);
		SDKMESH_SUBSET* subset = (SDKMESH_SUBSET*)(data + header->SubsetDataOffset);
		SDKMESH_FRAME* frame = (SDKMESH_FRAME*)(data + header->FrameDataOffset);
		SDKMESH_MATERIAL* material = (SDKMESH_MATERIAL*)(data + header->MaterialDataOffset);

		struct SDKVertex
		{
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec2 texcoord;
			glm::vec3 tangent;
		};

		assert(header->NumVertexBuffers == 1);
		assert(header->NumIndexBuffers == 1);
		assert(sizeof(SDKVertex) == vertex_buffer_array[0].StrideBytes);

		std::vector<SDKVertex> verts(vertex_buffer_array->NumVertices);
		memcpy(verts.data(), data + vertex_buffer_array->DataOffset, vertex_buffer_array->SizeBytes);

		std::vector<uint32_t> inds(index_buffer_array->NumIndices);
		uint32_t bytes_per_index = index_buffer_array->IndexType == 0 ? 2 : 4;
		assert(index_buffer_array->SizeBytes / bytes_per_index == index_buffer_array->NumIndices);
		for (uint32_t i = 0; i < index_buffer_array->NumIndices; ++i)
		{
			inds[i] = ((uint16_t*)(data + index_buffer_array->DataOffset))[i];
		}

		Mesh m{
			.first_vertex = 0,
			.vertex_count = (uint32_t)verts.size(),
			.first_index = 0,
			.index_count = (uint32_t)inds.size(),
		};

		Material mat{
			.basecolor_texture = (int)textures.size(),
			.normal_texture = (int)textures.size() + 1,
			.specular_texture = (int)textures.size() + 2,
		};

		MeshDraw mesh_draw{
			.transform = glm::mat4(1.0f),
			.mesh_index = 0,
			.material_index = 0,
		};

		indices = inds;
		for (uint32_t i = 0; i < indices.size(); i += 3)
		{
			std::swap(indices[i], indices[i + 2]);
		}
		vertices.resize(verts.size());
		for (size_t i = 0; i < verts.size(); ++i)
		{
			glm::vec3 p = verts[i].position;
			glm::vec3 n = verts[i].normal;
			glm::vec3 t = verts[i].tangent;
			p = glm::vec3(p.x, p.y, -p.z);
			n = glm::vec3(n.x, n.y, -n.z);
			t = glm::vec3(t.x, t.y, -t.z);
			vertices[i] = {
				.position = p,
				.normal = n,
				.tangent = glm::vec4(t, -1.0f),
				.uv = verts[i].texcoord,
			};
		}

		mesh_draws.push_back(mesh_draw);
		meshes.push_back(m);
		materials.push_back(mat);

		std::filesystem::path directory = std::filesystem::path(argv[1]).parent_path();
		std::filesystem::path diffuse_path = directory / std::filesystem::path(material->DiffuseTexture);
		std::filesystem::path normal_path = directory / std::filesystem::path(material->NormalTexture);
		std::filesystem::path specular_path = directory / std::filesystem::path("SpecularAOMap.dds");
		Texture diffuse, normal, specular;
		if (!load_texture(diffuse, diffuse_path.string().c_str(), device, allocator, command_pool, command_buffer, queue, scratch_buffer, true))
		{
			printf("Failed to load texture: %s\n", diffuse_path.string().c_str());
			return EXIT_FAILURE;
		}
		if (!load_texture(normal, normal_path.string().c_str(), device, allocator, command_pool, command_buffer, queue, scratch_buffer, false))
		{
			printf("Failed to load texture: %s\n", diffuse_path.string().c_str());
			return EXIT_FAILURE;
		}
		if (!load_texture(specular, specular_path.string().c_str(), device, allocator, command_pool, command_buffer, queue, scratch_buffer, false))
		{
			printf("Failed to load texture: %s\n", diffuse_path.string().c_str());
			return EXIT_FAILURE;
		}

		textures.insert(textures.end(), { diffuse, normal, specular });
	}
	else
	{
		printf("Unsupported file format: %s\n", ext.string().c_str());
		return EXIT_FAILURE;
	}

	struct Environment
	{
		Texture irradiance;
		Texture diffuse;
		Texture reflection;
		Mesh mesh;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		Buffer index_buffer;
		Buffer vertex_buffer;
	} environment;

	{
		std::vector<uint8_t> sdkmesh;
		std::filesystem::path path = "data/StPeters/SkyDome.sdkmesh";
		if (!read_binary_file(path.string().c_str(), sdkmesh))
		{
			printf("Failed to load sdkmesh\n");
			return EXIT_FAILURE;
		}

		uint8_t* data = sdkmesh.data();
		SDKMESH_HEADER* header = (SDKMESH_HEADER*)data;
		SDKMESH_VERTEX_BUFFER_HEADER* vertex_buffer_array = (SDKMESH_VERTEX_BUFFER_HEADER*)(data +
			header->VertexStreamHeadersOffset);
		SDKMESH_INDEX_BUFFER_HEADER* index_buffer_array = (SDKMESH_INDEX_BUFFER_HEADER*)(data +
			header->IndexStreamHeadersOffset);
		SDKMESH_MESH* mesh = (SDKMESH_MESH*)(data + header->MeshDataOffset);
		SDKMESH_SUBSET* subset = (SDKMESH_SUBSET*)(data + header->SubsetDataOffset);
		SDKMESH_FRAME* frame = (SDKMESH_FRAME*)(data + header->FrameDataOffset);
		SDKMESH_MATERIAL* material = (SDKMESH_MATERIAL*)(data + header->MaterialDataOffset);

		struct SDKVertex
		{
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec2 texcoord;
			glm::vec3 tangent;
		};

		assert(header->NumVertexBuffers == 1);
		assert(header->NumIndexBuffers == 1);
		assert(sizeof(SDKVertex) == vertex_buffer_array[0].StrideBytes);

		std::vector<SDKVertex> verts(vertex_buffer_array->NumVertices);
		memcpy(verts.data(), data + vertex_buffer_array->DataOffset, vertex_buffer_array->SizeBytes);

		std::vector<uint32_t> inds(index_buffer_array->NumIndices);
		uint32_t bytes_per_index = index_buffer_array->IndexType == 0 ? 2 : 4;
		assert(index_buffer_array->SizeBytes / bytes_per_index == index_buffer_array->NumIndices);
		for (uint32_t i = 0; i < index_buffer_array->NumIndices; ++i)
		{
			inds[i] = ((uint16_t*)(data + index_buffer_array->DataOffset))[i];
		}

		Mesh m{
			.first_vertex = 0,
			.vertex_count = (uint32_t)verts.size(),
			.first_index = 0,
			.index_count = (uint32_t)inds.size(),
		};

		for (uint32_t i = 0; i < inds.size(); i += 3)
		{
			std::swap(inds[i], inds[i + 2]);
		}

		environment.vertices.resize(verts.size());
		for (size_t i = 0; i < verts.size(); ++i)
		{
			glm::vec3 p = verts[i].position;
			glm::vec3 n = verts[i].normal;
			glm::vec3 t = verts[i].tangent;
			p = glm::vec3(p.x, p.y, -p.z);
			n = glm::vec3(n.x, n.y, -n.z);
			t = glm::vec3(t.x, t.y, -t.z);
			environment.vertices[i] = {
				.position = p,
				.normal = n,
				.tangent = glm::vec4(t, -1.0f),
				.uv = verts[i].texcoord,
			};
		}

		environment.indices = inds;
		environment.mesh = m;

		std::filesystem::path directory = path.parent_path();
		std::filesystem::path diffuse_path = directory / std::filesystem::path(material->DiffuseTexture);
		std::filesystem::path irradiance_path = directory / std::filesystem::path("IrradianceMap.dds");
		std::filesystem::path reflection_path = directory / std::filesystem::path("ReflectionMap.dds");
		Texture diffuse, irradiance, reflection;
		if (!load_texture(diffuse, diffuse_path.string().c_str(), device, allocator, command_pool, command_buffer, queue, scratch_buffer, true))
		{
			printf("Failed to load texture: %s\n", diffuse_path.string().c_str());
			return EXIT_FAILURE;
		}
		if (!load_texture(irradiance, irradiance_path.string().c_str(), device, allocator, command_pool, command_buffer, queue, scratch_buffer, false))
		{
			printf("Failed to load texture: %s\n", diffuse_path.string().c_str());
			return EXIT_FAILURE;
		}
		if (!load_texture(reflection, reflection_path.string().c_str(), device, allocator, command_pool, command_buffer, queue, scratch_buffer, false))
		{
			printf("Failed to load texture: %s\n", diffuse_path.string().c_str());
			return EXIT_FAILURE;
		}

		environment.diffuse = diffuse;
		environment.irradiance = irradiance;
		environment.reflection = reflection;
	}

	Buffer index_buffer = create_buffer(allocator, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, indices.data());
	Buffer vertex_buffer = create_buffer(allocator, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, vertices.data());

	environment.index_buffer = create_buffer(allocator, environment.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, environment.indices.data());
	environment.vertex_buffer = create_buffer(allocator, environment.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, environment.vertices.data());

	//constexpr float camera_fov = glm::radians(20.0f);
	constexpr float camera_fov = glm::radians(15.0f);
	OrbitCamera main_camera{
		.distance = 3.1f,
		.fov = camera_fov,
		.angles = glm::vec2(-0.49005f, 0.0508272f),
		.pan_pos = glm::vec2(-0.0168704f, 0.0729295f),
		.projection = glm::perspectiveRH_ZO(camera_fov, (float)swapchain.width / (float)swapchain.height, 0.1f, 100.0f)
	};

	// Defaults from Jorge's demo
	Lights lights{};
	glm::mat4 shadowmap_projection = glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f);
	float shadow_bias = -0.01f;
	lights.lights = {
		{
			.orbit_camera = {
				.distance = 1.91332996f,
				.fov = glm::radians(45.0f),
				.far_plane = 10.0f,
				.angles = glm::vec2(1.58050001f, -0.208219007f),
				.projection = shadowmap_projection
			},
			.falloff_width = 0.05f,
			.bias = shadow_bias,
			.attenuation = 1.0f / 128.0f,
			.color = glm::vec3(1.05f),
			.shadowmap = create_texture(device, allocator, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		},
		{
			.orbit_camera = {
				.distance = 3.24000001f,
				.fov = glm::radians(45.0f),
				.far_plane = 10.0f,
				.angles = glm::vec2(0.804539979f, -0.612405002f),
				.projection = shadowmap_projection
			},
			.falloff_width = 0.05f,
			.bias = shadow_bias,
			.attenuation = 1.0f / 128.0f,
			.color = glm::vec3(0.55f),
			.shadowmap = create_texture(device, allocator, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		},
		{
			.orbit_camera = {
				.distance = 2.0f,
				.fov = glm::radians(45.0f),
				.far_plane = 10.0f,
				.angles = glm::vec2(-2.73357010f, -0.262273014f),
				.projection = shadowmap_projection
			},
			.falloff_width = 0.05f,
			.bias = shadow_bias,
			.attenuation = 1.0f / 128.0f,
			.color = glm::vec3(1.55f),
			.shadowmap = create_texture(device, allocator, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		},
		{
			.orbit_camera = {
				.distance = 2.0f,
				.fov = glm::radians(45.0f),
				.far_plane = 10.0f,
				.angles = glm::vec2(0.0f),
				.projection = shadowmap_projection
			},
			.falloff_width = 0.05f,
			.bias = shadow_bias,
			.attenuation = 1.0f / 128.0f,
			.color = glm::vec3(0.0f),
			.shadowmap = create_texture(device, allocator, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		},
		{
			.orbit_camera = {
				.distance = 2.0f,
				.fov = glm::radians(45.0f),
				.far_plane = 10.0f,
				.angles = glm::vec2(0.0f),
				.projection = shadowmap_projection
			},
			.falloff_width = 0.05f,
			.bias = shadow_bias,
			.attenuation = 1.0f / 128.0f,
			.color = glm::vec3(0.0f),
			.shadowmap = create_texture(device, allocator, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		},
	};

	lights.gpu_lights.resize(lights.lights.size());
	for (size_t i = 0; i < lights.lights.size(); ++i)
	{
		glm::mat4 view = lights.lights[i].orbit_camera.compute_view();
		glm::mat4 inverse_view = glm::inverse(view);
		glm::vec3 pos = inverse_view[3];
		glm::vec3 dir = inverse_view[2];
		printf("%zu, pos: (%f %f %f), dir: (%f %f %f)\n", i, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z);
		const auto& l = lights.lights[i];
		glm::mat4 texture_scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, -0.5f, 1.0f));
		glm::mat4 texture_translate = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.0f));
		glm::mat4 texture_transform = texture_translate * texture_scale;
		GPULight gpu_light{
			.position = pos,
			.direction = dir,
			.falloff_start = cosf(0.5f * l.orbit_camera.fov),
			.falloff_width = lights.lights[i].falloff_width,
			.color = l.color,
			.attenuation = l.attenuation,
			.far_plane = l.orbit_camera.far_plane,
			.bias = l.bias,
			.view_projection = texture_transform * glm::perspectiveRH_ZO(l.orbit_camera.fov, 1.0f, 0.1f, l.orbit_camera.far_plane) * view,
		};
		lights.gpu_lights[i] = gpu_light;
	}

	lights.buffer = create_buffer(allocator, sizeof(GPULight) * lights.gpu_lights.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, lights.gpu_lights.data());

	{
		void* mapped = lights.buffer.map();
		memcpy(mapped, lights.gpu_lights.data(), sizeof(GPULight)* lights.gpu_lights.size());
		lights.buffer.unmap();
	}

	struct {
		Texture glare_texture;
		Texture tmp_render_targets[N_BLOOM_PASSES][2];
	} bloom_resources;

		
	bloom_resources.glare_texture = create_texture(device, allocator, swapchain.width / 2, swapchain.height / 2, 1, RENDER_TARGET_FORMAT,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	{
		uint32_t base = 2;
		for (uint32_t i = 0; i < N_BLOOM_PASSES; ++i)
		{
			for (int j = 0; j < 2; ++j)
				bloom_resources.tmp_render_targets[i][j] = create_texture(device, allocator, swapchain.width / base, swapchain.height / base, 1, RENDER_TARGET_FORMAT,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

			base *= 2;
		}
	}

	struct {
		Texture tmp_render_target;
		Texture coc_render_target;
	} dof_resources;

	dof_resources.tmp_render_target = create_texture(device, allocator, swapchain.width, swapchain.height, 1, swapchain.format,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	dof_resources.coc_render_target = create_texture(device, allocator, swapchain.width, swapchain.height, 1, VK_FORMAT_R8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

	Texture noise_texture;
	FAIL_ON_ERROR(load_texture(noise_texture, "data/Noise.dds", device, allocator, command_pool, command_buffer, queue, scratch_buffer, false));

	Shader vertex_shader{};
	Shader fragment_shader{};
	FAIL_ON_ERROR(load_shader(vertex_shader, compiler, device, "forward.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(fragment_shader, compiler, device, "forward.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program forward_program = create_program(device, { vertex_shader, fragment_shader }, true);
	VkPipeline pipeline = create_pipeline(device, { vertex_shader, fragment_shader }, forward_program.pipeline_layout, {RENDER_TARGET_FORMAT, LINEAR_DEPTH_FORMAT}, DEPTH_FORMAT, 
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = MSAA,
		});

	Shader gltf_vertex_shader{};
	Shader gltf_fragment_shader{};
	FAIL_ON_ERROR(load_shader(gltf_vertex_shader, compiler, device, "forward_gltf.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(gltf_fragment_shader, compiler, device, "forward_gltf.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program forward_gltf_program = create_program(device, { gltf_vertex_shader, gltf_fragment_shader }, true);
	VkPipeline forward_gltf_pipeline = create_pipeline(device, { gltf_vertex_shader, gltf_fragment_shader }, forward_gltf_program.pipeline_layout, { RENDER_TARGET_FORMAT, LINEAR_DEPTH_FORMAT }, DEPTH_FORMAT,
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = MSAA,
		});

	Shader forward_pbr_vertex_shader{};
	Shader forward_pbr_fragment_shader{};
	FAIL_ON_ERROR(load_shader(forward_pbr_vertex_shader, compiler, device, "forward_pbr.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(forward_pbr_fragment_shader, compiler, device, "forward_pbr.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program forward_pbr_program = create_program(device, { forward_pbr_vertex_shader, forward_pbr_fragment_shader }, true);
	VkPipeline forward_pbr_pipeline = create_pipeline(device, { forward_pbr_vertex_shader, forward_pbr_fragment_shader }, forward_pbr_program.pipeline_layout, { RENDER_TARGET_FORMAT }, DEPTH_FORMAT, 
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_TRUE,
			//.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		},
		{
			.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT,
		},
		VK_CULL_MODE_NONE
		);

	Shader gltf_eyes_vertex_shader{};
	Shader gltf_eyes_fragment_shader{};
	FAIL_ON_ERROR(load_shader(gltf_eyes_vertex_shader, compiler, device, "forward_eyes_gltf.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(gltf_eyes_fragment_shader, compiler, device, "forward_eyes_gltf.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program forward_eyes_gltf_program = create_program(device, { gltf_eyes_vertex_shader, gltf_eyes_fragment_shader }, true);
	VkPipeline forward_eyes_gltf_pipeline = create_pipeline(device, { gltf_eyes_vertex_shader, gltf_eyes_fragment_shader }, forward_eyes_gltf_program.pipeline_layout, { RENDER_TARGET_FORMAT, LINEAR_DEPTH_FORMAT }, DEPTH_FORMAT, 
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = MSAA,
		});

	Shader tearline_vertex_shader{};
	Shader tearline_fragment_shader{};
	FAIL_ON_ERROR(load_shader(tearline_vertex_shader, compiler, device, "tearline.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(tearline_fragment_shader, compiler, device, "tearline.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program tearline_program = create_program(device, { tearline_vertex_shader, tearline_fragment_shader }, true);
	VkPipeline tearline_pipeline = create_pipeline(device, { tearline_vertex_shader, tearline_fragment_shader }, tearline_program.pipeline_layout, { RENDER_TARGET_FORMAT }, DEPTH_FORMAT, 
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = MSAA,
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		},
		{
			.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.alphaBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT,
		}
	);

	Shader shadowmap_vertex_shader{};
	FAIL_ON_ERROR(load_shader(shadowmap_vertex_shader, compiler, device, "shadowmap.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	Program shadowmap_program = create_program(device, { shadowmap_vertex_shader }, true);
	VkPipeline shadowmap_pipeline = create_shadowmap_pipeline(device, { shadowmap_vertex_shader }, shadowmap_program.pipeline_layout, DEPTH_FORMAT);

	Shader sss_vertex_shader{};
	Shader sss_fragment_shader{};
	FAIL_ON_ERROR(load_shader(sss_vertex_shader, compiler, device, "separable_sss.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(sss_fragment_shader, compiler, device, "separable_sss.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program sss_program = create_program(device, { sss_vertex_shader, sss_fragment_shader }, true);
	VkPipeline sss_pipeline = create_pipeline(device, { sss_vertex_shader, sss_fragment_shader }, sss_program.pipeline_layout, { RENDER_TARGET_FORMAT }, VK_FORMAT_UNDEFINED);

	Shader env_vertex_shader{};
	Shader env_fragment_shader{};
	FAIL_ON_ERROR(load_shader(env_vertex_shader, compiler, device, "envmap.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(env_fragment_shader, compiler, device, "envmap.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program env_program = create_program(device, { env_vertex_shader, env_fragment_shader }, true);
	VkPipeline env_pipeline = create_pipeline(device, { env_vertex_shader, env_fragment_shader }, env_program.pipeline_layout, { RENDER_TARGET_FORMAT }, VK_FORMAT_UNDEFINED, 
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = MSAA,
		});

	Shader bloom_glare_detect_vertex_shader{};
	Shader bloom_glare_detect_fragment_shader{};
	FAIL_ON_ERROR(load_shader(bloom_glare_detect_vertex_shader, compiler, device, "bloom.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(bloom_glare_detect_fragment_shader, compiler, device, "bloom.hlsl", "glare_detect", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program bloom_glare_detect_program = create_program(device, { bloom_glare_detect_vertex_shader, bloom_glare_detect_fragment_shader }, true);
	VkPipeline bloom_glare_detect_pipeline = create_pipeline(device, { bloom_glare_detect_vertex_shader, bloom_glare_detect_fragment_shader }, bloom_glare_detect_program.pipeline_layout, { RENDER_TARGET_FORMAT });

	Shader bloom_blur_vertex_shader{};
	Shader bloom_blur_fragment_shader{};
	FAIL_ON_ERROR(load_shader(bloom_blur_vertex_shader, compiler, device, "bloom.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(bloom_blur_fragment_shader, compiler, device, "bloom.hlsl", "blur", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program bloom_blur_program = create_program(device, { bloom_blur_vertex_shader, bloom_blur_fragment_shader }, true);
	VkPipeline bloom_blur_pipeline = create_pipeline(device, { bloom_blur_vertex_shader, bloom_blur_fragment_shader }, bloom_blur_program.pipeline_layout, { RENDER_TARGET_FORMAT });

	Shader bloom_combine_vertex_shader{};
	Shader bloom_combine_fragment_shader{};
	FAIL_ON_ERROR(load_shader(bloom_combine_vertex_shader, compiler, device, "bloom.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(bloom_combine_fragment_shader, compiler, device, "bloom.hlsl", "combine", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program bloom_combine_program = create_program(device, { bloom_combine_vertex_shader, bloom_combine_fragment_shader }, true);
	VkPipeline bloom_combine_pipeline = create_pipeline(device, { bloom_combine_vertex_shader, bloom_combine_fragment_shader }, bloom_combine_program.pipeline_layout, { swapchain.format });

	Shader tonemap_vertex_shader{};
	Shader tonemap_fragment_shader{};
	FAIL_ON_ERROR(load_shader(tonemap_vertex_shader, compiler, device, "bloom.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(tonemap_fragment_shader, compiler, device, "bloom.hlsl", "combine", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program tonemap_program = create_program(device, { tonemap_vertex_shader, tonemap_fragment_shader }, true);
	VkPipeline tonemap_pipeline = create_pipeline(device, { tonemap_vertex_shader, tonemap_fragment_shader }, tonemap_program.pipeline_layout, { swapchain.format });

	Shader dof_coc_vertex_shader{};
	Shader dof_coc_fragment_shader{};
	FAIL_ON_ERROR(load_shader(dof_coc_vertex_shader, compiler, device, "depth_of_field.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(dof_coc_fragment_shader, compiler, device, "depth_of_field.hlsl", "circle_of_confusion", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program dof_coc_program = create_program(device, { dof_coc_vertex_shader, dof_coc_fragment_shader }, true);
	VkPipeline dof_coc_pipeline = create_pipeline(device, { dof_coc_vertex_shader, dof_coc_fragment_shader }, dof_coc_program.pipeline_layout, { dof_resources.coc_render_target.format });

	Shader dof_blur_vertex_shader{};
	Shader dof_blur_fragment_shader{};
	FAIL_ON_ERROR(load_shader(dof_blur_vertex_shader, compiler, device, "depth_of_field.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(dof_blur_fragment_shader, compiler, device, "depth_of_field.hlsl", "blur", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program dof_blur_program = create_program(device, { dof_blur_vertex_shader, dof_blur_fragment_shader }, true);
	VkPipeline dof_blur_pipeline = create_pipeline(device, { dof_blur_vertex_shader, dof_blur_fragment_shader }, dof_coc_program.pipeline_layout, { swapchain.format });

	Shader film_grain_vertex_shader{};
	Shader film_grain_fragment_shader{};
	FAIL_ON_ERROR(load_shader(film_grain_vertex_shader, compiler, device, "film_grain.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(film_grain_fragment_shader, compiler, device, "film_grain.hlsl", "film_grain", VK_SHADER_STAGE_FRAGMENT_BIT));
	Program film_grain_program = create_program(device, { film_grain_vertex_shader, film_grain_fragment_shader }, true);
	VkPipeline film_grain_pipeline = create_pipeline(device, { film_grain_vertex_shader, film_grain_fragment_shader }, film_grain_program.pipeline_layout, { swapchain.format });

	Texture depth_texture_msaa = create_texture(device, allocator, swapchain.width, swapchain.height, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1, MSAA);
	Texture depth_texture = create_texture(device, allocator, swapchain.width, swapchain.height, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1);
	Texture linear_depth_texture_msaa = create_texture(device, allocator, swapchain.width, swapchain.height, 1, LINEAR_DEPTH_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, MSAA);
	Texture linear_depth_texture = create_texture(device, allocator, swapchain.width, swapchain.height, 1, LINEAR_DEPTH_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	Texture main_render_target = create_texture(device, allocator, swapchain.width, swapchain.height, 1, RENDER_TARGET_FORMAT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	Texture tmp_render_target = create_texture(device, allocator, swapchain.width, swapchain.height, 1, RENDER_TARGET_FORMAT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	Texture main_render_target_msaa = create_texture(device, allocator, swapchain.width, swapchain.height, 1, RENDER_TARGET_FORMAT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, MSAA);


	VkSampler anisotropic_sampler = VK_NULL_HANDLE;
	VkSampler linear_sampler = VK_NULL_HANDLE;
	VkSampler linear_sampler_wrap = VK_NULL_HANDLE;
	VkSampler point_sampler = VK_NULL_HANDLE;
	VkSampler shadow_sampler = VK_NULL_HANDLE;
	{
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = 16.0f,
			.maxLod = VK_LOD_CLAMP_NONE
		};

		VK_CHECK(vkCreateSampler(device, &create_info, nullptr, &anisotropic_sampler));

		create_info.anisotropyEnable = VK_FALSE;
		create_info.maxAnisotropy = 1.0f;

		VK_CHECK(vkCreateSampler(device, &create_info, nullptr, &linear_sampler_wrap));

		create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		VK_CHECK(vkCreateSampler(device, &create_info, nullptr, &linear_sampler));

		create_info.magFilter = VK_FILTER_NEAREST;
		create_info.minFilter = VK_FILTER_NEAREST;
		create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		VK_CHECK(vkCreateSampler(device, &create_info, nullptr, &point_sampler));

		create_info.compareEnable = VK_TRUE;
		create_info.compareOp = VK_COMPARE_OP_LESS;
		create_info.magFilter = VK_FILTER_LINEAR;
		create_info.minFilter = VK_FILTER_LINEAR;
		create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		VK_CHECK(vkCreateSampler(device, &create_info, nullptr, &shadow_sampler));
	}

	glm::mat4 view = main_camera.compute_view();
	glm::mat4 proj = main_camera.projection;
	glm::mat4 viewproj = proj * view;
	glm::vec3 camera_pos = glm::inverse(view)[3];

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

		{ // Change image layouts
			VkImageMemoryBarrier2 barriers[] = {
				image_barrier(swapchain.images[image_index],
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(main_render_target_msaa.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(main_render_target.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(tmp_render_target.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(depth_texture_msaa.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_DEPTH_BIT),
				image_barrier(linear_depth_texture_msaa.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(linear_depth_texture.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(bloom_resources.glare_texture.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(dof_resources.coc_render_target.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
				image_barrier(dof_resources.tmp_render_target.image,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
			};

			pipeline_barrier(command_buffer, 0, nullptr, (uint32_t)std::size(barriers), barriers);

			VkImageMemoryBarrier2 barriers2[N_BLOOM_PASSES * 2];
			for (uint32_t i = 0; i < N_BLOOM_PASSES; ++i)
				for (uint32_t j = 0; j < 2; ++j)
					barriers2[i + (N_BLOOM_PASSES * j)] = image_barrier(bloom_resources.tmp_render_targets[i][j].image,
						VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
						VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

			pipeline_barrier(command_buffer, 0, nullptr, (uint32_t)std::size(barriers2), barriers2);
		}

		for (size_t i = 0; i < lights.lights.size(); ++i)
		{ // Do shadows
			const Texture& sm = lights.lights[i].shadowmap;
			VkImageMemoryBarrier2 barrier = image_barrier(sm.image,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_ASPECT_DEPTH_BIT);

			pipeline_barrier(command_buffer, {}, { barrier });

			VkRenderingAttachmentInfo depth_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = sm.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = {.depthStencil = {.depth = 1.0f} }
			};

			VkRenderingInfo rendering_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.renderArea = { 0, 0, SHADOWMAP_SIZE, SHADOWMAP_SIZE },
				.layerCount = 1,
				.pDepthAttachment = &depth_info,
			};

			vkCmdBeginRendering(command_buffer, &rendering_info);

			VkViewport viewport{
				.x = 0.0f,
				.y = (float)SHADOWMAP_SIZE,
				.width = (float)SHADOWMAP_SIZE,
				.height = -(float)SHADOWMAP_SIZE,
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};

			vkCmdSetViewport(command_buffer, 0, 1, &viewport);

			VkRect2D scissor{
				.offset = { 0, 0 },
				.extent = { SHADOWMAP_SIZE, SHADOWMAP_SIZE }
			};

			vkCmdSetScissor(command_buffer, 0, 1, &scissor);

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowmap_pipeline);

			DescriptorInfo descriptor_info[] = {
				DescriptorInfo(vertex_buffer.buffer),
			};

			vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, shadowmap_program.descriptor_update_template, shadowmap_program.pipeline_layout, 0, descriptor_info);
			vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			for (const auto& d : mesh_draws)
			{
				if (materials[d.material_index].type == Material::EYES || materials[d.material_index].type == Material::STANDARD) continue;

				struct {
					glm::mat4 mvp;
				} pc;

				/**
					 * This is for rendering linear values:
					 * Check this: http://www.mvps.org/directx/articles/linear_z/linearz.htm
				*/

				const GPULight& gl = lights.gpu_lights[i];
				glm::mat4 linear_projection = lights.lights[i].orbit_camera.projection;
				float q = -linear_projection[2][2];
				float n = linear_projection[3][2] / linear_projection[2][2];
				float f = -n * q / (1.0f - q);
				linear_projection[2][2] /= f;
				linear_projection[3][2] /= f;

				pc.mvp = linear_projection * lights.lights[i].orbit_camera.compute_view() * d.transform;

				vkCmdPushConstants(command_buffer, shadowmap_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

				const Mesh& mesh = meshes[d.mesh_index];
				vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, mesh.first_index, mesh.first_vertex, 0);
			}

			vkCmdEndRendering(command_buffer);
		}

		{ // Do environment map
			VkRenderingAttachmentInfo attachment = {
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = main_render_target_msaa.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
			};

			VkRenderingInfo rendering_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.renderArea = { 0, 0, swapchain.width, swapchain.height },
				.layerCount = 1,
				.colorAttachmentCount = 1,
				.pColorAttachments = &attachment,
			};

			vkCmdBeginRendering(command_buffer, &rendering_info);

			set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, env_pipeline);

			struct {
				glm::mat4 mvp;
				float intensity = ENVIRONMENT_INTENSITY;
			} pc;

			pc.mvp = viewproj;

			vkCmdPushConstants(command_buffer, env_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

			DescriptorInfo descriptor_info[] = {
				DescriptorInfo(environment.vertex_buffer.buffer),
				DescriptorInfo(linear_sampler),
				DescriptorInfo(environment.diffuse.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			};

			vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, env_program.descriptor_update_template, env_program.pipeline_layout, 0, descriptor_info);
			vkCmdBindIndexBuffer(command_buffer, environment.index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(command_buffer, environment.mesh.index_count, 1, environment.mesh.first_index, environment.mesh.first_vertex, 0);

			vkCmdEndRendering(command_buffer);

			VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
			pipeline_barrier(command_buffer, { barrier }, {});
		}

		// Do forward pass
		if (scene_is_gltf)
		{
			begin_rendering(command_buffer, swapchain.width, swapchain.height, {
				{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = main_render_target_msaa.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
					.resolveImageView = main_render_target.view,
					.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
				},
				{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = linear_depth_texture_msaa.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
					.resolveImageView = linear_depth_texture.view,
					.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
				}},
				{
					{
						.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
						.imageView = depth_texture_msaa.view,
						.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
						.resolveMode = VK_RESOLVE_MODE_MIN_BIT,
						.resolveImageView = depth_texture.view,
						.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.clearValue = {.depthStencil = {.depth = 1.0f} }
					}
				});

			set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);
			
			vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			for (const auto& d : mesh_draws)
			{
				assert(d.material_index >= 0);
				const Material& mat = materials[d.material_index];
				if (mat.type == Material::EYE_OCCLUSION || mat.type == Material::TEARLINE || mat.type == Material::STANDARD) continue;

				assert(mat.basecolor_texture >= 0);
				assert(mat.normal_texture >= 0);
				assert(mat.metallic_roughness_texture >= 0);
				assert(mat.occlusion_texture >= 0);


				const Mesh& mesh = meshes[d.mesh_index];

				switch (mat.type)
				{
				case Material::STANDARD:
				{
					//continue;
				}	break;
				case Material::SKIN:
				{
					struct {
						glm::mat4 viewproj;
						glm::mat4 model;
						uint32_t n_lights;
						glm::vec3 camera_pos;
						float translucency = SSS_TRANSLUCENCY;
						float sss_width = SSS_WIDTH;
						float ambient = AMBIENT_INTENSITY;
					} pc;

					pc.viewproj = viewproj;
					pc.model = d.transform;
					pc.n_lights = (uint32_t)lights.lights.size();
					pc.camera_pos = camera_pos;

					DescriptorInfo descriptor_info[] = {
						DescriptorInfo(vertex_buffer.buffer),
						DescriptorInfo(anisotropic_sampler),
						DescriptorInfo(linear_sampler),
						DescriptorInfo(point_sampler),
						DescriptorInfo(beckmann_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.basecolor_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.normal_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.metallic_roughness_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.occlusion_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(lights.buffer.buffer),
						DescriptorInfo(shadow_sampler),
						DescriptorInfo(lights.lights[0].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[1].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[2].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[3].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[4].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(environment.irradiance.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(environment.reflection.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					};

					draw_with_pipeline_and_program(command_buffer, mesh, forward_gltf_program, forward_gltf_pipeline, pc, descriptor_info);
				}	break;
				case Material::EYES:
				{
					struct {
						glm::mat4 viewproj;
						glm::mat4 model;
						glm::mat4 model_inverse;
						uint32_t n_lights;
						glm::vec3 camera_pos;
						float ambient = AMBIENT_INTENSITY;
						glm::vec3 gaze_direction;
					} pc;

					pc.viewproj = viewproj;
					pc.model = d.transform;
					pc.model_inverse = glm::inverse(d.transform);
					pc.n_lights = (uint32_t)lights.lights.size();
					pc.camera_pos = camera_pos;
					pc.gaze_direction = glm::normalize(glm::vec3(d.transform[2]));

					assert(mat.emissive_texture >= 0);
					DescriptorInfo descriptor_info[] = {
						DescriptorInfo(vertex_buffer.buffer),
						DescriptorInfo(anisotropic_sampler),
						DescriptorInfo(linear_sampler),
						DescriptorInfo(point_sampler),
						DescriptorInfo(beckmann_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.basecolor_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.normal_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.metallic_roughness_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.occlusion_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.emissive_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(lights.buffer.buffer),
						DescriptorInfo(shadow_sampler),
						DescriptorInfo(lights.lights[0].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[1].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[2].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[3].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[4].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(environment.irradiance.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(environment.reflection.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					};

					draw_with_pipeline_and_program(command_buffer, mesh, forward_eyes_gltf_program, forward_eyes_gltf_pipeline, pc, descriptor_info);

				}	break;
				default:
					assert(false);
				}
			}

			vkCmdEndRendering(command_buffer);
		}
		else
		{ 
			VkRenderingAttachmentInfo color_attachments[] = {
				{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = main_render_target_msaa.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
					.resolveImageView = main_render_target.view,
					.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
				},
				{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = linear_depth_texture_msaa.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
					.resolveImageView = linear_depth_texture.view,
					.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.clearValue = { .color = {0.0f, 0.0f, 0.0f, 0.0f} }
				},
			};


			VkRenderingAttachmentInfo depth_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = depth_texture_msaa.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = {.depthStencil = {.depth = 1.0f} }
			};

			VkRenderingInfo rendering_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.renderArea = { 0, 0, swapchain.width, swapchain.height },
				.layerCount = 1,
				.colorAttachmentCount = (uint32_t)std::size(color_attachments),
				.pColorAttachments = color_attachments,
				.pDepthAttachment = &depth_info,
			};

			vkCmdBeginRendering(command_buffer, &rendering_info);

			VkViewport viewport{
				.x = 0.0f,
				.y = (float)swapchain.height,
				.width = (float)swapchain.width,
				.height = -(float)swapchain.height,
				.minDepth = 0.0f,
				.maxDepth = 1.0f
			};

			vkCmdSetViewport(command_buffer, 0, 1, &viewport);

			VkRect2D scissor{
				.offset = { 0, 0 },
				.extent = { swapchain.width, swapchain.height },
			};

			vkCmdSetScissor(command_buffer, 0, 1, &scissor);

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			for (const auto& d : mesh_draws)
			{
				assert(d.material_index >= 0);
				const Material& mat = materials[d.material_index];

				assert(mat.basecolor_texture >= 0);
				assert(mat.normal_texture >= 0);
				assert(mat.specular_texture >= 0);

				DescriptorInfo descriptor_info[] = {
					DescriptorInfo(vertex_buffer.buffer),
					DescriptorInfo(anisotropic_sampler),
					DescriptorInfo(linear_sampler),
					DescriptorInfo(point_sampler),
					DescriptorInfo(beckmann_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					DescriptorInfo(textures[mat.basecolor_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					DescriptorInfo(textures[mat.normal_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					DescriptorInfo(textures[mat.specular_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					DescriptorInfo(lights.buffer.buffer),
					DescriptorInfo(shadow_sampler),
					DescriptorInfo(lights.lights[0].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(lights.lights[1].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(lights.lights[2].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(lights.lights[3].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(lights.lights[4].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(environment.irradiance.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				};

				vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, forward_program.descriptor_update_template, forward_program.pipeline_layout, 0, descriptor_info);

				struct {
					glm::mat4 mvp;
					uint32_t n_lights;
					glm::vec3 camera_pos;
					float translucency = SSS_TRANSLUCENCY;
					float sss_width = SSS_WIDTH;
					float ambient = AMBIENT_INTENSITY;
				} pc;

				pc.mvp = viewproj * d.transform;
				pc.n_lights = (uint32_t)lights.lights.size();
				pc.camera_pos = glm::inverse(view)[3];

				vkCmdPushConstants(command_buffer, forward_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

				const Mesh& mesh = meshes[d.mesh_index];
				vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, mesh.first_index, mesh.first_vertex, 0);
			}

			vkCmdEndRendering(command_buffer);
		}

		if (SSS_ENABLED)
		{ // Do SSS

			for (uint32_t pass = 0; pass < 2; ++pass)
			{
				VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT);

				pipeline_barrier(command_buffer, { barrier }, {});

				VkRenderingAttachmentInfo attachment_info{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = pass == 0 ? tmp_render_target.view : main_render_target.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
				};

				VkRenderingInfo rendering_info{
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea = { 0, 0, swapchain.width, swapchain.height },
					.layerCount = 1,
					.colorAttachmentCount = 1,
					.pColorAttachments = &attachment_info,
				};

				vkCmdBeginRendering(command_buffer, &rendering_info);

				VkViewport viewport{
					.x = 0.0f,
					.y = (float)swapchain.height,
					.width = (float)swapchain.width,
					.height = -(float)swapchain.height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f
				};

				vkCmdSetViewport(command_buffer, 0, 1, &viewport);

				VkRect2D scissor{
					.offset = { 0, 0 },
					.extent = { swapchain.width, swapchain.height },
				};

				vkCmdSetScissor(command_buffer, 0, 1, &scissor);

				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sss_pipeline);

				struct {
					glm::vec2 dir;
					float sss_width = SSS_WIDTH;
				} pc;

				pc.dir = pass == 0 ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 1.0f);

				DescriptorInfo descriptor_info[] = {
					DescriptorInfo(linear_sampler),
					DescriptorInfo(point_sampler),
					DescriptorInfo(pass == 0 ? main_render_target.view : tmp_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(linear_depth_texture.view, VK_IMAGE_LAYOUT_GENERAL),
				};

				vkCmdPushConstants(command_buffer, sss_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
				vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, sss_program.descriptor_update_template, sss_program.pipeline_layout, 0, descriptor_info);

				vkCmdDraw(command_buffer, 3, 1, 0, 0);

				vkCmdEndRendering(command_buffer);
			}
		}

#if 1
		{ // Do tearline
			begin_rendering(command_buffer, swapchain.width, swapchain.height, {
				{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = main_render_target.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				} },
				{
					{
						.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
						.imageView = depth_texture.view,
						.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
						.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
						.storeOp = VK_ATTACHMENT_STORE_OP_NONE,
					}
				});

				set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);

				vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

				for (const auto& d : mesh_draws)
				{
					assert(d.material_index >= 0);
					const Material& mat = materials[d.material_index];
					if (mat.type != Material::STANDARD) continue;

					assert(mat.normal_texture >= 0);

					const Mesh& mesh = meshes[d.mesh_index];
					struct {
						glm::mat4 viewproj;
						glm::mat4 model;
						uint32_t n_lights;
						glm::vec3 camera_pos;
						float ambient = AMBIENT_INTENSITY;
					} pc;

					pc.viewproj = viewproj;
					pc.model = d.transform;
					pc.n_lights = (uint32_t)lights.lights.size();
					pc.camera_pos = camera_pos;

					DescriptorInfo descriptor_info[] = {
						DescriptorInfo(vertex_buffer.buffer),
						DescriptorInfo(anisotropic_sampler),
						DescriptorInfo(linear_sampler),
						DescriptorInfo(point_sampler),
						DescriptorInfo(beckmann_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.basecolor_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.normal_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.metallic_roughness_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(textures[mat.occlusion_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(lights.buffer.buffer),
						DescriptorInfo(shadow_sampler),
						DescriptorInfo(lights.lights[0].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[1].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[2].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[3].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(lights.lights[4].shadowmap.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(environment.irradiance.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					};

					draw_with_pipeline_and_program(command_buffer, mesh, forward_pbr_program, forward_pbr_pipeline, pc, descriptor_info);
				}

#if 0
				for (const auto& d : mesh_draws)
				{
					assert(d.material_index >= 0);
					const Material& mat = materials[d.material_index];
					if (mat.type != Material::TEARLINE) continue;

					assert(mat.normal_texture >= 0);

					const Mesh& mesh = meshes[d.mesh_index];

					struct {
						glm::mat4 viewproj;
						glm::mat4 model;
						glm::vec3 camera_pos;
						float ambient = AMBIENT_INTENSITY;
						glm::vec2 pixel_size;
					} pc;

					pc.viewproj = viewproj;
					pc.model = d.transform;
					pc.camera_pos = camera_pos;
					pc.pixel_size = glm::vec2(1.0f / swapchain.width, 1.0f / swapchain.height);

					DescriptorInfo descriptor_info[] = {
						DescriptorInfo(vertex_buffer.buffer),
						DescriptorInfo(anisotropic_sampler),
						DescriptorInfo(linear_sampler),
						DescriptorInfo(textures[mat.normal_texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						DescriptorInfo(linear_depth_texture.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(environment.reflection.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					};

					draw_with_pipeline_and_program(command_buffer, mesh, tearline_program, tearline_pipeline, pc, descriptor_info);
				}
#endif

				vkCmdEndRendering(command_buffer);
		}
#endif


		if (BLOOM_ENABLED)
		{ // Do bloom
			VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT);

			pipeline_barrier(command_buffer, { barrier }, {});

			VkRenderingAttachmentInfo attachment_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = bloom_resources.glare_texture.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
			};

			VkRenderingInfo rendering_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.renderArea = { 0, 0, bloom_resources.glare_texture.width, bloom_resources.glare_texture.height },
				.layerCount = 1,
				.colorAttachmentCount = 1,
				.pColorAttachments = &attachment_info,
			};

			vkCmdBeginRendering(command_buffer, &rendering_info);

			set_viewport_and_scissor(command_buffer, bloom_resources.glare_texture.width, bloom_resources.glare_texture.height);

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_glare_detect_pipeline);

			struct {
				glm::vec2 dir;
				glm::vec2 pixel_size;
				float bloom_threshold = BLOOM_THRESHOLD;
				float exposure = EXPOSURE;
			} pc;

			pc.pixel_size = glm::vec2(1.0f / (float)bloom_resources.glare_texture.width, 1.0f / (float)bloom_resources.glare_texture.height);

			DescriptorInfo descriptor_info[] = {
				DescriptorInfo(linear_sampler),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
			};

			vkCmdPushConstants(command_buffer, bloom_glare_detect_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
			vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, bloom_glare_detect_program.descriptor_update_template, bloom_glare_detect_program.pipeline_layout, 0, descriptor_info);

			vkCmdDraw(command_buffer, 3, 1, 0, 0);

			vkCmdEndRendering(command_buffer);

			auto do_blur = [&](VkCommandBuffer cmd, const Texture& src, const Texture& dst, glm::vec2 dir)
				{
					glm::uvec2 rt_size = glm::uvec2(dst.width, dst.height);
					glm::vec2 pixel_size = 1.0f / glm::vec2(rt_size);

					VkRenderingAttachmentInfo attachment_info{
						.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
						.imageView = dst.view,
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
					};

					VkRenderingInfo rendering_info{
						.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
						.renderArea = { 0, 0, rt_size.x, rt_size.y},
						.layerCount = 1,
						.colorAttachmentCount = 1,
						.pColorAttachments = &attachment_info,
					};

					vkCmdBeginRendering(cmd, &rendering_info);

					set_viewport_and_scissor(cmd, rt_size.x, rt_size.y);

					struct {
						glm::vec2 dir;
						glm::vec2 pixel_size;
						float bloom_threshold = BLOOM_THRESHOLD;
						float exposure = EXPOSURE;
						glm::vec2 step;
					} pc;

					pc.dir = dir;
					pc.pixel_size = pixel_size;
					pc.step = pixel_size * BLOOM_WIDTH * dir;

					DescriptorInfo descriptor_info[] = {
						DescriptorInfo(linear_sampler),
						DescriptorInfo(src.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(src.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(src.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(src.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(src.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(src.view, VK_IMAGE_LAYOUT_GENERAL),
						DescriptorInfo(src.view, VK_IMAGE_LAYOUT_GENERAL),
					};

					vkCmdPushConstants(cmd, bloom_blur_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
					vkCmdPushDescriptorSetWithTemplateKHR(cmd, bloom_blur_program.descriptor_update_template, bloom_blur_program.pipeline_layout, 0, descriptor_info);

					vkCmdDraw(cmd, 3, 1, 0, 0);

					vkCmdEndRendering(cmd);
				};

			Texture current = bloom_resources.glare_texture;
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_blur_pipeline);

			for (uint32_t i = 0; i < N_BLOOM_PASSES; ++i) 
			{
				VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

				pipeline_barrier(command_buffer, { barrier }, {});
				do_blur(command_buffer, current, bloom_resources.tmp_render_targets[i][0], glm::vec2(1.0f, 0.0f));
				pipeline_barrier(command_buffer, { barrier }, {});
				do_blur(command_buffer, bloom_resources.tmp_render_targets[i][0], bloom_resources.tmp_render_targets[i][1], glm::vec2(0.0f, 1.0f));
				current = bloom_resources.tmp_render_targets[i][1];
			}

			{
				// Do combine 
				VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
					VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

				pipeline_barrier(command_buffer, { barrier }, {});

				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_combine_pipeline);

				VkRenderingAttachmentInfo attachment_info{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = views[image_index],
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
				};

				VkRenderingInfo rendering_info{
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea = { 0, 0, swapchain.width, swapchain.height},
					.layerCount = 1,
					.colorAttachmentCount = 1,
					.pColorAttachments = &attachment_info,
				};

				vkCmdBeginRendering(command_buffer, &rendering_info);

				set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);

				struct {
					glm::vec2 dir;
					glm::vec2 pixel_size;
					float bloom_threshold = BLOOM_THRESHOLD;
					float exposure = EXPOSURE;
					glm::vec2 step;
					float bloom_intensity = BLOOM_INTENSITY;
					float defocus = BLOOM_DEFOCUS;
				} pc;

				pc.pixel_size = 1.0f / glm::vec2(swapchain.width, swapchain.height);
				vkCmdPushConstants(command_buffer, bloom_combine_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

				DescriptorInfo descriptor_info[] = {
					DescriptorInfo(linear_sampler),
					DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(bloom_resources.tmp_render_targets[0][1].view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(bloom_resources.tmp_render_targets[1][1].view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(bloom_resources.tmp_render_targets[2][1].view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(bloom_resources.tmp_render_targets[3][1].view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(bloom_resources.tmp_render_targets[4][1].view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(bloom_resources.tmp_render_targets[5][1].view, VK_IMAGE_LAYOUT_GENERAL),
				};

				vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, bloom_combine_program.descriptor_update_template, bloom_combine_program.pipeline_layout, 0, descriptor_info);

				vkCmdDraw(command_buffer, 3, 1, 0, 0);

				vkCmdEndRendering(command_buffer);

				pipeline_barrier(command_buffer, { barrier }, {});
			}
		}
		else
		{ // Do tonemap
			VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

			pipeline_barrier(command_buffer, { barrier }, {});

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_pipeline);

			VkRenderingAttachmentInfo attachment_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = views[image_index],
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f} }
			};

			VkRenderingInfo rendering_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.renderArea = { 0, 0, swapchain.width, swapchain.height},
				.layerCount = 1,
				.colorAttachmentCount = 1,
				.pColorAttachments = &attachment_info,
			};

			vkCmdBeginRendering(command_buffer, &rendering_info);

			set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);

			struct {
				glm::vec2 dir;
				glm::vec2 pixel_size;
				float bloom_threshold = BLOOM_THRESHOLD;
				float exposure = EXPOSURE;
				glm::vec2 step;
				float bloom_intensity = BLOOM_INTENSITY;
				float defocus = BLOOM_DEFOCUS;
			} pc{};

			vkCmdPushConstants(command_buffer, tonemap_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

			DescriptorInfo descriptor_info[] = {
				DescriptorInfo(linear_sampler),
				DescriptorInfo(main_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(bloom_resources.tmp_render_targets[0][1].view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(bloom_resources.tmp_render_targets[1][1].view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(bloom_resources.tmp_render_targets[2][1].view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(bloom_resources.tmp_render_targets[3][1].view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(bloom_resources.tmp_render_targets[4][1].view, VK_IMAGE_LAYOUT_GENERAL),
				DescriptorInfo(bloom_resources.tmp_render_targets[5][1].view, VK_IMAGE_LAYOUT_GENERAL),
			};

			vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, tonemap_program.descriptor_update_template, tonemap_program.pipeline_layout, 0, descriptor_info);

			vkCmdDraw(command_buffer, 3, 1, 0, 0);

			vkCmdEndRendering(command_buffer);

			pipeline_barrier(command_buffer, { barrier }, {});
		}

		if (DOF_ENABLED)
		{ // Do depth of field
			VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
			pipeline_barrier(command_buffer, { barrier }, {});

			{
				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dof_coc_pipeline);

				begin_rendering(command_buffer, swapchain.width, swapchain.height, {
					{
						.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
						.imageView = dof_resources.coc_render_target.view,
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f}}
					} });

				set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);

				struct {
					float focus_distance = DOF_FOCUS_DISTANCE;
					float focus_range = DOF_FOCUS_RANGE;
					glm::vec2 focus_falloff = DOF_FOCUS_FALLOFF;
				} pc;

				vkCmdPushConstants(command_buffer, dof_coc_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

				DescriptorInfo descriptor_info[] = {
					DescriptorInfo(linear_sampler),
					DescriptorInfo(point_sampler),
					DescriptorInfo(linear_depth_texture.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(dof_resources.coc_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(linear_depth_texture.view, VK_IMAGE_LAYOUT_GENERAL),
				};

				vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, dof_coc_program.descriptor_update_template, dof_coc_program.pipeline_layout, 0, descriptor_info);

				vkCmdDraw(command_buffer, 3, 1, 0, 0);

				vkCmdEndRendering(command_buffer);
			}

			// Blur horizontal + vertical
			for (uint32_t i = 0; i < 2; ++i)
			{
				pipeline_barrier(command_buffer, { barrier }, {});

				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dof_blur_pipeline);

				begin_rendering(command_buffer, swapchain.width, swapchain.height, {
					{
						.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
						.imageView = i == 0 ? dof_resources.tmp_render_target.view : views[image_index],
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f}}
					} });

				set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);

				struct {
					float focus_distance = DOF_FOCUS_DISTANCE;
					float focus_range = DOF_FOCUS_RANGE;
					glm::vec2 focus_falloff = DOF_FOCUS_FALLOFF;
					glm::vec2 step;
				} pc;

				glm::vec2 dir = i == 0 ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 1.0f);
				glm::vec2 pixel_size = 1.0f / glm::vec2(swapchain.width, swapchain.height);
				pc.step = pixel_size * DOF_BLUR_WIDTH * dir;

				vkCmdPushConstants(command_buffer, dof_blur_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

				VkImageView in_view = i == 0 ? views[image_index] : dof_resources.tmp_render_target.view;
				DescriptorInfo descriptor_info[] = {
					DescriptorInfo(linear_sampler),
					DescriptorInfo(point_sampler),
					DescriptorInfo(linear_depth_texture.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(dof_resources.coc_render_target.view, VK_IMAGE_LAYOUT_GENERAL),
					DescriptorInfo(in_view, VK_IMAGE_LAYOUT_GENERAL),
				};

				vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, dof_coc_program.descriptor_update_template, dof_coc_program.pipeline_layout, 0, descriptor_info);

				vkCmdDraw(command_buffer, 3, 1, 0, 0);

				vkCmdEndRendering(command_buffer);
			}
		}

		if (FILM_GRAIN_ENABLED)
		{ // Do film grain
			VkMemoryBarrier2 barrier = memory_barrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
			pipeline_barrier(command_buffer, { barrier }, {});

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, film_grain_pipeline);

			begin_rendering(command_buffer, swapchain.width, swapchain.height, {
				{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = views[image_index],
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f}}
				} });

			set_viewport_and_scissor(command_buffer, swapchain.width, swapchain.height);

			struct {
				glm::vec2 pixel_size;
				float time;
				float exposure;
				float noise_intensity;
			} pc;

			pc.pixel_size = 1.0f / glm::vec2(swapchain.width, swapchain.height);
			pc.time = 2.5f * (SDL_GetTicks64() / 1000.0f);
			pc.exposure = EXPOSURE;
			pc.noise_intensity = FILM_GRAIN_NOISE_INTENSITY;

			vkCmdPushConstants(command_buffer, film_grain_program.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

			DescriptorInfo descriptor_info[] = {
				DescriptorInfo(linear_sampler),
				DescriptorInfo(linear_sampler_wrap),
				DescriptorInfo(noise_texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				DescriptorInfo(views[image_index], VK_IMAGE_LAYOUT_GENERAL),
			};

			vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, film_grain_program.descriptor_update_template, film_grain_program.pipeline_layout, 0, descriptor_info);

			vkCmdDraw(command_buffer, 3, 1, 0, 0);

			vkCmdEndRendering(command_buffer);
		}

		{ // Transition to present src
			VkImageMemoryBarrier2 barrier = image_barrier(swapchain.images[image_index], 
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

			pipeline_barrier(command_buffer, {}, { barrier });
		}


		VK_CHECK(vkEndCommandBuffer(command_buffer));

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

		VK_CHECK(vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX));
		VK_CHECK(vkResetFences(device, 1, &frame_fence));
	}

	VK_CHECK(vkDeviceWaitIdle(device));

	SDL_DestroyWindow(window);

	vkDestroySampler(device, anisotropic_sampler, nullptr);
	vkDestroySampler(device, linear_sampler, nullptr);
	vkDestroySampler(device, linear_sampler_wrap, nullptr);
	vkDestroySampler(device, point_sampler, nullptr);
	vkDestroySampler(device, shadow_sampler, nullptr);
	noise_texture.destroy();
	dof_resources.tmp_render_target.destroy();
	dof_resources.coc_render_target.destroy();
	bloom_resources.glare_texture.destroy();
	for (uint32_t i = 0; i < N_BLOOM_PASSES; ++i)
		for (uint32_t j = 0; j < 2; ++j)
			bloom_resources.tmp_render_targets[i][j].destroy();
	environment.diffuse.destroy();
	environment.irradiance.destroy();
	environment.reflection.destroy();
	environment.index_buffer.destroy();
	environment.vertex_buffer.destroy();	
	beckmann_lut.destroy();
	for (auto& l : lights.lights) l.shadowmap.destroy();
	for (Texture& t : textures) t.destroy();
	scratch_buffer.destroy();
	lights.buffer.destroy();
	vertex_buffer.destroy();
	index_buffer.destroy();
	depth_texture_msaa.destroy();
	depth_texture.destroy();
	linear_depth_texture.destroy();
	linear_depth_texture_msaa.destroy();
	main_render_target.destroy();
	main_render_target_msaa.destroy();
	tmp_render_target.destroy();
	destroy_program(device, forward_program);
	destroy_program(device, forward_gltf_program);
	destroy_program(device, forward_eyes_gltf_program);
	destroy_program(device, forward_pbr_program);
	destroy_program(device, tearline_program);
	destroy_program(device, tonemap_program);
	destroy_program(device, shadowmap_program);
	destroy_program(device, sss_program);
	destroy_program(device, env_program);
	destroy_program(device, bloom_glare_detect_program);
	destroy_program(device, bloom_blur_program);
	destroy_program(device, bloom_combine_program);
	destroy_program(device, dof_coc_program);
	destroy_program(device, dof_blur_program);
	destroy_program(device, film_grain_program);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipeline(device, forward_gltf_pipeline, nullptr);
	vkDestroyPipeline(device, forward_eyes_gltf_pipeline, nullptr);
	vkDestroyPipeline(device, shadowmap_pipeline, nullptr);
	vkDestroyPipeline(device, tonemap_pipeline, nullptr);
	vkDestroyPipeline(device, sss_pipeline , nullptr);
	vkDestroyPipeline(device, env_pipeline, nullptr);
	vkDestroyPipeline(device, bloom_glare_detect_pipeline, nullptr);
	vkDestroyPipeline(device, bloom_blur_pipeline, nullptr);
	vkDestroyPipeline(device, bloom_combine_pipeline, nullptr);
	vkDestroyPipeline(device, dof_coc_pipeline, nullptr);
	vkDestroyPipeline(device, dof_blur_pipeline, nullptr);
	vkDestroyPipeline(device, film_grain_pipeline, nullptr);
	vkDestroyPipeline(device, tearline_pipeline, nullptr);
	vkDestroyPipeline(device, forward_pbr_pipeline, nullptr);
	vkDestroyCommandPool(device, command_pool, nullptr);
	for (VkImageView view : views) vkDestroyImageView(device, view, nullptr);
	vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyFence(device, frame_fence, nullptr);
	vkDestroySemaphore(device, acquire_semaphore, nullptr);
	vkDestroySemaphore(device, release_semaphore, nullptr);
	vmaDestroyAllocator(allocator);
	vkDestroyDevice(device, nullptr);
#if _DEBUG
	vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
#endif
	vkDestroyInstance(instance, nullptr);

    return 0;
}