#pragma once

#include "Utils/TupleVector.h"

#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Windows.h>

#include <d3d11_4.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <Presentation.h>

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <Concurrency/Mutex.h>

namespace Helpers
{
	struct VkExcept : std::exception
	{
	public:
		VkExcept(VkResult result, std::string_view func)
			: m_Result(result),
			  m_Func(func),
			  m_Message(std::format("{} returned {}", func, string_VkResult(result))) {}

		virtual const char* what() const { return m_Message.c_str(); }

		auto  GetResult() const { return m_Result; }
		auto& GetFunc() const { return m_Func; }
		auto& GetMessage() const { return m_Message; }

	private:
		VkResult    m_Result;
		std::string m_Func;
		std::string m_Message;
	};

	struct HrExcept : std::exception
	{
	public:
		HrExcept(HRESULT result, std::string_view func)
			: m_Result(result),
			  m_Func(func),
			  m_Message(std::format("{} returned {:08X}", func, (uint32_t) result)) {}

		virtual const char* what() const { return m_Message.c_str(); }

		auto  GetResult() const { return m_Result; }
		auto& GetFunc() const { return m_Func; }
		auto& GetMessage() const { return m_Message; }

	private:
		HRESULT     m_Result;
		std::string m_Func;
		std::string m_Message;
	};

	void VkReport(VkResult result, std::string_view func);
	void HrReport(HRESULT result, std::string_view func);

	inline void VkExpect(VkResult result, std::string_view func)
	{
		if (result >= VK_SUCCESS)
			return;
		VkReport(result, func);
		throw VkExcept(result, func);
	}
	template <VkResult... Allowed>
	requires(sizeof...(Allowed) > 0)
	inline VkResult VkExpect(VkResult result, std::string_view func)
	{
		if (result >= VK_SUCCESS)
			return result;
		if ((false || ... || (result == Allowed)))
			return result;
		VkReport(result, func);
		throw VkExcept(result, func);
	}
	inline bool VkValidate(VkResult result, std::string_view func)
	{
		if (result >= VK_SUCCESS)
			return true;
		VkReport(result, func);
		return false;
	}
	template <VkResult... Allowed>
	requires(sizeof...(Allowed) > 0)
	inline VkResult VkValidate(VkResult result, std::string_view func)
	{
		if (result >= VK_SUCCESS)
			return result;
		if ((false || ... || (result == Allowed)))
			return result;
		VkReport(result, func);
		return result;
	}

	inline void HrExpect(HRESULT result, std::string_view func)
	{
		if (result >= S_OK)
			return;
		HrReport(result, func);
		throw HrExcept(result, func);
	}
	template <HRESULT... Allowed>
	requires(sizeof...(Allowed) > 0)
	inline HRESULT HrExpect(HRESULT result, std::string_view func)
	{
		if (result >= S_OK)
			return result;
		if ((false || ... || (result == Allowed)))
			return result;
		HrReport(result, func);
		throw HrExcept(result, func);
	}
	inline bool HrValidate(HRESULT result, std::string_view func)
	{
		if (result >= S_OK)
			return true;
		HrReport(result, func);
		return false;
	}
	template <HRESULT... Allowed>
	requires(sizeof...(Allowed) > 0)
	inline HRESULT HrValidate(HRESULT result, std::string_view func)
	{
		if (result >= S_OK)
			return result;
		if ((false || ... || (result == Allowed)))
			return result;
		HrReport(result, func);
		return result;
	}
} // namespace Helpers

#define VK_EXPECT(func, ...)   ::Helpers::VkExpect(func(__VA_ARGS__), #func)
#define VK_VALIDATE(func, ...) ::Helpers::VkValidate(func(__VA_ARGS__), #func)
#define VK_INVALID(func, ...)  if (!::Helpers::VkValidate(func(__VA_ARGS__), #func))

#define HR_EXPECT(func, ...)   ::Helpers::HrExpect(func(__VA_ARGS__), #func)
#define HR_VALIDATE(func, ...) ::Helpers::HrValidate(func(__VA_ARGS__), #func)
#define HR_INVALID(func, ...)  if (!::Helpers::HrValidate(func(__VA_ARGS__), #func))

namespace Vk
{
	struct FrameState;
	struct SwapchainFrameState;
	struct SwapchainState;
	struct Context;
} // namespace Vk

namespace DX
{
	struct Context;
} // namespace DX

namespace Wnd
{
	struct Context;
	struct Handle;
} // namespace Wnd

namespace Vk
{
	struct FrameState
	{
		std::vector<std::function<void()>> Destroys;

		VkCommandPool   Pool          = nullptr;
		VkCommandBuffer CmdBuf        = nullptr;
		VkSemaphore     Timeline      = nullptr;
		uint64_t        TimelineValue = 0;
		VkSemaphore     RenderDone    = nullptr;
	};

