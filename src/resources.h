#pragma once

#include "common.h"

struct Buffer
{
	VmaAllocator allocator;
	VkBuffer buffer;
	VmaAllocation allocation;

	inline void* map()
	{
		void* data = nullptr;
		VK_CHECK(vmaMapMemory(allocator, allocation, &data));
		return data;
	}

	inline void unmap() { vmaUnmapMemory(allocator, allocation); }
	inline void destroy() { vmaDestroyBuffer(allocator, buffer, allocation); }
};

struct Texture
{
	VkImage image;
	VkImageView view;
	VmaAllocation allocation;
	VmaAllocator allocator;
	VkDevice device;

	void destroy()
	{
		vkDestroyImageView(device, view, nullptr);
		vmaDestroyImage(allocator, image, allocation);
	}
};

Buffer create_buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocation_flags = 0, void* initial_data = nullptr);
VkImageView create_image_view(VkDevice device, VkImage image, VkImageViewType type, VkFormat format);
Texture create_texture(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkImageUsageFlags usage);
