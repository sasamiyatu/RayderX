#include "resources.h"
#include "dds.h"
#include <filesystem>
#include <optional>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

VkMemoryBarrier2 memory_barrier(VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask, VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask)
{
	VkMemoryBarrier2 barrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = src_stage_mask,
		.srcAccessMask = src_access_mask,
		.dstStageMask = dst_stage_mask,
		.dstAccessMask = dst_access_mask
	};

	return barrier;
}

VkImageMemoryBarrier2 image_barrier(VkImage image, VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask, VkImageLayout old_layout, VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask, VkImageLayout new_layout, VkImageAspectFlags aspect)
{
	VkImageMemoryBarrier2 barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext = nullptr,
		.srcStageMask = src_stage_mask,
		.srcAccessMask = src_access_mask,
		.dstStageMask = dst_stage_mask,
		.dstAccessMask = dst_access_mask,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.image = image,
		.subresourceRange{
			.aspectMask = aspect,
			.baseMipLevel = 0,
			.levelCount = VK_REMAINING_MIP_LEVELS,
			.baseArrayLayer = 0,
			.layerCount = VK_REMAINING_ARRAY_LAYERS
		}
	};
	return barrier;
}

VkImageMemoryBarrier2 image_barrier(VkImage image, VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask, VkImageLayout old_layout, VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask, VkImageLayout new_layout, VkImageSubresourceRange range)
{
	VkImageMemoryBarrier2 barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext = nullptr,
		.srcStageMask = src_stage_mask,
		.srcAccessMask = src_access_mask,
		.dstStageMask = dst_stage_mask,
		.dstAccessMask = dst_access_mask,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.image = image,
		.subresourceRange = range,
	};
	return barrier;
}

void pipeline_barrier(VkCommandBuffer command_buffer, std::initializer_list<VkMemoryBarrier2> memory_barriers, std::initializer_list<VkImageMemoryBarrier2> image_barriers)
{
	VkDependencyInfo info{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = (uint32_t)memory_barriers.size(),
		.pMemoryBarriers = memory_barriers.begin(),
		.bufferMemoryBarrierCount = 0,
		.pBufferMemoryBarriers = nullptr,
		.imageMemoryBarrierCount = (uint32_t)image_barriers.size(),
		.pImageMemoryBarriers = image_barriers.begin()
	};

	vkCmdPipelineBarrier2(command_buffer, &info);
}

void pipeline_barrier(VkCommandBuffer command_buffer, uint32_t memory_barrier_count, const VkMemoryBarrier2* memory_barriers, uint32_t image_barrier_count, const VkImageMemoryBarrier2* image_barriers)
{
	VkDependencyInfo info{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = memory_barrier_count,
		.pMemoryBarriers = memory_barriers,
		.bufferMemoryBarrierCount = 0,
		.pBufferMemoryBarriers = nullptr,
		.imageMemoryBarrierCount = image_barrier_count,
		.pImageMemoryBarriers = image_barriers
	};

	vkCmdPipelineBarrier2(command_buffer, &info);
}

Buffer create_buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocation_flags, void* initial_data)
{
	VkBufferCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
	};

	VmaAllocationCreateInfo allocate_info{
		.flags = allocation_flags,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VK_CHECK(vmaCreateBuffer(allocator, &create_info, &allocate_info, &buffer, &allocation, nullptr));

	Buffer b = {
		.allocator = allocator,
		.buffer = buffer,
		.allocation = allocation,
		.size = size,
	};

	if (initial_data)
	{
		void* data = b.map();
		memcpy(data, initial_data, size);
		b.unmap();
	}

	return b;
}

static VkImageAspectFlags determine_aspect_flags(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D16_UNORM:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

VkImageView create_image_view(VkDevice device, VkImage image, VkImageViewType type, VkFormat format)
{
	VkImageViewCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = type,
		.format = format,
		.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
			.aspectMask = determine_aspect_flags(format),
			.baseMipLevel = 0,
			.levelCount = VK_REMAINING_MIP_LEVELS,
			.baseArrayLayer = 0,
			.layerCount = VK_REMAINING_ARRAY_LAYERS,
		}
	};

	VkImageView view = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImageView(device, &create_info, nullptr, &view));
	return view;
}

