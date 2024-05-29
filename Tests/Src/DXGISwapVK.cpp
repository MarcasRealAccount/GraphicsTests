#include "Shared.h"
#include "Utils/TupleVector.h"

#include <chrono>
#include <iostream>

static constexpr const char* c_InstanceExtensions[] {
	nullptr
};
static constexpr const char* c_DeviceExtensions[] {
	"VK_KHR_external_memory_win32",
	nullptr
};

struct DXGISwapchain
{
	Wnd::Handle* Window = nullptr;

	IDCompositionTarget*  DCompTarget         = nullptr;
	IDCompositionVisual3* DCompVisual         = nullptr;
	IDXGISwapChain4*      Swapchain           = nullptr;
	ID3D11Texture2D*      BackBuffer          = nullptr;
	ID3D11Resource*       BackBufferResource  = nullptr;
	ID3D11Texture2D*      FrontBuffer         = nullptr;
	ID3D11Resource*       FrontBufferResource = nullptr;
	void*                 FrontBufferHandle   = nullptr;

	VkExtent2D     Extents       = {};
	VkImage        ResolveImage  = nullptr;
	VkDeviceMemory ResolveMemory = nullptr;
	VkImageView    ResolveView   = nullptr;
	VkImage        Image         = nullptr;
	VkDeviceMemory ImageMemory   = nullptr;
	VkImageView    View          = nullptr;

	Vk::FrameState* Frames = nullptr;
};

static bool InitDXGISwapchain(DXGISwapchain* swapchain, Wnd::Handle* window);
static void DeInitDXGISwapchain(DXGISwapchain* swapchain);

int DXGISwapVK(size_t argc, const std::string_view* argv)
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
		spec.SeparateThread = true;
		if (!Wnd::Init(&spec))
			return 1;
	}
	{
		Vk::ContextSpec spec {};
		spec.AppName          = "DXGISwapVK";
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
		spec.WithComposition = true;
		if (!DX::Init(&spec))
		{
			Vk::DeInit();
			Wnd::DeInit();
			return 1;
		}
	}

	DXGISwapchain* swapchains = new DXGISwapchain[numSwapchains];
	for (int64_t i = 0; i < numSwapchains; ++i)
	{
		Wnd::Spec spec {};
		spec.Title          = std::format("DXGISwapVK Window {}", i);
		spec.Flags         |= Wnd::WindowCreateFlag::NoBitmap;
		Wnd::Handle* window = Wnd::Create(&spec);
		if (!InitDXGISwapchain(&swapchains[i], window))
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
		.resolveMode        = VK_RESOLVE_MODE_MIN_BIT,
		.resolveImageView   = nullptr,
		.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
	VkSemaphoreSubmitInfo timelineSig {
		.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.pNext       = nullptr,
		.semaphore   = nullptr,
		.value       = 0,
		.stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.deviceIndex = 0
	};
	VkSubmitInfo2 submit {
		.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.pNext                    = nullptr,
		.flags                    = 0,
		.waitSemaphoreInfoCount   = 1,
		.pWaitSemaphoreInfos      = &imageReadyWait,
		.commandBufferInfoCount   = 1,
		.pCommandBufferInfos      = &cmdBufInfo,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos    = &timelineSig
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

			depInfo.pImageMemoryBarriers = &preImageBarrier;
			preImageBarrier.image        = swapchain.Image;
			vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);
			preImageBarrier.image = swapchain.ResolveImage;
			vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);

			colAttach.imageView                    = swapchain.View;
			colAttach.resolveImageView             = swapchain.ResolveView;
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
			postImageBarrier.image       = swapchain.Image;
			vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);
			postImageBarrier.image = swapchain.ResolveImage;
			vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);

			VK_INVALID(vkEndCommandBuffer, frame.CmdBuf)
			{
				continue;
			}

			cmdBufInfo.commandBuffer = frame.CmdBuf;
			imageReadyWait.semaphore = frame.Timeline;
			imageReadyWait.value     = frame.TimelineValue;
			timelineSig.semaphore    = frame.Timeline;
			timelineSig.value        = ++frame.TimelineValue;
			VK_INVALID(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr)
			{
				continue;
			}

			start = Clock::now();
			DX::g_Context->D3D11DeviceContext->CopyResource(swapchain.BackBufferResource, swapchain.FrontBufferResource);
			HR_INVALID(swapchain.Swapchain->Present, 0, 0)
			{
				continue;
			}
			end          = Clock::now();
			presentTime += std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
		}
		Vk::NextFrame();

		avgPresentTime = avgPresentTime * 0.99 + presentTime * 0.01;
		avgWaitTime    = avgWaitTime * 0.99 + waitTime * 0.01;
	}

	for (int64_t i = 0; i < numSwapchains; ++i)
		DeInitDXGISwapchain(&swapchains[i]);
	delete[] swapchains;

	DX::DeInit();
	Vk::DeInit();
	Wnd::DeInit();
	return 0;
}

