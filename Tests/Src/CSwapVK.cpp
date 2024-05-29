#include "Shared.h"
#include "Utils/TupleVector.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include <Concurrency/Mutex.h>

static constexpr const char* c_InstanceExtensions[] {
	nullptr
};
static constexpr const char* c_DeviceExtensions[] {
	"VK_KHR_external_memory_win32",
	"VK_KHR_external_semaphore_win32",
	nullptr
};

struct CSwapchain
{
	Wnd::Handle* Window = nullptr;

	IDCompositionTarget*  DCompTarget         = nullptr;
	IDCompositionVisual3* DCompVisual         = nullptr;
	IPresentationManager* PresentationManager = nullptr;
	IPresentationSurface* Surface             = nullptr;
	IUnknown*             DCompSurface        = nullptr;
	HANDLE                SurfaceHandle       = nullptr;

	struct Buffer
	{
		ID3D11Texture2D* D3D11BackBuffer            = nullptr;
		ID3D11Texture2D* D3D11InteropBuffer         = nullptr;
		ID3D11Resource*  D3D11BackBufferResource    = nullptr;
		ID3D11Resource*  D3D11InteropBufferResource = nullptr;

		IPresentationBuffer* PresentationBuffer = nullptr;
		HANDLE               D3D11TextureHandle = nullptr;
		VkImage              Image              = nullptr;
		VkDeviceMemory       ImageMemory        = nullptr;
		ID3D11Fence*         Fence              = nullptr;
		HANDLE               FenceHandle        = nullptr;
		VkSemaphore          Timeline           = nullptr;
		uint64_t             TimelineValue      = 0;
		std::atomic_uint8_t  State              = 0;
	};
	uint32_t             BufferCount    = 0;
	uint32_t             BufferIndex    = 0;
	std::atomic_uint32_t UsableBuffers  = 0;
	std::atomic_uint32_t OptimalBuffer  = ~0U;
	Buffer*              Buffers        = nullptr;
	HANDLE*              Events         = nullptr;
	HANDLE               RetireEvent    = nullptr;
	HANDLE               TerminateEvent = nullptr;
	ID3D11Fence*         RetireFence    = nullptr;

	VkExtent2D                        Extents = {};
	TupleVector<VkImage, VkImageView> Images;

	Vk::SwapchainFrameState* Frames = nullptr;

	Concurrency::Mutex PresentMtx;
	std::atomic_bool   PresentThreadRunning = false;
	std::thread        PresentThread1;
	std::thread        PresentThread2;
};

static bool InitCSwapchain(CSwapchain* swapchain, Wnd::Handle* window, uint32_t bufferCount = 2);
static void DeInitCSwapchain(CSwapchain* swapchain);
static bool CSwapchainAcquireNextImage(CSwapchain* swapchain, VkSemaphore imageReady, uint32_t* imageIndex);
static bool CSwapchainPresent(CSwapchain* swapchain, uint32_t imageIndex, uint32_t waitSemaphoreCount, const VkSemaphore* waitSemaphores);
static void CSwapchainPresentThreadFunc1(CSwapchain* swapchain);
static void CSwapchainPresentThreadFunc2(CSwapchain* swapchain);