Texture create_texture(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkImageUsageFlags usage, uint32_t mip_levels, VkSampleCountFlagBits sample_count, uint32_t array_layers, bool is_cubemap)
{
	VkImageCreateFlags flags = is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	VkImageCreateInfo image_create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = flags,
		.imageType = depth != 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, depth },
		.mipLevels = mip_levels,
		.arrayLayers = array_layers,
		.samples = sample_count,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
	};

	VmaAllocationCreateInfo allocation_info{
		.usage = VMA_MEMORY_USAGE_AUTO
	};

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VK_CHECK(vmaCreateImage(allocator, &image_create_info, &allocation_info, &image, &allocation, nullptr));

	VkImageViewType view_type = is_cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : (depth != 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D);
	VkImageView view = create_image_view(device, image, view_type, format);

	return {
		.image = image,
		.view = view,
		.allocation = allocation,
		.allocator = allocator,
		.device = device,
		.width = width,
		.height = height,
		.format = format,
		.mip_levels = mip_levels,
		.array_layers = array_layers,
	};
}

static constexpr uint32_t fourcc(const char str[5])
{
	return (str[0] << 0) | (str[1] << 8) | (str[2] << 16) | (str[3] << 24);
}

static_assert(fourcc("DDS ") == DDS_MAGIC);

static VkFormat get_format(const DDS_HEADER* header, bool srgb)
{
	if (header->ddspf.dwFlags & DDPF_FOURCC)
	{
		switch (header->ddspf.dwFourCC)
		{
		case fourcc("DXT1"):
			return srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case fourcc("DXT3"):
			return srgb ? VK_FORMAT_BC2_SRGB_BLOCK : VK_FORMAT_BC2_UNORM_BLOCK;
		case fourcc("DXT5"):
			return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
		case fourcc("ATI2"):
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case 113:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case 116:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		default:
		{
			char tmp[5] = {};
			memcpy(tmp, &header->ddspf.dwFourCC, 4);
			printf("Unknown format with fourcc %s\n", tmp);
			return VK_FORMAT_UNDEFINED;
		}
		}
	}
	else if (header->ddspf.dwFlags & DDPF_RGB)
	{
		// 3 channels textures aren't supported on most desktop hardware
		if (header->ddspf.dwRGBBitCount == 32 || header->ddspf.dwRGBBitCount == 24)
		{
			if (header->ddspf.dwRBitMask == 0x00ff0000 && header->ddspf.dwGBitMask == 0x0000ff00 && header->ddspf.dwBBitMask == 0x000000ff)
				return srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
			else if (header->ddspf.dwRBitMask == 0x000000ff && header->ddspf.dwGBitMask == 0x0000ff00 && header->ddspf.dwBBitMask == 0x00ff0000)
				return srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
			else
				return VK_FORMAT_UNDEFINED;
		}
		else if (header->ddspf.dwRGBBitCount == 8)
		{
			assert(header->ddspf.dwRBitMask == 0xFF);
			return srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
		}
		else
		{
			return VK_FORMAT_UNDEFINED;
		}
	}
	else if (header->ddspf.dwFlags & DDPF_LUMINANCE)
	{
		assert(header->ddspf.dwRBitMask == 0xFF);
		assert(header->ddspf.dwRGBBitCount == 8);
		return srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
	}
	else
	{
		return VK_FORMAT_UNDEFINED;
	}

	return VK_FORMAT_UNDEFINED;
}

