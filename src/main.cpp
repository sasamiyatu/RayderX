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

static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
static constexpr uint32_t SHADOWMAP_SIZE = 2048;

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
		// HACK: NVIDIA driver has weird aritfacts on my laptop so forcing usage of integrated GPU
		//if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
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



VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayoutCreateFlags flags = 0)
{
	VkDescriptorSetLayoutCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = flags,
		.bindingCount = (uint32_t)bindings.size(),
		.pBindings = bindings.data(),
	};

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &create_info, nullptr, &layout));
	return layout;
}

std::vector<VkDescriptorSetLayoutBinding> get_descriptor_set_layout_binding(std::initializer_list<Shader> shaders)
{
	VkDescriptorType resources[32] = {};
	uint32_t descriptor_counts[32] = {};
	uint32_t resource_mask = 0;
	VkShaderStageFlags stage_flags = 0;

	for (const Shader& shader : shaders)
	{
		stage_flags |= shader.stage;
		for (uint32_t i = 0; i < 32; ++i)
		{
			if (shader.resource_mask & (1 << i))
			{
				resource_mask |= (1 << i);
				descriptor_counts[i] = shader.descriptor_counts[i];
				resources[i] = shader.descriptor_types[i];
			}
		}
	}

	std::vector<VkDescriptorSetLayoutBinding> bindings;

	for (uint32_t i = 0; i < 32; ++i)
	{
		if (resource_mask & (1 << i))
		{
			VkDescriptorSetLayoutBinding binding{
				.binding = i,
				.descriptorType = resources[i],
				.descriptorCount = descriptor_counts[i],
				.stageFlags = stage_flags,
				.pImmutableSamplers = nullptr
			};
			bindings.push_back(binding);
		}
	}

	return bindings;
}

VkPipelineLayout create_pipeline_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> set_layouts = {}, std::initializer_list<Shader> shaders = {})
{
	uint32_t push_constant_size = 0;
	VkShaderStageFlags push_constant_stages = 0;

	for (const auto& s : shaders)
	{
		push_constant_size = std::max(push_constant_size, (uint32_t)s.push_constants_size);
		push_constant_stages |= s.stage;
	}

	VkPushConstantRange range{
		.stageFlags = push_constant_stages,
		.offset = 0,
		.size = push_constant_size
	};

	VkPipelineLayoutCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = (uint32_t)set_layouts.size(),
		.pSetLayouts = set_layouts.begin(),
		.pushConstantRangeCount = push_constant_size != 0 ? 1u : 0u,
		.pPushConstantRanges = push_constant_size != 0 ? &range : nullptr
	};
	
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreatePipelineLayout(device, &create_info, nullptr, &layout));

	return layout;
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

VkPipeline create_pipeline(VkDevice device, std::initializer_list<Shader> shaders, VkPipelineLayout layout, std::initializer_list<VkFormat> color_attachment_formats, VkFormat depth_format = VK_FORMAT_UNDEFINED)
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
		.colorAttachmentCount = (uint32_t)color_attachment_formats.size(),
		.pColorAttachmentFormats = color_attachment_formats.begin(),
		.depthAttachmentFormat = depth_format,
	};

	std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments(color_attachment_formats.size());
	for (size_t i = 0; i < color_attachment_formats.size(); ++i)
	{
		color_blend_attachments[i] = {
			.blendEnable = VK_FALSE,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
		};
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

VkDescriptorUpdateTemplate create_descriptor_update_template(VkDevice device, VkDescriptorSetLayout layout, VkPipelineLayout pipeline_layout, std::initializer_list<Shader> shaders, bool uses_push_descriptors = false)
{
	uint32_t resource_mask = 0;
	VkDescriptorType descriptor_types[32] = {};
	uint32_t descriptor_counts[32] = {};
	for (const auto& shader : shaders)
	{
		for (uint32_t i = 0; i < 32; ++i)
		{
			if (shader.resource_mask & (1 << i))
			{
				resource_mask |= (1 << i);
				descriptor_types[i] = shader.descriptor_types[i];
				descriptor_counts[i] = shader.descriptor_counts[i];
			}
		}
	}

	std::vector<VkDescriptorUpdateTemplateEntry> entries;

	uint32_t offset = 0;
	for (uint32_t i = 0; i < 32; ++i)
	{
		if (resource_mask & (1 << i))
		{
			uint32_t stride = sizeof(DescriptorInfo);

			for (uint32_t j = 0; j < descriptor_counts[i]; ++j)
			{
				VkDescriptorUpdateTemplateEntry entry{
					.dstBinding = i,
					.dstArrayElement = j,
					.descriptorCount = 1,
					.descriptorType = descriptor_types[i],
					.offset = offset,
					.stride = stride,
				};
				entries.push_back(entry);
				offset += stride;
			}
		}
	}

	VkDescriptorUpdateTemplateCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
		.descriptorUpdateEntryCount = (uint32_t)entries.size(),
		.pDescriptorUpdateEntries = entries.data(),
		.templateType = uses_push_descriptors ? VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS :  VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
		.descriptorSetLayout = layout,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.pipelineLayout = pipeline_layout,
	};

	VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorUpdateTemplate(device, &create_info, nullptr, &update_template));
	return update_template;
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