int CSwapVK(size_t argc, const std::string_view* argv)
{
	int64_t numFramesInFlight = 1;
	int64_t numSwapchains     = 1;
	for (size_t i = 1; i < argc; ++i)
	{
		if (argv[i] == "-h" || argv[i] == "--help")
		{
			std::cout << "STMS Help\n"
						 "Options:\n"
						 "  '-h' | '--help':       Shows this help info\n"
						 "  '-f' | '--frames':     Set number of frames in flight, default 1, minimum 1\n"
						 "  '-s' | '--swapchains': Set number of swapchains to create, default 4, minimum 1\n";
			return 0;
		}
		else if (argv[i] == "-f" || argv[i] == "--frames")
		{
			if (++i >= argc)
				break;
			numFramesInFlight = std::strtoll(argv[i].data(), nullptr, 10);
			if (numFramesInFlight < 1)
			{
				std::cout << "Frames In Flight needs to be 1 or higher!\n";
				return 1;
			}
		}
		else if (argv[i] == "-s" || argv[i] == "--swapchains")
		{
			if (++i >= argc)
				break;
			numSwapchains = std::strtoll(argv[i].data(), nullptr, 10);
			if (numSwapchains < 1)
			{
				std::cout << "Number of swapchains needs to be 1 or higher!\n";
				return 1;
			}
		}
	}

	{
		Wnd::ContextSpec spec {};
		spec.SeparateThread = false;
		if (!Wnd::Init(&spec))
			return 1;
	}
	{
		Vk::ContextSpec spec {};
		spec.AppName          = "CSwapVK";
		spec.AppVersion       = VK_MAKE_API_VERSION(0, 1, 0, 0);
		spec.InstanceExtCount = (uint32_t) (sizeof(c_InstanceExtensions) / sizeof(*c_InstanceExtensions) - 1);
		spec.InstanceExts     = c_InstanceExtensions;
		spec.DeviceExtCount   = (uint32_t) (sizeof(c_DeviceExtensions) / sizeof(*c_DeviceExtensions) - 1);
		spec.DeviceExts       = c_DeviceExtensions;
		spec.FramesInFlight   = (uint32_t) numFramesInFlight;
		if (!Vk::Init(&spec))
		{
			Wnd::DeInit();
			return 1;
		}
	}
	{
		DX::ContextSpec spec {};
		spec.WithComposition  = true;
		spec.WithPresentation = true;
		if (!DX::Init(&spec))
		{
			Vk::DeInit();
			Wnd::DeInit();
			return 1;
		}
	}

	CSwapchain* swapchains = new CSwapchain[numSwapchains];
	for (int64_t i = 0; i < numSwapchains; ++i)
	{
		Wnd::Spec spec {};
		spec.Title          = std::format("CSwapVK Window {}", i);
		spec.Flags         |= Wnd::WindowCreateFlag::NoBitmap;
		Wnd::Handle* window = Wnd::Create(&spec);
		if (!InitCSwapchain(&swapchains[i], window, 2))
		{
			Wnd::Destroy(window);
			DX::DeInit();
			Vk::DeInit();
			Wnd::DeInit();
			return 1;
		}
	}

	TupleVector<VkSemaphore, uint64_t> timelines((size_t) numSwapchains);

	VkSemaphoreWaitInfo waitInfo {
		.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.pNext          = nullptr,
		.flags          = 0,
		.semaphoreCount = (uint32_t) timelines.size(),
		.pSemaphores    = timelines.column<0>(),
		.pValues        = timelines.column<1>()
	};
	VkCommandBufferBeginInfo beginInfo {
		.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext            = nullptr,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr
	};
	VkImageMemoryBarrier2 preImageBarrier {
		.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext               = nullptr,
		.srcStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		.srcAccessMask       = VK_ACCESS_2_NONE,
		.dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image               = nullptr,
		.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
	};
	VkImageMemoryBarrier2 postImageBarrier {
		.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext               = nullptr,
		.srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		.dstAccessMask       = VK_ACCESS_2_NONE,
		.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout           = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image               = nullptr,
		.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
	};
	VkDependencyInfo depInfo {
		.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.pNext                    = nullptr,
		.dependencyFlags          = 0,
		.memoryBarrierCount       = 0,
		.bufferMemoryBarrierCount = 0,
		.imageMemoryBarrierCount  = 1,
		.pImageMemoryBarriers     = &preImageBarrier
	};
	VkRenderingAttachmentInfo colAttach {
		.sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext              = nullptr,
		.imageView          = nullptr,
		.imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode        = VK_RESOLVE_MODE_NONE,
		.resolveImageView   = nullptr,
		.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue         = { .color = { .float32 = { 0.05f, 0.1f, 0.05f, 0.95f } } }
	};
	VkRenderingInfo renderingInfo {
		.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.pNext                = nullptr,
		.flags                = 0,
		.renderArea           = {{ 0, 0 }, { 0, 0 }},
		.layerCount           = 1,
		.viewMask             = 0,
		.colorAttachmentCount = 1,
		.pColorAttachments    = &colAttach,
		.pDepthAttachment     = nullptr,
		.pStencilAttachment   = nullptr
	};
	VkCommandBufferSubmitInfo cmdBufInfo {
		.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.pNext         = nullptr,
		.commandBuffer = nullptr,
		.deviceMask    = 0
	};
	VkSemaphoreSubmitInfo imageReadyWait {
		.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.pNext       = nullptr,
		.semaphore   = nullptr,
		.value       = 0,
		.stageMask   = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.deviceIndex = 0
	};
	VkSemaphoreSubmitInfo signals[2] {
		{
         .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .pNext       = nullptr,
         .semaphore   = nullptr,
         .value       = 0,
         .stageMask   = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
         .deviceIndex = 0,
		 },
		{
         .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .pNext       = nullptr,
         .semaphore   = nullptr,
         .value       = 0,
         .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
         .deviceIndex = 0,
		 }
	};
	VkSubmitInfo2 submit {
		.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.pNext                    = nullptr,
		.flags                    = 0,
		.waitSemaphoreInfoCount   = 1,
		.pWaitSemaphoreInfos      = &imageReadyWait,
		.commandBufferInfoCount   = 1,
		.pCommandBufferInfos      = &cmdBufInfo,
		.signalSemaphoreInfoCount = 2,
		.pSignalSemaphoreInfos    = signals
	};

	using Clock = std::chrono::high_resolution_clock;
	Clock::time_point start, end;

	double avgDeltaTime    = 0.0;
	double avgPresentTime  = 0.0;
	double avgWaitTime     = 0.0;
	auto   previousTime    = Clock::now();
	auto   updateTitleTime = previousTime;
	auto   startTime       = previousTime;
	while (!Wnd::QuitSignaled())
	{
		auto   currentTime = Clock::now();
		double deltaTime   = std::chrono::duration_cast<std::chrono::duration<double>>(currentTime - previousTime).count();
		previousTime       = currentTime;
		avgDeltaTime       = avgDeltaTime * 0.99 + deltaTime * 0.01;
		bool updateTitle   = currentTime - updateTitleTime > std::chrono::duration<double>(1.0);
		if (updateTitle)
			updateTitleTime = currentTime;

		Wnd::PollEvents();
		if (Wnd::QuitSignaled())
			break;

		uint32_t curFrame = Vk::g_Context->CurrentFrame;

		for (int64_t i = 0; i < numSwapchains; ++i)
		{
			if (Wnd::GetWantsClose(swapchains[i].Window))
				Wnd::SignalQuit();
			auto [semaphore, value] = timelines[i];
			semaphore               = swapchains[i].Frames[curFrame].Timeline;
			value                   = swapchains[i].Frames[curFrame].TimelineValue;
		}

		double waitTime = 0.0, presentTime = 0.0;
		start = Clock::now();
		VK_EXPECT(vkWaitSemaphores, Vk::g_Context->Device, &waitInfo, ~0ULL);
		end       = Clock::now();
		waitTime += std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

		for (int64_t i = 0; i < numSwapchains; ++i)
		{
			auto& swapchain = swapchains[i];
			auto& frame     = swapchain.Frames[Vk::g_Context->CurrentFrame];
			for (auto& destroy : frame.Destroys)
				destroy();
			frame.Destroys.clear();

			if (!CSwapchainAcquireNextImage(&swapchain, frame.ImageReady, &frame.ImageIndex))
				continue;

			if (updateTitle)
			{
				Wnd::SetWindowTitle(swapchain.Window, std::format("DXGISwapVK Window {}, FrameTime {:.4} us, FPS {:.5}, PresentTime {:.4} us, WaitTime {:.4} us", i, avgDeltaTime * 1e6, 1.0 / avgDeltaTime, avgPresentTime * 1e6, avgWaitTime * 1e6));
			}

			VK_INVALID(vkResetCommandPool, Vk::g_Context->Device, frame.Pool, 0)
			{
				continue;
			}
			VK_INVALID(vkBeginCommandBuffer, frame.CmdBuf, &beginInfo)
			{
				continue;
			}

			postImageBarrier.image       = swapchain.Images.entry<0>(frame.ImageIndex);
			preImageBarrier.image        = swapchain.Images.entry<0>(frame.ImageIndex);
			colAttach.imageView          = swapchain.Images.entry<1>(frame.ImageIndex);
			depInfo.pImageMemoryBarriers = &preImageBarrier;
			vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);

			colAttach.clearValue.color.float32[0]  = 0.5f + 0.5f * sinf(0.17f + std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - startTime).count() * 3.1415f);
			colAttach.clearValue.color.float32[1]  = 0.5f + 0.5f * sinf(std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - startTime).count() * 3.10f);
			colAttach.clearValue.color.float32[2]  = 0.5f + 0.5f * sinf(0.65f + std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - startTime).count() * 3.2f);
			colAttach.clearValue.color.float32[3]  = 0.5f + 0.5f * sinf(0.3f + std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - startTime).count() * 0.5f);
			colAttach.clearValue.color.float32[0] *= colAttach.clearValue.color.float32[3];
			colAttach.clearValue.color.float32[1] *= colAttach.clearValue.color.float32[3];
			colAttach.clearValue.color.float32[2] *= colAttach.clearValue.color.float32[3];
			renderingInfo.renderArea.extent        = swapchain.Extents;
			vkCmdBeginRendering(frame.CmdBuf, &renderingInfo);
			vkCmdEndRendering(frame.CmdBuf);

			depInfo.pImageMemoryBarriers = &postImageBarrier;
			vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);

			VK_INVALID(vkEndCommandBuffer, frame.CmdBuf)
			{
				continue;
			}

			cmdBufInfo.commandBuffer = frame.CmdBuf;
			imageReadyWait.semaphore = frame.ImageReady;
			signals[0].semaphore     = frame.RenderDone;
			signals[1].semaphore     = frame.Timeline;
			signals[1].value         = ++frame.TimelineValue;
			VK_INVALID(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr)
			{
				continue;
			}

			start = Clock::now();
			if (!CSwapchainPresent(&swapchain, frame.ImageIndex, 1, &frame.RenderDone))
				continue;
			end          = Clock::now();
			presentTime += std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
		}
		Vk::NextFrame();

		avgPresentTime = avgPresentTime * 0.99 + presentTime * 0.01;
		avgWaitTime    = avgWaitTime * 0.99 + waitTime * 0.01;
	}

	for (int64_t i = 0; i < numSwapchains; ++i)
		DeInitCSwapchain(&swapchains[i]);
	delete[] swapchains;

	DX::DeInit();
	Vk::DeInit();
	Wnd::DeInit();
	return 0;
}

