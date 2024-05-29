#include "Shared.h"

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include <Concurrency/Mutex.h>
#include <UTF/UTF.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwmapi.lib")

namespace Helpers
{
	void VkReport(VkResult result, std::string_view func)
	{
		std::cout << std::format("{} returned unexpected {}\n", func, string_VkResult(result));
	}

	void HrReport(HRESULT result, std::string_view func)
	{
		std::cout << std::format("{} returned unexpected {:08X}\n", func, (uint32_t) result);
	}
} // namespace Helpers

namespace Vk
{
	Context* g_Context = nullptr;

	bool InitFrameState(Context* context, FrameState* frame)
	{
		if (!context || !frame)
			return false;

		VkCommandPoolCreateInfo pCreateInfo {
			.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext            = nullptr,
			.flags            = 0,
			.queueFamilyIndex = 0
		};
		VkCommandBufferAllocateInfo allocInfo {
			.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.pNext              = nullptr,
			.commandPool        = nullptr,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};
		VkSemaphoreTypeCreateInfo stCreateInfo {
			.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.pNext         = nullptr,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue  = 0
		};
		VkSemaphoreCreateInfo sCreateInfo {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = &stCreateInfo,
			.flags = 0
		};
		VK_INVALID(vkCreateCommandPool, context->Device, &pCreateInfo, nullptr, &frame->Pool)
		{
			return false;
		}
		allocInfo.commandPool = frame->Pool;
		VK_INVALID(vkAllocateCommandBuffers, context->Device, &allocInfo, &frame->CmdBuf)
		{
			vkDestroyCommandPool(context->Device, frame->Pool, nullptr);
			frame->Pool = nullptr;
			return false;
		}
		VK_INVALID(vkCreateSemaphore, context->Device, &sCreateInfo, nullptr, &frame->Timeline)
		{
			vkDestroyCommandPool(context->Device, frame->Pool, nullptr);
			frame->CmdBuf = nullptr;
			frame->Pool   = nullptr;
			return false;
		}
		sCreateInfo.pNext = nullptr;
		VK_INVALID(vkCreateSemaphore, context->Device, &sCreateInfo, nullptr, &frame->RenderDone)
		{
			vkDestroySemaphore(context->Device, frame->Timeline, nullptr);
			vkDestroyCommandPool(context->Device, frame->Pool, nullptr);
			frame->CmdBuf = nullptr;
			frame->Pool   = nullptr;
			return false;
		}
		return true;
	}

	void DeInitFrameState(Context* context, FrameState* frame)
	{
		if (!context || !frame)
			return;

		VkSemaphoreWaitInfo waitInfo {
			.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.pNext          = nullptr,
			.flags          = 0,
			.semaphoreCount = 1,
			.pSemaphores    = &frame->Timeline,
			.pValues        = &frame->TimelineValue
		};
		vkWaitSemaphores(g_Context->Device, &waitInfo, ~0ULL);
		for (auto& destroy : frame->Destroys)
			destroy();
		vkDestroySemaphore(context->Device, frame->RenderDone, nullptr);
		vkDestroySemaphore(context->Device, frame->Timeline, nullptr);
		vkDestroyCommandPool(context->Device, frame->Pool, nullptr);
		frame->RenderDone = nullptr;
		frame->Timeline   = nullptr;
	}

