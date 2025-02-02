#pragma once

#ifdef _WIN32
#include "windows.h"
#include <atlbase.h>
#else _WIN32
#include <dxc/WinAdapter.h>
#endif

#include <dxc/dxcapi.h>

#include "common.h"

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

	VkDescriptorType descriptor_types[32];
	uint32_t descriptor_counts[32];
	uint32_t resource_mask;
	size_t push_constants_size;
};


struct DescriptorInfo
{
	union
	{
		VkDescriptorImageInfo image_info;
		VkDescriptorBufferInfo buffer_info;
		VkAccelerationStructureKHR acceleration_structure;
	};

	inline DescriptorInfo() {}

	inline DescriptorInfo(VkSampler sampler)
	{
		image_info = {};
		image_info.sampler = sampler;
	}

	inline DescriptorInfo(VkBuffer buffer, size_t offset = 0, size_t range = VK_WHOLE_SIZE)
	{
		buffer_info = {};
		buffer_info.buffer = buffer;
		buffer_info.offset = offset;
		buffer_info.range = range;
	}

	inline DescriptorInfo(VkImageView image_view, VkImageLayout layout)
	{
		image_info = {};
		image_info.imageView = image_view;
		image_info.imageLayout = layout;
	}

	inline DescriptorInfo(VkSampler sampler, VkImageView image_view, VkImageLayout layout)
	{
		image_info = {};
		image_info.sampler = sampler;
		image_info.imageView = image_view;
		image_info.imageLayout = layout;
	}

	inline DescriptorInfo(VkAccelerationStructureKHR as)
	{
		acceleration_structure = as;
	}
};

bool create_shader_compiler(ShaderCompiler& compiler);
bool load_shader(Shader& shader, const ShaderCompiler& compiler, VkDevice device, const char* filepath, const char* entry_point, VkShaderStageFlagBits shader_stage);