bool InitCSwapchain(CSwapchain* swapchain, Wnd::Handle* window, uint32_t bufferCount)
{
	if (!Vk::g_Context || !DX::g_Context || !swapchain || !window)
		return false;

	if (bufferCount < 2 || bufferCount > 31)
		return false;

	IDCompositionVisual2* visual       = nullptr;
	IDXGIResource1*       dxgiResource = nullptr;

	Wnd::GetWindowSize(window, swapchain->Extents.width, swapchain->Extents.height);

	D3D11_TEXTURE2D_DESC textureDesc {
		.Width          = (UINT) swapchain->Extents.width,
		.Height         = (UINT) swapchain->Extents.height,
		.MipLevels      = 1,
		.ArraySize      = 1,
		.Format         = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc     = {1, 0},
		.Usage          = D3D11_USAGE_DEFAULT,
		.BindFlags      = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
		.CPUAccessFlags = 0,
		.MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE/* | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE*/
	};
	D3D11_TEXTURE2D_DESC interopTextureDesc {
		.Width          = (UINT) swapchain->Extents.width,
		.Height         = (UINT) swapchain->Extents.height,
		.MipLevels      = 1,
		.ArraySize      = 1,
		.Format         = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc     = {1, 0},
		.Usage          = D3D11_USAGE_DEFAULT,
		.BindFlags      = D3D11_BIND_RENDER_TARGET,
		.CPUAccessFlags = 0,
		.MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE
	};
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
		.format                = VK_FORMAT_R8G8B8A8_UNORM,
		.extent                = {swapchain->Extents.width, swapchain->Extents.height, 1},
		.mipLevels             = 1,
		.arrayLayers           = 1,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = nullptr,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
	};
	VkMemoryWin32HandlePropertiesKHR handleProps {
		.sType          = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR,
		.pNext          = nullptr,
		.memoryTypeBits = 0
	};
	VkMemoryRequirements             mReq {};
	VkImportMemoryWin32HandleInfoKHR imHandleInfo {
		.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
		.pNext      = nullptr,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
		.handle     = nullptr,
		.name       = nullptr
	};
	VkMemoryAllocateInfo mAllocInfo {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext           = &imHandleInfo,
		.allocationSize  = 0,
		.memoryTypeIndex = 0
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
	VkImportSemaphoreWin32HandleInfoKHR isHandleInfo {
		.sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
		.pNext      = nullptr,
		.semaphore  = nullptr,
		.flags      = 0,
		.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE_BIT,
		.handle     = nullptr,
		.name       = nullptr
	};
	VkImageViewCreateInfo ivCreateInfo {
		.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext            = nullptr,
		.flags            = 0,
		.image            = nullptr,
		.viewType         = VK_IMAGE_VIEW_TYPE_2D,
		.format           = VK_FORMAT_R8G8B8A8_UNORM,
		.components       = {},
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	};
	SystemInterruptTime time { 0 };

	swapchain->Window = window;
	HR_INVALID(DCompositionCreateSurfaceHandle, COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &swapchain->SurfaceHandle)
	{
		goto INITFAILED;
	}
	HR_INVALID(DX::g_Context->PresentationFactory->CreatePresentationManager, &swapchain->PresentationManager)
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain->PresentationManager->CreatePresentationSurface, swapchain->SurfaceHandle, &swapchain->Surface)
	{
		goto INITFAILED;
	}
	HR_INVALID(DX::g_Context->DCompDevice->CreateSurfaceFromHandle, swapchain->SurfaceHandle, &swapchain->DCompSurface)
	{
		goto INITFAILED;
	}

	HR_INVALID(DX::g_Context->DCompDevice->CreateTargetForHwnd, Wnd::GetNativeHandle(window), TRUE, &swapchain->DCompTarget)
	{
		goto INITFAILED;
	}
	HR_INVALID(DX::g_Context->DCompDevice2->CreateVisual, &visual)
	{
		goto INITFAILED;
	}
	HR_INVALID(visual->QueryInterface, &swapchain->DCompVisual)
	{
		visual->Release();
		goto INITFAILED;
	}
	visual->Release();
	HR_INVALID(swapchain->DCompVisual->SetContent, swapchain->DCompSurface)
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain->DCompTarget->SetRoot, swapchain->DCompVisual)
	{
		goto INITFAILED;
	}
	HR_INVALID(DX::g_Context->DCompDevice2->Commit)
	{
		goto INITFAILED;
	}

	HR_INVALID(swapchain->Surface->SetAlphaMode, DXGI_ALPHA_MODE_PREMULTIPLIED)
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain->Surface->SetColorSpace, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
	{
		goto INITFAILED;
	}

	swapchain->BufferCount   = bufferCount;
	swapchain->UsableBuffers = swapchain->BufferCount;
	swapchain->Buffers       = new CSwapchain::Buffer[swapchain->BufferCount];
	swapchain->Events        = new HANDLE[3 + swapchain->BufferCount];
	HR_INVALID(swapchain->PresentationManager->GetLostEvent, &swapchain->Events[0])
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain->PresentationManager->GetPresentRetiringFence, __uuidof(ID3D11Fence), (void**) &swapchain->RetireFence)
	{
		goto INITFAILED;
	}
	swapchain->RetireEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!swapchain->RetireEvent)
		goto INITFAILED;
	swapchain->TerminateEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!swapchain->TerminateEvent)
		goto INITFAILED;
	swapchain->Events[1]                          = swapchain->TerminateEvent;
	swapchain->Events[2 + swapchain->BufferCount] = swapchain->RetireEvent;
	for (uint32_t i = 2; i < 2 + swapchain->BufferCount; ++i)
		swapchain->Events[i] = nullptr;
	for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
	{
		auto& buffer = swapchain->Buffers[i];

		swapchain->Events[2 + i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!swapchain->Events[2 + i])
			goto INITFAILED;
		HR_INVALID(DX::g_Context->D3D11Device->CreateTexture2D, &textureDesc, nullptr, &buffer.D3D11BackBuffer)
		{
			goto INITFAILED;
		}
		HR_INVALID(DX::g_Context->D3D11Device->CreateTexture2D, &interopTextureDesc, nullptr, &buffer.D3D11InteropBuffer)
		{
			goto INITFAILED;
		}
		HR_INVALID(swapchain->PresentationManager->AddBufferFromResource, buffer.D3D11BackBuffer, &buffer.PresentationBuffer)
		{
			goto INITFAILED;
		}
		HR_INVALID(buffer.D3D11InteropBuffer->QueryInterface, &dxgiResource)
		{
			goto INITFAILED;
		}
		HR_INVALID(dxgiResource->CreateSharedHandle, nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &buffer.D3D11TextureHandle)
		{
			dxgiResource->Release();
			goto INITFAILED;
		}
		dxgiResource->Release();
		HR_INVALID(buffer.D3D11BackBuffer->QueryInterface, &buffer.D3D11BackBufferResource)
		{
			goto INITFAILED;
		}
		HR_INVALID(buffer.D3D11InteropBuffer->QueryInterface, &buffer.D3D11InteropBufferResource)
		{
			goto INITFAILED;
		}

		VK_INVALID(vkCreateImage, Vk::g_Context->Device, &iCreateInfo, nullptr, &buffer.Image)
		{
			goto INITFAILED;
		}
		VK_INVALID(vkGetMemoryWin32HandlePropertiesKHR, Vk::g_Context->Device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, buffer.D3D11TextureHandle, &handleProps)
		{
			goto INITFAILED;
		}
		vkGetImageMemoryRequirements(Vk::g_Context->Device, buffer.Image, &mReq);
		imHandleInfo.handle        = buffer.D3D11TextureHandle;
		mAllocInfo.allocationSize  = mReq.size;
		mAllocInfo.memoryTypeIndex = Vk::FindDeviceMemoryIndex(handleProps.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_INVALID(vkAllocateMemory, Vk::g_Context->Device, &mAllocInfo, nullptr, &buffer.ImageMemory)
		{
			goto INITFAILED;
		}
		VK_INVALID(vkBindImageMemory, Vk::g_Context->Device, buffer.Image, buffer.ImageMemory, 0)
		{
			goto INITFAILED;
		}

		HR_INVALID(DX::g_Context->D3D11Device->CreateFence, 0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**) &buffer.Fence)
		{
			goto INITFAILED;
		}
		HR_INVALID(buffer.Fence->CreateSharedHandle, nullptr, GENERIC_ALL, nullptr, &buffer.FenceHandle)
		{
			goto INITFAILED;
		}

		VK_INVALID(vkCreateSemaphore, Vk::g_Context->Device, &sCreateInfo, nullptr, &buffer.Timeline)
		{
			goto INITFAILED;
		}
		isHandleInfo.semaphore = buffer.Timeline;
		isHandleInfo.handle    = buffer.FenceHandle;
		VK_INVALID(vkImportSemaphoreWin32HandleKHR, Vk::g_Context->Device, &isHandleInfo)
		{
			goto INITFAILED;
		}
	}
	sCreateInfo.pNext = nullptr;

	swapchain->Frames = new Vk::SwapchainFrameState[Vk::g_Context->FramesInFlight];
	for (uint32_t i = 0; i < Vk::g_Context->FramesInFlight; ++i)
	{
		auto& frame = swapchain->Frames[i];
		if (!Vk::InitFrameState(Vk::g_Context, &frame))
			goto INITFAILED;
		VK_INVALID(vkCreateSemaphore, Vk::g_Context->Device, &sCreateInfo, nullptr, &frame.ImageReady)
		{
			goto INITFAILED;
		}
	}

	swapchain->Images.resize(swapchain->BufferCount);
	for (size_t i = 0; i < swapchain->BufferCount; ++i)
	{
		swapchain->Images.entry<0>(i) = swapchain->Buffers[i].Image;
		ivCreateInfo.image            = swapchain->Buffers[i].Image;
		VK_INVALID(vkCreateImageView, Vk::g_Context->Device, &ivCreateInfo, nullptr, &swapchain->Images.entry<1>(i))
		{
			goto INITFAILED;
		}
	}

	/*HR_EXPECT(swapchain->PresentationManager->Present);
	for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
	{
		IPresentStatistics*     statistics   = nullptr;
		auto&                   buffer       = swapchain->Buffers[i];
		ID3D11RenderTargetView* renderTarget = nullptr;
		DX::g_Context->D3D11Device->CreateRenderTargetView(buffer.D3D11BackBufferResource, nullptr, &renderTarget);
		float clearColor[4] { 0.1f, 0.1f, 0.1f, 0.95f };
		DX::g_Context->D3D11DeviceContext->ClearRenderTargetView(renderTarget, clearColor);
		if (renderTarget)
			renderTarget->Release();
		DX::g_Context->D3D11DeviceContext->Flush();
		HR_EXPECT(swapchain->Surface->SetBuffer, buffer.PresentationBuffer);
		SystemInterruptTime time { 0 };
		HR_EXPECT(swapchain->PresentationManager->SetTargetTime, time);
		UINT64 signalValue = swapchain->PresentationManager->GetNextPresentId();
		HR_EXPECT(swapchain->PresentationManager->Present);
		swapchain->RetireFence->SetEventOnCompletion(signalValue, swapchain->RetireEvent);
	}*/

	swapchain->PresentThreadRunning = true;
	swapchain->PresentThread1       = std::thread(&CSwapchainPresentThreadFunc1, swapchain);
	swapchain->PresentThread2       = std::thread(&CSwapchainPresentThreadFunc2, swapchain);

	return true;