	bool Init(const ContextSpec* spec)
	{
		if (spec && spec->FramesInFlight < 1)
			return false;

		Context* context = new Context();

		// Create Instance
		{
			VkApplicationInfo appInfo {
				.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
				.pNext              = nullptr,
				.pApplicationName   = spec ? spec->AppName : "TestApp",
				.applicationVersion = spec ? spec->AppVersion : VK_MAKE_API_VERSION(0, 1, 0, 0),
				.pEngineName        = "Tests",
				.engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0),
				.apiVersion         = VK_API_VERSION_1_3
			};
			VkInstanceCreateInfo createInfo {
				.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
				.pNext                   = nullptr,
				.flags                   = 0,
				.pApplicationInfo        = &appInfo,
				.enabledLayerCount       = 0,
				.ppEnabledLayerNames     = nullptr,
				.enabledExtensionCount   = spec ? spec->InstanceExtCount : 0,
				.ppEnabledExtensionNames = spec ? spec->InstanceExts : nullptr
			};
			VK_INVALID(vkCreateInstance, &createInfo, nullptr, &context->Instance)
			{
				delete context;
				return false;
			}
		}
		// Select Physical Device
		{
			uint32_t count = 0;
			VK_INVALID(vkEnumeratePhysicalDevices, context->Instance, &count, nullptr)
			{
				vkDestroyInstance(context->Instance, nullptr);
				delete context;
				return false;
			}
			VkPhysicalDevice* devices = new VkPhysicalDevice[count];
			VK_INVALID(vkEnumeratePhysicalDevices, context->Instance, &count, devices)
			{
				delete[] devices;
				vkDestroyInstance(context->Instance, nullptr);
				delete context;
				return false;
			}
			for (uint32_t i = 0; i < count; ++i)
			{
				VkPhysicalDeviceProperties props {};
				vkGetPhysicalDeviceProperties(devices[i], &props);
				if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{
					context->PhysicalDevice = devices[i];
					break;
				}
			}
			delete[] devices;
			if (!context->PhysicalDevice)
			{
				std::cout << "Failed to find appropriate Vulkan Physical Device\n";
				vkDestroyInstance(context->Instance, nullptr);
				delete context;
				return false;
			}
		}
		// Create Device
		{
			float                   prios[] { 1.0f };
			VkDeviceQueueCreateInfo qCreateInfo {
				.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.pNext            = nullptr,
				.flags            = 0,
				.queueFamilyIndex = 0,
				.queueCount       = 1,
				.pQueuePriorities = prios
			};
			VkPhysicalDeviceVulkan13Features Vk13DeviceFeatures = {
				.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
				.pNext            = nullptr,
				.synchronization2 = VK_TRUE,
				.dynamicRendering = VK_TRUE
			};
			VkPhysicalDeviceVulkan12Features Vk12DeviceFeatures = {
				.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
				.pNext             = &Vk13DeviceFeatures,
				.timelineSemaphore = VK_TRUE
			};
			VkPhysicalDeviceVulkan11Features Vk11DeviceFeatures = {
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
				.pNext = &Vk12DeviceFeatures
			};
			VkPhysicalDeviceFeatures2 DeviceFeatures = {
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
				.pNext = &Vk11DeviceFeatures
			};
			VkDeviceCreateInfo createInfo {
				.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				.pNext                   = spec ? &spec->DeviceFeatures : &DeviceFeatures,
				.flags                   = 0,
				.queueCreateInfoCount    = 1,
				.pQueueCreateInfos       = &qCreateInfo,
				.enabledLayerCount       = 0,
				.ppEnabledLayerNames     = nullptr,
				.enabledExtensionCount   = spec ? spec->DeviceExtCount : 0,
				.ppEnabledExtensionNames = spec ? spec->DeviceExts : nullptr,
				.pEnabledFeatures        = nullptr
			};
			VK_INVALID(vkCreateDevice, context->PhysicalDevice, &createInfo, nullptr, &context->Device)
			{
				vkDestroyInstance(context->Instance, nullptr);
				delete context;
				return false;
			}
			vkGetDeviceQueue(context->Device, 0, 0, &context->Queue);
		}
		context->FramesInFlight = spec ? spec->FramesInFlight : 1;
		context->CurrentFrame   = 0;
		context->Frames         = new FrameState[context->FramesInFlight];
		for (uint32_t i = 0; i < context->FramesInFlight; ++i)
		{
			if (!InitFrameState(context, &context->Frames[i]))
			{
				for (uint32_t j = 0; j < i; ++j)
					DeInitFrameState(context, &context->Frames[j]);
				delete[] context->Frames;
				vkDestroyDevice(context->Device, nullptr);
				vkDestroyInstance(context->Instance, nullptr);
				delete context;
				return false;
			}
		}
		g_Context = context;
		return true;
	}

	void DeInit()
	{
		if (!g_Context)
			return;
		if (g_Context->Frames)
		{
			for (uint32_t i = 0; i < g_Context->FramesInFlight; ++i)
				DeInitFrameState(g_Context, &g_Context->Frames[i]);
			delete[] g_Context->Frames;
		}
		vkDestroyDevice(g_Context->Device, nullptr);
		vkDestroyInstance(g_Context->Instance, nullptr);
		delete g_Context;
		g_Context = nullptr;
	}

	void NextFrame()
	{
		if (!g_Context)
			return;
		g_Context->CurrentFrame = (g_Context->CurrentFrame + 1) % g_Context->FramesInFlight;
	}

	bool InitSwapchainState(SwapchainState* swapchain, Wnd::Handle* window, bool withFrames)
	{
		if (!g_Context || !swapchain || !window)
			return false;

		swapchain->Window = window;
		VK_INVALID(createSurface, window, &swapchain->Surface)
		{
			swapchain->Window = nullptr;
			return false;
		}

		VkSurfaceCapabilitiesKHR caps {};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_Context->PhysicalDevice, swapchain->Surface, &caps);

		swapchain->Extents = caps.currentExtent;
		VkSwapchainCreateInfoKHR createInfo {
			.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.pNext                 = nullptr,
			.flags                 = 0,
			.surface               = swapchain->Surface,
			.minImageCount         = std::min<uint32_t>(caps.minImageCount + 1, caps.maxImageCount),
			.imageFormat           = VK_FORMAT_B8G8R8A8_UNORM,
			.imageColorSpace       = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
			.imageExtent           = swapchain->Extents,
			.imageArrayLayers      = 1,
			.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
			.pQueueFamilyIndices   = nullptr,
			.preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
			.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode           = VK_PRESENT_MODE_MAILBOX_KHR,
			.clipped               = VK_FALSE,
			.oldSwapchain          = nullptr
		};
		VK_INVALID(vkCreateSwapchainKHR, g_Context->Device, &createInfo, nullptr, &swapchain->Swapchain)
		{
			vkDestroySurfaceKHR(g_Context->Instance, swapchain->Surface, nullptr);
			swapchain->Surface = nullptr;
			swapchain->Window  = nullptr;
			swapchain->Extents = {};
			return false;
		}
		uint32_t imageCount = 0;
		VK_INVALID(vkGetSwapchainImagesKHR, g_Context->Device, swapchain->Swapchain, &imageCount, nullptr)
		{
			vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
			vkDestroySurfaceKHR(g_Context->Instance, swapchain->Surface, nullptr);
			swapchain->Swapchain = nullptr;
			swapchain->Surface   = nullptr;
			swapchain->Window    = nullptr;
			swapchain->Extents   = {};
			return false;
		}
		swapchain->Images.resize(imageCount);
		VK_INVALID(vkGetSwapchainImagesKHR, g_Context->Device, swapchain->Swapchain, &imageCount, swapchain->Images.column<0>())
		{
			vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
			vkDestroySurfaceKHR(g_Context->Instance, swapchain->Surface, nullptr);
			swapchain->Swapchain = nullptr;
			swapchain->Surface   = nullptr;
			swapchain->Window    = nullptr;
			swapchain->Extents   = {};
			return false;
		}
		VkImageViewCreateInfo ivCreateInfo {
			.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext            = nullptr,
			.flags            = 0,
			.image            = nullptr,
			.viewType         = VK_IMAGE_VIEW_TYPE_2D,
			.format           = VK_FORMAT_B8G8R8A8_UNORM,
			.components       = {},
			.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
		};
		for (uint32_t i = 0; i < imageCount; ++i)
		{
			ivCreateInfo.image = swapchain->Images.entry<0>(i);
			VK_INVALID(vkCreateImageView, g_Context->Device, &ivCreateInfo, nullptr, &swapchain->Images.entry<1>(i))
			{
				for (uint32_t j = 0; j < i; ++j)
					vkDestroyImageView(g_Context->Device, swapchain->Images.entry<1>(j), nullptr);
				swapchain->Images.clear();
				vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
				vkDestroySurfaceKHR(g_Context->Instance, swapchain->Surface, nullptr);
				swapchain->Swapchain = nullptr;
				swapchain->Surface   = nullptr;
				swapchain->Window    = nullptr;
				swapchain->Extents   = {};
				return false;
			}
		}

		if (withFrames)
		{
			VkSemaphoreCreateInfo sCreateInfo {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0
			};

			swapchain->Frames = new SwapchainFrameState[g_Context->FramesInFlight];
			for (uint32_t i = 0; i < g_Context->FramesInFlight; ++i)
			{
				if (!InitFrameState(g_Context, &swapchain->Frames[i]))
				{
					for (uint32_t j = 0; j < i; ++j)
					{
						vkDestroySemaphore(g_Context->Device, swapchain->Frames[i].ImageReady, nullptr);
						DeInitFrameState(g_Context, &swapchain->Frames[j]);
					}
					delete[] swapchain->Frames;
					for (uint32_t j = 0; j < swapchain->Images.size(); ++j)
						vkDestroyImageView(g_Context->Device, swapchain->Images.entry<1>(j), nullptr);
					swapchain->Images.clear();
					vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
					vkDestroySurfaceKHR(g_Context->Instance, swapchain->Surface, nullptr);
					swapchain->Swapchain = nullptr;
					swapchain->Surface   = nullptr;
					swapchain->Window    = nullptr;
					swapchain->Extents   = {};
					return false;
				}
				VK_INVALID(vkCreateSemaphore, g_Context->Device, &sCreateInfo, nullptr, &swapchain->Frames[i].ImageReady)
				{
					for (uint32_t j = 0; j < i; ++j)
					{
						vkDestroySemaphore(g_Context->Device, swapchain->Frames[i].ImageReady, nullptr);
						DeInitFrameState(g_Context, &swapchain->Frames[j]);
					}
					DeInitFrameState(g_Context, &swapchain->Frames[i]);
					delete[] swapchain->Frames;
					for (uint32_t j = 0; j < swapchain->Images.size(); ++j)
						vkDestroyImageView(g_Context->Device, swapchain->Images.entry<1>(j), nullptr);
					swapchain->Images.clear();
					vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
					vkDestroySurfaceKHR(g_Context->Instance, swapchain->Surface, nullptr);
					swapchain->Swapchain = nullptr;
					swapchain->Surface   = nullptr;
					swapchain->Window    = nullptr;
					swapchain->Extents   = {};
					return false;
				}
				swapchain->Frames->ImageIndex = 0;
			}
		}
		return true;
	}

	void DeInitSwapchainState(SwapchainState* swapchain)
	{
		if (!g_Context || !swapchain)
			return;

		if (swapchain->Frames)
		{
			for (uint32_t i = 0; i < g_Context->FramesInFlight; ++i)
			{
				DeInitFrameState(g_Context, &swapchain->Frames[i]);
				vkDestroySemaphore(g_Context->Device, swapchain->Frames[i].ImageReady, nullptr);
			}
			delete[] swapchain->Frames;
		}
		for (uint32_t i = 0; i < swapchain->Images.size(); ++i)
			vkDestroyImageView(g_Context->Device, swapchain->Images.entry<1>(i), nullptr);
		swapchain->Images.clear();
		vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
		vkDestroySurfaceKHR(g_Context->Instance, swapchain->Surface, nullptr);
		swapchain->Swapchain = nullptr;
		swapchain->Surface   = nullptr;
		swapchain->Window    = nullptr;
		swapchain->Extents   = {};
	}

	bool SwapchainAcquireImage(SwapchainState* swapchain)
	{
		if (!g_Context || !swapchain)
			return false;

		if (swapchain->Invalidated &&
			!SwapchainResize(swapchain))
			return false;

		auto&    frame  = swapchain->Frames[g_Context->CurrentFrame];
		VkResult result = vkAcquireNextImageKHR(g_Context->Device, swapchain->Swapchain, ~0ULL, frame.ImageReady, nullptr, &frame.ImageIndex);
		switch (result)
		{
		case VK_ERROR_OUT_OF_DATE_KHR:
		case VK_SUBOPTIMAL_KHR:
			if (!SwapchainResize(swapchain))
				return false;
			VK_INVALID(vkAcquireNextImageKHR, g_Context->Device, swapchain->Swapchain, ~0ULL, frame.ImageReady, nullptr, &frame.ImageIndex)
			{
				return false;
			}
			break;
		default:
			if (!Helpers::VkValidate(result, "vkAcquireNextImageKHR"))
				return false;
			break;
		}
		return true;
	}

	bool SwapchainPresent(SwapchainState* swapchain)
	{
		if (!g_Context || !swapchain)
			return false;

		auto& frame = swapchain->Frames[g_Context->CurrentFrame];

		VkPresentInfoKHR presentInfo {
			.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.pNext              = nullptr,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores    = &frame.RenderDone,
			.swapchainCount     = 1,
			.pSwapchains        = &swapchain->Swapchain,
			.pImageIndices      = &frame.ImageIndex,
			.pResults           = nullptr
		};
		VkResult result = vkQueuePresentKHR(g_Context->Queue, &presentInfo);
		switch (result)
		{
		case VK_ERROR_OUT_OF_DATE_KHR:
		case VK_SUBOPTIMAL_KHR:
			swapchain->Invalidated = true;
			break;
		default:
			if (!Helpers::VkValidate(result, "vkQueuePresentKHR"))
				return false;
			break;
		}
		return true;
	}

	bool SwapchainResize(SwapchainState* swapchain)
	{
		if (!g_Context || !swapchain)
			return false;

		auto oldSwapchain = swapchain->Swapchain;

		VkSurfaceCapabilitiesKHR caps {};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_Context->PhysicalDevice, swapchain->Surface, &caps);

		swapchain->Extents = caps.currentExtent;
		VkSwapchainCreateInfoKHR createInfo {
			.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.pNext                 = nullptr,
			.flags                 = 0,
			.surface               = swapchain->Surface,
			.minImageCount         = std::min<uint32_t>(caps.minImageCount + 1, caps.maxImageCount),
			.imageFormat           = VK_FORMAT_B8G8R8A8_UNORM,
			.imageColorSpace       = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
			.imageExtent           = swapchain->Extents,
			.imageArrayLayers      = 1,
			.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
			.pQueueFamilyIndices   = nullptr,
			.preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
			.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode           = VK_PRESENT_MODE_MAILBOX_KHR,
			.clipped               = VK_FALSE,
			.oldSwapchain          = oldSwapchain
		};
		VK_INVALID(vkCreateSwapchainKHR, g_Context->Device, &createInfo, nullptr, &swapchain->Swapchain)
		{
			return false;
		}
		if (swapchain->Frames)
		{
			swapchain->Frames[g_Context->CurrentFrame].Destroys.emplace_back(
				[oldSwapchain, images = std::move(swapchain->Images)]() {
					for (auto [image, view] : images)
						vkDestroyImageView(g_Context->Device, view, nullptr);
					vkDestroySwapchainKHR(g_Context->Device, oldSwapchain, nullptr);
				});
		}
		else
		{
			g_Context->Frames[g_Context->CurrentFrame].Destroys.emplace_back(
				[oldSwapchain, images = std::move(swapchain->Images)]() {
					for (auto [image, view] : images)
						vkDestroyImageView(g_Context->Device, view, nullptr);
					vkDestroySwapchainKHR(g_Context->Device, oldSwapchain, nullptr);
				});
		}
		uint32_t imageCount = 0;
		VK_INVALID(vkGetSwapchainImagesKHR, g_Context->Device, swapchain->Swapchain, &imageCount, nullptr)
		{
			vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
			swapchain->Swapchain   = nullptr;
			swapchain->Invalidated = true;
			return false;
		}
		swapchain->Images.resize(imageCount);
		VK_INVALID(vkGetSwapchainImagesKHR, g_Context->Device, swapchain->Swapchain, &imageCount, swapchain->Images.column<0>())
		{
			vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
			swapchain->Swapchain   = nullptr;
			swapchain->Invalidated = true;
			return false;
		}
		VkImageViewCreateInfo ivCreateInfo {
			.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext            = nullptr,
			.flags            = 0,
			.image            = nullptr,
			.viewType         = VK_IMAGE_VIEW_TYPE_2D,
			.format           = VK_FORMAT_B8G8R8A8_UNORM,
			.components       = {},
			.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
		};
		for (uint32_t i = 0; i < imageCount; ++i)
		{
			ivCreateInfo.image = swapchain->Images.entry<0>(i);
			VK_INVALID(vkCreateImageView, g_Context->Device, &ivCreateInfo, nullptr, &swapchain->Images.entry<1>(i))
			{
				for (uint32_t j = 0; j < i; ++j)
					vkDestroyImageView(g_Context->Device, swapchain->Images.entry<1>(j), nullptr);
				swapchain->Images.clear();
				vkDestroySwapchainKHR(g_Context->Device, swapchain->Swapchain, nullptr);
				swapchain->Swapchain   = nullptr;
				swapchain->Invalidated = true;
				return false;
			}
		}

		swapchain->Invalidated = false;
		return true;
	}

	uint32_t FindDeviceMemoryIndex(uint32_t typeBits, VkMemoryPropertyFlags flags)
	{
		if (!g_Context)
			return ~0U;

		VkPhysicalDeviceMemoryProperties props {};
		vkGetPhysicalDeviceMemoryProperties(g_Context->PhysicalDevice, &props);

		for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
		{
			if ((typeBits & (1U << i)) != 0 && (props.memoryTypes[i].propertyFlags & flags) == flags)
				return i;
		}
		return ~0U;
	}

	VkResult createSurface(Wnd::Handle* window, VkSurfaceKHR* surface)
	{
		VkWin32SurfaceCreateInfoKHR createInfo {
			.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
			.pNext     = nullptr,
			.flags     = 0,
			.hinstance = Wnd::GetInstance(),
			.hwnd      = Wnd::GetNativeHandle(window)
		};
		return vkCreateWin32SurfaceKHR(g_Context->Instance, &createInfo, nullptr, surface);
	}
} // namespace Vk

