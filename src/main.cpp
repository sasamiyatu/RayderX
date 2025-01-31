#if _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_vulkan.h"
#include "Volk/volk.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#ifdef _WIN32
#include "windows.h"
#include <atlbase.h>
#else _WIN32
#include <dxc/WinAdapter.h>
#endif

#include <dxc/dxcapi.h>

#include <vector>
#include <stdio.h>
#include <filesystem>

#include "scene.h"
#include "resources.h"

static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

#define VK_CHECK(x)                                         \
	do { 					                                \
		VkResult err = x;                                   \
		if (err != VK_SUCCESS) {	                                        \
			                                                \
				printf("Detected Vulkan error: %d\n", err); \
				abort();                                    \
		}                                                   \
	} while(0)

#define FAIL_ON_ERROR(x)                                    \
	do {                                                    \
		bool success = x;                                   \
		if (!success) {                                     \
			abort();                                        \
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
		.apiVersion = VK_API_VERSION_1_4
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

	VkPhysicalDeviceVulkan14Features features14{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = &features13,
		.maintenance5 = VK_TRUE,
	};

	VkDeviceCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features14,
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

void create_swapchain(Swapchain& swapchain, VkDevice device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t width, uint32_t height)
{
	VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
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

std::string read_text_file(const char* filepath)
{
	FILE* f = fopen(filepath, "rb");
	if (!f)
	{
		printf("Failed to open file %s", filepath);
		return std::string();
	}

	fseek(f, 0, SEEK_END);
	long filesize = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* data = (uint8_t*)malloc(filesize);
	assert(data);
	size_t bytes_read = fread(data, 1, filesize, f);
	assert(bytes_read == filesize);

	return std::string((const char*)data, (size_t)filesize);
}

struct ShaderCompiler
{
	CComPtr<IDxcUtils> dxc_utils;
	CComPtr<IDxcCompiler3> compiler;
	CComPtr<IDxcIncludeHandler> include_handler;
};

struct Shader
{
	std::vector<uint8_t> spirv;
	VkShaderStageFlagBits stage;
	std::string entry_point;
};

static const wchar_t* get_shader_type_str(VkShaderStageFlagBits shader_stage)
{
	switch (shader_stage)
	{
	case VK_SHADER_STAGE_VERTEX_BIT:
		return L"vs_6_6";
	case VK_SHADER_STAGE_FRAGMENT_BIT:
		return L"ps_6_6";
	case VK_SHADER_STAGE_COMPUTE_BIT:
		return L"cs_6_6";
	default:
		assert(false);
		return nullptr;
	}
}

bool load_shader(Shader& shader, const ShaderCompiler& compiler, VkDevice device, const char* filepath, const char* entry_point, VkShaderStageFlagBits shader_stage)
{
	auto cwd = std::filesystem::current_path(); // Current working directory
	std::filesystem::current_path(cwd / std::filesystem::path(std::string("shaders")));

	std::string shader_src = read_text_file(filepath);
	if (shader_src.empty()) return false;

	DxcBuffer src{};
	src.Ptr = shader_src.data();
	src.Size = shader_src.length();
	src.Encoding = DXC_CP_ACP;

	std::wstring ep_str(entry_point, entry_point + strlen(entry_point));
	LPCWSTR args[] = {
		L"-E", ep_str.data(),
		L"-T", get_shader_type_str(shader_stage),
		L"-Zs", L"-spirv",
		L"-fvk-use-scalar-layout",
		L"-fspv-target-env=vulkan1.3",
		L"-HV 2021",
#if OPTIMIZE_SHADERS
		L"-O3"
#else
		L"-O0"
#endif
	};

	CComPtr<IDxcResult> results;
	compiler.compiler->Compile(&src, args, _countof(args), compiler.include_handler, IID_PPV_ARGS(&results));

	CComPtr<IDxcBlobUtf8> errors = nullptr;
	results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
	// Note that d3dcompiler would return null if no errors or warnings are present.
	// IDxcCompiler3::Compile will always return an error buffer, but its length
	// will be zero if there are no warnings or errors.
	if (errors != nullptr && errors->GetStringLength() != 0)
		printf("Shader compilation warnings/errors: %s\n", errors->GetStringPointer());

	HRESULT hrStatus;
	results->GetStatus(&hrStatus);
	if (FAILED(hrStatus))
	{
		printf("Shader Compilation Failed\n");
		std::filesystem::current_path(cwd);
		return false;
	}

	CComPtr<IDxcBlob> shd = nullptr;
	CComPtr<IDxcBlobUtf16> shader_name = nullptr;
	results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shd), &shader_name);

	shader.spirv.resize(shd->GetBufferSize());
	memcpy(shader.spirv.data(), shd->GetBufferPointer(), shd->GetBufferSize());
	shader.stage = shader_stage;
	shader.entry_point = entry_point;

	std::filesystem::current_path(cwd);

	return true;
}

VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings, VkDescriptorSetLayoutCreateFlags flags = 0)
{
	VkDescriptorSetLayoutCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = flags,
		.bindingCount = (uint32_t)bindings.size(),
		.pBindings = bindings.begin(),
	};

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &create_info, nullptr, &layout));
	return layout;
}