INITFAILED:
	DeInitCSwapchain(swapchain);
	return false;
}

void DeInitCSwapchain(CSwapchain* swapchain)
{
	if (!Vk::g_Context || !DX::g_Context || !swapchain)
		return;

	if (swapchain->PresentThreadRunning)
	{
		swapchain->PresentThreadRunning = false;
		SetEvent(swapchain->TerminateEvent);
		swapchain->PresentThread2.join();
		swapchain->PresentThread1.join();
	}

	if (swapchain->Frames)
	{
		for (uint32_t i = 0; i < Vk::g_Context->FramesInFlight; ++i)
		{
			auto& frame = swapchain->Frames[i];
			Vk::DeInitFrameState(Vk::g_Context, &frame);
			vkDestroySemaphore(Vk::g_Context->Device, frame.ImageReady, nullptr);
		}
		delete[] swapchain->Frames;
		swapchain->Frames = nullptr;
	}
	for (auto [image, view] : swapchain->Images)
		vkDestroyImageView(Vk::g_Context->Device, view, nullptr);
	swapchain->Images.clear();
	if (swapchain->Buffers)
	{
		for (uint32_t i = 0; i < 2 + swapchain->BufferCount; ++i)
		{
			if (i == 1)
				continue;
			if (swapchain->Events[i])
				CloseHandle(swapchain->Events[i]);
		}
		for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
		{
			auto& buffer = swapchain->Buffers[i];
			vkDestroySemaphore(Vk::g_Context->Device, buffer.Timeline, nullptr);
			if (buffer.FenceHandle)
				CloseHandle(buffer.FenceHandle);
			if (buffer.Fence)
				buffer.Fence->Release();
			vkDestroyImage(Vk::g_Context->Device, buffer.Image, nullptr);
			vkFreeMemory(Vk::g_Context->Device, buffer.ImageMemory, nullptr);
			if (buffer.PresentationBuffer)
				buffer.PresentationBuffer->Release();
			if (buffer.D3D11TextureHandle)
				CloseHandle(buffer.D3D11TextureHandle);
			if (buffer.D3D11InteropBufferResource)
				buffer.D3D11InteropBufferResource->Release();
			if (buffer.D3D11BackBufferResource)
				buffer.D3D11BackBufferResource->Release();
			if (buffer.D3D11InteropBuffer)
				buffer.D3D11InteropBuffer->Release();
			if (buffer.D3D11BackBuffer)
				buffer.D3D11BackBuffer->Release();
		}
		delete[] swapchain->Buffers;
		delete[] swapchain->Events;
		swapchain->Buffers = nullptr;
		swapchain->Events  = nullptr;
	}
	if (swapchain->TerminateEvent)
		CloseHandle(swapchain->TerminateEvent);
	if (swapchain->RetireEvent)
		CloseHandle(swapchain->RetireEvent);
	if (swapchain->DCompTarget)
		swapchain->DCompTarget->Release();
	if (swapchain->DCompVisual)
		swapchain->DCompVisual->Release();
	if (swapchain->DCompSurface)
		swapchain->DCompSurface->Release();
	if (swapchain->Surface)
		swapchain->Surface->Release();
	if (swapchain->PresentationManager)
		swapchain->PresentationManager->Release();
	if (swapchain->SurfaceHandle)
		CloseHandle(swapchain->SurfaceHandle);
}