namespace DX
{
	Context* g_Context = nullptr;

	bool Init(const ContextSpec* spec)
	{
		Context* context = new Context();

		UINT d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		if (spec && spec->WithPresentation)
			d3d11DeviceFlags |= D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;

		ID3D11Device*        d3d11Device        = nullptr;
		ID3D11DeviceContext* d3d11DeviceContext = nullptr;
		HR_INVALID(D3D11CreateDevice, nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, d3d11DeviceFlags, nullptr, 0, D3D11_SDK_VERSION, &d3d11Device, nullptr, &d3d11DeviceContext)
		{
			delete context;
			return false;
		}
		HR_INVALID(d3d11Device->QueryInterface, &context->D3D11Device)
		{
			std::cout << "Failed to Query ID3D11Device5\n";
			d3d11Device->Release();
			d3d11DeviceContext->Release();
			delete context;
			return false;
		}
		d3d11Device->Release();
		HR_INVALID(d3d11DeviceContext->QueryInterface, &context->D3D11DeviceContext)
		{
			std::cout << "Failed to Query ID3D11DeviceContext4\n";
			context->DXGIDevice->Release();
			context->D3D11Device->Release();
			d3d11DeviceContext->Release();
			delete context;
			return false;
		}
		d3d11DeviceContext->Release();
		HR_INVALID(d3d11Device->QueryInterface, &context->DXGIDevice)
		{
			std::cout << "Failed to Query IDXGIDevice4\n";
			context->D3D11Device->Release();
			delete context;
			return false;
		}
		HR_INVALID(CreateDXGIFactory2, 0, __uuidof(IDXGIFactory7), (void**) &context->DXGIFactory)
		{
			context->DXGIDevice->Release();
			context->D3D11DeviceContext->Release();
			context->D3D11Device->Release();
			delete context;
			return false;
		}
		if (spec && spec->WithComposition)
		{
			HR_INVALID(DCompositionCreateDevice3, context->DXGIDevice, __uuidof(IDCompositionDevice4), (void**) &context->DCompDevice2)
			{
				context->DXGIFactory->Release();
				context->DXGIDevice->Release();
				context->D3D11DeviceContext->Release();
				context->D3D11Device->Release();
				delete context;
				return false;
			}
			HR_INVALID(context->DCompDevice2->QueryInterface, &context->DCompDevice)
			{
				context->DCompDevice2->Release();
				context->DXGIFactory->Release();
				context->DXGIDevice->Release();
				context->D3D11DeviceContext->Release();
				context->D3D11Device->Release();
				delete context;
				return false;
			}
			BOOL supportsCompositionTextures = false;
			HR_INVALID(context->DCompDevice2->CheckCompositionTextureSupport, context->D3D11Device, &supportsCompositionTextures)
			{
				context->DCompDevice->Release();
				context->DCompDevice2->Release();
				context->DXGIFactory->Release();
				context->DXGIDevice->Release();
				context->D3D11DeviceContext->Release();
				context->D3D11Device->Release();
				delete context;
				return false;
			}
			if (!supportsCompositionTextures)
			{
				context->DCompDevice->Release();
				context->DCompDevice2->Release();
				context->DXGIFactory->Release();
				context->DXGIDevice->Release();
				context->D3D11DeviceContext->Release();
				context->D3D11Device->Release();
				delete context;
				return false;
			}
		}
		if (spec && spec->WithPresentation)
		{
			HR_INVALID(CreatePresentationFactory, context->D3D11Device, __uuidof(IPresentationFactory), (void**) &context->PresentationFactory)
			{
				if (context->DCompDevice)
					context->DCompDevice->Release();
				if (context->DCompDevice2)
					context->DCompDevice2->Release();
				context->DXGIFactory->Release();
				context->DXGIDevice->Release();
				context->D3D11DeviceContext->Release();
				context->D3D11Device->Release();
				delete context;
				return false;
			}
			if (!context->PresentationFactory->IsPresentationSupportedWithIndependentFlip())
			{
				context->PresentationFactory->Release();
				if (context->DCompDevice)
					context->DCompDevice->Release();
				if (context->DCompDevice2)
					context->DCompDevice2->Release();
				context->DXGIFactory->Release();
				context->DXGIDevice->Release();
				context->D3D11DeviceContext->Release();
				context->D3D11Device->Release();
				delete context;
				return false;
			}
		}

		g_Context = context;
		return true;
	}

