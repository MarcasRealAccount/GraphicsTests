# Graphics Tests
This repo contains a collection of graphical tests that I write when I feel like it.
All tests are built and compiled into a single executable that can run the tests.

The most notable graphics test is currently [CSwapVK](https://github.com/MarcasRealAccount/GraphicsTests/blob/main/Tests/Src/CSwapVK.cpp), which implements [Composition Swapchains](https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain-portal) for use with vulkan the same way a normal vulkan swapchain would be used.  
It does depend on the `VK_KHR_external_memory_win32` and `VK_KHR_external_semaphore_win32` device extensions, but apart from that it should work out of the box on modern systems which support Composition Swapchains.  
The implementation for the actual Surface and Swapchain extensions can be found at [CSwap](https://github.com/MarcasRealAccount/GraphicsTests/tree/main/Tests/Src/CSwap), they're not perfect, with mailbox presentation mode they do cause validation layer warnings about the image ready semaphores not being signaled, I think this happens when the recording runs ahead of rendering.  
I also wrote a rather basic document for a potential implementation of this at [CSwap Doc](https://github.com/MarcasRealAccount/GraphicsTests/blob/main/Docs/CSwap.md).  
It does somewhat depend on the window being created with [WS_EX_NOREDIRECTIONBITMAP](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles#:~:text=WS_EX_NOREDIRECTIONBITMAP), which makes it so that the window does not get created with a bitmap, allowing us to make the images instead.
Currently the implementation should always support the following capabilities:
| Format | Colorspace |
| --- | --- |
| VK_FORMAT_B8G8R8A8_UNORM | VK_COLOR_SPACE_SRGB_NONLINEAR_KHR |
| VK_FORMAT_R8G8B8A8_UNORM | VK_COLOR_SPACE_SRGB_NONLINEAR_KHR |
| VK_FORMAT_R16G16B16A16_SFLOAT | VK_COLOR_SPACE_SRGB_NONLINEAR_KHR |
| VK_FORMAT_R16G16B16A16_SFLOAT | VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT |
| VK_FORMAT_A2B10G10R10_UNORM_PACK32 | VK_COLOR_SPACE_SRGB_NONLINEAR_KHR |
| VK_FORMAT_A2B10G10R10_UNORM_PACK32 | VK_COLOR_SPACE_HDR10_ST2084_EXT |

| Present modes |
| --- |
| VK_PRESENT_MODE_FIFO_KHR |
| VK_PRESENT_MODE_MAILBOX_KHR |

| VkSurfaceCapabilitiesKHR field | VkSurfaceCapabilitiesKHR value |
| --- | --- |
| minImageCount | 2 |
| maxImageCount | 8, but configurable up to 31 (limited by PresentationManager, although more images could be created without adding them) |
| currentExtent | { width, height } of window |
| minImageExtent | { 1, 1 } |
| maxImageExtent | { 65535, 65535 } (Not really limited) |
| maxImageArrayLayers | 1 |
| supportedTransforms | VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR (We can support all transforms, but for now I couldn't be bothered to implement them) |
| currentTransform | VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR |
| supportedCompositeAlpha | VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR \| VK_COMPOSITE_ALHPA_PRE_MULTIPLIED_BIT_KHR \| VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR |
| supportedUsageFlags | VK_IMAGE_USAGE_TRANSFER_SRC_BIT \| VK_IMAGE_USAGE_TRANSFER_DST_BIT \| VK_IMAGE_USAGE_SAMPLED_BIT \| VK_IMAGE_USAGE_STORAGE_BIT \| VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT \| VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |

So now after this has been made you're probably wondering why no vulkan drivers support it yet, well long story short, it takes time (even if I only spent a few days to implement it), not to mention they would probably have to rewrite a lot of the original `VK_KHR_win32_surface` implementation to make it work.
But alas it would be nice if they did implement support for this.