static constexpr uint8_t CSwapchainBufferStateRenderable      = 0;
static constexpr uint8_t CSwapchainBufferStateRendering       = 1;
static constexpr uint8_t CSwapchainBufferStateDoubleRendering = 2;
static constexpr uint8_t CSwapchainBufferStateWaiting         = 3;
static constexpr uint8_t CSwapchainBufferStateDoubleWaiting   = 4;
static constexpr uint8_t CSwapchainBufferStatePresentable     = 5;
static constexpr uint8_t CSwapchainBufferStatePresenting      = 6;

bool CSwapchainAcquireNextImage(CSwapchain* swapchain, VkSemaphore imageReady, uint32_t* imageIndex)
{
	if (!swapchain || !imageReady || !imageIndex)
		return false;

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
		.semaphore   = imageReady,
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

	swapchain->UsableBuffers.wait(0);
	swapchain->PresentMtx.Lock();
	uint32_t startIndex = swapchain->BufferIndex;
	do
	{
		uint32_t currentIndex  = swapchain->BufferIndex;
		auto&    buffer        = swapchain->Buffers[currentIndex];
		swapchain->BufferIndex = (currentIndex + 1) % swapchain->BufferCount;
		uint8_t state          = buffer.State.load();
		switch (state)
		{
		case CSwapchainBufferStateRenderable:              // Renderable state
			buffer.State = CSwapchainBufferStateRendering; // Transition to Rendering state
			--swapchain->UsableBuffers;                    // And decrement usable buffer count
			swapchain->PresentMtx.Unlock();
			*imageIndex = currentIndex;
			VK_EXPECT(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr);
			return true;
		case CSwapchainBufferStateWaiting:                       // Waiting state
			buffer.State = CSwapchainBufferStateDoubleRendering; // Transition to DoubleRendering state
			--swapchain->UsableBuffers;                          // And decrement usable buffer count
			swapchain->PresentMtx.Unlock();
			*imageIndex                   = currentIndex;
			wait.semaphore                = buffer.Timeline;
			wait.value                    = buffer.TimelineValue;
			submit.waitSemaphoreInfoCount = 1;
			submit.pWaitSemaphoreInfos    = &wait;
			VK_EXPECT(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr);
			return true;
		case CSwapchainBufferStatePresentable:             // Presentable state
			buffer.State = CSwapchainBufferStateRendering; // Transition to Rendering state
			--swapchain->UsableBuffers;                    // And decrement usable buffer count
			swapchain->PresentMtx.Unlock();
			*imageIndex = currentIndex;
			VK_EXPECT(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr);
			return true;

		case CSwapchainBufferStateRendering:       // Rendering state
		case CSwapchainBufferStateDoubleRendering: // DoubleRendering state
		case CSwapchainBufferStateDoubleWaiting:   // DoubleWaiting state
		case CSwapchainBufferStatePresenting:      // Presenting state
			break;
		default: // Dafuq happened here???
			std::cerr << std::format("Unknown buffer state {} for buffer {} of swapchain {}!!\n", state, currentIndex, (void*) swapchain);
			break;
		}
	}
	while (swapchain->BufferIndex != startIndex);
	swapchain->PresentMtx.Unlock();
	return false;
}

