#include "resources.h"
#include "dds.h"
#include <filesystem>

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
		.imageType = VK_IMAGE_TYPE_2D,
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

	VkImageViewType view_type = is_cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
	VkImageView view = create_image_view(device, image, view_type, format);

	return {
		.image = image,
		.view = view,
		.allocation = allocation,
		.allocator = allocator,
		.device = device,
		.width = width,
		.height = height
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

	if (has_dx10)
	{
		printf("DX10 extension not supported\n");
		return false;
	}

	bool is_cubemap = header->dwCaps & DDSCAPS_COMPLEX && header->dwCaps2 & DDSCAPS2_CUBEMAP;
	if (is_cubemap)
	{
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEX);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEX);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEY);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEY);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEZ);
		assert(header->dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEZ);
	}

	size_t file_image_size = data.size() - sizeof(uint32_t) - sizeof(DDS_HEADER);

	VkFormat format = get_format(header, is_srgb);
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

	size_t image_size = is_compressed
		? get_image_size_bc(header->dwWidth, header->dwHeight, mip_levels, (uint32_t)block_size)
		: get_image_size(header->dwWidth, header->dwHeight, mip_levels, rgb_bit_count);

	if (is_cubemap) image_size *= 6;
	assert(image_size == file_image_size);

	size_t required_size = !is_compressed && rgb_bit_count == 24
		? get_image_size(header->dwWidth, header->dwHeight, mip_levels, 32)
		: image_size;

	assert(required_size <= scratch.size);

	uint32_t array_layers = is_cubemap ? 6 : 1;

	texture = create_texture(device, allocator, header->dwWidth, header->dwHeight, 1, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mip_levels, VK_SAMPLE_COUNT_1_BIT, array_layers, is_cubemap);

	void* mapped = scratch.map();
	if (!is_compressed && rgb_bit_count == 24)
	{
		uint32_t src_data_start = sizeof(uint32_t) + sizeof(DDS_HEADER);
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
		memcpy(mapped, header + 1, image_size);
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
			.imageExtent = {width, height, 1}
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
