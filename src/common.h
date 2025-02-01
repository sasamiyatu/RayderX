#pragma once

#include "Volk/volk.h"
#include "vma/vk_mem_alloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <string>
#include <vector>

#define VK_CHECK(x)                                         \
	do { 					                                \
		VkResult err = x;                                   \
		if (err != VK_SUCCESS) {	                                        \
			                                                \
				printf("Detected Vulkan error: %d\n", err); \
				abort();                                    \
		}                                                   \
	} while(0)

#define FAIL_ON_ERROR(x)                                    \
	do {                                                    \
		bool success = x;                                   \
		if (!success) {                                     \
			abort();                                        \
		}                                                   \
	} while(0)


bool read_binary_file(const char* filepath, std::vector<uint8_t>&data);
std::string read_text_file(const char* filepath);