bool CSwapchainPresent(CSwapchain* swapchain, uint32_t imageIndex, uint32_t waitSemaphoreCount, const VkSemaphore* waitSemaphores)
{
	if (!swapchain || imageIndex >= swapchain->BufferCount || (waitSemaphoreCount && !waitSemaphores))
		return false;

	auto& buffer = swapchain->Buffers[imageIndex];
	if (buffer.State != CSwapchainBufferStateRendering &&
		buffer.State != CSwapchainBufferStateDoubleRendering) // Buffer is not in Rendering or DoubleRendering state
		return false;
	if (!waitSemaphoreCount)
	{
		buffer.State = CSwapchainBufferStatePresentable; // Transition to Presentable state
		++swapchain->UsableBuffers;                      // And increment usable buffer count
		swapchain->UsableBuffers.notify_all();
	}
	else
	{
		VkSemaphoreSubmitInfo* waits = new VkSemaphoreSubmitInfo[waitSemaphoreCount];
		for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
		{
			waits[i] = {
				.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.pNext       = nullptr,
				.semaphore   = waitSemaphores[i],
				.value       = 0,
				.stageMask   = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
				.deviceIndex = 0
			};
		}
		VkSemaphoreSubmitInfo signal {
			.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.pNext       = nullptr,
			.semaphore   = buffer.Timeline,
			.value       = ++buffer.TimelineValue,
			.stageMask   = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			.deviceIndex = 0
		};
		VkSubmitInfo2 submit {
			.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.pNext                    = nullptr,
			.flags                    = 0,
			.waitSemaphoreInfoCount   = waitSemaphoreCount,
			.pWaitSemaphoreInfos      = waits,
			.commandBufferInfoCount   = 0,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos    = &signal
		};
		HANDLE event = swapchain->Events[3 + imageIndex];
		HR_EXPECT(buffer.Fence->SetEventOnCompletion, buffer.TimelineValue, event);
		if (buffer.State == CSwapchainBufferStateDoubleRendering)
		{
			buffer.State             = CSwapchainBufferStateDoubleWaiting;
			swapchain->OptimalBuffer = imageIndex;
		}
		else
		{
			buffer.State = CSwapchainBufferStateWaiting; // Transition to Waiting state
			++swapchain->UsableBuffers;                  // And increment usable buffer count
			swapchain->UsableBuffers.notify_all();
		}
		VK_EXPECT(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr);
	}
	return true;
}

