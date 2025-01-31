#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Volk/volk.h"
#include "vma/vk_mem_alloc.h"

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