VkFormat get_format(DXGI_FORMAT format)
{
	switch (format)
	{
		case DXGI_FORMAT_UNKNOWN:
			return VK_FORMAT_UNDEFINED;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		case DXGI_FORMAT_R32G32B32A32_UINT:
			VK_FORMAT_R32G32B32A32_UINT;
		case DXGI_FORMAT_R32G32B32A32_SINT:
			VK_FORMAT_R32G32B32A32_SINT;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			assert(false);
			return VK_FORMAT_UNDEFINED;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			return VK_FORMAT_R16G16B16A16_UNORM;
		case DXGI_FORMAT_R16G16B16A16_UINT:
			return VK_FORMAT_R16G16B16A16_UINT;
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			return VK_FORMAT_R16G16B16A16_SNORM;
		case DXGI_FORMAT_R16G16B16A16_SINT:
			return VK_FORMAT_R16G16B16A16_SINT;
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
			return VK_FORMAT_R32G32_SFLOAT;
		case DXGI_FORMAT_R32G32_UINT:
			return VK_FORMAT_R32G32_UINT;
		case DXGI_FORMAT_R32G32_SINT:
			return VK_FORMAT_R32G32_SINT;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
			assert(false);
			return VK_FORMAT_UNDEFINED;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case DXGI_FORMAT_R8G8B8A8_UINT:
			return VK_FORMAT_R8G8B8A8_UINT;
		case DXGI_FORMAT_R8G8B8A8_SNORM:
			return VK_FORMAT_R8G8B8A8_SNORM;
		case DXGI_FORMAT_R8G8B8A8_SINT:
			return VK_FORMAT_R8G8B8A8_SINT;
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
			return VK_FORMAT_R16G16_SFLOAT;
		case DXGI_FORMAT_R16G16_UNORM:
			return VK_FORMAT_R16G16_UNORM;
		case DXGI_FORMAT_R16G16_UINT:
			return VK_FORMAT_R16G16_UINT;
		case DXGI_FORMAT_R16G16_SNORM:
			return VK_FORMAT_R16G16_SNORM;
		case DXGI_FORMAT_R16G16_SINT:
			return VK_FORMAT_R16G16_SINT;
		case DXGI_FORMAT_R32_TYPELESS:
			return VK_FORMAT_R32_SFLOAT;
		case DXGI_FORMAT_D32_FLOAT:
			return VK_FORMAT_D32_SFLOAT;
		case DXGI_FORMAT_R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case DXGI_FORMAT_R32_UINT:
			return VK_FORMAT_R32_UINT;
		case DXGI_FORMAT_R32_SINT:
			return VK_FORMAT_R32_SINT;
		case DXGI_FORMAT_R24G8_TYPELESS:
			assert(false);
			return VK_FORMAT_UNDEFINED;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			assert(false);
			return VK_FORMAT_UNDEFINED;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
			return VK_FORMAT_R8G8_UNORM;
		case DXGI_FORMAT_R8G8_UINT:
			return VK_FORMAT_R8G8_UINT;
		case DXGI_FORMAT_R8G8_SNORM:
			return VK_FORMAT_R8G8_SNORM;
		case DXGI_FORMAT_R8G8_SINT:
			return VK_FORMAT_R8G8_SINT;
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
			return VK_FORMAT_R16_SFLOAT;
		case DXGI_FORMAT_D16_UNORM:
			return VK_FORMAT_D16_UNORM;
		case DXGI_FORMAT_R16_UNORM:
			return VK_FORMAT_R16_UNORM;
		case DXGI_FORMAT_R16_UINT:
			return VK_FORMAT_R16_UINT;
		case DXGI_FORMAT_R16_SNORM:
			return VK_FORMAT_R16_SNORM;
		case DXGI_FORMAT_R16_SINT:
			return VK_FORMAT_R16_SINT;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
			return VK_FORMAT_R8_UNORM;
		case DXGI_FORMAT_R8_UINT:
			return VK_FORMAT_R8_UINT;
		case DXGI_FORMAT_R8_SNORM:
			return VK_FORMAT_R8_SNORM;
		case DXGI_FORMAT_R8_SINT:
			return VK_FORMAT_R8_SINT;
		case DXGI_FORMAT_A8_UNORM:
			return VK_FORMAT_A8_UNORM;
		case DXGI_FORMAT_R1_UNORM:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
			assert(false);
			return VK_FORMAT_UNDEFINED;
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case DXGI_FORMAT_BC1_UNORM_SRGB:
			return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
			return VK_FORMAT_BC2_UNORM_BLOCK;
		case DXGI_FORMAT_BC2_UNORM_SRGB:
			return VK_FORMAT_BC2_SRGB_BLOCK;
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
			return VK_FORMAT_BC3_UNORM_BLOCK;
		case DXGI_FORMAT_BC3_UNORM_SRGB:
			return VK_FORMAT_BC3_SRGB_BLOCK;
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
			return VK_FORMAT_BC4_UNORM_BLOCK;
		case DXGI_FORMAT_BC4_SNORM:
			return VK_FORMAT_BC4_SNORM_BLOCK;
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case DXGI_FORMAT_BC5_SNORM:
			return VK_FORMAT_BC5_SNORM_BLOCK;
		case DXGI_FORMAT_B5G6R5_UNORM:
			return VK_FORMAT_B5G6R5_UNORM_PACK16;
		case DXGI_FORMAT_B5G5R5A1_UNORM:
			return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
			assert(false);
			return VK_FORMAT_UNDEFINED;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return VK_FORMAT_B8G8R8A8_SRGB;
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
			return VK_FORMAT_BC6H_UFLOAT_BLOCK;
		case DXGI_FORMAT_BC6H_SF16:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
			return VK_FORMAT_BC7_UNORM_BLOCK;
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return VK_FORMAT_BC7_SRGB_BLOCK;
		case DXGI_FORMAT_AYUV:
		case DXGI_FORMAT_Y410:
		case DXGI_FORMAT_Y416:
		case DXGI_FORMAT_NV12:
		case DXGI_FORMAT_P010:
		case DXGI_FORMAT_P016:
		case DXGI_FORMAT_420_OPAQUE:
		case DXGI_FORMAT_YUY2:
		case DXGI_FORMAT_Y210:
		case DXGI_FORMAT_Y216:
		case DXGI_FORMAT_NV11:
		case DXGI_FORMAT_AI44:
		case DXGI_FORMAT_IA44:
		case DXGI_FORMAT_P8:
		case DXGI_FORMAT_A8P8:
		case DXGI_FORMAT_B4G4R4A4_UNORM:
		case DXGI_FORMAT_P208:
		case DXGI_FORMAT_V208:
		case DXGI_FORMAT_V408:
		case DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE:
		case DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE:
		case DXGI_FORMAT_FORCE_UINT:
		default:
			assert(false);
			return VK_FORMAT_UNDEFINED;
	}
}

