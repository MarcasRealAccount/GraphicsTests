#include <Build.h>

#include "CSwap.h"

#include <cstdint>

#include <atomic>
#include <bit>
#include <thread>

#if BUILD_IS_CONFIG_DEBUG
	#include <stdexcept>
#endif

#include <d3d11_4.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <Presentation.h>

#include <Concurrency/Mutex.h> // Faster mutexes

#pragma comment(lib, "Synchronization.lib")

// Provided by VK_KHR_external_memory_win32
typedef struct VkMemoryWin32HandlePropertiesKHR
{
	VkStructureType sType;
	void*           pNext;
	uint32_t        memoryTypeBits;
} VkMemoryWin32HandlePropertiesKHR;

typedef struct VkImportMemoryWin32HandleInfoKHR
{
	VkStructureType                    sType;
	const void*                        pNext;
	VkExternalMemoryHandleTypeFlagBits handleType;
	HANDLE                             handle;
	LPCWSTR                            name;
} VkImportMemoryWin32HandleInfoKHR;

typedef VkResult (*PFN_vkGetMemoryWin32HandlePropertiesKHR)(
	VkDevice                           device,
	VkExternalMemoryHandleTypeFlagBits handleType,
	HANDLE                             handle,
	VkMemoryWin32HandlePropertiesKHR*  pMemoryWin32HandleProperties);

// Provided by VK_KHR_external_semaphore_win32
typedef struct VkImportSemaphoreWin32HandleInfoKHR
{
	VkStructureType                       sType;
	const void*                           pNext;
	VkSemaphore                           semaphore;
	VkSemaphoreImportFlags                flags;
	VkExternalSemaphoreHandleTypeFlagBits handleType;
	HANDLE                                handle;
	LPCWSTR                               name;
} VkImportSemaphoreWin32HandleInfoKHR;

typedef VkResult (*PFN_vkImportSemaphoreWin32HandleKHR)(
	VkDevice                                   device,
	const VkImportSemaphoreWin32HandleInfoKHR* pImportSemaphoreWin32HandleInfo);

//
// Supported Formats
// DXGI_FORMAT_B8G8R8A8_UNORM	  => VK_FORMAT_B8G8R8A8_UNORM
// DXGI_FORMAT_R8G8B8A8_UNORM	  => VK_FORMAT_R8G8B8A8_UNORM
// DXGI_FORMAT_R16G16B16A16_FLOAT => VK_FORMAT_R16G16B16A16_SFLOAT
// DXGI_FORMAT_R10G10B10A2_UNORM  => VK_FORMAT_A2B10G10R10_UNORM_PACK32
//
// Supported ColorSpaces
// DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709    => VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
// DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709    => VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
// DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 => VK_COLOR_SPACE_HDR10_ST2084_EXT
//
// Supported Pairs
// DXGI_FORMAT_B8G8R8A8_UNORM + DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
// DXGI_FORMAT_R8G8B8A8_UNORM + DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
// DXGI_FORMAT_R16G16B16A16_UNORM + DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
// DXGI_FORMAT_R16G16B16A16_UNORM + DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
// DXGI_FORMAT_R10G10B10A2_UNORM + DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
// DXGI_FORMAT_R10G10B10A2_UNORM + DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
//
static constexpr VkSurfaceFormatKHR c_SupportedFormatPairs[] {
	{VK_FORMAT_B8G8R8A8_UNORM,            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR      },
	{ VK_FORMAT_R8G8B8A8_UNORM,           VK_COLOR_SPACE_SRGB_NONLINEAR_KHR      },
	{ VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_SRGB_NONLINEAR_KHR      },
	{ VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
	{ VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR      },
	{ VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT        },
};

static constexpr uint32_t c_SupportedFormatPairCount = sizeof(c_SupportedFormatPairs) / sizeof(*c_SupportedFormatPairs);

static constexpr VkPresentModeKHR c_SupportedPresentModes[] {
	VK_PRESENT_MODE_FIFO_KHR,
	VK_PRESENT_MODE_MAILBOX_KHR
};
static constexpr uint32_t c_SupportedPresentModeCount = sizeof(c_SupportedPresentModes) / sizeof(*c_SupportedPresentModes);

DXGI_FORMAT ToDXGIFormat(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return DXGI_FORMAT_R10G10B10A2_UNORM;
	default: return DXGI_FORMAT_B8G8R8A8_UNORM;
	}
}

DXGI_COLOR_SPACE_TYPE ToDXGIColorSpace(VkColorSpaceKHR colorSpace)
{
	switch (colorSpace)
	{
	case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
	case VK_COLOR_SPACE_HDR10_ST2084_EXT: return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	default: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	}
}

DXGI_ALPHA_MODE ToDXGIAlphaMode(VkCompositeAlphaFlagBitsKHR compositeAlpha)
{
	switch (compositeAlpha)
	{
	case VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR: return DXGI_ALPHA_MODE_IGNORE;
	case VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR: return DXGI_ALPHA_MODE_PREMULTIPLIED;
	case VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR: return DXGI_ALPHA_MODE_STRAIGHT;
	case VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR: return DXGI_ALPHA_MODE_IGNORE;
	default: return DXGI_ALPHA_MODE_IGNORE;
	}
}

static constexpr uint32_t c_WinCSMaxBufferCount = 8;

static constexpr uint8_t c_WinCSBufferRenderable      = 0;
static constexpr uint8_t c_WinCSBufferRendering       = 1;
static constexpr uint8_t c_WinCSBufferDoubleRendering = 2;
static constexpr uint8_t c_WinCSBufferWaiting         = 3;
static constexpr uint8_t c_WinCSBufferDoubleWaiting   = 4;
static constexpr uint8_t c_WinCSBufferPresentable     = 5;
static constexpr uint8_t c_WinCSBufferPresenting      = 6;

