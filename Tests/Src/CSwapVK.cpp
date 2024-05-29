#include "CSwap/CSwap.h"
#include "Shared.h"
#include "Utils/TupleVector.h"

#include <format>
#include <iostream>
#include <thread>

static constexpr const char* c_InstanceExtensions[] {
	VK_KHR_SURFACE_EXTENSION_NAME,
	/*VK_EXT_WINCS_SURFACE_EXTENSION_NAME,*/
	nullptr
};
static constexpr const char* c_DeviceExtensions[] {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	"VK_KHR_external_memory_win32",
	"VK_KHR_external_semaphore_win32",
	nullptr
};

struct SwapchainState
{
	Wnd::Handle*                      Window    = nullptr;
	VkSurfaceKHR                      Surface   = nullptr;
	VkSwapchainKHR                    Swapchain = nullptr;
	VkExtent2D                        Extents   = {};
	TupleVector<VkImage, VkImageView> Images;
	Vk::SwapchainFrameState*          Frames = nullptr;
};

bool InitSwapchainState(SwapchainState* swapchain, Wnd::Handle* window, bool withFrames = false);
void DeInitSwapchainState(SwapchainState* swapchain);

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
		spec.SeparateThread = true;
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

	SwapchainState* swapchains = new SwapchainState[numSwapchains];
	for (int64_t i = 0; i < numSwapchains; ++i)
	{
		Wnd::Spec spec {};
		spec.Title          = std::format("CSwapVK Window {}", i);
		spec.Flags         |= Wnd::WindowCreateFlag::NoBitmap;
		Wnd::Handle* window = Wnd::Create(&spec);
		if (!InitSwapchainState(&swapchains[i], window, true))
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
		.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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

			VK_INVALID(wincs_surface_vkAcquireNextImageKHR, Vk::g_Context->Device, swapchain.Swapchain, ~0ULL, frame.ImageReady, nullptr, &frame.ImageIndex)
			{
				continue;
			}

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

			VkPresentInfoKHR presentInfo {
				.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.pNext              = nullptr,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores    = &frame.RenderDone,
				.swapchainCount     = 1,
				.pSwapchains        = &swapchain.Swapchain,
				.pImageIndices      = &frame.ImageIndex,
				.pResults           = nullptr
			};
			start = Clock::now();
			VK_INVALID(wincs_surface_vkQueuePresentKHR, Vk::g_Context->Queue, &presentInfo)
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
		DeInitSwapchainState(&swapchains[i]);
	delete[] swapchains;

	Vk::DeInit();
	Wnd::DeInit();
	return 0;
}