static size_t get_image_size_bc(uint32_t width, uint32_t height, uint32_t levels, uint32_t block_size)
{
	size_t result = 0;
	for (uint32_t i = 0; i < levels; ++i)
	{
		result += std::max(1u, (width + 3) / 4 ) * std::max(1u, (height + 3) / 4) * block_size;
		width = width > 1 ? (width >> 1) : 1;
		height = height > 1 ? (height >> 1) : 1;
	}

	return result;
}

static size_t get_image_size(uint32_t width, uint32_t height, uint32_t levels, uint32_t bits_per_pixel)
{
	assert(bits_per_pixel % 8 == 0);
	size_t result = 0;
	for (uint32_t i = 0; i < levels; ++i)
	{
		result += width * height * (bits_per_pixel / 8);
		width = width > 1 ? (width >> 1) : 1;
		height = height > 1 ? (height >> 1) : 1;
	}

	return result;
}

static size_t get_block_size(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		return 8;
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC2_SRGB_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
		return 16;
	default:
		return 0;
	}
}

static bool is_compressed_format(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC2_SRGB_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
		return true;
	default:
		return false;
	}
}

static uint32_t get_format_bitcount(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return 128;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return 64;
	default:
		return 0;
	}
}

bool load_texture(Texture& texture, const char* path, VkDevice device, VmaAllocator allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer, VkQueue queue, const Buffer& scratch, bool is_srgb)
{
	std::filesystem::path p = path;
	if (!p.has_extension() || p.extension() != ".dds")
	{
		printf("Unsupported file format: '%s'\n", path);
		return false;
	}

	std::vector<uint8_t> data;
	if (!read_binary_file(path, data))
	{
		printf("Failed to open file '%s'\n", path);
		return false;
	}

	if (*(uint32_t*)data.data() != DDS_MAGIC)
	{
		printf("Invalid DDS file '%s': bad magic!\n", path);
		return false;
	}

	assert(*(uint32_t*)data.data() == fourcc("DDS "));

	DDS_HEADER* header = (DDS_HEADER*)(data.data() + sizeof(uint32_t));

	bool has_dx10 = header->ddspf.dwFourCC == fourcc("DX10");

	DDS_HEADER_DXT10* header_dx10 = has_dx10 ?  (DDS_HEADER_DXT10*)(header + 1) : nullptr;

	if (has_dx10)
	{
		printf("DX10 extension not supported\n");
		//return false;
	}

	bool complex = header->dwCaps & DDSCAPS_COMPLEX;
	bool is_cubemap = has_dx10 ? (header_dx10->miscFlag & DDS_RESOURCE_MISC_TEXTURECUBE): (header->dwCaps& DDSCAPS_COMPLEX) && (header->dwCaps2 & DDSCAPS2_CUBEMAP);
	if (is_cubemap)
	{
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEX);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEX);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEY);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEY);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEZ);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEZ);
	}

	bool is_volume = header->dwCaps & DDSCAPS_COMPLEX && header->dwCaps2 & DDSCAPS2_VOLUME;

	size_t file_image_size = data.size() - sizeof(uint32_t) - sizeof(DDS_HEADER) - (has_dx10 ? sizeof(DDS_HEADER_DXT10) : 0);

	VkFormat format = has_dx10 ? get_format(header_dx10->dxgiFormat) : get_format(header, is_srgb);
	if (format == VK_FORMAT_UNDEFINED)
	{
		printf("Unsupported format\n");
		return false;
	}

	bool is_compressed = is_compressed_format(format);

	size_t block_size = get_block_size(format);
	assert(!is_compressed || block_size != 0);

	uint32_t mip_levels = header->dwMipMapCount == 0 ? 1 : header->dwMipMapCount;

	uint32_t rgb_bit_count = header->ddspf.dwRGBBitCount != 0 ? header->ddspf.dwRGBBitCount : get_format_bitcount(format);

	bool has_depth = header->dwDepth != 0;
	uint32_t depth = has_depth ? header->dwDepth : 1;

	size_t image_size = is_compressed
		? get_image_size_bc(header->dwWidth, header->dwHeight, mip_levels, (uint32_t)block_size)
		: get_image_size(header->dwWidth, header->dwHeight, mip_levels, rgb_bit_count) * depth;

	if (is_cubemap) image_size *= 6;
	assert(image_size == file_image_size);

	size_t required_size = !is_compressed && rgb_bit_count == 24
		? get_image_size(header->dwWidth, header->dwHeight, mip_levels, 32)
		: image_size;

	assert(required_size <= scratch.size);

	uint32_t array_layers = is_cubemap ? 6 : 1;

	texture = create_texture(device, allocator, header->dwWidth, header->dwHeight, depth, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mip_levels, VK_SAMPLE_COUNT_1_BIT, array_layers, is_cubemap);

	void* mapped = scratch.map();
	if (!is_compressed && rgb_bit_count == 24)
	{
		uint32_t src_data_start = sizeof(uint32_t) + sizeof(DDS_HEADER) + (has_dx10 ? sizeof(DDS_HEADER_DXT10) : 0);
		uint8_t* write_ptr = (uint8_t*)mapped;
		for (uint32_t i = 0; i < image_size; i += 3)
		{
			*write_ptr++ = data[src_data_start + i + 0];
			*write_ptr++ = data[src_data_start + i + 1];
			*write_ptr++ = data[src_data_start + i + 2];
			*write_ptr++ = 255;
		}
	}
	else
	{
		uint8_t* data_start = (uint8_t*)header + sizeof(DDS_HEADER) + (has_dx10 ? sizeof(DDS_HEADER_DXT10) : 0);
		memcpy(mapped, data_start, image_size);
	}
	scratch.unmap();

	vkResetCommandPool(device, command_pool, 0);

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

	{ // Undefined -> transfer dst optimal
		VkImageMemoryBarrier2 barrier = image_barrier(texture.image,
			0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT);

		pipeline_barrier(command_buffer, {}, { barrier });
	}

	VkDeviceSize offset = 0;
	uint32_t width = header->dwWidth;
	uint32_t height = header->dwHeight;
	for (uint32_t i = 0; i < mip_levels; ++i)
	{
		VkBufferImageCopy copy{
			.bufferOffset = offset,
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i,
				.baseArrayLayer = 0,
				.layerCount = array_layers
			},
			.imageOffset = {0, 0, 0 },
			.imageExtent = {width, height, depth}
		};

		vkCmdCopyBufferToImage(command_buffer, scratch.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		offset += is_compressed 
			?  (std::max(1u, (width + 3) / 4) * std::max(1u, (height + 3) / 4) * block_size)
			: width * height * (rgb_bit_count / 8) * array_layers;

		width = width > 1 ? (width >> 1) : 1;
		height = height > 1 ? (height >> 1) : 1;
	}

	// Transfer dst optimal -> shader read only optimal
	{
		VkImageMemoryBarrier2 barrier = image_barrier(texture.image,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT);
		pipeline_barrier(command_buffer, {}, { barrier });
	}

	VK_CHECK(vkEndCommandBuffer(command_buffer));

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer
	};

	VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));

	return true;
}