//
// EventThread1: Either waits for a buffer retirement or a buffer rendered.
//	Buffer rendered is a buffer that was specified to be done after a semaphore being signaled
// EventThread2: Wait for VBlank to immediately present. Always transitions a single Presentable buffer to Presenting
//
// FIFO Transitions:
//	vkAcquireNextImageKHR:
//		Renderable:      Transition to Rendering, Signal semaphore and fence immediately
//		Rendering:       Not acquired
//      DoubleRendering: Not acquired
//		Waiting:         Not acquired
//      DoubleWaiting:   Not acquired
//      Presentable:     Not acquired
//      Presenting:      Not acquired
//	vkQueuePresentKHR:
//		Renderable:      Not presentable
//		Rendering:       If pPresentInfo->waitSemaphoreCount > 0 transition to Waiting, otherwise transition to Presentable
//      DoubleRendering: Not used
//      Waiting:         Not presentable
//      DoubleWaiting:   Not presentable
//      Presentable:     Not presentable
//      Presenting:      Not presentable
//	EventThread1:
//		OnBufferRetire:   Transition to Renderable if buffer is Presenting
//		OnBufferRendered: Transition to Presentable if buffer is Waiting
//	EventThread2: Transition oldest Presentable to Presenting
//
// Mailbox Transitions:
//	vkAcquireNextImageKHR:
//		Renderable:      Transition to Rendering, Signal semaphore and fence immediately
//		Rendering:       Not acquired
//		DoubleRendering: Not acquired
//		Waiting:         Transition to DoubleRendering, Signal semaphore and fence on bufferRendered
//		DoubleWaiting:   Not acquired
//		Presentable:     Transition to Rendering, Signal semaphore and fence immediately
//		Presenting:      Not acquired
//	vkQueuePresentKHR:
//		Renderable:      Not presentable
//		Rendering:       If pPresentInfo->waitSemaphoreCount > 0 transition to Waiting, otherwise transition to Presentable
//		DoubleRendering: If pPresentInfo->waitSemaphoreCount > 0 transition to DoubleWaiting, otherwise transition to Presentable
//		Waiting:         Not presentable
//		DoubleWaiting:   Not presentable
//		Presentable:     Not presentable
//		Presenting:      Not presentable
//	EventThread1:
//		OnBufferRetire:   Transition to Renderable if buffer is Presenting
//		OnBufferRendered: Transition to Presentable if buffer is Waiting, if buffer is DoubleWaiting, skip transition unless fence value is the current expected fence value
//	EventThread2: Transition newest Presentable to Presenting
//

struct WinCSSurface
{
	HINSTANCE             HInstance;
	HWND                  HWnd;
	ID3D11Device5*        D3D11Device;
	IDCompositionDevice*  DCompDevice;
	IPresentationManager* Manager;
	HANDLE                SurfaceHandle;
	IPresentationSurface* Surface;
	IUnknown*             DCompSurface;
	IDCompositionTarget*  DCompTarget;
	IDCompositionVisual*  DCompVisual;

	struct WinCSSwapchain* Swapchain;
};

struct WinCSSwapchainBuffer
{
	IPresentationBuffer* PresentationBuffer;
	ID3D11Texture2D*     Texture;
	HANDLE               TextureHandle;
	ID3D11Fence*         PresentFence;
	HANDLE               PresentFenceHandle;
	UINT64               PresentFenceValue;
	VkImage              vkImage;
	VkDeviceMemory       vkImageMemory;
	VkSemaphore          vkTimeline;
	std::atomic_bool     Invalidated;
	std::atomic_uint8_t  State;
};

struct WinCSSwapchain
{
	WinCSSurface* Surface;

	std::atomic_uint32_t UsableBufferCount;
	uint32_t             BufferCount;
	uint32_t             BufferIndex;
	WinCSSwapchainBuffer Buffers[c_WinCSMaxBufferCount];
	uint32_t             PresentQueue[c_WinCSMaxBufferCount];
	HANDLE               Events[3 + c_WinCSMaxBufferCount]; // [0]: Lost, [1]: Terminate, [2]: OnBufferRetire, [3,...]: OnBufferRendered
	ID3D11Fence*         RetireFence;

	UINT                          Width;
	UINT                          Height;
	DXGI_FORMAT                   Format;
	DXGI_COLOR_SPACE_TYPE         ColorSpace;
	DXGI_ALPHA_MODE               AlphaMode;
	VkPresentModeKHR              PresentMode;
	VkSurfaceTransformFlagBitsKHR Transform;

	VkQueue Queue;

	Concurrency::Mutex Mtx;
	std::atomic_bool   EventThreadsRunning;
	std::thread        EventThread1;
	std::thread        EventThread2; // Could potentially be a single global thread that presents for every created swapchain
};

static void WinCSEventThreadFunc1(WinCSSwapchain* swapchain);
static void WinCSEventThreadFunc2(WinCSSwapchain* swapchain);

VkResult vkCreateWinCSSurfaceEXT(
	VkInstance                         instance,
	const VkWinCSSurfaceCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks*       pAllocator,
	VkSurfaceKHR*                      pSurface)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!instance || !pCreateInfo || !pSurface || !pCreateInfo->hwnd)
		throw std::runtime_error("Nullptrs passed to vkCreateWinCSSurfaceEXT");
	if (pCreateInfo->sType != VK_STRUCTURE_TYPE_WINCS_SURFACE_CREATE_INFO_EXT)
		throw std::runtime_error("Wrong sType for VkWinCSSurfaceCreateInfoEXT");
	if (pAllocator && !pAllocator->pfnAllocation)
		throw std::runtime_error("Broken allocator pased to vkCreateWinCSSurfaceEXT");
