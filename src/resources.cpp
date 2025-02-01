#include "resources.h"
#include "dds.h"
#include <filesystem>

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

void pipeline_barrier(VkCommandBuffer command_buffer, std::initializer_list<VkImageMemoryBarrier2> image_barriers)
{
	VkDependencyInfo info{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 0,
		.pMemoryBarriers = nullptr,
		.bufferMemoryBarrierCount = 0,
		.pBufferMemoryBarriers = nullptr,
		.imageMemoryBarrierCount = (uint32_t)image_barriers.size(),
		.pImageMemoryBarriers = image_barriers.begin()
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
			.layerCount = 1,
		}
	};

	VkImageView view = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImageView(device, &create_info, nullptr, &view));
	return view;
}

Texture create_texture(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkImageUsageFlags usage, uint32_t mip_levels)
{
	VkImageCreateInfo image_create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, depth },
		.mipLevels = mip_levels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
	};

	VmaAllocationCreateInfo allocation_info{
		.usage = VMA_MEMORY_USAGE_AUTO
	};

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VK_CHECK(vmaCreateImage(allocator, &image_create_info, &allocation_info, &image, &allocation, nullptr));

	VkImageView view = create_image_view(device, image, VK_IMAGE_VIEW_TYPE_2D, format);

	return {
		.image = image,
		.view = view,
		.allocation = allocation,
		.allocator = allocator,
		.device = device
	};
}

static constexpr uint32_t fourcc(const char str[5])
{
	return (str[0] << 0) | (str[1] << 8) | (str[2] << 16) | (str[3] << 24);
}

static_assert(fourcc("DDS ") == DDS_MAGIC);

static VkFormat get_format(const DDS_HEADER* header)
{
	if (header->ddspf.dwFlags & DDPF_FOURCC)
	{
		switch (header->ddspf.dwFourCC)
		{
		case fourcc("DXT1"):
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case fourcc("DXT3"):
			return VK_FORMAT_BC2_UNORM_BLOCK;
		case fourcc("DXT5"):
			return VK_FORMAT_BC3_UNORM_BLOCK;
		case fourcc("ATI2"):
			return VK_FORMAT_BC5_UNORM_BLOCK;
		default:
		{
			char tmp[5] = {};
			memcpy(tmp, &header->ddspf.dwFourCC, 4);
			printf("Unknown format with fourcc %s\n", tmp);
			return VK_FORMAT_UNDEFINED;
		}
		}
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

static size_t get_block_size(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		return 8;
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
		return 16;
	default:
		return 0;
	}
}

bool load_texture(Texture& texture, const char* path, VkDevice device, VmaAllocator allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer, VkQueue queue, const Buffer& scratch)
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

	VkFormat format = get_format(header);
	if (format == VK_FORMAT_UNDEFINED)
	{
		printf("Unsupported format\n");
		return false;
	}

	size_t block_size = get_block_size(format);
	assert(block_size != 0);

	size_t image_size = get_image_size_bc(header->dwWidth, header->dwHeight, header->dwMipMapCount, block_size);
	size_t file_image_size = data.size() - sizeof(uint32_t) - sizeof(DDS_HEADER);

	assert(image_size == file_image_size);
	assert(image_size <= scratch.size);

	texture = create_texture(device, allocator, header->dwWidth, header->dwHeight, 1, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, header->dwMipMapCount);

	void* mapped = scratch.map();
	memcpy(mapped, header + 1, image_size);
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

		pipeline_barrier(command_buffer, { barrier });
	}

	VkDeviceSize offset = 0;
	uint32_t width = header->dwWidth;
	uint32_t height = header->dwHeight;
	for (uint32_t i = 0; i < header->dwMipMapCount; ++i)
	{
		VkBufferImageCopy copy{
			.bufferOffset = offset,
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.imageOffset = {0, 0, 0 },
			.imageExtent = {width, height, 1}
		};

		vkCmdCopyBufferToImage(command_buffer, scratch.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		offset += std::max(1u, (width + 3) / 4) * std::max(1u, (height + 3) / 4) * block_size;
		width = width > 1 ? (width >> 1) : 1;
		height = height > 1 ? (height >> 1) : 1;
	}

	// Transfer dst optimal -> shader read only optimal
	{
		VkImageMemoryBarrier2 barrier = image_barrier(texture.image,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT);
		pipeline_barrier(command_buffer, { barrier });
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