	struct SwapchainFrameState : public FrameState
	{
		VkSemaphore ImageReady = nullptr;
		uint32_t    ImageIndex = 0;
	};

	struct SwapchainState
	{
		Wnd::Handle*                      Window    = nullptr;
		VkSurfaceKHR                      Surface   = nullptr;
		VkSwapchainKHR                    Swapchain = nullptr;
		VkExtent2D                        Extents   = {};
		TupleVector<VkImage, VkImageView> Images;
		SwapchainFrameState*              Frames      = nullptr;
		bool                              Invalidated = false;
	};

	struct Context
	{
		VkInstance       Instance       = nullptr;
		VkPhysicalDevice PhysicalDevice = nullptr;
		VkDevice         Device         = nullptr;
		VkQueue          Queue          = nullptr;

		uint32_t    FramesInFlight = 0;
		uint32_t    CurrentFrame   = 0;
		FrameState* Frames         = nullptr;
	};

	extern Context* g_Context;

	struct ContextSpec
	{
		const char* AppName    = "TestApp";
		uint32_t    AppVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);

		uint32_t           InstanceExtCount = 0;
		const char* const* InstanceExts     = nullptr;
		uint32_t           DeviceExtCount   = 0;
		const char* const* DeviceExts       = nullptr;

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

		uint32_t FramesInFlight = 1;
	};

	bool InitFrameState(Context* context, FrameState* frame);
	void DeInitFrameState(Context* context, FrameState* frame);

	bool Init(const ContextSpec* spec = nullptr);
	void DeInit();

	void NextFrame();

	bool InitSwapchainState(SwapchainState* swapchain, Wnd::Handle* window, bool withFrames = false);
	void DeInitSwapchainState(SwapchainState* swapchain);
	bool SwapchainAcquireImage(SwapchainState* swapchain);
	bool SwapchainPresent(SwapchainState* swapchain);
	bool SwapchainResize(SwapchainState* swapchain);

	uint32_t FindDeviceMemoryIndex(uint32_t typeBits, VkMemoryPropertyFlags flags);

	VkResult createSurface(Wnd::Handle* window, VkSurfaceKHR* surface);
} // namespace Vk

namespace DX
{
	struct Context
	{
		ID3D11Device5*        D3D11Device         = nullptr;
		ID3D11DeviceContext4* D3D11DeviceContext  = nullptr;
		IDXGIDevice4*         DXGIDevice          = nullptr;
		IDXGIFactory7*        DXGIFactory         = nullptr;
		IDCompositionDevice*  DCompDevice         = nullptr;
		IDCompositionDevice4* DCompDevice2        = nullptr;
		IPresentationFactory* PresentationFactory = nullptr;
	};

	extern Context* g_Context;

	struct ContextSpec
	{
		bool WithComposition  = false;
		bool WithPresentation = false;
	};

	struct CSwapchainSpec
	{
		Wnd::Handle*          Window         = nullptr;
		uint32_t              MinBufferCount = 0;
		DXGI_FORMAT           Format         = DXGI_FORMAT_UNKNOWN;
		DXGI_COLOR_SPACE_TYPE ColorSpace     = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		uint32_t              Width          = 0;
		uint32_t              Height         = 0;
		DXGI_ALPHA_MODE       AlphaMode      = DXGI_ALPHA_MODE_UNSPECIFIED;
	};

	bool Init(const ContextSpec* spec = nullptr);
	void DeInit();
} // namespace DX

namespace Wnd
{
	static constexpr int32_t c_CenterX = (int32_t) (1U << 31);
	static constexpr int32_t c_CenterY = (int32_t) (1U << 31);

	namespace WindowCreateFlag
	{
		static constexpr uint64_t None      = 0x0000;
		static constexpr uint64_t Maximized = 0x0001;
		static constexpr uint64_t Minimized = 0x0002;
		static constexpr uint64_t Visible   = 0x0004;
		static constexpr uint64_t Decorated = 0x0008;
		static constexpr uint64_t NoBitmap  = 0x0100;

		static constexpr uint64_t Default = Visible | Decorated;
	} // namespace WindowCreateFlag

	extern Context* g_Context;

	struct ContextSpec
	{
		bool SeparateThread = false;
	};

	bool Init(const ContextSpec* spec = nullptr);
	void DeInit();
	void PollEvents();
	void WaitForEvent();
	bool QuitSignaled();
	void SignalQuit();

	HINSTANCE GetInstance();

	struct Spec
	{
		std::string Title = "TestWindow";
		int32_t     x = c_CenterX, y = c_CenterY;
		uint32_t    w = 1280, h = 720;
		uint64_t    Flags = WindowCreateFlag::Default;
	};