#endif
	VkResult result = VK_ERROR_INITIALIZATION_FAILED;
	HRESULT  hr;

	WinCSSurface surface;
	surface.HInstance     = pCreateInfo->hinstance;
	surface.HWnd          = pCreateInfo->hwnd;
	surface.D3D11Device   = nullptr;
	surface.DCompDevice   = nullptr;
	surface.Manager       = nullptr;
	surface.SurfaceHandle = nullptr;
	surface.Surface       = nullptr;
	surface.DCompSurface  = nullptr;
	surface.DCompTarget   = nullptr;
	surface.DCompVisual   = nullptr;
	surface.Swapchain     = nullptr;
	do
	{
		IDXGIDevice*          dxgiDevice          = nullptr;
		IPresentationFactory* presentationFactory = nullptr;
		ID3D11Device*         d3d11Device         = nullptr;

		hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&d3d11Device,
			nullptr,
			nullptr);
		if (hr < S_OK)
			break;

		hr = d3d11Device->QueryInterface(&surface.D3D11Device);
		d3d11Device->Release();
		d3d11Device = nullptr;
		if (hr < 0)
			break;

		hr = surface.D3D11Device->QueryInterface(&dxgiDevice);
		if (hr < 0)
			break;

		hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), (void**) &surface.DCompDevice);
		dxgiDevice->Release();
		dxgiDevice = nullptr;
		if (hr < S_OK)
			break;

		hr = CreatePresentationFactory(surface.D3D11Device, __uuidof(IPresentationFactory), (void**) &presentationFactory);
		if (hr < S_OK)
			break;

		if (!presentationFactory->IsPresentationSupported())
		{
			presentationFactory->Release();
			presentationFactory = nullptr;
			break;
		}

		hr = presentationFactory->CreatePresentationManager(&surface.Manager);
		presentationFactory->Release();
		presentationFactory = nullptr;
		if (hr < S_OK)
			break;

		hr = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surface.SurfaceHandle);
		if (hr < S_OK)
			break;

		hr = surface.Manager->CreatePresentationSurface(surface.SurfaceHandle, &surface.Surface);
		if (hr < S_OK)
			break;

		hr = surface.DCompDevice->CreateSurfaceFromHandle(surface.SurfaceHandle, &surface.DCompSurface);
		if (hr < S_OK)
			break;

		hr = surface.DCompDevice->CreateTargetForHwnd(surface.HWnd, TRUE, &surface.DCompTarget);
		if (hr < S_OK)
			break;

		hr = surface.DCompDevice->CreateVisual(&surface.DCompVisual);
		if (hr < S_OK)
			break;

		hr = surface.DCompVisual->SetContent(surface.DCompSurface);
		if (hr < S_OK)
			break;

		hr = surface.DCompTarget->SetRoot(surface.DCompVisual);
		if (hr < S_OK)
			break;

		hr = surface.DCompDevice->Commit();
		if (hr < S_OK)
			break;

		hr = surface.DCompDevice->WaitForCommitCompletion();
		if (hr < S_OK)
			break;

		WinCSSurface* allocedSurface = nullptr;
		if (pAllocator)
			allocedSurface = (WinCSSurface*) pAllocator->pfnAllocation(pAllocator->pUserData, sizeof(WinCSSurface), alignof(WinCSSurface), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
		else
			allocedSurface = new WinCSSurface();
		if (!allocedSurface)
		{
			result = VK_ERROR_OUT_OF_HOST_MEMORY;
			break;
		}
		memcpy(allocedSurface, &surface, sizeof(WinCSSurface));
		*pSurface = (VkSurfaceKHR) allocedSurface;
		return VK_SUCCESS;
	}
	while (false);
	if (surface.DCompVisual)
		surface.DCompVisual->Release();
	if (surface.DCompTarget)
		surface.DCompTarget->Release();
	if (surface.DCompSurface)
		surface.DCompSurface->Release();
	if (surface.Surface)
		surface.Surface->Release();
	if (surface.SurfaceHandle)
		CloseHandle(surface.SurfaceHandle);
	if (surface.Manager)
		surface.Manager->Release();
	if (surface.DCompDevice)
		surface.DCompDevice->Release();
	if (surface.D3D11Device)
		surface.D3D11Device->Release();
	return result;
}

void wincs_surface_vkDestroySurfaceKHR(
	VkInstance                   instance,
	VkSurfaceKHR                 surface,
	const VkAllocationCallbacks* pAllocator)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!instance)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkDestroySurfaceKHR");
	if (pAllocator && !pAllocator->pfnFree)
		throw std::runtime_error("Broken allocator pased to wincs_surface_vkDestroySurfaceKHR");
#endif
	if (!surface)
		return;

	WinCSSurface* pSurface = (WinCSSurface*) surface;
	pSurface->DCompVisual->Release();
	pSurface->DCompTarget->Release();
	pSurface->DCompSurface->Release();
	pSurface->Surface->Release();
	CloseHandle(pSurface->SurfaceHandle);
	pSurface->Manager->Release();
	pSurface->DCompDevice->Release();
	pSurface->D3D11Device->Release();
	if (pAllocator)
		pAllocator->pfnFree(pAllocator->pUserData, pSurface);
	else
		delete pSurface;
}

VkResult wincs_surface_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
	VkPhysicalDevice          physicalDevice,
	VkSurfaceKHR              surface,
	VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!physicalDevice || !surface || !pSurfaceCapabilities)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
#endif

	WinCSSurface* pSurface = (WinCSSurface*) surface;

	RECT clientRect {};
	if (!GetClientRect(pSurface->HWnd, &clientRect))
		return VK_ERROR_SURFACE_LOST_KHR;
	uint32_t width  = (uint32_t) (clientRect.right - clientRect.left);
	uint32_t height = (uint32_t) (clientRect.bottom - clientRect.top);

	/*VkSurfaceTransformFlagsKHR supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR | VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR | VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR;
	if (width == height)
		supportedTransforms |= VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR | VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR;*/

	pSurfaceCapabilities->minImageCount           = 2;
	pSurfaceCapabilities->maxImageCount           = c_WinCSMaxBufferCount;
	pSurfaceCapabilities->currentExtent           = { width, height };
	pSurfaceCapabilities->minImageExtent          = { 1, 1 };
	pSurfaceCapabilities->maxImageExtent          = { 0xFFFF, 0xFFFF };
	pSurfaceCapabilities->maxImageArrayLayers     = 1;
	pSurfaceCapabilities->supportedTransforms     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	pSurfaceCapabilities->currentTransform        = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
	pSurfaceCapabilities->supportedUsageFlags     = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	return VK_SUCCESS;
}

VkResult wincs_surface_vkGetPhysicalDeviceSurfaceFormatsKHR(
	VkPhysicalDevice    physicalDevice,
	VkSurfaceKHR        surface,
	uint32_t*           pSurfaceFormatCount,
	VkSurfaceFormatKHR* pSurfaceFormats)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!physicalDevice || !surface || !pSurfaceFormatCount)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkGetPhysicalDeviceSurfaceFormatsKHR");
#endif

	if (!pSurfaceFormats)
	{
		*pSurfaceFormatCount = c_SupportedFormatPairCount;
		return VK_SUCCESS;
	}
	if (*pSurfaceFormatCount == 0)
		return VK_INCOMPLETE;
	if (*pSurfaceFormatCount < c_SupportedFormatPairCount)
	{
		for (uint32_t i = 0; i < *pSurfaceFormatCount; ++i)
			pSurfaceFormats[i] = c_SupportedFormatPairs[i];
		return VK_INCOMPLETE;
	}
	*pSurfaceFormatCount = c_SupportedFormatPairCount;
	for (uint32_t i = 0; i < c_SupportedFormatPairCount; ++i)
		pSurfaceFormats[i] = c_SupportedFormatPairs[i];
	return VK_SUCCESS;
}

VkResult wincs_surface_vkGetPhysicalDeviceSurfacePresentModesKHR(
	VkPhysicalDevice  physicalDevice,
	VkSurfaceKHR      surface,
	uint32_t*         pPresentModeCount,
	VkPresentModeKHR* pPresentModes)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!physicalDevice || !surface || !pPresentModeCount)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkGetPhysicalDeviceSurfacePresentModesKHR");
#endif

	if (!pPresentModes)
	{
		*pPresentModeCount = c_SupportedPresentModeCount;
		return VK_SUCCESS;
	}
	if (*pPresentModeCount == 0)
		return VK_INCOMPLETE;
	if (*pPresentModeCount < c_SupportedPresentModeCount)
	{
		for (uint32_t i = 0; i < *pPresentModeCount; ++i)
			pPresentModes[i] = c_SupportedPresentModes[i];
		return VK_INCOMPLETE;
	}
	*pPresentModeCount = c_SupportedPresentModeCount;
	for (uint32_t i = 0; i < c_SupportedPresentModeCount; ++i)
		pPresentModes[i] = c_SupportedPresentModes[i];
	return VK_SUCCESS;
}

