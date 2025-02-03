#include "shaders.h"

#include "spirv_reflect.h"

#include <filesystem>

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

	SpvReflectShaderModule mod;
	SpvReflectResult result = spvReflectCreateShaderModule(shader.spirv.size(), shader.spirv.data(), &mod);
	if (result != SPV_REFLECT_RESULT_SUCCESS)
	{
		printf("Failed to reflect shader module\n");
		return false;
	}

	// Only gather resources from set 0
	if (mod.descriptor_set_count > 0)
	{
		const SpvReflectDescriptorSet& set = mod.descriptor_sets[0];
		for (uint32_t i = 0; i < set.binding_count; ++i)
		{
			const SpvReflectDescriptorBinding* binding = set.bindings[i];
			shader.descriptor_types[binding->binding] = (VkDescriptorType)binding->descriptor_type;
			shader.resource_mask |= 1 << binding->binding;
			shader.descriptor_counts[binding->binding] = binding->count;
		}
	}

	auto entry_point_iter = std::find_if(mod.entry_points, mod.entry_points + mod.entry_point_count, [&](const SpvReflectEntryPoint& eps)
		{
			return eps.id == mod.entry_point_id;
		});

	assert(entry_point_iter != mod.entry_points + mod.entry_point_count);
	shader.local_size = glm::uvec3(entry_point_iter->local_size.x, entry_point_iter->local_size.y, entry_point_iter->local_size.z);

	if (mod.push_constant_block_count > 0)
	{
		const SpvReflectBlockVariable& block = mod.push_constant_blocks[0];
		shader.push_constants_size = block.size;
	}

	spvReflectDestroyShaderModule(&mod);

	return true;
}


VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayoutCreateFlags flags)
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

VkPipelineLayout create_pipeline_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> set_layouts, std::initializer_list<Shader> shaders)
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


VkDescriptorUpdateTemplate create_descriptor_update_template(VkDevice device, VkDescriptorSetLayout layout, VkPipelineLayout pipeline_layout, std::initializer_list<Shader> shaders, bool uses_push_descriptors)
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

	VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
	if (shaders.size() == 1 && shaders.begin()->stage == VK_SHADER_STAGE_COMPUTE_BIT)
		bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;

	VkDescriptorUpdateTemplateCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
		.descriptorUpdateEntryCount = (uint32_t)entries.size(),
		.pDescriptorUpdateEntries = entries.data(),
		.templateType = uses_push_descriptors ? VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS : VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
		.descriptorSetLayout = layout,
		.pipelineBindPoint = bind_point,
		.pipelineLayout = pipeline_layout,
	};

	if (entries.size() == 0) return VK_NULL_HANDLE;

	VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorUpdateTemplate(device, &create_info, nullptr, &update_template));
	return update_template;
}

Program create_program(VkDevice device, std::initializer_list<Shader> shaders, bool use_push_descriptors)
{
	VkDescriptorSetLayout descriptor_set_layout = create_descriptor_set_layout(device, get_descriptor_set_layout_binding(shaders),
		use_push_descriptors ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT : 0);
	VkPipelineLayout pipeline_layout = create_pipeline_layout(device, { descriptor_set_layout }, shaders);
	VkDescriptorUpdateTemplate update_template = create_descriptor_update_template(device, descriptor_set_layout, pipeline_layout, shaders, use_push_descriptors);
	return {
		.shaders = shaders,
		.descriptor_set_layout = descriptor_set_layout,
		.pipeline_layout = pipeline_layout,
		.descriptor_update_template	= update_template
	};
}

void destroy_program(VkDevice device, Program& program)
{
	vkDestroyDescriptorSetLayout(device, program.descriptor_set_layout, nullptr);
	vkDestroyPipelineLayout(device, program.pipeline_layout, nullptr);
	vkDestroyDescriptorUpdateTemplate(device, program.descriptor_update_template, nullptr);	
}
