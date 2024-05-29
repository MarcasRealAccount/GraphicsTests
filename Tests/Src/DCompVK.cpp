#include "Shared.h"

#include <cstddef>
#include <cstdint>

#include <chrono>
#include <iostream>
#include <string_view>

static constexpr const char* c_InstanceExtensions[] {
	nullptr
};
static constexpr const char* c_DeviceExtensions[] {
	"VK_KHR_external_memory_win32",
	nullptr
};

struct DCompSwapchain
{
	uint32_t CurrentImage = ~0U;
	uint32_t ImageCount   = 0;
	uint32_t Width        = 0;
	uint32_t Height       = 0;

	struct DCompBuffer
	{
		ID3D11Texture2D*      D3D11BackTexture   = nullptr;
		ID3D11Texture2D*      D3D11FrontTexture  = nullptr;
		ID3D11Resource*       D3D11BackResource  = nullptr;
		ID3D11Resource*       D3D11FrontResource = nullptr;
		IDCompositionTexture* DCompTexture       = nullptr;
		HANDLE                ShareHandle        = nullptr;
		VkDeviceMemory        ResolveMemory      = nullptr;
		VkImage               ResolveImage       = nullptr;
		VkImage               Image              = nullptr;
		VkSemaphore           AcquireSemaphore   = nullptr;
		ID3D11Fence*          AvailabilityFence  = nullptr;
		VkCommandPool         Pool               = nullptr;
		VkCommandBuffer       CmdBuf             = nullptr;
		VkSemaphore           Timeline           = nullptr;
		uint64_t              TimelineValue      = 0;
	};
	DCompBuffer*   Buffers            = nullptr;
	HANDLE*        AvailabilityEvents = nullptr;
	VkDeviceMemory ImageMemory        = nullptr;

	Wnd::Handle*          Window     = nullptr;
	IDCompositionTarget*  CompTarget = nullptr;
	IDCompositionVisual3* CompVisual = nullptr;
};

struct DCompSwapchainSpec
{
	Wnd::Handle* Window     = nullptr;
	uint32_t     ImageCount = 2;
	uint32_t     Width      = 0;
	uint32_t     Height     = 0;
};

static bool InitDCompSwapchain(DCompSwapchain* swapchain, const DCompSwapchainSpec* spec);
static void DeInitDCompSwapchain(DCompSwapchain* swapchain);
static bool DCompSwapchainGetImages(DCompSwapchain* swapchain, uint32_t* imageCount, VkImage* images);
static bool DCompSwapchainAcquireNextImage(DCompSwapchain* swapchain, uint64_t timeout, VkSemaphore semaphore, uint32_t* imageOut);
static bool DCompSwapchainPresent(DCompSwapchain* swapchain, uint32_t image, uint32_t waitSemaphoreCount, VkSemaphore* waitSemaphores);