	void DeInit()
	{
		if (!g_Context)
			return;

		if (g_Context->DCompDevice2)
			g_Context->DCompDevice2->Release();
		if (g_Context->DCompDevice)
			g_Context->DCompDevice->Release();
		if (g_Context->DXGIFactory)
			g_Context->DXGIFactory->Release();
		if (g_Context->DXGIDevice)
			g_Context->DXGIDevice->Release();
		if (g_Context->D3D11DeviceContext)
			g_Context->D3D11DeviceContext->Release();
		if (g_Context->D3D11Device)
			g_Context->D3D11Device->Release();
		delete g_Context;
		g_Context = nullptr;
	}

	bool InitCSwapchain(CSwapchain* swapchain, const CSwapchainSpec* spec)
	{
		if (!g_Context || !swapchain || !spec)
			return false;

		if (spec->MinBufferCount >= 8)
			return false;
		

		return true;
	}

	void DeInitCSwapchain(CSwapchain* swapchain)
	{
	}
} // namespace DX

namespace Wnd
{
	static constexpr UINT Wnd_WM_CREATE_WINDOW  = WM_APP + 1;
	static constexpr UINT Wnd_WM_DESTROY_WINDOW = WM_APP + 2;
	static constexpr UINT Wnd_WM_DEINIT         = WM_APP + 3;

	namespace WindowFlag
	{
		static constexpr uint64_t None       = 0x0000;
		static constexpr uint64_t Maximized  = 0x0001;
		static constexpr uint64_t Minimized  = 0x0002;
		static constexpr uint64_t Visible    = 0x0004;
		static constexpr uint64_t Decorated  = 0x0008;
		static constexpr uint64_t WantsClose = 0x8000;