	Handle* Create(const Spec* spec);
	void    Destroy(Handle* window);
	HWND    GetNativeHandle(Handle* window);
	void    Show(Handle* window);
	void    Hide(Handle* window);
	void    Maximize(Handle* window);
	void    Minimize(Handle* window);
	void    Restore(Handle* window);
	bool    IsMaximized(Handle* window);
	bool    IsMinimized(Handle* window);
	void    SetWindowTitle(Handle* window, std::string_view title);
	void    GetWindowTitle(Handle* window, std::string& title);
	bool    GetWantsClose(Handle* window);
	void    SetWantsClose(Handle* window, bool wantsClose);
	void    GetWindowPos(Handle* window, int32_t& x, int32_t& y, bool raw = false);
	void    GetWindowSize(Handle* window, uint32_t& w, uint32_t& h, bool raw = false);
	void    GetWindowRect(Handle* window, int32_t& x, int32_t& y, uint32_t& w, uint32_t& h, bool raw = false);
	void    SetWindowPos(Handle* window, int32_t x, int32_t y, bool raw = false);
	void    SetWindowSize(Handle* window, uint32_t w, uint32_t h, bool raw = false);
	void    SetWindowRect(Handle* window, int32_t x, int32_t y, uint32_t w, uint32_t h, bool raw = false);
} // namespace Wnd

typedef struct VkImportMemoryWin32HandleInfoKHR
{
	VkStructureType                    sType;
	const void*                        pNext;
	VkExternalMemoryHandleTypeFlagBits handleType;
	HANDLE                             handle;
	LPCWSTR                            name;
} VkImportMemoryWin32HandleInfoKHR;

typedef struct VkMemoryWin32HandlePropertiesKHR
{
	VkStructureType sType;
	void*           pNext;
	uint32_t        memoryTypeBits;
} VkMemoryWin32HandlePropertiesKHR;

typedef struct VkMemoryGetWin32HandleInfoKHR
{
	VkStructureType                    sType;
	const void*                        pNext;
	VkDeviceMemory                     memory;
	VkExternalMemoryHandleTypeFlagBits handleType;
} VkMemoryGetWin32HandleInfoKHR;

typedef VkFlags VkWin32SurfaceCreateFlagsKHR;
typedef struct VkWin32SurfaceCreateInfoKHR
{
	VkStructureType              sType;
	const void*                  pNext;
	VkWin32SurfaceCreateFlagsKHR flags;
	HINSTANCE                    hinstance;
	HWND                         hwnd;
} VkWin32SurfaceCreateInfoKHR;

typedef struct VkExportSemaphoreWin32HandleInfoKHR
{
	VkStructureType            sType;
	const void*                pNext;
	const SECURITY_ATTRIBUTES* pAttributes;
	DWORD                      dwAccess;
	LPCWSTR                    name;
} VkExportSemaphoreWin32HandleInfoKHR;
typedef struct VkSemaphoreGetWin32HandleInfoKHR
{
	VkStructureType                       sType;
	const void*                           pNext;
	VkSemaphore                           semaphore;
	VkExternalSemaphoreHandleTypeFlagBits handleType;
} VkSemaphoreGetWin32HandleInfoKHR;
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

typedef VkResult (*PFN_vkGetMemoryWin32HandleKHR)(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* pGetWin32HandleInfo, HANDLE* pHandle);
typedef VkResult (*PFN_vkGetMemoryWin32HandlePropertiesKHR)(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE handle, VkMemoryWin32HandlePropertiesKHR* pMemoryWin32HandleProperties);
typedef VkResult (*PFN_vkCreateWin32SurfaceKHR)(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
typedef VkResult (*PFN_vkGetSemaphoreWin32HandleKHR)(VkDevice device, const VkSemaphoreGetWin32HandleInfoKHR* pGetWin32HandleInfo, HANDLE* pHandle);
typedef VkResult (*PFN_vkImportSemaphoreWin32HandleKHR)(VkDevice device, const VkImportSemaphoreWin32HandleInfoKHR* pImportSemaphoreWin32HandleInfo);

extern "C" VkResult vkGetMemoryWin32HandleKHR(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* pGetWin32HandleInfo, HANDLE* pHandle);
extern "C" VkResult vkGetMemoryWin32HandlePropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE handle, VkMemoryWin32HandlePropertiesKHR* pMemoryWin32HandleProperties);
extern "C" VkResult vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
extern "C" VkResult vkGetSemaphoreWin32HandleKHR(VkDevice device, const VkSemaphoreGetWin32HandleInfoKHR* pGetWin32HandleInfo, HANDLE* pHandle);
extern "C" VkResult vkImportSemaphoreWin32HandleKHR(VkDevice device, const VkImportSemaphoreWin32HandleInfoKHR* pImportSemaphoreWin32HandleInfo);