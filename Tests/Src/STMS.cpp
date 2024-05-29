#include "Shared.h"
#include "Utils/TupleVector.h"

#include <cstdlib>

#include <chrono>
#include <iostream>

static constexpr const char* c_InstanceExtensions[] {
	VK_KHR_SURFACE_EXTENSION_NAME,
	"VK_KHR_win32_surface",
	nullptr
};
static constexpr const char* c_DeviceExtensions[] {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	nullptr
};

int STMS(size_t argc, const std::string_view* argv)
{
	int64_t numFramesInFlight = 1;
	int64_t numSwapchains     = 4;
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
		spec.AppName          = "STMS";
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

	Vk::SwapchainState* swapchains = new Vk::SwapchainState[numSwapchains];
	for (int64_t i = 0; i < numSwapchains; ++i)
	{
		Wnd::Spec spec {};
		spec.Title = std::format("STMS Window {}", i);
		// spec.Flags         |= Wnd::WindowCreateFlag::NoBitmap;
		Wnd::Handle* window = Wnd::Create(&spec);

		if (!Vk::InitSwapchainState(&swapchains[i], window, true))
		{
			Wnd::Destroy(window);
			for (int64_t j = 0; j < i; ++j)
				Vk::DeInitSwapchainState(&swapchains[j]);
			Vk::DeInit();
			Wnd::DeInit();
			delete[] swapchains;
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
		.clearValue         = { .color = { .float32 = { 0.15f, 0.15f, 0.15f, 1.0f } } }
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
         .stageMask   = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
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

		size_t renderedSwapchains = 0;
		for (int64_t i = 0; i < numSwapchains; ++i)
		{
			auto& swapchain = swapchains[i];
			if (Wnd::IsMinimized(swapchain.Window))
				continue;

			if (updateTitle)
			{
				Wnd::SetWindowTitle(swapchain.Window, std::format("STMS Window {}, FrameTime {:.4} us, FPS {:.5}, PresentTime {:.4} us, WaitTime {:.4} us", i, avgDeltaTime * 1e6, 1.0 / avgDeltaTime, avgPresentTime * 1e6, avgWaitTime * 1e6));
			}

			auto& frame = swapchain.Frames[curFrame];
			for (auto& destroy : frame.Destroys)
				destroy();
			frame.Destroys.clear();
			waitTime += std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

			start = Clock::now();
			if (!Vk::SwapchainAcquireImage(&swapchain))
				continue;
			end = Clock::now();

			VK_INVALID(vkResetCommandPool, Vk::g_Context->Device, frame.Pool, 0)
			{
				continue;
			}
			VK_INVALID(vkBeginCommandBuffer, frame.CmdBuf, &beginInfo)
			{
				continue;
			}

			preImageBarrier.image           = swapchain.Images.entry<0>(frame.ImageIndex);
			postImageBarrier.image          = swapchain.Images.entry<0>(frame.ImageIndex);
			colAttach.imageView             = swapchain.Images.entry<1>(frame.ImageIndex);
			renderingInfo.renderArea.extent = swapchain.Extents;
			depInfo.pImageMemoryBarriers    = &preImageBarrier;
			vkCmdPipelineBarrier2(frame.CmdBuf, &depInfo);
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
			if (!Vk::SwapchainPresent(&swapchain))
				continue;
			end          = Clock::now();
			presentTime += std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
			++renderedSwapchains;
		}
		Vk::NextFrame();

		avgPresentTime = avgPresentTime * 0.99 + presentTime * 0.01;
		avgWaitTime    = avgWaitTime * 0.99 + waitTime * 0.01;
		if (!renderedSwapchains)
			Wnd::WaitForEvent();
	}

	for (int64_t i = 0; i < numSwapchains; ++i)
	{
		auto window = swapchains[i].Window;
		Vk::DeInitSwapchainState(&swapchains[i]);
		Wnd::Destroy(window);
	}
	delete[] swapchains;

	Vk::DeInit();
	Wnd::DeInit();
	return 0;
}