static uint32_t get_mip_count(uint32_t texture_width, uint32_t texture_height)
{
	return (uint32_t)(std::floor(std::log2(std::max(texture_width, texture_height)))) + 1;
}

bool load_png_or_jpg_texture(Texture& texture, const uint8_t* data, size_t data_size, VkDevice device, VmaAllocator allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer, VkQueue queue, const Buffer& scratch, bool is_srgb)
{
	int width, height, channels;
	constexpr int required_channels = 4;
	stbi_uc* loaded_data = stbi_load_from_memory(data, (int)data_size, &width, &height, &channels, required_channels);
	if (!loaded_data) return false;

	VkFormat format = is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
	uint32_t mip_levels = get_mip_count(width, height);
	texture = create_texture(device, allocator, width, height, 1, format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mip_levels);

	size_t image_size = width * height * 4;
	assert(image_size <= scratch.size);
	void* mapped = scratch.map();
	memcpy(mapped, loaded_data, image_size);
	scratch.unmap();

	vkResetCommandPool(device, command_pool, 0);

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

	{ // Undefined -> transfer dst optimal
		VkImageMemoryBarrier2 barrier = image_barrier(texture.image,
			0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT);

		pipeline_barrier(command_buffer, {}, { barrier });
	}

	VkBufferImageCopy copy{
		.bufferOffset = 0,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.imageOffset = {0, 0, 0 },
		.imageExtent = {(uint32_t)width, (uint32_t)height, 1u}
	};

	vkCmdCopyBufferToImage(command_buffer, scratch.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	// Transfer dst optimal -> shader read only optimal
	{
		VkImageMemoryBarrier2 barrier = image_barrier(texture.image,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT);
		pipeline_barrier(command_buffer, {}, { barrier });
	}

	VK_CHECK(vkEndCommandBuffer(command_buffer));

	VkSubmitInfo submit_info{
	.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	.commandBufferCount = 1,
	.pCommandBuffers = &command_buffer
	};

	VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));

	stbi_image_free(loaded_data);

	return true;
}