struct OrbitCamera
{
	float distance = 2.0f;
	float fov = glm::radians(45.0f);
	float far_plane = 10.0f;
	glm::vec2 angles = glm::vec2(0.0f, 0.0f);
	glm::vec2 pan_pos = glm::vec2(0.0f, 0.0f);
	glm::mat4 projection = glm::perspectiveRH_ZO(fov, 1280.0f / 720.0f, 1.0f, far_plane);

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

	std::filesystem::path ext = std::filesystem::path(argv[1]).extension();
	if (ext == ".glb" || ext == ".gltf")
	{
		if (!load_scene(argv[1], meshes, vertices, indices, mesh_draws))
		{
			printf("Failed to load scene!\n");
			return 1;
		}
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

	Buffer index_buffer = create_buffer(allocator, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, indices.data());
	Buffer vertex_buffer = create_buffer(allocator, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, vertices.data());

	constexpr float camera_fov = glm::radians(20.0f);
	OrbitCamera main_camera{
		.distance = 3.1f,
		.fov = camera_fov,
		.angles = glm::vec2(-0.49005f, 0.0508272f),
		.pan_pos = glm::vec2(-0.0168704f, 0.0729295f),
		.projection = glm::perspectiveRH_ZO(camera_fov, (float)swapchain.width / (float)swapchain.height, 0.1f, 100.0f)
	};

	// Defaults from Jorge's demo
	Lights lights{};
	glm::mat4 shadowmap_projection = glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
	float shadow_bias = -0.034f;
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
		GPULight gpu_light{
			.position = pos,
			.direction = dir,
			.falloff_start = cosf(0.5f * l.orbit_camera.fov),
			.falloff_width = lights.lights[i].falloff_width,
			.color = l.color,
			.attenuation = l.attenuation,
			.far_plane = l.orbit_camera.far_plane,
			.bias = l.bias,
			.view_projection = glm::perspectiveRH_ZO(l.orbit_camera.fov, 1.0f, 0.1f, l.orbit_camera.far_plane) * view,
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

	Shader vertex_shader{};
	Shader fragment_shader{};

	FAIL_ON_ERROR(load_shader(vertex_shader, compiler, device, "forward.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(fragment_shader, compiler, device, "forward.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));

	VkDescriptorSetLayout descriptor_set_layout = create_descriptor_set_layout(device, get_descriptor_set_layout_binding({ vertex_shader, fragment_shader }), 
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT);
	VkPipelineLayout pipeline_layout = create_pipeline_layout(device, {descriptor_set_layout}, {vertex_shader, fragment_shader});
	VkDescriptorUpdateTemplate update_template = create_descriptor_update_template(device, descriptor_set_layout, pipeline_layout, { vertex_shader, fragment_shader }, true);
	VkPipeline pipeline = create_pipeline(device, { vertex_shader, fragment_shader }, pipeline_layout, {swapchain.format}, DEPTH_FORMAT);

	Shader shadowmap_vertex_shader{};
	FAIL_ON_ERROR(load_shader(shadowmap_vertex_shader, compiler, device, "shadowmap.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	VkDescriptorSetLayout shadowmap_descriptor_set_layout = create_descriptor_set_layout(device, get_descriptor_set_layout_binding({ shadowmap_vertex_shader }),
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT);
	VkPipelineLayout shadowmap_pipeline_layout = create_pipeline_layout(device, { shadowmap_descriptor_set_layout }, { shadowmap_vertex_shader });
	VkDescriptorUpdateTemplate shadowmap_update_template = create_descriptor_update_template(device, shadowmap_descriptor_set_layout, shadowmap_pipeline_layout, 
		{ shadowmap_vertex_shader }, true);
	VkPipeline shadowmap_pipeline = create_shadowmap_pipeline(device, { shadowmap_vertex_shader }, shadowmap_pipeline_layout, DEPTH_FORMAT);

	Texture depth_texture = create_texture(device, allocator, swapchain.width, swapchain.height, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

	VkSampler anisotropic_sampler = VK_NULL_HANDLE;
	VkSampler linear_sampler = VK_NULL_HANDLE;
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

		create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		create_info.anisotropyEnable = VK_FALSE;
		create_info.maxAnisotropy = 1.0f;

		VK_CHECK(vkCreateSampler(device, &create_info, nullptr, &linear_sampler));

		create_info.compareEnable = VK_TRUE;
		create_info.compareOp = VK_COMPARE_OP_LESS;
		create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		VK_CHECK(vkCreateSampler(device, &create_info, nullptr, &shadow_sampler));
	}


	glm::mat4 view = main_camera.compute_view();
	glm::mat4 proj = main_camera.projection;
	glm::mat4 viewproj = proj * view;

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
				.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
				.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
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

		for (const auto& l : lights.lights)
		{
			VkImageMemoryBarrier2 barrier = image_barrier(l.shadowmap.image, 
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_ASPECT_DEPTH_BIT);

			pipeline_barrier(command_buffer, { barrier });

			// Shadow maps
			VkRenderingAttachmentInfo depth_info{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = l.shadowmap.view,
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

			vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, shadowmap_update_template, shadowmap_pipeline_layout, 0, descriptor_info);
			vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			for (const auto& d : mesh_draws)
			{
				struct {
					glm::mat4 mvp;
				} pc;

				/**
 * This is for rendering linear values:
 * Check this: http://www.mvps.org/directx/articles/linear_z/linearz.htm
 */
				/*
				D3DXMATRIX linearProjection = projection;
				float Q = projection._33;
				float N = -projection._43 / projection._33;
				float F = -N * Q / (1 - Q);
				linearProjection._33 /= F;
				linearProjection._43 /= F;
				*/

				glm::mat4 linear_proj = proj;
				float Q = proj[2][2];
				float N = -proj[2][3] / Q;
				float F = -N * Q / (1.0f - Q);
				linear_proj[2][2] /= F;	
				linear_proj[2][3] /= F;
				pc.mvp = l.orbit_camera.projection * l.orbit_camera.compute_view() * d.transform;

				vkCmdPushConstants(command_buffer, shadowmap_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

				const Mesh& mesh = meshes[d.mesh_index];
				vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, mesh.first_index, mesh.first_vertex, 0);
			}

			vkCmdEndRendering(command_buffer);
		}

		VkRenderingAttachmentInfo attachment_info{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = views[image_index],
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = {.color = {0.0f, 0.0f, 0.0f, 1.0f} }
		};

		VkRenderingAttachmentInfo depth_info {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depth_texture.view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = {.depthStencil = {.depth = 1.0f} }
		};

		VkRenderingInfo rendering_info {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = { 0, 0, swapchain.width, swapchain.height },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachment_info,
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
			};

			vkCmdPushDescriptorSetWithTemplateKHR(command_buffer, update_template, pipeline_layout, 0, descriptor_info);

			struct {
				glm::mat4 mvp;
				uint32_t n_lights;
				glm::vec3 camera_pos;
			} pc;

			pc.mvp = viewproj * d.transform;
			pc.n_lights = (uint32_t)lights.lights.size();
			pc.camera_pos = glm::inverse(view)[3];

			vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

			const Mesh& mesh = meshes[d.mesh_index];
			vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, mesh.first_index, mesh.first_vertex, 0);
		}

		vkCmdEndRendering(command_buffer);

		{
			VkImageMemoryBarrier2 barrier = image_barrier(swapchain.images[image_index], 
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

			pipeline_barrier(command_buffer, { barrier }	);
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
	vkDestroySampler(device, shadow_sampler, nullptr);
	beckmann_lut.destroy();
	for (auto& l : lights.lights) l.shadowmap.destroy();
	for (Texture& t : textures) t.destroy();
	scratch_buffer.destroy();
	lights.buffer.destroy();
	vertex_buffer.destroy();
	index_buffer.destroy();
	depth_texture.destroy();
	vkDestroyDescriptorUpdateTemplate(device, update_template, nullptr);
	vkDestroyDescriptorUpdateTemplate(device, shadowmap_update_template, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(device, shadowmap_descriptor_set_layout, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyPipelineLayout(device, shadowmap_pipeline_layout, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipeline(device, shadowmap_pipeline, nullptr);
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