		static constexpr uint64_t MinMax = Maximized | Minimized;
	} // namespace WindowFlag

	struct Handle
	{
		virtual ~Handle() = default;

		HWND HWnd = nullptr;

		int32_t  x, y;
		uint32_t w, h;

		int32_t  rawX, rawY;
		uint32_t rawW, rawH;

		int32_t rawMarginX;
		int32_t rawMarginY;
		int32_t rawMarginW;
		int32_t rawMarginH;

		std::string Title;
	};

	struct SameThreadHandle : public Handle
	{
		uint64_t Flags = 0;
	};

	struct SeparateThreadHandle : public Handle
	{
		std::atomic_uint64_t Flags = 0;

		Concurrency::SharedMutex Mtx;
	};

	struct Context
	{
		virtual ~Context() = default;

		bool SeparateThread = false;

		HINSTANCE HInstance  = nullptr;
		HWND      HelperHWnd = nullptr;

		std::vector<Handle*> Windows;
	};

	struct SameThreadContext : public Context
	{
	public:
		bool QuitSignaled = false;
	};

	struct SeparateThreadContext : public Context
	{
	public:
		std::atomic_bool   QuitSignaled      = false;
		std::atomic_bool   Status            = false;
		std::atomic_bool   Loaded            = false;
		std::atomic_bool   Running           = false;
		std::atomic_bool   AcceptingMessages = false;
		std::atomic_size_t MessageCount      = 0;
		std::thread        WindowThread;

		Concurrency::SharedMutex Mtx;
	};

	Context* g_Context = nullptr;

	static bool    InitCommon(Context* context);
	static void    DeInitCommon(Context* context);
	static void    WindowThreadFunc();
	static bool    IntInitHandle(Context* context, Handle* handle, const Spec* spec);
	static void    IntDeInitHandle(Context* context, Handle* handle);
	static LRESULT HelperWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
	static LRESULT WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

	static LRESULT SendWindowMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
	{
		if (!g_Context || !g_Context->SeparateThread)
			return FALSE;
		((SeparateThreadContext*) g_Context)->AcceptingMessages.wait(false);
		return SendMessageW(g_Context->HelperHWnd, Msg, wParam, lParam);
	}

	bool Init(const ContextSpec* spec)
	{
		if (spec && spec->SeparateThread)
		{
			SeparateThreadContext* context = new SeparateThreadContext();
			g_Context                      = context;

			context->SeparateThread = true;
			context->HInstance      = (HINSTANCE) GetModuleHandleW(nullptr);
			context->Running        = true;
			context->WindowThread   = std::thread(&WindowThreadFunc);
			context->Loaded.wait(false);
			if (!context->Status)
			{
				delete context;
				g_Context = nullptr;
				return false;
			}
			return true;
		}

		SameThreadContext* context = new SameThreadContext();
		context->SeparateThread    = false;
		context->HInstance         = (HINSTANCE) GetModuleHandleW(nullptr);
		if (!InitCommon(context))
		{
			delete context;
			return false;
		}
		g_Context = context;
		return true;
	}

	void DeInit()
	{
		if (!g_Context)
			return;
		if (g_Context->SeparateThread)
		{
			SeparateThreadContext* stContext = (SeparateThreadContext*) g_Context;
			if (stContext->Running)
			{
				SendWindowMessage(Wnd_WM_DEINIT, 0, 0);
				stContext->WindowThread.join();
			}
		}
		else
		{
			DeInitCommon(g_Context);
		}
		delete g_Context;
		g_Context = nullptr;
	}

	void PollEvents()
	{
		if (!g_Context || g_Context->SeparateThread)
			return;

		MSG msg {};
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				((SameThreadContext*) g_Context)->QuitSignaled = true;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	void WaitForEvent()
	{
		if (!g_Context)
			return;
		if (g_Context->SeparateThread)
		{
			SeparateThreadContext* stContext = (SeparateThreadContext*) g_Context;
			stContext->MessageCount.wait(stContext->MessageCount.load());
			return;
		}

		MSG  msg {};
		BOOL result = GetMessageW(&msg, nullptr, 0, 0);
		if (result > 0)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		else if (result <= 0)
		{
			SameThreadContext* stContext = (SameThreadContext*) g_Context;
			stContext->QuitSignaled      = true;
		}
	}

	bool QuitSignaled()
	{
		if (!g_Context)
			return false;
		return g_Context->SeparateThread
				   ? ((SeparateThreadContext*) g_Context)->QuitSignaled.load()
				   : ((SameThreadContext*) g_Context)->QuitSignaled;
	}

	void SignalQuit()
	{
		if (!g_Context)
			return;
		if (QuitSignaled())
			return;
		if (g_Context->SeparateThread)
			((SeparateThreadContext*) g_Context)->QuitSignaled = true;
		else
			((SameThreadContext*) g_Context)->QuitSignaled = true;
	}

	HINSTANCE GetInstance()
	{
		return g_Context ? g_Context->HInstance : nullptr;
	}

	Handle* Create(const Spec* spec)
	{
		if (!g_Context || !spec)
			return nullptr;
		if (g_Context->SeparateThread)
		{
			Handle* handle = nullptr;
			if (!SendWindowMessage(Wnd_WM_CREATE_WINDOW, (WPARAM) &handle, (LPARAM) spec))
				return nullptr;
			return handle;
		}
		Handle* handle = new SameThreadHandle();
		if (!IntInitHandle(g_Context, handle, spec))
		{
			delete handle;
			return nullptr;
		}
		g_Context->Windows.emplace_back(handle);
		return handle;
	}

	void Destroy(Handle* window)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
		{
			SendWindowMessage(Wnd_WM_DESTROY_WINDOW, (WPARAM) window, 0);
			return;
		}
		IntDeInitHandle(g_Context, window);
		std::erase(g_Context->Windows, window);
		delete window;
	}

	HWND GetNativeHandle(Handle* window)
	{
		if (!g_Context || !window)
			return nullptr;
		return window->HWnd;
	}

	void Show(Handle* window)
	{
		if (!g_Context || !window)
			return;
		uint64_t type = 0;
		if (g_Context->SeparateThread)
		{
			SeparateThreadHandle* stHandle = (SeparateThreadHandle*) window;
			if (stHandle->Flags & WindowFlag::Visible)
				return;
			stHandle->Flags |= WindowFlag::Visible;
			type             = stHandle->Flags & WindowFlag::MinMax;
			if (type == WindowFlag::MinMax)
				stHandle->Flags &= ~WindowFlag::MinMax;
		}
		else
		{
			SameThreadHandle* stHandle = (SameThreadHandle*) window;
			if (stHandle->Flags & WindowFlag::Visible)
				return;
			stHandle->Flags |= WindowFlag::Visible;
			type             = stHandle->Flags & WindowFlag::MinMax;
			if (type == WindowFlag::MinMax)
				stHandle->Flags &= ~WindowFlag::MinMax;
		}
		switch (type)
		{
		case WindowFlag::Maximized: ShowWindow(window->HWnd, SW_MAXIMIZE); break;
		case WindowFlag::Minimized: ShowWindow(window->HWnd, SW_MINIMIZE); break;
		default: ShowWindow(window->HWnd, SW_NORMAL); break;
		}
	}