void CSwapchainPresentThreadFunc1(CSwapchain* swapchain)
{
	while (swapchain->PresentThreadRunning)
	{
		DWORD eventIndex = WaitForMultipleObjects(3 + swapchain->BufferCount, swapchain->Events, FALSE, INFINITE);
		if (!swapchain->PresentThreadRunning)
			break;
		if (eventIndex < WAIT_OBJECT_0 || eventIndex >= WAIT_OBJECT_0 + 3 + swapchain->BufferCount)
			continue;
		if (eventIndex == WAIT_OBJECT_0) // PresentationManager Lost Event
			break;

		if (eventIndex == WAIT_OBJECT_0 + 2)
		{
			// Retire event
			ResetEvent(swapchain->RetireEvent);
			swapchain->PresentMtx.Lock();
			for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
			{
				auto& buffer = swapchain->Buffers[i];
				if (buffer.State != CSwapchainBufferStatePresenting) // Not in Presenting state
					continue;
				boolean available = FALSE;
				HR_EXPECT(buffer.PresentationBuffer->IsAvailable, &available);
				if (available)
				{
					buffer.State = CSwapchainBufferStateRenderable; // Transition to Renderable state
					++swapchain->UsableBuffers;                     // And increment usable buffer count
					swapchain->UsableBuffers.notify_all();
				}
			}
			swapchain->PresentMtx.Unlock();
			continue;
		}

		// Waitable event
		ResetEvent(swapchain->Events[eventIndex]);
		uint32_t bufferIndex = (uint32_t) (eventIndex - WAIT_OBJECT_0 - 3);
		auto&    buffer      = swapchain->Buffers[bufferIndex];
		if (buffer.State == CSwapchainBufferStateWaiting) // Waiting state
		{
			buffer.State             = CSwapchainBufferStatePresentable; // Transition to Presentable state
			swapchain->OptimalBuffer = bufferIndex;
		}
		else if (buffer.State == CSwapchainBufferStateDoubleWaiting) // DoubleWaiting state
		{
			uint64_t value = 0;
			VK_EXPECT(vkGetSemaphoreCounterValue, Vk::g_Context->Device, buffer.Timeline, &value);
			if (value != buffer.TimelineValue)
				continue; // Skip non identical timeline values, represents the previous present

			buffer.State = CSwapchainBufferStatePresentable; // Transition to Presentable state
			++swapchain->UsableBuffers;                      // And Increment usable buffer count
			swapchain->UsableBuffers.notify_all();
			swapchain->OptimalBuffer = bufferIndex;
		}
	}
}