VkResult wincs_surface_vkGetPhysicalDeviceSurfaceSupportKHR(
	VkPhysicalDevice physicalDevice,
	uint32_t         queueFamilyIndex,
	VkSurfaceKHR     surface,
	VkBool32*        pSupported)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!physicalDevice || !surface || !pSupported)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkGetPhysicalDeviceSurfaceSupportKHR");
#endif

	WinCSSurface* pSurface = (WinCSSurface*) surface;

	uint32_t queueCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, nullptr);
	if (!queueCount)
		return VK_ERROR_SURFACE_LOST_KHR;
	if (queueFamilyIndex >= queueCount)
	{
		*pSupported = false;
		return VK_SUCCESS;
	}
	VkQueueFamilyProperties* queueProps = new VkQueueFamilyProperties[queueCount];
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps);

	if (!(queueProps->queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
	{
		delete[] queueProps;
		*pSupported = false;
		return VK_SUCCESS;
	}
	delete[] queueProps;

	IDXGIDevice*      dxgiDevice = nullptr;
	IDXGIAdapter*     adapter    = nullptr;
	DXGI_ADAPTER_DESC adapterDesc {};

	HRESULT hr = pSurface->D3D11Device->QueryInterface(&dxgiDevice);
	if (hr < 0)
		return VK_ERROR_SURFACE_LOST_KHR;
	hr = dxgiDevice->GetAdapter(&adapter);
	dxgiDevice->Release();
	dxgiDevice = nullptr;
	if (hr < 0)
		return VK_ERROR_SURFACE_LOST_KHR;
	hr = adapter->GetDesc(&adapterDesc);
	adapter->Release();
	adapter = nullptr;
	if (hr < 0)
		return VK_ERROR_SURFACE_LOST_KHR;

	VkPhysicalDeviceVulkan11Properties vk11Props {};
	vk11Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
	vk11Props.pNext = nullptr;
	VkPhysicalDeviceProperties2 props {};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props.pNext = &vk11Props;
	vkGetPhysicalDeviceProperties2(physicalDevice, &props);

	uint64_t physicalDeviceLUID;
	uint64_t adapterLUID;
	memcpy(&physicalDeviceLUID, vk11Props.deviceLUID, sizeof(LUID));
	memcpy(&adapterLUID, &adapterDesc.AdapterLuid, sizeof(LUID));

	*pSupported = adapterLUID == physicalDeviceLUID;
	return VK_SUCCESS;
}

VkResult wincs_surface_vkCreateSwapchainKHR(
	VkDevice                        device,
	const VkSwapchainCreateInfoKHR* pCreateInfo,
	const VkAllocationCallbacks*    pAllocator,
	VkSwapchainKHR*                 pSwapchain)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!device || !pCreateInfo || !pSwapchain || !pCreateInfo->surface)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkCreateSwapchainKHR");
	if (pCreateInfo->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR)
		throw std::runtime_error("Wrong sType for VkSwapchainCreateInfoKHR");
	if (pAllocator && !pAllocator->pfnAllocation)
		throw std::runtime_error("Broken allocator pased to wincs_surface_vkCreateSwapchainKHR");
