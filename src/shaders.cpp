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