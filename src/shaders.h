#pragma once

#ifdef _WIN32
#include "windows.h"
#include <atlbase.h>
#else _WIN32
#include <dxc/WinAdapter.h>
#endif

#include <dxc/dxcapi.h>

#include "common.h"

#include <glm/glm.hpp>

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

	glm::uvec3 local_size;

	inline glm::uvec3 get_dispatch_size(glm::uvec3 global_size) const { return (global_size + local_size - 1u) / local_size; }
	inline glm::uvec3 get_dispatch_size(uint32_t global_size_x, uint32_t global_size_y, uint32_t global_size_z) const 
	{ 
		return (glm::uvec3(global_size_x, global_size_y, global_size_z) + local_size - 1u) / local_size;
	}
};

struct Program
{
	std::vector<Shader> shaders;

	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkDescriptorUpdateTemplate descriptor_update_template;
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

VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayoutCreateFlags flags = 0);
std::vector<VkDescriptorSetLayoutBinding> get_descriptor_set_layout_binding(std::initializer_list<Shader> shaders);
VkPipelineLayout create_pipeline_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> set_layouts = {}, std::initializer_list<Shader> shaders = {});
VkDescriptorUpdateTemplate create_descriptor_update_template(VkDevice device, VkDescriptorSetLayout layout, VkPipelineLayout pipeline_layout, std::initializer_list<Shader> shaders, bool uses_push_descriptors = false);
bool create_shader_compiler(ShaderCompiler& compiler);
bool load_shader(Shader& shader, const ShaderCompiler& compiler, VkDevice device, const char* filepath, const char* entry_point, VkShaderStageFlagBits shader_stage);
Program create_program(VkDevice device, std::initializer_list<Shader> shaders, bool use_push_descriptors);
void destroy_program(VkDevice device, Program& program);