int DCompVK(size_t argc, const std::string_view* argv)
{
	{
		Wnd::ContextSpec spec {};
		spec.SeparateThread = true;
		if (!Wnd::Init(&spec))
			return 1;
	}
	{
		DX::ContextSpec spec {};
		spec.WithComposition = true;
		if (!DX::Init(&spec))
		{
			Wnd::DeInit();
			return 1;
		}
	}
	{
		Vk::ContextSpec spec {};
		spec.AppName          = "DCompVK";
		spec.AppVersion       = VK_MAKE_API_VERSION(0, 1, 0, 0);
		spec.InstanceExtCount = (uint32_t) (sizeof(c_InstanceExtensions) / sizeof(*c_InstanceExtensions) - 1);
		spec.InstanceExts     = c_InstanceExtensions;
		spec.DeviceExtCount   = (uint32_t) (sizeof(c_DeviceExtensions) / sizeof(*c_DeviceExtensions) - 1);
		spec.DeviceExts       = c_DeviceExtensions;
		if (!Vk::Init(&spec))
		{
			DX::DeInit();
			Wnd::DeInit();
			return 1;
		}
	}

	Wnd::Handle* window = nullptr;
	{
		Wnd::Spec spec {};
		spec.Flags |= Wnd::WindowCreateFlag::NoBitmap;
		window      = Wnd::Create(&spec);
	}

	DCompSwapchain swapchain {};
	VkSemaphore    imageReady = nullptr;
	uint32_t       imageIndex = 0;
	uint32_t       imageCount = 0;
	VkImage*       images     = nullptr;
	VkImageView*   imageViews = nullptr;
	{
		DCompSwapchainSpec spec {};
		spec.Window     = window;
		spec.ImageCount = 2;
		if (!InitDCompSwapchain(&swapchain, &spec))
		{
			Wnd::Destroy(window);
			Vk::DeInit();
			DX::DeInit();
			Wnd::DeInit();
			return 1;
		}
		DCompSwapchainGetImages(&swapchain, &imageCount, nullptr);
		images     = new VkImage[imageCount];
		imageViews = new VkImageView[imageCount];
		DCompSwapchainGetImages(&swapchain, &imageCount, images);
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
		for (uint32_t i = 0; i < imageCount; ++i)
		{
			ivCreateInfo.image = images[i];
			vkCreateImageView(Vk::g_Context->Device, &ivCreateInfo, nullptr, &imageViews[i]);
		}

		VkSemaphoreCreateInfo createInfo {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0
		};
		vkCreateSemaphore(Vk::g_Context->Device, &createInfo, nullptr, &imageReady);
	}

	auto& frame = Vk::g_Context->Frames[0];

	using Clock = std::chrono::high_resolution_clock;

	VkCommandBufferBeginInfo beginInfo {
		.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext            = nullptr,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr
	};
	VkImageMemoryBarrier2 swapchainImagePreBarrier {
		.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext               = nullptr,
		.srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
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
	VkImageMemoryBarrier2 swapchainImagePostBarrier {
		.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext               = nullptr,
		.srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		.dstAccessMask       = VK_ACCESS_2_NONE,
		.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
		.pImageMemoryBarriers     = nullptr
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
		.clearValue         = { .color = { .float32 = { 0.95f, 0.1f, 0.05f, 0.95f } } }
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
	VkSemaphoreSubmitInfo signals[] {
		{
         .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .pNext       = nullptr,
         .semaphore   = nullptr,
         .value       = 0,
         .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
         .deviceIndex = 0,
		 },
		{
         .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .pNext       = nullptr,
         .semaphore   = nullptr,
         .value       = 0,
         .stageMask   = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
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
		.signalSemaphoreInfoCount = (uint32_t) (sizeof(signals) / sizeof(*signals)),
		.pSignalSemaphoreInfos    = signals
	};

	double avgDeltaTime    = 0.0;
	double avgPresentTime  = 0.0;
	double avgWaitTime     = 0.0;
	auto   previousTime    = Clock::now();
	auto   updateTitleTime = previousTime;
	while (!Wnd::QuitSignaled())
	{
		if (Wnd::GetWantsClose(window))
			break;

		Clock::time_point start, end;
		double            waitTime    = 0.0;
		double            presentTime = 0.0;

		auto   currentTime = Clock::now();
		double deltaTime   = std::chrono::duration_cast<std::chrono::duration<double>>(currentTime - previousTime).count();
		previousTime       = currentTime;
		avgDeltaTime       = avgDeltaTime * 0.99 + deltaTime * 0.01;
		bool updateTitle   = currentTime - updateTitleTime > std::chrono::duration<double>(0.25);
		if (updateTitle)
		{
			updateTitleTime = currentTime;
			Wnd::SetWindowTitle(window, std::format("FrameTime: {:.4} us, FPS {:.5} FPS, PresentTime: {:.4} us, WaitTime: {:.4} us", avgDeltaTime * 1e6, 1.0 / avgDeltaTime, avgPresentTime * 1e6, avgWaitTime * 1e6));
		}

		// Wait for currentFrame - framesInFlight to finish
		{
			VkSemaphoreWaitInfo waitInfo {
				.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.pNext          = nullptr,
				.flags          = 0,
				.semaphoreCount = 1,
				.pSemaphores    = &frame.Timeline,
				.pValues        = &frame.TimelineValue
			};
			start = Clock::now();
			vkWaitSemaphores(Vk::g_Context->Device, &waitInfo, ~0ULL);
			end       = Clock::now();
			waitTime += std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
		}

		DCompSwapchainAcquireNextImage(&swapchain, ~0ULL, imageReady, &imageIndex);

		VK_VALIDATE(vkResetCommandPool, Vk::g_Context->Device, frame.Pool, 0);
		VK_VALIDATE(vkBeginCommandBuffer, frame.CmdBuf, &beginInfo);

		swapchainImagePreBarrier.image  = images[imageIndex];
		swapchainImagePostBarrier.image = images[imageIndex];
		depInfo.pImageMemoryBarriers    = &swapchainImagePreBarrier;
		vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);

		colAttach.imageView             = imageViews[imageIndex];
		renderingInfo.renderArea.extent = { swapchain.Width, swapchain.Height };
		vkCmdBeginRendering(frame.CmdBuf, &renderingInfo);

		vkCmdEndRendering(frame.CmdBuf);

		depInfo.pImageMemoryBarriers = &swapchainImagePostBarrier;
		vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);

		VK_VALIDATE(vkEndCommandBuffer, frame.CmdBuf);

		cmdBufInfo.commandBuffer = frame.CmdBuf;
		imageReadyWait.semaphore = imageReady;
		signals[0].semaphore     = frame.RenderDone;
		signals[1].semaphore     = frame.Timeline;
		signals[1].value         = ++frame.TimelineValue;
		VK_VALIDATE(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr);

		start = Clock::now();
		DCompSwapchainPresent(&swapchain, imageIndex, 1, &frame.RenderDone);
		end          = Clock::now();
		presentTime += std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

		avgWaitTime    = avgWaitTime * 0.99 + waitTime * 0.01;
		avgPresentTime = avgPresentTime * 0.99 + presentTime * 0.01;

		Wnd::PollEvents();
	}

	DeInitDCompSwapchain(&swapchain);
	Wnd::Destroy(window);

	Vk::DeInit();
	DX::DeInit();
	Wnd::DeInit();
	return 0;
}

bool InitDCompSwapchain(DCompSwapchain* swapchain, const DCompSwapchainSpec* spec)
{
	if (!swapchain || !spec)
		return false;

	if (!spec->Window)
		return false;
	if (spec->ImageCount < 2)
		return false;
	swapchain->CurrentImage = ~0U;
	swapchain->ImageCount   = spec->ImageCount;
	if (spec->Width == 0 ||
		spec->Height == 0)
	{
		Wnd::GetWindowSize(spec->Window, swapchain->Width, swapchain->Height);
	}
	else
	{
		swapchain->Width  = spec->Width;
		swapchain->Height = spec->Height;
	}
	swapchain->Window = spec->Window;

	IDCompositionVisual2* compVisual   = nullptr;
	IDXGIResource1*       dxgiResource = nullptr;
	D3D11_TEXTURE2D_DESC  d3d11BackTextureDesc {
		 .Width          = (UINT) swapchain->Width,
		 .Height         = (UINT) swapchain->Height,
		 .MipLevels      = 1,
		 .ArraySize      = 1,
		 .Format         = DXGI_FORMAT_R8G8B8A8_UNORM,
		 .SampleDesc     = {1, 0},
		 .Usage          = D3D11_USAGE_DEFAULT,
		 .BindFlags      = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
		 .CPUAccessFlags = 0,
		 .MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE
	};
	D3D11_TEXTURE2D_DESC d3d11FrontTextureDesc {
		.Width          = (UINT) swapchain->Width,
		.Height         = (UINT) swapchain->Height,
		.MipLevels      = 1,
		.ArraySize      = 1,
		.Format         = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc     = {1, 0},
		.Usage          = D3D11_USAGE_DEFAULT,
		.BindFlags      = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
		.CPUAccessFlags = 0,
		.MiscFlags      = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX
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
		.format                = VK_FORMAT_R8G8B8A8_UNORM,
		.extent                = {swapchain->Width, swapchain->Height, 1},
		.mipLevels             = 1,
		.arrayLayers           = 1,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
		.format                = VK_FORMAT_R8G8B8A8_UNORM,
		.extent                = {swapchain->Width, swapchain->Height, 1},
		.mipLevels             = 1,
		.arrayLayers           = 1,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
	VkImageMemoryBarrier2 preImageBarriers[] {
		{
         .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .pNext               = nullptr,
         .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
         .srcAccessMask       = VK_ACCESS_2_NONE,
         .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
         .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = nullptr,
         .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
		 },
		{
         .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .pNext               = nullptr,
         .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
         .srcAccessMask       = VK_ACCESS_2_NONE,
         .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
         .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = nullptr,
         .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
		 }
	};
	VkImageMemoryBarrier2 postImageBarriers[] {
		{
         .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .pNext               = nullptr,
         .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
         .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
         .dstStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
         .dstAccessMask       = VK_ACCESS_2_NONE,
         .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = nullptr,
         .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
		 },
		{
         .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .pNext               = nullptr,
         .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
         .srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
         .dstStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
         .dstAccessMask       = VK_ACCESS_2_NONE,
         .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = nullptr,
         .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
		 }
	};
	VkDependencyInfo depInfo {
		.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.pNext                    = nullptr,
		.dependencyFlags          = 0,
		.memoryBarrierCount       = 0,
		.bufferMemoryBarrierCount = 0,
		.imageMemoryBarrierCount  = 2,
		.pImageMemoryBarriers     = nullptr
	};
	size_t   imageMemorySize     = 0;
	size_t   imageMemoryOffset   = 0;
	uint32_t imageMemoryTypeBits = ~0U;

	HR_INVALID(DX::g_Context->DCompDevice->CreateTargetForHwnd, Wnd::GetNativeHandle(swapchain->Window), true, &swapchain->CompTarget)
	{
		goto INITFAILED;
	}
	HR_INVALID(DX::g_Context->DCompDevice2->CreateVisual, &compVisual)
	{
		goto INITFAILED;
	}
	HR_INVALID(compVisual->QueryInterface, &swapchain->CompVisual)
	{
		compVisual->Release();
		goto INITFAILED;
	}
	compVisual->Release();
	HR_INVALID(swapchain->CompTarget->SetRoot, swapchain->CompVisual)
	{
		goto INITFAILED;
	}
	HR_INVALID(DX::g_Context->DCompDevice2->Commit)
	{
		goto INITFAILED;
	}

	swapchain->Buffers            = new DCompSwapchain::DCompBuffer[swapchain->ImageCount];
	swapchain->AvailabilityEvents = new HANDLE[swapchain->ImageCount];
	for (uint32_t i = 0; i < swapchain->ImageCount; ++i)
		swapchain->AvailabilityEvents[i] = nullptr;
	for (uint32_t i = 0; i < swapchain->ImageCount; ++i)
	{
		auto& buffer                     = swapchain->Buffers[i];
		swapchain->AvailabilityEvents[i] = CreateEventW(nullptr, TRUE, TRUE, nullptr);
		HR_INVALID(DX::g_Context->D3D11Device->CreateTexture2D, &d3d11BackTextureDesc, nullptr, &buffer.D3D11BackTexture)
		{
			goto INITFAILED;
		}
		HR_INVALID(DX::g_Context->D3D11Device->CreateTexture2D, &d3d11FrontTextureDesc, nullptr, &buffer.D3D11FrontTexture)
		{
			goto INITFAILED;
		}
		HR_INVALID(DX::g_Context->DCompDevice2->CreateCompositionTexture, buffer.D3D11BackTexture, &buffer.DCompTexture)
		{
			goto INITFAILED;
		}
		buffer.DCompTexture->SetAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED);
		HR_INVALID(buffer.D3D11BackTexture->QueryInterface, &buffer.D3D11BackResource)
		{
			goto INITFAILED;
		}
		HR_INVALID(buffer.D3D11FrontTexture->QueryInterface, &buffer.D3D11FrontResource)
		{
			goto INITFAILED;
		}
		HR_INVALID(buffer.D3D11FrontTexture->QueryInterface, &dxgiResource)
		{
			goto INITFAILED;
		}
		HR_INVALID(dxgiResource->CreateSharedHandle, nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &buffer.ShareHandle)
		{
			dxgiResource->Release();
			goto INITFAILED;
		}
		dxgiResource->Release();

		VK_INVALID(vkCreateImage, Vk::g_Context->Device, &riCreateInfo, nullptr, &buffer.ResolveImage)
		{
			goto INITFAILED;
		}
		VK_INVALID(vkCreateImage, Vk::g_Context->Device, &iCreateInfo, nullptr, &buffer.Image)
		{
			goto INITFAILED;
		}
		VK_INVALID(vkGetMemoryWin32HandlePropertiesKHR, Vk::g_Context->Device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, buffer.ShareHandle, &handleProps)
		{
			goto INITFAILED;
		}
		vkGetImageMemoryRequirements(Vk::g_Context->Device, buffer.ResolveImage, &mReq);
		rimHandleInfo.handle        = buffer.ShareHandle;
		rmAllocInfo.allocationSize  = mReq.size;
		rmAllocInfo.memoryTypeIndex = Vk::FindDeviceMemoryIndex(handleProps.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_INVALID(vkAllocateMemory, Vk::g_Context->Device, &rmAllocInfo, nullptr, &buffer.ResolveMemory)
		{
			goto INITFAILED;
		}
		VK_INVALID(vkBindImageMemory, Vk::g_Context->Device, buffer.ResolveImage, buffer.ResolveMemory, 0)
		{
			goto INITFAILED;
		}
		vkGetImageMemoryRequirements(Vk::g_Context->Device, buffer.Image, &mReq);
		imageMemoryTypeBits &= mReq.memoryTypeBits;
		imageMemorySize      = (imageMemorySize + mReq.alignment - 1) / mReq.alignment * mReq.alignment + mReq.size;
	}
	mAllocInfo.allocationSize  = imageMemorySize;
	mAllocInfo.memoryTypeIndex = Vk::FindDeviceMemoryIndex(imageMemoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_INVALID(vkAllocateMemory, Vk::g_Context->Device, &mAllocInfo, nullptr, &swapchain->ImageMemory)
	{
		goto INITFAILED;
	}
	for (uint32_t i = 0; i < swapchain->ImageCount; ++i)
	{
		auto& buffer = swapchain->Buffers[i];
		vkGetImageMemoryRequirements(Vk::g_Context->Device, buffer.Image, &mReq);
		imageMemoryOffset = (imageMemoryOffset + mReq.alignment - 1) / mReq.alignment * mReq.alignment;
		VK_INVALID(vkBindImageMemory, Vk::g_Context->Device, buffer.Image, swapchain->ImageMemory, imageMemoryOffset)
		{
			goto INITFAILED;
		}
		imageMemoryOffset += mReq.size;
	}
	for (uint32_t i = 0; i < swapchain->ImageCount; ++i)
	{
		auto& buffer = swapchain->Buffers[i];
		VK_INVALID(vkCreateCommandPool, Vk::g_Context->Device, &pCreateInfo, nullptr, &buffer.Pool)
		{
			goto INITFAILED;
		}
		allocInfo.commandPool = buffer.Pool;
		VK_INVALID(vkAllocateCommandBuffers, Vk::g_Context->Device, &allocInfo, &buffer.CmdBuf)
		{
			goto INITFAILED;
		}
		VK_INVALID(vkCreateSemaphore, Vk::g_Context->Device, &sCreateInfo, nullptr, &buffer.Timeline)
		{
			goto INITFAILED;
		}

		VkCommandBufferBeginInfo beginInfo {
			.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext            = nullptr,
			.flags            = 0,
			.pInheritanceInfo = nullptr
		};
		VK_INVALID(vkBeginCommandBuffer, buffer.CmdBuf, &beginInfo)
		{
			goto INITFAILED;
		}

		preImageBarriers[0].image    = buffer.ResolveImage;
		preImageBarriers[1].image    = buffer.Image;
		postImageBarriers[0].image   = buffer.ResolveImage;
		postImageBarriers[1].image   = buffer.Image;
		depInfo.pImageMemoryBarriers = preImageBarriers;
		vkCmdPipelineBarrier2(buffer.CmdBuf, &depInfo);

		VkImageBlit region {
			.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
			.srcOffsets     = { { 0, 0, 0 }, { (int32_t) swapchain->Width, (int32_t) swapchain->Height, 1 } },
			.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
			.dstOffsets     = { { 0, 0, 0 }, { (int32_t) swapchain->Width, (int32_t) swapchain->Height, 1 } }
		};
		vkCmdBlitImage(buffer.CmdBuf, buffer.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.ResolveImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);

		depInfo.pImageMemoryBarriers = postImageBarriers;
		vkCmdPipelineBarrier2(buffer.CmdBuf, &depInfo);

		VK_INVALID(vkEndCommandBuffer, buffer.CmdBuf)
		{
			goto INITFAILED;
		}
	}
	return true;

INITFAILED:
	DeInitDCompSwapchain(swapchain);
	return false;
}

void DeInitDCompSwapchain(DCompSwapchain* swapchain)
{
	if (!swapchain)
		return;

	for (uint32_t i = 0; i < swapchain->ImageCount; ++i)
	{
		auto& buffer = swapchain->Buffers[i];
		vkDestroySemaphore(Vk::g_Context->Device, buffer.Timeline, nullptr);
		vkDestroyCommandPool(Vk::g_Context->Device, buffer.Pool, nullptr);
		vkDestroyImage(Vk::g_Context->Device, buffer.Image, nullptr);
		vkDestroyImage(Vk::g_Context->Device, buffer.ResolveImage, nullptr);
		vkFreeMemory(Vk::g_Context->Device, buffer.ResolveMemory, nullptr);
		if (buffer.AvailabilityFence)
			buffer.AvailabilityFence->Release();
		if (swapchain->AvailabilityEvents[i])
			CloseHandle(swapchain->AvailabilityEvents[i]);
		if (buffer.ShareHandle)
			CloseHandle(buffer.ShareHandle);
		if (buffer.DCompTexture)
			buffer.DCompTexture->Release();
		if (buffer.D3D11FrontResource)
			buffer.D3D11FrontResource->Release();
		if (buffer.D3D11BackResource)
			buffer.D3D11BackResource->Release();
		if (buffer.D3D11FrontTexture)
			buffer.D3D11FrontTexture->Release();
		if (buffer.D3D11BackTexture)
			buffer.D3D11BackTexture->Release();
	}
	delete[] swapchain->Buffers;
	delete[] swapchain->AvailabilityEvents;
	swapchain->Buffers      = nullptr;
	swapchain->ImageCount   = 0;
	swapchain->CurrentImage = 0;
	vkFreeMemory(Vk::g_Context->Device, swapchain->ImageMemory, nullptr);
	if (swapchain->CompVisual)
		swapchain->CompVisual->Release();
	swapchain->CompVisual = nullptr;
	if (swapchain->CompTarget)
		swapchain->CompTarget->Release();
	swapchain->CompTarget = nullptr;
	swapchain->Window     = nullptr;
	swapchain->Width      = 0;
	swapchain->Height     = 0;
}

bool DCompSwapchainGetImages(DCompSwapchain* swapchain, uint32_t* imageCount, VkImage* images)
{
	if (!swapchain || !imageCount)
		return false;
	if (!images)
	{
		*imageCount = swapchain->ImageCount;
		return true;
	}
	if (*imageCount < swapchain->ImageCount)
	{
		*imageCount = swapchain->ImageCount;
		return false;
	}
	for (uint32_t i = 0; i < swapchain->ImageCount; ++i)
		images[i] = swapchain->Buffers[i].Image;
	*imageCount = swapchain->ImageCount;
	return true;
}

bool DCompSwapchainAcquireNextImage(DCompSwapchain* swapchain, uint64_t timeout, VkSemaphore semaphore, uint32_t* imageOut)
{
	if (!swapchain || !imageOut)
		return false;

	while (true)
	{
		DWORD index = WaitForMultipleObjectsEx(swapchain->ImageCount, swapchain->AvailabilityEvents, FALSE, (DWORD) std::min<size_t>(timeout / 1'000'000, std::numeric_limits<DWORD>::max()), FALSE);
		if (index < WAIT_OBJECT_0 || index >= WAIT_OBJECT_0 + swapchain->ImageCount)
			continue;
		uint32_t image = (uint32_t) index - WAIT_OBJECT_0;

		VkSemaphoreSubmitInfo signalSemaphore {
			.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.pNext       = nullptr,
			.semaphore   = semaphore,
			.value       = 0,
			.stageMask   = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.deviceIndex = 0
		};
		VkSubmitInfo2 submit {
			.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.pNext                    = nullptr,
			.waitSemaphoreInfoCount   = 0,
			.pWaitSemaphoreInfos      = nullptr,
			.commandBufferInfoCount   = 0,
			.pCommandBufferInfos      = nullptr,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos    = &signalSemaphore
		};
		VK_INVALID(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr)
		{
			return false;
		}

		ResetEvent(swapchain->AvailabilityEvents[image]);
		if (swapchain->Buffers[image].AvailabilityFence)
			swapchain->Buffers[image].AvailabilityFence->Release();
		swapchain->Buffers[image].AvailabilityFence = nullptr;
		*imageOut                                   = image;
		return true;
	}
}

bool DCompSwapchainPresent(DCompSwapchain* swapchain, uint32_t image, uint32_t waitSemaphoreCount, VkSemaphore* waitSemaphores)
{
	if (!swapchain)
		return false;
	if (image >= swapchain->ImageCount)
		return false;

	auto& buffer = swapchain->Buffers[image];
	{
		VkSemaphoreSubmitInfo* waitSemas = new VkSemaphoreSubmitInfo[waitSemaphoreCount];
		for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
		{
			waitSemas[i] = {
				.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.pNext       = nullptr,
				.semaphore   = waitSemaphores[i],
				.value       = 0,
				.stageMask   = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				.deviceIndex = 0
			};
		}
		VkSemaphoreSubmitInfo timelineSignal {
			.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.pNext       = nullptr,
			.semaphore   = buffer.Timeline,
			.value       = ++buffer.TimelineValue,
			.stageMask   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			.deviceIndex = 0
		};
		VkCommandBufferSubmitInfo cmdBufInfo {
			.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.pNext         = nullptr,
			.commandBuffer = buffer.CmdBuf,
			.deviceMask    = 0
		};
		VkSubmitInfo2 submit {
			.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.pNext                    = nullptr,
			.flags                    = 0,
			.waitSemaphoreInfoCount   = waitSemaphoreCount,
			.pWaitSemaphoreInfos      = waitSemas,
			.commandBufferInfoCount   = 1,
			.pCommandBufferInfos      = &cmdBufInfo,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos    = &timelineSignal
		};
		VK_INVALID(vkQueueSubmit2, Vk::g_Context->Queue, 1, &submit, nullptr)
		{
			return false;
		}
		delete[] waitSemas;

		VkSemaphoreWaitInfo waitInfo {
			.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.pNext          = nullptr,
			.flags          = 0,
			.semaphoreCount = 1,
			.pSemaphores    = &buffer.Timeline,
			.pValues        = &buffer.TimelineValue,
		};
		VK_INVALID(vkWaitSemaphores, Vk::g_Context->Device, &waitInfo, ~0ULL) // INFO: This should be replaced with some other event mechanism that sets the content
		{
			return false;
		}
	}

	DX::g_Context->D3D11DeviceContext->CopyResource(buffer.D3D11BackResource, buffer.D3D11FrontResource);

	HR_INVALID(swapchain->CompVisual->SetContent, buffer.DCompTexture)
	{
		return false;
	}
	HR_INVALID(DX::g_Context->DCompDevice2->Commit)
	{
		return false;
	}
	if (swapchain->CurrentImage != ~0U)
	{
		auto&  currentBuffer = swapchain->Buffers[swapchain->CurrentImage];
		UINT64 fenceValue    = 0;
		HR_INVALID(currentBuffer.DCompTexture->GetAvailableFence, &fenceValue, __uuidof(ID3D11Fence), (void**) &currentBuffer.AvailabilityFence)
		{
			return false;
		}
		if (!currentBuffer.AvailabilityFence)
		{
			return false;
		}
		HR_INVALID(currentBuffer.AvailabilityFence->SetEventOnCompletion, fenceValue, swapchain->AvailabilityEvents[swapchain->CurrentImage])
		{
			return false;
		}
	}
	swapchain->CurrentImage = image;
	return true;
}