VkPipelineLayout create_pipeline_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> set_layouts = {}, uint32_t push_constants_size = 0, VkShaderStageFlags push_constant_stages = 0)
{
	VkPushConstantRange range{
		.stageFlags = push_constant_stages,
		.offset = 0,
		.size = push_constants_size
	};

	VkPipelineLayoutCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = (uint32_t)set_layouts.size(),
		.pSetLayouts = set_layouts.begin(),
		.pushConstantRangeCount = push_constants_size != 0 ? 1u : 0u,
		.pPushConstantRanges = push_constants_size != 0 ? &range : nullptr 
	};
	
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreatePipelineLayout(device, &create_info, nullptr, &layout));

	return layout;
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
		.cullMode = VK_CULL_MODE_NONE,
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

bool create_shader_compiler(ShaderCompiler& compiler)
{
	CComPtr<IDxcUtils> dxc_utils;
	CComPtr<IDxcCompiler3> comp;
	CComPtr<IDxcIncludeHandler> include_handler;

	if (DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils)) != 0)
		return false;
	if (DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&comp)) != 0)
		return false;
	if (dxc_utils->CreateDefaultIncludeHandler(&include_handler) != 0)
		return false;

	compiler.dxc_utils = dxc_utils;
	compiler.compiler = comp;
	compiler.include_handler = include_handler;

	return true;
}


int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("Usage: %s <scene file>\n", argv[0]);
		return 1;
	}

	std::vector<Mesh> meshes;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<MeshDraw> mesh_draws;
	if (!load_scene(argv[1], meshes, vertices, indices, mesh_draws))
	{
		printf("Failed to load scene!\n");
		return 1;
	}

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

	ShaderCompiler compiler{};
	if (!create_shader_compiler(compiler))
	{
		printf("Failed to create shader compiler!\n");
		return EXIT_FAILURE;
	}

	VmaAllocator allocator = create_allocator(instance, physical_device, device);

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

	Buffer index_buffer = create_buffer(allocator, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, indices.data());
	Buffer vertex_buffer = create_buffer(allocator, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, vertices.data());

	Shader vertex_shader{};
	Shader fragment_shader{};

	FAIL_ON_ERROR(load_shader(vertex_shader, compiler, device, "forward.hlsl", "vs_main", VK_SHADER_STAGE_VERTEX_BIT));
	FAIL_ON_ERROR(load_shader(fragment_shader, compiler, device, "forward.hlsl", "fs_main", VK_SHADER_STAGE_FRAGMENT_BIT));

	VkDescriptorSetLayout descriptor_set_layout = create_descriptor_set_layout(device, {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		}},
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT);
	VkPipelineLayout pipeline_layout = create_pipeline_layout(device, {descriptor_set_layout}, sizeof(glm::mat4), VK_SHADER_STAGE_VERTEX_BIT);
	VkPipeline pipeline = create_pipeline(device, { vertex_shader, fragment_shader }, pipeline_layout, {swapchain.format}, DEPTH_FORMAT);

	Texture depth_texture = create_texture(device, allocator, swapchain.width, swapchain.height, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 proj = glm::perspectiveFovRH_ZO(glm::radians(75.0f), (float)swapchain.width, (float)swapchain.height, 0.1f, 1000.0f);
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

		VkRenderingAttachmentInfo attachment_info{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = views[image_index],
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		};

		VkRenderingAttachmentInfo depth_info{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depth_texture.view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = {.depthStencil = {.depth = 1.0f} }
		};

		VkRenderingInfo rendering_info{
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

		VkDescriptorBufferInfo buffer_info{
			.buffer = vertex_buffer.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE
		};

		VkWriteDescriptorSet write{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = 0,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &buffer_info,
		};
		vkCmdPushDescriptorSet(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &write);

		for (const auto& d : mesh_draws)
		{
			glm::mat4 mvp = viewproj * d.transform;
			vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

			const Mesh& mesh = meshes[d.mesh_index];
			vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, mesh.first_index, mesh.first_vertex, 0);
		}

		vkCmdEndRendering(command_buffer);

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

	vertex_buffer.destroy();
	index_buffer.destroy();
	depth_texture.destroy();
	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyCommandPool(device, command_pool, nullptr);
	for (VkImageView view : views) vkDestroyImageView(device, view, nullptr);
	vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyFence(device, frame_fence, nullptr);
	vkDestroySemaphore(device, acquire_semaphore, nullptr);
	vkDestroySemaphore(device, release_semaphore, nullptr);
	vmaDestroyAllocator(allocator);
	vkDestroyDevice(device, nullptr);
	vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
	vkDestroyInstance(instance, nullptr);

    return 0;
}