#pragma once

#include "common.h"
#include "vma/vk_mem_alloc.h"

struct Buffer
{
	VmaAllocator allocator;
	VkBuffer buffer;
	VmaAllocation allocation;
	VkDeviceSize size;

	inline void* map() const
	{
		void* data = nullptr;
		VK_CHECK(vmaMapMemory(allocator, allocation, &data));
		return data;
	}

	inline void unmap() const { vmaUnmapMemory(allocator, allocation); }
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

VkMemoryBarrier2 memory_barrier(VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask, VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask);

VkImageMemoryBarrier2 image_barrier(VkImage image, 
	VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask, VkImageLayout old_layout, 
	VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask, VkImageLayout new_layout,
	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

void pipeline_barrier(VkCommandBuffer command_buffer, std::initializer_list<VkMemoryBarrier2> memory_barriers, std::initializer_list<VkImageMemoryBarrier2> image_barriers);
void pipeline_barrier(VkCommandBuffer command_buffer, 
	uint32_t memory_barrier_count = 0, const VkMemoryBarrier2* memory_barriers = nullptr, 
	uint32_t image_barrier_count = 0, const VkImageMemoryBarrier2* image_barriers = nullptr);

Buffer create_buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocation_flags = 0, void* initial_data = nullptr);
VkImageView create_image_view(VkDevice device, VkImage image, VkImageViewType type, VkFormat format);
Texture create_texture(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, VkImageUsageFlags usage, uint32_t mip_levels = 1, VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT);
bool load_texture(Texture& texture, const char* path, VkDevice device, VmaAllocator allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer, VkQueue queue, const Buffer& scratch, bool is_srgb = false);