#endif

	VkResult result = VK_ERROR_INITIALIZATION_FAILED;

	auto pVkGetMemoryWin32HandlePropertiesKHR = (PFN_vkGetMemoryWin32HandlePropertiesKHR) vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR");
	auto pVkImportSemaphoreWin32HandleKHR     = (PFN_vkImportSemaphoreWin32HandleKHR) vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR");
	if (!pVkGetMemoryWin32HandlePropertiesKHR || !pVkImportSemaphoreWin32HandleKHR)
		return VK_ERROR_EXTENSION_NOT_PRESENT;

	if (pCreateInfo->oldSwapchain)
	{
		do
		{
		}
		while (false);
	}
	else
	{
		WinCSSwapchain* swapchain = nullptr;
		HRESULT         hr;
		IDXGIResource1* dxgiResource = nullptr;
		do
		{
			if (pCreateInfo->minImageCount >= c_WinCSMaxBufferCount ||
				pCreateInfo->imageExtent.width == 0 ||
				pCreateInfo->imageExtent.height == 0 ||
				pCreateInfo->imageArrayLayers != 1)
				break;

			{
				bool formatPairSupported = false;
				for (uint32_t i = 0; i < c_SupportedFormatPairCount; ++i)
				{
					auto& pair = c_SupportedFormatPairs[i];
					if (pCreateInfo->imageFormat == pair.format && pCreateInfo->imageColorSpace == pair.colorSpace)
					{
						formatPairSupported = true;
						break;
					}
				}
				if (!formatPairSupported)
					break;
			}
			{
				bool presentSupported = false;
				for (uint32_t i = 0; i < c_SupportedPresentModeCount; ++i)
				{
					if (pCreateInfo->presentMode == c_SupportedPresentModes[i])
					{
						presentSupported = true;
						break;
					}
				}
				if (!presentSupported)
					break;
			}

			WinCSSurface* surface = (WinCSSurface*) pCreateInfo->surface;
			if (surface->Swapchain)
			{
				result = VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
				break;
			}

			const VkWinCSSwapchainQueueInfoEXT* pQueueInfo = nullptr;
			for (const void* pNext = pCreateInfo->pNext; pNext;)
			{
				if (((VkBaseInStructure*) pNext)->sType == VK_STRUCTURE_TYPE_WINCS_SWAPCHAIN_QUEUE_INFO_EXT)
					pQueueInfo = (const VkWinCSSwapchainQueueInfoEXT*) pNext;
				pNext = ((VkBaseInStructure*) pNext)->pNext;
			}
			if (!pQueueInfo)
				break;

			if (pAllocator)
				swapchain = (WinCSSwapchain*) pAllocator->pfnAllocation(pAllocator->pUserData, sizeof(WinCSSwapchain), alignof(WinCSSwapchain), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
			else
				swapchain = new WinCSSwapchain();
			if (!swapchain)
			{
				result = VK_ERROR_OUT_OF_HOST_MEMORY;
				break;
			}

			surface->Swapchain = swapchain;
			swapchain->Surface = surface;

			swapchain->Queue = pQueueInfo->queue;

			swapchain->Width       = pCreateInfo->imageExtent.width;
			swapchain->Height      = pCreateInfo->imageExtent.height;
			swapchain->Format      = ToDXGIFormat(pCreateInfo->imageFormat);
			swapchain->ColorSpace  = ToDXGIColorSpace(pCreateInfo->imageColorSpace);
			swapchain->AlphaMode   = ToDXGIAlphaMode(pCreateInfo->compositeAlpha);
			swapchain->PresentMode = pCreateInfo->presentMode;
			swapchain->Transform   = pCreateInfo->preTransform;
			if (pCreateInfo->minImageCount < 2)
				swapchain->BufferCount = 2;
			else
				swapchain->BufferCount = pCreateInfo->minImageCount;
			swapchain->UsableBufferCount = swapchain->BufferCount;
			swapchain->BufferIndex       = 0;
			swapchain->RetireFence       = nullptr;
			for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
			{
				swapchain->PresentQueue[i] = ~0U;

				auto& buffer              = swapchain->Buffers[i];
				buffer.PresentationBuffer = nullptr;
				buffer.Texture            = nullptr;
				buffer.TextureHandle      = nullptr;
				buffer.PresentFence       = nullptr;
				buffer.PresentFenceHandle = nullptr;
				buffer.PresentFenceValue  = 0;
				buffer.vkImage            = nullptr;
				buffer.vkImageMemory      = nullptr;
				buffer.vkTimeline         = nullptr;
				buffer.Invalidated        = false;
				buffer.State              = c_WinCSBufferRenderable;
			}
			for (uint32_t i = 0; i < 3 + c_WinCSMaxBufferCount; ++i)
				swapchain->Events[i] = nullptr;
			hr = surface->Manager->GetLostEvent(&swapchain->Events[0]);
			if (hr < S_OK)
				break;
			hr = surface->Manager->GetPresentRetiringFence(__uuidof(ID3D11Fence), (void**) &swapchain->RetireFence);
			if (hr < S_OK)
				break;
			swapchain->Events[1] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			swapchain->Events[2] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!swapchain->Events[1] || !swapchain->Events[2])
				break;

			uint32_t i = 0;
			for (; i < swapchain->BufferCount; ++i)
			{
				auto& buffer             = swapchain->Buffers[i];
				swapchain->Events[3 + i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				if (!swapchain->Events[3 + i])
					break;

				D3D11_TEXTURE2D_DESC textureDesc {
					.Width          = swapchain->Width,
					.Height         = swapchain->Height,
					.MipLevels      = 1,
					.ArraySize      = 1,
					.Format         = swapchain->Format,
					.SampleDesc     = {1, 0},
					.Usage          = D3D11_USAGE_DEFAULT,
					.BindFlags      = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
					.CPUAccessFlags = 0,
					.MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE
				};
				hr = surface->D3D11Device->CreateTexture2D(&textureDesc, nullptr, &buffer.Texture);
				if (hr < S_OK)
					break;
				hr = surface->Manager->AddBufferFromResource(buffer.Texture, &buffer.PresentationBuffer);
				if (hr < S_OK)
					break;

				hr = buffer.Texture->QueryInterface(&dxgiResource);
				if (hr < S_OK)
					break;
				hr = dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &buffer.TextureHandle);
				dxgiResource->Release();
				dxgiResource = nullptr;
				if (hr < S_OK)
					break;

				hr = surface->D3D11Device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**) &buffer.PresentFence);
				if (hr < S_OK)
					break;
				hr = buffer.PresentFence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &buffer.PresentFenceHandle);
				if (hr < S_OK)
					break;

				VkMemoryWin32HandlePropertiesKHR handleProps {
					.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR,
					.pNext = nullptr
				};
				result = pVkGetMemoryWin32HandlePropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, buffer.TextureHandle, &handleProps);
				if (result < VK_SUCCESS)
					break;

				VkExternalMemoryImageCreateInfo emiCreateInfo {
					.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
					.pNext       = nullptr,
					.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
				};
				VkImageCreateInfo iCreateInfo {
					.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
					.pNext                 = &emiCreateInfo,
					.flags                 = 0,
					.imageType             = VK_IMAGE_TYPE_2D,
					.format                = pCreateInfo->imageFormat,
					.extent                = {swapchain->Width, swapchain->Height, 1},
					.mipLevels             = 1,
					.arrayLayers           = 1,
					.samples               = VK_SAMPLE_COUNT_1_BIT,
					.tiling                = VK_IMAGE_TILING_OPTIMAL,
					.usage                 = pCreateInfo->imageUsage,
					.sharingMode           = pCreateInfo->imageSharingMode,
					.queueFamilyIndexCount = pCreateInfo->queueFamilyIndexCount,
					.pQueueFamilyIndices   = pCreateInfo->pQueueFamilyIndices,
					.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
				};
				result = vkCreateImage(device, &iCreateInfo, pAllocator, &buffer.vkImage);
				if (result < VK_SUCCESS)
					break;

				VkMemoryRequirements mReq {};
				vkGetImageMemoryRequirements(device, buffer.vkImage, &mReq);

				VkImportMemoryWin32HandleInfoKHR imHandleInfo {
					.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
					.pNext      = nullptr,
					.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
					.handle     = buffer.TextureHandle,
					.name       = nullptr
				};
				VkMemoryAllocateInfo mAllocInfo {
					.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
					.pNext           = &imHandleInfo,
					.allocationSize  = mReq.size,
					.memoryTypeIndex = (uint32_t) std::countr_zero(handleProps.memoryTypeBits)
				};
				result = vkAllocateMemory(device, &mAllocInfo, pAllocator, &buffer.vkImageMemory);
				if (result < VK_SUCCESS)
					break;
				result = vkBindImageMemory(device, buffer.vkImage, buffer.vkImageMemory, 0);
				if (result < VK_SUCCESS)
					break;

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
				result = vkCreateSemaphore(device, &sCreateInfo, pAllocator, &buffer.vkTimeline);
				if (result < VK_SUCCESS)
					break;

				VkImportSemaphoreWin32HandleInfoKHR isHandleInfo {
					.sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
					.pNext      = nullptr,
					.semaphore  = buffer.vkTimeline,
					.flags      = 0,
					.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE_BIT,
					.handle     = buffer.PresentFenceHandle,
					.name       = nullptr
				};
				result = pVkImportSemaphoreWin32HandleKHR(device, &isHandleInfo);
				if (result < VK_SUCCESS)
					break;
			}
			if (i < swapchain->BufferCount)
				break;

			swapchain->EventThreadsRunning = true;
			swapchain->EventThread1        = std::thread(&WinCSEventThreadFunc1, swapchain);
			swapchain->EventThread2        = std::thread(&WinCSEventThreadFunc2, swapchain);

			*pSwapchain = (VkSwapchainKHR) swapchain;
			return VK_SUCCESS;
		}
		while (false);
		if (swapchain)
		{
			swapchain->Surface->Swapchain = nullptr;
			for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
			{
				auto& buffer = swapchain->Buffers[i];
				if (buffer.vkTimeline)
					vkDestroySemaphore(device, buffer.vkTimeline, pAllocator);
				if (buffer.vkImage)
					vkDestroyImage(device, buffer.vkImage, pAllocator);
				if (buffer.vkImageMemory)
					vkFreeMemory(device, buffer.vkImageMemory, pAllocator);
				if (buffer.PresentFenceHandle)
					CloseHandle(buffer.PresentFenceHandle);
				if (buffer.PresentFence)
					buffer.PresentFence->Release();
				if (buffer.TextureHandle)
					CloseHandle(buffer.TextureHandle);
				if (buffer.Texture)
					buffer.Texture->Release();
				if (buffer.PresentationBuffer)
					buffer.PresentationBuffer->Release();
			}
			for (uint32_t i = 0; i < 3 + swapchain->BufferCount; ++i)
			{
				if (swapchain->Events[i])
					CloseHandle(swapchain->Events[i]);
			}
			if (swapchain->RetireFence)
				swapchain->RetireFence->Release();
			if (pAllocator)
				pAllocator->pfnFree(pAllocator->pUserData, swapchain);
			else
				free(swapchain);
		}
	}

	return result;
}