bool InitDXGISwapchain(DXGISwapchain* swapchain, Wnd::Handle* window)
{
	if (!Vk::g_Context || !DX::g_Context || !swapchain || !window)
		return false;

	Wnd::GetWindowSize(window, swapchain->Extents.width, swapchain->Extents.height);
	swapchain->Extents.width          *= 1.5;
	swapchain->Extents.height         *= 1.5;
	IDCompositionVisual2* visual       = nullptr;
	IDXGISwapChain1*      swapchain1   = nullptr;
	IDXGIResource1*       dxgiResource = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc {
		.Width       = (UINT) swapchain->Extents.width,
		.Height      = (UINT) swapchain->Extents.height,
		.Format      = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.Stereo      = FALSE,
		.SampleDesc  = {1, 0},
		.BufferUsage = DXGI_USAGE_BACK_BUFFER,
		.BufferCount = 2,
		.Scaling     = DXGI_SCALING_STRETCH,
		.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
		.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED,
		.Flags       = 0
	};
	D3D11_TEXTURE2D_DESC frontBufferDesc {
		.Width          = (UINT) swapchain->Extents.width,
		.Height         = (UINT) swapchain->Extents.height,
		.MipLevels      = 1,
		.ArraySize      = 1,
		.Format         = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.SampleDesc     = {1, 0},
		.Usage          = D3D11_USAGE_DEFAULT,
		.BindFlags      = D3D11_BIND_RENDER_TARGET,
		.CPUAccessFlags = 0,
		.MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE
	};
	VkExternalMemoryImageCreateInfo remiCreateInfo {
		.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.pNext       = nullptr,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
	};
	VkImageCreateInfo riCreateInfo {
		.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext                 = &remiCreateInfo,
		.flags                 = 0,
		.imageType             = VK_IMAGE_TYPE_2D,
		.format                = VK_FORMAT_R16G16B16A16_SFLOAT,
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
	VkImageCreateInfo iCreateInfo {
		.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext                 = nullptr,
		.flags                 = 0,
		.imageType             = VK_IMAGE_TYPE_2D,
		.format                = VK_FORMAT_R16G16B16A16_SFLOAT,
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
		.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR,
		.pNext = nullptr
	};
	VkMemoryRequirements             mReq {};
	VkImportMemoryWin32HandleInfoKHR rimHandleInfo {
		.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
		.pNext      = nullptr,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
		.handle     = nullptr,
		.name       = nullptr
	};
	VkMemoryAllocateInfo rmAllocInfo {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext           = &rimHandleInfo,
		.allocationSize  = 0,
		.memoryTypeIndex = 0
	};
	VkMemoryAllocateInfo mAllocInfo {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext           = nullptr,
		.allocationSize  = 0,
		.memoryTypeIndex = 0
	};
	VkImageViewCreateInfo ivCreateInfo {
		.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext            = nullptr,
		.flags            = 0,
		.image            = nullptr,
		.viewType         = VK_IMAGE_VIEW_TYPE_2D,
		.format           = VK_FORMAT_R16G16B16A16_SFLOAT,
		.components       = {},
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	};

	swapchain->Window = window;
	HR_INVALID(DX::g_Context->DCompDevice->CreateTargetForHwnd, Wnd::GetNativeHandle(window), true, &swapchain->DCompTarget)
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

	HR_INVALID(DX::g_Context->DXGIFactory->CreateSwapChainForComposition, DX::g_Context->DXGIDevice, &swapchainDesc, nullptr, &swapchain1)
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain1->QueryInterface, &swapchain->Swapchain)
	{
		swapchain1->Release();
		goto INITFAILED;
	}
	swapchain1->Release();
	HR_INVALID(swapchain->DCompVisual->SetContent, swapchain->Swapchain)
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
	HR_INVALID(swapchain->Swapchain->GetBuffer, 0, __uuidof(ID3D11Texture2D), (void**) &swapchain->BackBuffer)
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain->BackBuffer->QueryInterface, &swapchain->BackBufferResource)
	{
		goto INITFAILED;
	}
	HR_INVALID(DX::g_Context->D3D11Device->CreateTexture2D, &frontBufferDesc, nullptr, &swapchain->FrontBuffer)
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain->FrontBuffer->QueryInterface, &swapchain->FrontBufferResource)
	{
		goto INITFAILED;
	}
	HR_INVALID(swapchain->FrontBuffer->QueryInterface, &dxgiResource)
	{
		goto INITFAILED;
	}
	HR_INVALID(dxgiResource->CreateSharedHandle, nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &swapchain->FrontBufferHandle)
	{
		dxgiResource->Release();
		goto INITFAILED;
	}
	dxgiResource->Release();

	VK_INVALID(vkCreateImage, Vk::g_Context->Device, &riCreateInfo, nullptr, &swapchain->ResolveImage)
	{
		goto INITFAILED;
	}
	VK_INVALID(vkGetMemoryWin32HandlePropertiesKHR, Vk::g_Context->Device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, swapchain->FrontBufferHandle, &handleProps)
	{
		goto INITFAILED;
	}
	vkGetImageMemoryRequirements(Vk::g_Context->Device, swapchain->ResolveImage, &mReq);
	rimHandleInfo.handle        = swapchain->FrontBufferHandle;
	rmAllocInfo.allocationSize  = mReq.size;
	rmAllocInfo.memoryTypeIndex = Vk::FindDeviceMemoryIndex(handleProps.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_INVALID(vkAllocateMemory, Vk::g_Context->Device, &rmAllocInfo, nullptr, &swapchain->ResolveMemory)
	{
		goto INITFAILED;
	}
	VK_INVALID(vkBindImageMemory, Vk::g_Context->Device, swapchain->ResolveImage, swapchain->ResolveMemory, 0)
	{
		goto INITFAILED;
	}
	ivCreateInfo.image = swapchain->ResolveImage;
	VK_INVALID(vkCreateImageView, Vk::g_Context->Device, &ivCreateInfo, nullptr, &swapchain->ResolveView)
	{
		goto INITFAILED;
	}

	VK_INVALID(vkCreateImage, Vk::g_Context->Device, &riCreateInfo, nullptr, &swapchain->Image)
	{
		goto INITFAILED;
	}
	vkGetImageMemoryRequirements(Vk::g_Context->Device, swapchain->ResolveImage, &mReq);
	mAllocInfo.allocationSize  = mReq.size;
	mAllocInfo.memoryTypeIndex = Vk::FindDeviceMemoryIndex(mReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_INVALID(vkAllocateMemory, Vk::g_Context->Device, &mAllocInfo, nullptr, &swapchain->ImageMemory)
	{
		goto INITFAILED;
	}
	VK_INVALID(vkBindImageMemory, Vk::g_Context->Device, swapchain->Image, swapchain->ImageMemory, 0)
	{
		goto INITFAILED;
	}
	ivCreateInfo.image = swapchain->Image;
	VK_INVALID(vkCreateImageView, Vk::g_Context->Device, &ivCreateInfo, nullptr, &swapchain->View)
	{
		goto INITFAILED;
	}

	swapchain->Frames = new Vk::FrameState[Vk::g_Context->FramesInFlight];
	for (uint32_t i = 0; i < Vk::g_Context->FramesInFlight; ++i)
	{
		if (!Vk::InitFrameState(Vk::g_Context, &swapchain->Frames[i]))
			goto INITFAILED;
	}

	return true;

INITFAILED:
	DeInitDXGISwapchain(swapchain);
	return false;
}

void DeInitDXGISwapchain(DXGISwapchain* swapchain)
{
	if (!Vk::g_Context || !DX::g_Context || !swapchain)
		return;

	if (swapchain->Frames)
	{
		for (uint32_t i = 0; i < Vk::g_Context->FramesInFlight; ++i)
			Vk::DeInitFrameState(Vk::g_Context, &swapchain->Frames[i]);
		delete[] swapchain->Frames;
	}
	vkDestroyImageView(Vk::g_Context->Device, swapchain->View, nullptr);
	vkDestroyImage(Vk::g_Context->Device, swapchain->Image, nullptr);
	vkFreeMemory(Vk::g_Context->Device, swapchain->ImageMemory, nullptr);
	vkDestroyImageView(Vk::g_Context->Device, swapchain->ResolveView, nullptr);
	vkDestroyImage(Vk::g_Context->Device, swapchain->ResolveImage, nullptr);
	vkFreeMemory(Vk::g_Context->Device, swapchain->ResolveMemory, nullptr);
	if (swapchain->FrontBufferHandle)
		CloseHandle(swapchain->FrontBufferHandle);
	if (swapchain->FrontBufferResource)
		swapchain->FrontBufferResource->Release();
	if (swapchain->FrontBuffer)
		swapchain->FrontBuffer->Release();
	if (swapchain->BackBufferResource)
		swapchain->BackBufferResource->Release();
	if (swapchain->BackBuffer)
		swapchain->BackBuffer->Release();
	if (swapchain->DCompVisual)
		swapchain->DCompVisual->Release();
	if (swapchain->DCompTarget)
		swapchain->DCompTarget->Release();
	if (swapchain->Swapchain)
		swapchain->Swapchain->Release();
	swapchain->View                = nullptr;
	swapchain->Image               = nullptr;
	swapchain->ImageMemory         = nullptr;
	swapchain->ResolveView         = nullptr;
	swapchain->ResolveImage        = nullptr;
	swapchain->ResolveMemory       = nullptr;
	swapchain->FrontBufferHandle   = nullptr;
	swapchain->FrontBufferResource = nullptr;
	swapchain->FrontBuffer         = nullptr;
	swapchain->BackBufferResource  = nullptr;
	swapchain->BackBuffer          = nullptr;
	swapchain->DCompVisual         = nullptr;
	swapchain->DCompTarget         = nullptr;
	swapchain->Swapchain           = nullptr;
	swapchain->Window              = nullptr;
	swapchain->Extents             = {};
}