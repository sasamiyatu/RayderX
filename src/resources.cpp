#include "resources.h"

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
		.allocation = allocation
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
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};

	VkImageView view = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImageView(device, &create_info, nullptr, &view));
	return view;
}

Texture create_texture(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkImageUsageFlags usage)
{
	VkImageCreateInfo image_create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, depth },
		.mipLevels = 1,
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