bool InitSwapchainState(SwapchainState* swapchain, Wnd::Handle* window, bool withFrames)
{
	if (!Vk::g_Context || !swapchain || !window)
		return false;

	swapchain->Window = window;
	do
	{
		VkWinCSSurfaceCreateInfoEXT surfaceCreateInfo {
			.sType     = VK_STRUCTURE_TYPE_WINCS_SURFACE_CREATE_INFO_EXT,
			.pNext     = nullptr,
			.flags     = 0,
			.hinstance = Wnd::GetInstance(),
			.hwnd      = Wnd::GetNativeHandle(window)
		};
		VK_INVALID(vkCreateWinCSSurfaceEXT, Vk::g_Context->Instance, &surfaceCreateInfo, nullptr, &swapchain->Surface)
		{
			break;
		}

		VkSurfaceCapabilitiesKHR caps {};
		VK_INVALID(wincs_surface_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, Vk::g_Context->PhysicalDevice, swapchain->Surface, &caps)
		{
			break;
		}

		swapchain->Extents         = caps.currentExtent;
		swapchain->Extents.width  *= 2;
		swapchain->Extents.height *= 2;
		VkWinCSSwapchainQueueInfoEXT queueInfo {
			.sType = VK_STRUCTURE_TYPE_WINCS_SWAPCHAIN_QUEUE_INFO_EXT,
			.pNext = nullptr,
			.queue = Vk::g_Context->Queue
		};
		VkSwapchainCreateInfoKHR createInfo {
			.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.pNext                 = &queueInfo,
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
			.compositeAlpha        = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			.presentMode           = VK_PRESENT_MODE_MAILBOX_KHR,
			.clipped               = VK_FALSE,
			.oldSwapchain          = nullptr
		};
		VK_INVALID(wincs_surface_vkCreateSwapchainKHR, Vk::g_Context->Device, &createInfo, nullptr, &swapchain->Swapchain)
		{
			break;
		}

		uint32_t imageCount = 0;
		VK_INVALID(wincs_surface_vkGetSwapchainImagesKHR, Vk::g_Context->Device, swapchain->Swapchain, &imageCount, nullptr)
		{
			break;
		}
		swapchain->Images.resize(imageCount, nullptr, nullptr);
		VK_INVALID(wincs_surface_vkGetSwapchainImagesKHR, Vk::g_Context->Device, swapchain->Swapchain, &imageCount, swapchain->Images.column<0>())
		{
			break;
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
		uint32_t i = 0;
		for (; i < imageCount; ++i)
		{
			ivCreateInfo.image = swapchain->Images.entry<0>(i);
			VK_INVALID(vkCreateImageView, Vk::g_Context->Device, &ivCreateInfo, nullptr, &swapchain->Images.entry<1>(i))
			{
				break;
			}
		}
		if (i < imageCount)
			break;

		if (!withFrames)
			return true;

		VkSemaphoreCreateInfo sCreateInfo {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0
		};
		swapchain->Frames = new Vk::SwapchainFrameState[Vk::g_Context->FramesInFlight];
		for (i = 0; i < Vk::g_Context->FramesInFlight; ++i)
		{
			if (!Vk::InitFrameState(Vk::g_Context, &swapchain->Frames[i]))
				break;
			VK_INVALID(vkCreateSemaphore, Vk::g_Context->Device, &sCreateInfo, nullptr, &swapchain->Frames[i].ImageReady)
			{
				break;
			}
		}
		if (i < Vk::g_Context->FramesInFlight)
			break;
		return true;
	}
	while (false);

	if (swapchain->Frames)
	{
		for (uint32_t i = 0; i < Vk::g_Context->FramesInFlight; ++i)
		{
			Vk::DeInitFrameState(Vk::g_Context, &swapchain->Frames[i]);
			vkDestroySemaphore(Vk::g_Context->Device, swapchain->Frames[i].ImageReady, nullptr);
		}
		delete[] swapchain->Frames;
		swapchain->Frames = nullptr;
	}
	for (auto [image, view] : swapchain->Images)
		vkDestroyImageView(Vk::g_Context->Device, view, nullptr);
	swapchain->Images.clear();
	wincs_surface_vkDestroySwapchainKHR(Vk::g_Context->Device, swapchain->Swapchain, nullptr);
	wincs_surface_vkDestroySurfaceKHR(Vk::g_Context->Instance, swapchain->Surface, nullptr);
	swapchain->Swapchain = nullptr;
	swapchain->Surface   = nullptr;
	swapchain->Window    = nullptr;
	swapchain->Extents   = {};
	return false;
}

void DeInitSwapchainState(SwapchainState* swapchain)
{
	if (!Vk::g_Context || !swapchain)
		return;

	if (swapchain->Frames)
	{
		for (uint32_t i = 0; i < Vk::g_Context->FramesInFlight; ++i)
		{
			Vk::DeInitFrameState(Vk::g_Context, &swapchain->Frames[i]);
			vkDestroySemaphore(Vk::g_Context->Device, swapchain->Frames[i].ImageReady, nullptr);
		}
		delete[] swapchain->Frames;
		swapchain->Frames = nullptr;
	}
	for (auto [image, view] : swapchain->Images)
		vkDestroyImageView(Vk::g_Context->Device, view, nullptr);
	swapchain->Images.clear();
	wincs_surface_vkDestroySwapchainKHR(Vk::g_Context->Device, swapchain->Swapchain, nullptr);
	wincs_surface_vkDestroySurfaceKHR(Vk::g_Context->Instance, swapchain->Surface, nullptr);
	swapchain->Swapchain = nullptr;
	swapchain->Surface   = nullptr;
	swapchain->Window    = nullptr;
	swapchain->Extents   = {};
}