	void Hide(Handle* window)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
		{
			SeparateThreadHandle* stHandle = (SeparateThreadHandle*) window;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
			stHandle->Flags &= ~WindowFlag::Visible;
		}
		else
		{
			SameThreadHandle* stHandle = (SameThreadHandle*) window;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
			stHandle->Flags &= ~WindowFlag::Visible;
		}
		ShowWindow(window->HWnd, SW_HIDE);
	}

	void Maximize(Handle* window)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
		{
			SeparateThreadHandle* stHandle = (SeparateThreadHandle*) window;
			if ((stHandle->Flags & WindowFlag::MinMax) == WindowFlag::Maximized)
				return;
			stHandle->Flags &= ~WindowFlag::MinMax;
			stHandle->Flags |= WindowFlag::Maximized;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
		}
		else
		{
			SameThreadHandle* stHandle = (SameThreadHandle*) window;
			if ((stHandle->Flags & WindowFlag::MinMax) == WindowFlag::Maximized)
				return;
			stHandle->Flags &= ~WindowFlag::MinMax;
			stHandle->Flags |= WindowFlag::Maximized;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
		}
		ShowWindow(window->HWnd, SW_MAXIMIZE);
	}

	void Minimize(Handle* window)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
		{
			SeparateThreadHandle* stHandle = (SeparateThreadHandle*) window;
			if ((stHandle->Flags & WindowFlag::MinMax) == WindowFlag::Minimized)
				return;
			stHandle->Flags &= ~WindowFlag::MinMax;
			stHandle->Flags |= WindowFlag::Minimized;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
		}
		else
		{
			SameThreadHandle* stHandle = (SameThreadHandle*) window;
			if ((stHandle->Flags & WindowFlag::MinMax) == WindowFlag::Minimized)
				return;
			stHandle->Flags &= ~WindowFlag::MinMax;
			stHandle->Flags |= WindowFlag::Minimized;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
		}
		ShowWindow(window->HWnd, SW_MINIMIZE);
	}

	void Restore(Handle* window)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
		{
			SeparateThreadHandle* stHandle = (SeparateThreadHandle*) window;
			if ((stHandle->Flags & WindowFlag::MinMax) == 0)
				return;
			stHandle->Flags &= ~WindowFlag::MinMax;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
		}
		else
		{
			SameThreadHandle* stHandle = (SameThreadHandle*) window;
			if ((stHandle->Flags & WindowFlag::MinMax) == 0)
				return;
			stHandle->Flags &= ~WindowFlag::MinMax;
			if (!(stHandle->Flags & WindowFlag::Visible))
				return;
		}
		ShowWindow(window->HWnd, SW_NORMAL);
	}

	bool IsMaximized(Handle* window)
	{
		if (!g_Context || !window)
			return false;
		return g_Context->SeparateThread
				   ? (((SeparateThreadHandle*) window)->Flags & WindowFlag::MinMax) == WindowFlag::Maximized
				   : (((SameThreadHandle*) window)->Flags & WindowFlag::MinMax) == WindowFlag::Maximized;
	}

	bool IsMinimized(Handle* window)
	{
		if (!g_Context || !window)
			return false;
		return g_Context->SeparateThread
				   ? (((SeparateThreadHandle*) window)->Flags & WindowFlag::MinMax) == WindowFlag::Minimized
				   : (((SameThreadHandle*) window)->Flags & WindowFlag::MinMax) == WindowFlag::Minimized;
	}

	void SetWindowTitle(Handle* window, std::string_view title)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.Lock();
		window->Title = title;
		if (g_Context->SeparateThread)
		{
			((SeparateThreadHandle*) window)->Mtx.Unlock();

			SeparateThreadHandle* stHandle = (SeparateThreadHandle*) window;
			if (!(stHandle->Flags & WindowFlag::Decorated))
				return;
		}
		else
		{
			SameThreadHandle* stHandle = (SameThreadHandle*) window;
			if (!(stHandle->Flags & WindowFlag::Decorated))
				return;
		}

		auto titleW = UTF::Convert<wchar_t, char>(title);
		SetWindowTextW(window->HWnd, titleW.c_str());
	}

	void GetWindowTitle(Handle* window, std::string& title)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.LockShared();
		title = window->Title;
		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.UnlockShared();
	}

	bool GetWantsClose(Handle* window)
	{
		if (!g_Context || !window)
			return false;
		return g_Context->SeparateThread
				   ? ((SeparateThreadHandle*) window)->Flags & WindowFlag::WantsClose
				   : ((SameThreadHandle*) window)->Flags & WindowFlag::WantsClose;
	}

	void SetWantsClose(Handle* window, bool wantsClose)
	{
		if (!g_Context || !window)
			return;
		if (g_Context->SeparateThread)
		{
			if (wantsClose)
				((SeparateThreadHandle*) window)->Flags |= WindowFlag::WantsClose;
			else
				((SeparateThreadHandle*) window)->Flags &= ~WindowFlag::WantsClose;
		}
		else
		{
			if (wantsClose)
				((SameThreadHandle*) window)->Flags |= WindowFlag::WantsClose;
			else
				((SameThreadHandle*) window)->Flags &= ~WindowFlag::WantsClose;
		}
	}

	void GetWindowPos(Handle* window, int32_t& x, int32_t& y, bool raw)
	{
		if (!g_Context || !window)
		{
			x = 0;
			y = 0;
			return;
		}

		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.LockShared();
		x = raw ? window->rawX : window->x;
		y = raw ? window->rawY : window->y;
		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.UnlockShared();
	}

	void GetWindowSize(Handle* window, uint32_t& w, uint32_t& h, bool raw)
	{
		if (!g_Context || !window)
		{
			w = 0;
			h = 0;
			return;
		}

		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.LockShared();
		w = raw ? window->rawW : window->w;
		h = raw ? window->rawH : window->h;
		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.UnlockShared();
	}

	void GetWindowRect(Handle* window, int32_t& x, int32_t& y, uint32_t& w, uint32_t& h, bool raw)
	{
		if (!g_Context || !window)
		{
			x = 0;
			y = 0;
			w = 0;
			h = 0;
			return;
		}

		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.LockShared();
		x = raw ? window->rawX : window->x;
		y = raw ? window->rawY : window->y;
		w = raw ? window->rawW : window->w;
		h = raw ? window->rawH : window->h;
		if (g_Context->SeparateThread)
			((SeparateThreadHandle*) window)->Mtx.UnlockShared();
	}

	void SetWindowPos(Handle* window, int32_t x, int32_t y, bool raw)
	{
		if (!g_Context || !window)
			return;

		if (raw)
			::SetWindowPos(window->HWnd, nullptr, (int) (x + window->rawMarginX), (int) (y + window->rawMarginY), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOSENDCHANGING);
		else
			::SetWindowPos(window->HWnd, nullptr, (int) x, (int) y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOSENDCHANGING);
	}

	void SetWindowSize(Handle* window, uint32_t w, uint32_t h, bool raw)
	{
		if (!g_Context || !window)
			return;

		if (raw)
			::SetWindowPos(window->HWnd, nullptr, 0, 0, (int) (w - window->rawMarginW), (int) (h - window->rawMarginH), SWP_NOMOVE | SWP_NOZORDER | SWP_NOSENDCHANGING);
		else
			::SetWindowPos(window->HWnd, nullptr, 0, 0, (int) w, (int) h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOSENDCHANGING);
	}

	void SetWindowRect(Handle* window, int32_t x, int32_t y, uint32_t w, uint32_t h, bool raw)
	{
		if (!g_Context || !window)
			return;

		if (raw)
			::SetWindowPos(window->HWnd, nullptr, (int) (x + window->rawMarginX), (int) (y + window->rawMarginY), (int) (w - window->rawMarginW), (int) (h - window->rawMarginH), SWP_NOZORDER | SWP_NOSENDCHANGING);
		else
			::SetWindowPos(window->HWnd, nullptr, (int) x, (int) y, (int) w, (int) h, SWP_NOZORDER | SWP_NOSENDCHANGING);
	}

	bool InitCommon(Context* context)
	{
		if (!context)
			return false;
		WNDCLASSEXW wndClass {
			.cbSize        = sizeof(wndClass),
			.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
			.lpfnWndProc   = &WndProc,
			.cbClsExtra    = 0,
			.cbWndExtra    = 0,
			.hInstance     = context->HInstance,
			.hIcon         = nullptr,
			.hCursor       = LoadCursorW(nullptr, IDC_ARROW),
			.hbrBackground = nullptr,
			.lpszMenuName  = nullptr,
			.lpszClassName = L"TestWindow",
			.hIconSm       = nullptr
		};
		RegisterClassExW(&wndClass);
		wndClass.style         = 0;
		wndClass.lpfnWndProc   = &HelperWndProc;
		wndClass.hCursor       = nullptr;
		wndClass.lpszClassName = L"TestHelperWindow";
		RegisterClassExW(&wndClass);

		context->HelperHWnd = CreateWindowExW(
			WS_EX_OVERLAPPEDWINDOW,
			L"TestHelperWindow",
			L"TestHelperWindow",
			WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
			0,
			0,
			1,
			1,
			nullptr,
			nullptr,
			context->HInstance,
			nullptr);
		if (!context->HelperHWnd)
		{
			UnregisterClassW(L"TestHelperWindow", context->HInstance);
			UnregisterClassW(L"TestWindow", context->HInstance);
			return false;
		}
		ShowWindow(context->HelperHWnd, SW_HIDE);
		return true;
	}

	void DeInitCommon(Context* context)
	{
		if (!context)
			return;
		if (context->SeparateThread)
			((SeparateThreadContext*) context)->Mtx.Lock();
		for (auto window : context->Windows)
			IntDeInitHandle(context, window);
		context->Windows.clear();
		if (context->SeparateThread)
			((SeparateThreadContext*) context)->Mtx.Unlock();
		DestroyWindow(context->HelperHWnd);
		context->HelperHWnd = nullptr;
		UnregisterClassW(L"TestHelperWindow", context->HInstance);
		UnregisterClassW(L"TestWindow", context->HInstance);
	}

	void WindowThreadFunc()
	{
		SeparateThreadContext* stContext = (SeparateThreadContext*) g_Context;
		if (!InitCommon(g_Context))
		{
			stContext->Status = false;
			stContext->Loaded = true;
			stContext->Loaded.notify_one();
			return;
		}

		stContext->Status = true;
		stContext->Loaded = true;
		stContext->Loaded.notify_one();

		MSG msg {};
		PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
		stContext->AcceptingMessages = true;
		stContext->AcceptingMessages.notify_all();

		while (stContext->Running)
		{
			BOOL result = GetMessageW(&msg, nullptr, 0, 0);
			if (result <= 0)
			{
				stContext->QuitSignaled = true;
				stContext->Running      = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		stContext->Running           = false;
		stContext->AcceptingMessages = false;

		DeInitCommon(g_Context);
	}

	bool IntInitHandle(Context* context, Handle* handle, const Spec* spec)
	{
		if (!context || !handle || !spec)
			return false;

		int32_t windowX = spec->x;
		int32_t windowY = spec->y;
		if (windowX == c_CenterX ||
			windowY == c_CenterY)
		{
			// auto primaryMonitor = GetPrimaryMonitor();
			// if (windowX == c_CenterX)
			//	windowX = primaryMonitor.WorkX + (primaryMonitor.WorkW - spec->w) / 2;
			// if (windowY == c_CenterY)
			//	windowY = primaryMonitor.WorkY + (primaryMonitor.WorkH - spec->h) / 2;
		}
		DWORD    exStyle = WS_EX_APPWINDOW;
		DWORD    style   = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX;
		uint64_t flags   = 0;
		if (spec->Flags & WindowCreateFlag::NoBitmap)
			exStyle |= WS_EX_NOREDIRECTIONBITMAP;
		if (spec->Flags & WindowCreateFlag::Decorated)
		{
			flags |= WindowFlag::Decorated;
			style |= WS_MAXIMIZEBOX | WS_THICKFRAME;
		}
		if (spec->Flags & WindowCreateFlag::Visible)
			flags |= WindowFlag::Visible;
		if (spec->Flags & WindowCreateFlag::Maximized)
			flags |= WindowFlag::Maximized;
		else if (spec->Flags & WindowCreateFlag::Minimized)
			flags |= WindowFlag::Minimized;
		auto titleW  = UTF::Convert<wchar_t, char>(spec->Title);
		handle->HWnd = CreateWindowExW(
			exStyle,
			L"TestWindow",
			titleW.c_str(),
			style,
			(int) windowX,
			(int) windowY,
			(int) spec->w,
			(int) spec->h,
			nullptr,
			nullptr,
			context->HInstance,
			nullptr);
		if (!handle->HWnd)
			return false;
		SetPropW(handle->HWnd, L"TestHandle", handle);
		handle->Title = spec->Title;
		if (context->SeparateThread)
			((SeparateThreadHandle*) handle)->Flags = flags;
		else
			((SameThreadHandle*) handle)->Flags = flags;

		WINDOWINFO wi {};
		wi.cbSize = sizeof(wi);
		GetWindowInfo(handle->HWnd, &wi);
		handle->x          = (int32_t) wi.rcClient.left;
		handle->y          = (int32_t) wi.rcClient.top;
		handle->w          = (uint32_t) (wi.rcClient.right - wi.rcClient.left);
		handle->h          = (uint32_t) (wi.rcClient.bottom - wi.rcClient.top);
		handle->rawX       = (int32_t) wi.rcWindow.left;
		handle->rawY       = (int32_t) wi.rcWindow.top;
		handle->rawW       = (uint32_t) (wi.rcWindow.right - wi.rcWindow.left);
		handle->rawH       = (uint32_t) (wi.rcWindow.bottom - wi.rcWindow.top);
		handle->rawMarginX = handle->x - handle->rawX;
		handle->rawMarginY = handle->y - handle->rawY;
		handle->rawMarginW = handle->w - handle->rawW;
		handle->rawMarginH = handle->h - handle->rawH;

		if (spec->Flags & WindowFlag::Visible)
		{
			if (spec->Flags & WindowFlag::Maximized)
				ShowWindow(handle->HWnd, SW_MAXIMIZE);
			else if (spec->Flags & WindowFlag::Minimized)
				ShowWindow(handle->HWnd, SW_MINIMIZE);
			else
				ShowWindow(handle->HWnd, SW_NORMAL);
		}
		else
		{
			ShowWindow(handle->HWnd, SW_HIDE);
		}
		return true;
	}

	void IntDeInitHandle(Context* context, Handle* handle)
	{
		if (!context || !handle)
			return;
		RemovePropW(handle->HWnd, L"TestHandle");
		DestroyWindow(handle->HWnd);
		handle->HWnd = nullptr;
	}

	LRESULT HelperWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
	{
		if (g_Context && g_Context->SeparateThread)
		{
			++((SeparateThreadContext*) g_Context)->MessageCount;
			((SeparateThreadContext*) g_Context)->MessageCount.notify_all();
		}
		switch (Msg)
		{
		case Wnd_WM_CREATE_WINDOW:
		{
			if (!wParam || !lParam)
				return FALSE;

			SeparateThreadHandle* stHandle = new SeparateThreadHandle();
			if (!IntInitHandle(g_Context, stHandle, (const Spec*) lParam))
			{
				delete stHandle;
				return FALSE;
			}
			SeparateThreadContext* stContext = (SeparateThreadContext*) g_Context;
			stContext->Mtx.Lock();
			stContext->Windows.emplace_back(stHandle);
			stContext->Mtx.Unlock();
			*(Handle**) wParam = stHandle;
			return TRUE;
		}
		case Wnd_WM_DESTROY_WINDOW:
		{
			if (!wParam)
				return TRUE;

			IntDeInitHandle(g_Context, (Handle*) wParam);
			SeparateThreadContext* stContext = (SeparateThreadContext*) g_Context;
			stContext->Mtx.Lock();
			std::erase(stContext->Windows, (Handle*) wParam);
			stContext->Mtx.Unlock();
			delete (SeparateThreadHandle*) wParam;
			return TRUE;
		}
		case Wnd_WM_DEINIT:
			PostQuitMessage(0);
			return TRUE;
		}
		return DefWindowProcW(hWnd, Msg, wParam, lParam);
	}

	LRESULT WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
	{
		if (g_Context && g_Context->SeparateThread)
		{
			++((SeparateThreadContext*) g_Context)->MessageCount;
			((SeparateThreadContext*) g_Context)->MessageCount.notify_all();
		}
		Handle* window = (Handle*) GetPropW(hWnd, L"TestHandle");
		if (!window)
			return DefWindowProcW(hWnd, Msg, wParam, lParam);

		switch (Msg)
		{
		case WM_CLOSE:
			if (g_Context->SeparateThread)
				((SeparateThreadHandle*) window)->Flags |= WindowFlag::WantsClose;
			else
				((SameThreadHandle*) window)->Flags |= WindowFlag::WantsClose;
			return 0;
		case WM_MOVE:
			if (g_Context->SeparateThread)
				((SeparateThreadHandle*) window)->Mtx.Lock();
			window->x    = (int32_t) (short) LOWORD(lParam);
			window->y    = (int32_t) (short) HIWORD(lParam);
			window->rawX = window->x - window->rawMarginX;
			window->rawY = window->y - window->rawMarginY;
			if (g_Context->SeparateThread)
				((SeparateThreadHandle*) window)->Mtx.Unlock();
			return 0;
		case WM_SIZE:
			if (g_Context->SeparateThread)
			{
				SeparateThreadHandle* stHandle = (SeparateThreadHandle*) window;
				switch (wParam)
				{
				case SIZE_RESTORED:
					stHandle->Flags &= ~WindowFlag::MinMax;
					break;
				case SIZE_MINIMIZED:
					stHandle->Flags &= ~WindowFlag::MinMax;
					stHandle->Flags |= WindowFlag::Minimized;
					break;
				case SIZE_MAXIMIZED:
					stHandle->Flags &= ~WindowFlag::MinMax;
					stHandle->Flags |= WindowFlag::Maximized;
					break;
				}
				stHandle->Mtx.Lock();
			}
			else
			{
				SameThreadHandle* stHandle = (SameThreadHandle*) window;
				switch (wParam)
				{
				case SIZE_RESTORED:
					stHandle->Flags &= ~WindowFlag::MinMax;
					break;
				case SIZE_MINIMIZED:
					stHandle->Flags &= ~WindowFlag::MinMax;
					stHandle->Flags |= WindowFlag::Minimized;
					break;
				case SIZE_MAXIMIZED:
					stHandle->Flags &= ~WindowFlag::MinMax;
					stHandle->Flags |= WindowFlag::Maximized;
					break;
				}
			}
			window->w    = (uint32_t) (short) LOWORD(lParam);
			window->h    = (uint32_t) (short) HIWORD(lParam);
			window->rawW = window->w + window->rawMarginW;
			window->rawH = window->h + window->rawMarginH;
			if (g_Context->SeparateThread)
				((SeparateThreadHandle*) window)->Mtx.Unlock();
			return 0;
		}
		return DefWindowProcW(hWnd, Msg, wParam, lParam);
	}
} // namespace Wnd

extern "C" VkResult vkGetMemoryWin32HandleKHR(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* pGetWin32HandleInfo, HANDLE* pHandle)
{
	auto func = (PFN_vkGetMemoryWin32HandleKHR) vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR");
	if (!func)
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	return func(device, pGetWin32HandleInfo, pHandle);
}

extern "C" VkResult vkGetMemoryWin32HandlePropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE handle, VkMemoryWin32HandlePropertiesKHR* pMemoryWin32HandleProperties)
{
	auto func = (PFN_vkGetMemoryWin32HandlePropertiesKHR) vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR");
	if (!func)
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	return func(device, handleType, handle, pMemoryWin32HandleProperties);
}

extern "C" VkResult vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
	auto func = (PFN_vkCreateWin32SurfaceKHR) vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
	if (!func)
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	return func(instance, pCreateInfo, pAllocator, pSurface);
}

extern "C" VkResult vkGetSemaphoreWin32HandleKHR(VkDevice device, const VkSemaphoreGetWin32HandleInfoKHR* pGetWin32HandleInfo, HANDLE* pHandle)
{
	auto func = (PFN_vkGetSemaphoreWin32HandleKHR) vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR");
	if (!func)
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	return func(device, pGetWin32HandleInfo, pHandle);
}

extern "C" VkResult vkImportSemaphoreWin32HandleKHR(VkDevice device, const VkImportSemaphoreWin32HandleInfoKHR* pImportSemaphoreWin32HandleInfo)
{
	auto func = (PFN_vkImportSemaphoreWin32HandleKHR) vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR");
	if (!func)
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	return func(device, pImportSemaphoreWin32HandleInfo);
}