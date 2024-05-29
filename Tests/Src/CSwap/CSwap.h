#pragma once

#include <vulkan/vulkan.h>

// VK_EXT_wincs_surface

#define VK_EXT_WINCS_SURFACE_EXTENSION_NAME "VK_EXT_wincs_surface"
#define VK_EXT_WINCS_SURFACE_SPEC_VERSION   1

#define VK_STRUCTURE_TYPE_WINCS_SURFACE_CREATE_INFO_EXT  ((VkStructureType) 1000009001)
#define VK_STRUCTURE_TYPE_WINCS_SWAPCHAIN_QUEUE_INFO_EXT ((VkStructureType) 1000009002)

typedef struct HINSTANCE__* HINSTANCE;
typedef struct HWND__*      HWND;

typedef VkFlags VkWinCSSurfaceCreateFlagsEXT;
struct VkWinCSSurfaceCreateInfoEXT
{
	VkStructureType              sType;
	const void*                  pNext;
	VkWinCSSurfaceCreateFlagsEXT flags;
	HINSTANCE                    hinstance;
	HWND                         hwnd;
};

struct VkWinCSSwapchainQueueInfoEXT
{
	VkStructureType sType;
	const void*     pNext;
	VkQueue         queue;
};

VkResult vkCreateWinCSSurfaceEXT(
	VkInstance                         instance,
	const VkWinCSSurfaceCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks*       pAllocator,
	VkSurfaceKHR*                      pSurface);

// VK_EXT_wincs_surface Overrides VK_KHR_surface

void wincs_surface_vkDestroySurfaceKHR(
	VkInstance                   instance,
	VkSurfaceKHR                 surface,
	const VkAllocationCallbacks* pAllocator);
VkResult wincs_surface_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
	VkPhysicalDevice          physicalDevice,
	VkSurfaceKHR              surface,
	VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
VkResult wincs_surface_vkGetPhysicalDeviceSurfaceFormatsKHR(
	VkPhysicalDevice    physicalDevice,
	VkSurfaceKHR        surface,
	uint32_t*           pSurfaceFormatCount,
	VkSurfaceFormatKHR* pSurfaceFormats);
VkResult wincs_surface_vkGetPhysicalDeviceSurfacePresentModesKHR(
	VkPhysicalDevice  physicalDevice,
	VkSurfaceKHR      surface,
	uint32_t*         pPresentModeCount,
	VkPresentModeKHR* pPresentModes);
VkResult wincs_surface_vkGetPhysicalDeviceSurfaceSupportKHR(
	VkPhysicalDevice physicalDevice,
	uint32_t         queueFamilyIndex,
	VkSurfaceKHR     surface,
	VkBool32*        pSupported);

// VK_EXT_wincs_surface Overrides VK_KHR_swapcahin

VkResult wincs_surface_vkCreateSwapchainKHR(
	VkDevice                        device,
	const VkSwapchainCreateInfoKHR* pCreateInfo,
	const VkAllocationCallbacks*    pAllocator,
	VkSwapchainKHR*                 pSwapchain);
void wincs_surface_vkDestroySwapchainKHR(
	VkDevice                     device,
	VkSwapchainKHR               swapchain,
	const VkAllocationCallbacks* pAllocator);
VkResult wincs_surface_vkGetSwapchainImagesKHR(
	VkDevice       device,
	VkSwapchainKHR swapchain,
	uint32_t*      pSwapchainImageCount,
	VkImage*       pSwapchainImages);
VkResult wincs_surface_vkAcquireNextImageKHR(
	VkDevice       device,
	VkSwapchainKHR swapchain,
	uint64_t       timeout,
	VkSemaphore    semaphore,
	VkFence        fence,
	uint32_t*      pImageIndex);
VkResult wincs_surface_vkQueuePresentKHR(
	VkQueue                 queue,
	const VkPresentInfoKHR* pPresentInfo);