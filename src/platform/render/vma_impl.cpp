// The one translation unit that compiles VulkanMemoryAllocator (header-only library, MIT).
// Vulkan functions come from volk at run time (no static linking to vulkan-1.lib).
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#pragma warning(push, 0)
#include <vk_mem_alloc.h>
#pragma warning(pop)