void wincs_surface_vkDestroySwapchainKHR(
	VkDevice                     device,
	VkSwapchainKHR               swapchain,
	const VkAllocationCallbacks* pAllocator)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!device)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkDestroySwapchainKHR");
	if (pAllocator && !pAllocator->pfnFree)
		throw std::runtime_error("Broken allocator pased to vkCreateWinCSSurfaceEXT");
#endif
	if (!swapchain)
		return;

	WinCSSwapchain* pSwapchain = (WinCSSwapchain*) swapchain;

	pSwapchain->Surface->Swapchain  = nullptr;
	pSwapchain->EventThreadsRunning = false;
	SetEvent(pSwapchain->Events[1]);
	pSwapchain->EventThread1.join();
	pSwapchain->EventThread2.join();

	for (uint32_t i = 0; i < pSwapchain->BufferCount; ++i)
	{
		auto& buffer = pSwapchain->Buffers[i];
		vkDestroySemaphore(device, buffer.vkTimeline, pAllocator);
		vkDestroyImage(device, buffer.vkImage, pAllocator);
		vkFreeMemory(device, buffer.vkImageMemory, pAllocator);
		CloseHandle(buffer.PresentFenceHandle);
		buffer.PresentFence->Release();
		CloseHandle(buffer.TextureHandle);
		buffer.Texture->Release();
		buffer.PresentationBuffer->Release();
	}
	for (uint32_t i = 0; i < 3 + pSwapchain->BufferCount; ++i)
		CloseHandle(pSwapchain->Events[i]);
	pSwapchain->RetireFence->Release();
	if (pAllocator)
		pAllocator->pfnFree(pAllocator->pUserData, swapchain);
	else
		delete pSwapchain;
}

VkResult wincs_surface_vkGetSwapchainImagesKHR(
	VkDevice       device,
	VkSwapchainKHR swapchain,
	uint32_t*      pSwapchainImageCount,
	VkImage*       pSwapchainImages)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!device || !swapchain || !pSwapchainImageCount)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkGetSwapchainImagesKHR");
#endif

	WinCSSwapchain* pSwapchain = (WinCSSwapchain*) swapchain;

	if (!pSwapchainImages)
	{
		*pSwapchainImageCount = pSwapchain->BufferCount;
		return VK_SUCCESS;
	}
	if (*pSwapchainImageCount == 0)
		return VK_INCOMPLETE;
	if (*pSwapchainImageCount < pSwapchain->BufferCount)
	{
		for (uint32_t i = 0; i < *pSwapchainImageCount; ++i)
			pSwapchainImages[i] = pSwapchain->Buffers[i].vkImage;
		return VK_INCOMPLETE;
	}
	*pSwapchainImageCount = pSwapchain->BufferCount;
	for (uint32_t i = 0; i < pSwapchain->BufferCount; ++i)
		pSwapchainImages[i] = pSwapchain->Buffers[i].vkImage;
	return VK_SUCCESS;
}