void generate_mipmaps(const std::vector<Texture>& textures, VkDevice device, VmaAllocator allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer, VkQueue queue, const Buffer& scratch)
{
	vkResetCommandPool(device, command_pool, 0);

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

	for (const auto& t : textures)
	{
		VkImageMemoryBarrier2 barrier = image_barrier(t.image,
			0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		pipeline_barrier(command_buffer, {}, { barrier });

		int32_t width = t.width;
		int32_t height = t.height;
		for (uint32_t i = 1; i < t.mip_levels; ++i)
		{
			VkImageMemoryBarrier2 prebarrier = image_barrier(t.image,
				0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = i,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			);

			pipeline_barrier(command_buffer, {}, { prebarrier });

			int32_t mip_width = std::max(1, width >> 1);
			int32_t mip_height = std::max(1, height >> 1);

			VkImageBlit region{
				.srcSubresource{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = i - 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.srcOffsets = {{0, 0, 0}, {width, height, 1}},
				.dstSubresource{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = i,
					.baseArrayLayer = 0,
					.layerCount = 1
				},
				.dstOffsets = {{0, 0, 0}, {mip_width, mip_height, 1}},
			};
			vkCmdBlitImage(command_buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);

			VkImageMemoryBarrier2 postbarrier = image_barrier(t.image,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = i,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			);

			pipeline_barrier(command_buffer, {}, { postbarrier });

			width = mip_width;
			height = mip_height;
		}

		VkImageMemoryBarrier2 final_barrier = image_barrier(t.image,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		pipeline_barrier(command_buffer, {}, { final_barrier });
	}

	VK_CHECK(vkEndCommandBuffer(command_buffer));

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer
	};

	VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));
}