void CSwapchainPresentThreadFunc2(CSwapchain* swapchain)
{
	while (swapchain->PresentThreadRunning)
	{
		DWORD eventIndex = DCompositionWaitForCompositorClock(2, swapchain->Events, INFINITE);
		if (!swapchain->PresentThreadRunning)
			break;
		if (eventIndex < WAIT_OBJECT_0 || eventIndex >= WAIT_OBJECT_0 + 3)
			continue;
		if (eventIndex == WAIT_OBJECT_0) // PresentationManager Lost Event
			throw 0;

		// Try to present a buffer
		uint32_t bufferIndex = ~0U;
		swapchain->PresentMtx.Lock();
		{
			uint32_t optimalBufferIndex = swapchain->OptimalBuffer.load();
			if (optimalBufferIndex == ~0U)
			{
				swapchain->PresentMtx.Unlock();
				continue;
			}
			auto& optimalBuffer = swapchain->Buffers[optimalBufferIndex];
			if (optimalBuffer.State == CSwapchainBufferStatePresentable)
			{
				optimalBuffer.State = CSwapchainBufferStatePresenting; // Transition to Presenting state
				--swapchain->UsableBuffers;                            // And decrement usable buffer count
				bufferIndex = optimalBufferIndex;
			}
			else
			{
				for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
				{
					auto& buffer = swapchain->Buffers[i];
					if (buffer.State != CSwapchainBufferStatePresentable) // Not Presentable state
						continue;
					buffer.State = CSwapchainBufferStatePresenting; // Transition to Presenting state
					--swapchain->UsableBuffers;                     // And decrement usable buffer count
					bufferIndex = i;
					break;
				}
			}
		}
		swapchain->PresentMtx.Unlock();
		if (bufferIndex == ~0U) // No presentable buffers available
			continue;

		auto& buffer = swapchain->Buffers[bufferIndex];
		DX::g_Context->D3D11DeviceContext->CopyResource(buffer.D3D11BackBufferResource, buffer.D3D11InteropBufferResource);
		/*ID3D11RenderTargetView* renderTarget = nullptr;
		DX::g_Context->D3D11Device->CreateRenderTargetView(buffer.D3D11BackBufferResource, nullptr, &renderTarget);
		float clearColor[4] { 0.1f, 0.1f, 0.1f, 0.95f };
		DX::g_Context->D3D11DeviceContext->ClearRenderTargetView(renderTarget, clearColor);
		if (renderTarget)
			renderTarget->Release();
		DX::g_Context->D3D11DeviceContext->Flush();*/
		RECT rect {};
		rect.left   = 0;
		rect.top    = 0;
		rect.right  = swapchain->Extents.width;
		rect.bottom = swapchain->Extents.height;
		HR_EXPECT(swapchain->Surface->SetSourceRect, &rect);
		HR_EXPECT(swapchain->Surface->SetBuffer, buffer.PresentationBuffer);
		SystemInterruptTime time { 0 };
		HR_EXPECT(swapchain->PresentationManager->SetTargetTime, time);
		UINT64 signalValue = swapchain->PresentationManager->GetNextPresentId();
		swapchain->PresentationManager->CancelPresentsFrom(0);
		HR_EXPECT(swapchain->PresentationManager->Present);
		swapchain->RetireFence->SetEventOnCompletion(signalValue, swapchain->RetireEvent);
	}
}