VkResult wincs_surface_vkAcquireNextImageKHR(
	VkDevice       device,
	VkSwapchainKHR swapchain,
	uint64_t       timeout,
	VkSemaphore    semaphore,
	VkFence        fence,
	uint32_t*      pImageIndex)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!device || !swapchain || !pImageIndex)
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkGetSwapchainImagesKHR");
#endif

	WinCSSwapchain* pSwapchain = (WinCSSwapchain*) swapchain;

	if (timeout != ~0ULL)
	{
		uint64_t now = 0;
		QueryPerformanceCounter((LARGE_INTEGER*) &now);
		uint64_t end = now + (timeout + 99) / 100;

		uint32_t compareValue = 0;
		while (end > now)
		{
			DWORD timeLeft = (DWORD) std::min<size_t>(end - now, INFINITE - 1);
			WaitOnAddress(&pSwapchain->UsableBufferCount, &compareValue, 4, timeLeft);
			if (pSwapchain->UsableBufferCount)
				break;
			QueryPerformanceCounter((LARGE_INTEGER*) &now);
		}
		if (!pSwapchain->UsableBufferCount)
			return VK_TIMEOUT;
	}
	else
	{
		uint32_t compareValue = 0;
		WaitOnAddress(&pSwapchain->UsableBufferCount, &compareValue, 4, INFINITE);
	}

	VkSemaphoreSubmitInfo wait {
		.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.pNext       = nullptr,
		.semaphore   = nullptr,
		.value       = 0,
		.stageMask   = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		.deviceIndex = 0
	};
	VkSemaphoreSubmitInfo signal {
		.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.pNext       = nullptr,
		.semaphore   = semaphore,
		.value       = 0,
		.stageMask   = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		.deviceIndex = 0
	};
	VkSubmitInfo2 submit {
		.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.pNext                    = nullptr,
		.flags                    = 0,
		.waitSemaphoreInfoCount   = 0,
		.commandBufferInfoCount   = 0,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos    = &signal
	};
	switch (pSwapchain->PresentMode)
	{
	case VK_PRESENT_MODE_FIFO_KHR:
	{
		pSwapchain->Mtx.Lock();
		uint32_t startIndex = pSwapchain->BufferIndex;
		do
		{
			uint32_t currentIndex   = pSwapchain->BufferIndex;
			auto&    buffer         = pSwapchain->Buffers[currentIndex];
			pSwapchain->BufferIndex = (currentIndex + 1) % pSwapchain->BufferCount;
			uint8_t state           = buffer.State.load();
			if (state != c_WinCSBufferRenderable)
				continue;
			buffer.State = c_WinCSBufferDoubleRendering; // Transition to Rendering
			--pSwapchain->UsableBufferCount;             // And decrement usable buffer count
			pSwapchain->Mtx.Unlock();
			*pImageIndex = currentIndex;
			if (semaphore)
				return vkQueueSubmit2(pSwapchain->Queue, 1, &submit, fence);
			else if (fence)
				return vkQueueSubmit2(pSwapchain->Queue, 0, nullptr, fence);
		}
		while (pSwapchain->BufferIndex != startIndex);
		pSwapchain->Mtx.Unlock();
		break;
	}
	case VK_PRESENT_MODE_MAILBOX_KHR:
	{
		pSwapchain->Mtx.Lock();
		uint32_t startIndex = pSwapchain->BufferIndex;
		do
		{
			uint32_t currentIndex   = pSwapchain->BufferIndex;
			pSwapchain->BufferIndex = (currentIndex + 1) % pSwapchain->BufferCount;
			if (pSwapchain->PresentQueue[0] == currentIndex)
				continue;
			auto&   buffer = pSwapchain->Buffers[currentIndex];
			uint8_t state  = buffer.State.load();
			switch (state)
			{
			case c_WinCSBufferRenderable:
			case c_WinCSBufferPresentable:
				buffer.State = c_WinCSBufferRendering; // Transition to Rendering
				--pSwapchain->UsableBufferCount;       // And decrement usable buffer count
				pSwapchain->Mtx.Unlock();
				*pImageIndex = currentIndex;
				if (semaphore)
					return vkQueueSubmit2(pSwapchain->Queue, 1, &submit, fence);
				else
					return vkQueueSubmit2(pSwapchain->Queue, 0, nullptr, fence);
			case c_WinCSBufferWaiting:
				buffer.State = c_WinCSBufferDoubleRendering; // Transition to DoubleRendering
				--pSwapchain->UsableBufferCount;             // And decrement usable buffer count
				pSwapchain->Mtx.Unlock();
				if (!semaphore)
					submit.signalSemaphoreInfoCount = 0;
				submit.waitSemaphoreInfoCount = 1;
				submit.pWaitSemaphoreInfos    = &wait;
				wait.semaphore                = buffer.vkTimeline;
				wait.value                    = buffer.PresentFenceValue;
				*pImageIndex                  = currentIndex;
				return vkQueueSubmit2(pSwapchain->Queue, 1, &submit, fence);
			}
		}
		while (pSwapchain->BufferIndex != startIndex);
		pSwapchain->Mtx.Unlock();
		break;
	}
	}
	return VK_NOT_READY;
}

VkResult wincs_surface_vkQueuePresentKHR(
	VkQueue                 queue,
	const VkPresentInfoKHR* pPresentInfo)
{
#if BUILD_IS_CONFIG_DEBUG
	if (!queue || !pPresentInfo || (pPresentInfo->swapchainCount && (!pPresentInfo->pSwapchains || !pPresentInfo->pImageIndices)) || (pPresentInfo->waitSemaphoreCount && !pPresentInfo->pWaitSemaphores))
		throw std::runtime_error("Nullptrs passed to wincs_surface_vkQueuePresentKHR");
#endif

	VkResult result = VK_SUCCESS;
	for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
	{
		WinCSSwapchain* swapchain  = (WinCSSwapchain*) pPresentInfo->pSwapchains[i];
		uint32_t        imageIndex = pPresentInfo->pImageIndices[i];

		VkResult subResult = VK_SUCCESS;
		do
		{
			if (imageIndex >= swapchain->BufferCount)
			{
				subResult = VK_SUBOPTIMAL_KHR;
				break;
			}

			auto&   buffer = swapchain->Buffers[imageIndex];
			uint8_t state  = buffer.State;
			if (state != c_WinCSBufferRendering &&
				state != c_WinCSBufferDoubleRendering)
			{
				subResult = VK_SUBOPTIMAL_KHR;
				break;
			}

			if (!pPresentInfo->waitSemaphoreCount)
			{
				buffer.State = c_WinCSBufferPresentable; // Transition to Presentable
				switch (swapchain->PresentMode)
				{
				case VK_PRESENT_MODE_FIFO_KHR:
					swapchain->Mtx.Lock();
					for (uint32_t j = 0; j < swapchain->BufferCount; ++j)
					{
						if (swapchain->PresentQueue[j] == ~0U)
						{
							swapchain->PresentQueue[j] = imageIndex;
							break;
						}
					}
					swapchain->Mtx.Unlock();
					break;
				case VK_PRESENT_MODE_MAILBOX_KHR:
					swapchain->Mtx.Lock();
					if (swapchain->PresentQueue[0] != ~0U)
					{
						++swapchain->UsableBufferCount; // Increment usable buffer count
						swapchain->UsableBufferCount.notify_one();
					}
					swapchain->PresentQueue[0] = imageIndex;
					swapchain->Mtx.Unlock();
					break;
				}
			}
			else
			{
				VkSemaphoreSubmitInfo* waits = new VkSemaphoreSubmitInfo[pPresentInfo->waitSemaphoreCount];
				for (uint32_t j = 0; j < pPresentInfo->waitSemaphoreCount; ++j)
				{
					waits[j] = {
						.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
						.pNext       = nullptr,
						.semaphore   = pPresentInfo->pWaitSemaphores[i],
						.value       = 0,
						.stageMask   = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
						.deviceIndex = 0
					};
				}
				VkSemaphoreSubmitInfo signal {
					.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
					.pNext       = nullptr,
					.semaphore   = buffer.vkTimeline,
					.value       = ++buffer.PresentFenceValue,
					.stageMask   = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
					.deviceIndex = 0
				};
				VkSubmitInfo2 submit {
					.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
					.pNext                    = nullptr,
					.flags                    = 0,
					.waitSemaphoreInfoCount   = pPresentInfo->waitSemaphoreCount,
					.pWaitSemaphoreInfos      = waits,
					.commandBufferInfoCount   = 0,
					.signalSemaphoreInfoCount = 1,
					.pSignalSemaphoreInfos    = &signal
				};
				if (state == c_WinCSBufferDoubleRendering)
				{
					buffer.State = c_WinCSBufferDoubleWaiting; // Transition to DoubleWaiting
				}
				else
				{
					buffer.State = c_WinCSBufferWaiting; // Transition to Waiting
					if (swapchain->PresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
					{
						++swapchain->UsableBufferCount; // And increment usable buffer count
						swapchain->UsableBufferCount.notify_one();
					}
				}
				subResult = vkQueueSubmit2(queue, 1, &submit, nullptr);
				delete[] waits;
				HRESULT hr = buffer.PresentFence->SetEventOnCompletion(buffer.PresentFenceValue, swapchain->Events[3 + imageIndex]);
				if (hr < S_OK)
				{
					subResult = VK_SUBOPTIMAL_KHR;
					break;
				}
			}
		}
		while (false);

		if (pPresentInfo->pResults)
			pPresentInfo->pResults[i] = subResult;

		do
		{
			if (subResult == VK_ERROR_DEVICE_LOST)
				result = subResult;
			if (result == VK_ERROR_DEVICE_LOST)
				break;
			if (subResult == VK_ERROR_SURFACE_LOST_KHR)
				result = subResult;
			if (result == VK_ERROR_SURFACE_LOST_KHR)
				break;
			if (subResult == VK_ERROR_OUT_OF_DATE_KHR)
				result = subResult;
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
				break;
			if (subResult == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
				result = subResult;
			if (result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
				break;
			if (subResult == VK_SUBOPTIMAL_KHR)
				result = subResult;
		}
		while (false);
	}
	return result;
}

void WinCSEventThreadFunc1(WinCSSwapchain* swapchain)
{
	while (swapchain->EventThreadsRunning)
	{
		DWORD eventIndex = WaitForMultipleObjects(3 + swapchain->BufferCount, swapchain->Events, FALSE, INFINITE);
		if (eventIndex < WAIT_OBJECT_0 || eventIndex >= WAIT_OBJECT_0 + 3 + swapchain->BufferCount)
		{
			// TODO: Problems happened, let's assume it didn't happen for the time being
			continue;
		}
		if (eventIndex == WAIT_OBJECT_0) // Lost event
		{
			// TODO: Presentation Manager was lost, but for the moment let's assume it didn't happen
			continue;
		}
		if (eventIndex == WAIT_OBJECT_0 + 1) // Terminate event
			break;
		ResetEvent(swapchain->Events[eventIndex]);
		if (eventIndex == WAIT_OBJECT_0 + 2) // OnBufferRetire
		{
			for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
			{
				auto& buffer = swapchain->Buffers[i];
				if (buffer.State != c_WinCSBufferPresenting)
					continue;
				boolean available = FALSE;
				buffer.PresentationBuffer->IsAvailable(&available);
				if (available)
				{
					buffer.State = c_WinCSBufferRenderable; // Transition to Renderable state
					++swapchain->UsableBufferCount;         // And increment usable buffer count
					swapchain->UsableBufferCount.notify_one();
				}
			}
			continue;
		}

		// OnBufferRendered
		uint32_t imageIndex = (uint32_t) (eventIndex - WAIT_OBJECT_0 - 3);
		swapchain->Mtx.Lock();
		auto& buffer = swapchain->Buffers[imageIndex];
		if (buffer.State == c_WinCSBufferWaiting)
		{
			buffer.State = c_WinCSBufferPresentable; // Transition to Presentable state
		}
		else if (buffer.State == c_WinCSBufferDoubleWaiting)
		{
			UINT64 value = buffer.PresentFence->GetCompletedValue();
			if (value != buffer.PresentFenceValue)
			{
				swapchain->Mtx.Unlock();
				continue; // Skip old present
			}

			buffer.State = c_WinCSBufferPresentable; // Transition to Presentable state
		}
		switch (swapchain->PresentMode)
		{
		case VK_PRESENT_MODE_FIFO_KHR:
			for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
			{
				if (swapchain->PresentQueue[i] == ~0U)
				{
					swapchain->PresentQueue[i] = imageIndex;
					break;
				}
			}
			break;
		case VK_PRESENT_MODE_MAILBOX_KHR:
			if (swapchain->PresentQueue[0] != ~0U)
			{
				++swapchain->UsableBufferCount; // Increment usable buffer count
				swapchain->UsableBufferCount.notify_one();
			}
			swapchain->PresentQueue[0] = imageIndex;
			break;
		}
		swapchain->Mtx.Unlock();
	}
}

void WinCSEventThreadFunc2(WinCSSwapchain* swapchain)
{
	while (swapchain->EventThreadsRunning)
	{
		DWORD eventIndex = DCompositionWaitForCompositorClock(2, swapchain->Events, INFINITE);
		if (eventIndex < WAIT_OBJECT_0 || eventIndex >= WAIT_OBJECT_0 + 3 + swapchain->BufferCount)
		{
			// TODO: Problems happened, let's assume it didn't happen for the time being
			continue;
		}
		if (eventIndex == WAIT_OBJECT_0) // Lost event
		{
			// TODO: Presentation Manager was lost, but for the moment let's assume it didn't happen
			continue;
		}
		if (eventIndex == WAIT_OBJECT_0 + 1) // Terminate event
			break;

		swapchain->Mtx.Lock();
		uint32_t imageIndex = swapchain->PresentQueue[0];
		for (uint32_t i = 1; i < swapchain->BufferCount; ++i)
			swapchain->PresentQueue[i - 1] = swapchain->PresentQueue[i];
		swapchain->PresentQueue[swapchain->BufferCount - 1] = ~0U;
		swapchain->Mtx.Unlock();
		if (imageIndex == ~0U)
			continue; // Skip frame as nothing was presented

		WinCSSurface* surface = swapchain->Surface;

		auto& buffer = swapchain->Buffers[imageIndex];
		buffer.State = c_WinCSBufferPresenting; // Transition to Presenting
		RECT rect {
			.left   = 0,
			.top    = 0,
			.right  = (LONG) swapchain->Width,
			.bottom = (LONG) swapchain->Height
		};
		surface->Surface->SetSourceRect(&rect);
		surface->Surface->SetAlphaMode(swapchain->AlphaMode);
		surface->Surface->SetColorSpace(swapchain->ColorSpace);
		surface->Surface->SetBuffer(buffer.PresentationBuffer);

		/*POINT pos {};
		GetCursorPos(&pos);
		SetWindowPos(surface->HWnd, nullptr, pos.x + 5, pos.y + 5, 0, 0, SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOSENDCHANGING);*/

		/*RECT rect2 {};
		GetWindowRect(surface->HWnd, &rect2);

		PresentationTransform transform {
			.M11 = 1.0f,
			.M12 = 0.0f,
			.M21 = 0.0f,
			.M22 = 1.0f,
			.M31 = 500.0f - rect2.left,
			.M32 = 500.0f - rect2.top
		};
		surface->Surface->SetTransform(&transform);*/
		// surface->Surface->SetLetterboxingMargins(100.0f, 50.0f, 75.0f, 100.0f);

		SystemInterruptTime time { 0 };
		surface->Manager->SetTargetTime(time);
		UINT64 id = surface->Manager->GetNextPresentId();
		surface->Manager->Present();
		swapchain->RetireFence->SetEventOnCompletion(id, swapchain->Events[2]);
	}
}