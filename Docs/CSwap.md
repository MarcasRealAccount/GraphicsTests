# Information
This document will go over everything that is needed in order to implement and use the more modern [Composition Swapchains](https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain-portal) for use with vulkan.

## Preliminaries
Implementing this will require the `VK_KHR_external_memory_win32` and `VK_KHR_external_semaphore_win32` device extensions.

## Create a Composition Swapchain capable surface
To make a Composition Swapchain capable surface all we need is `IPresentationManager*` and `IPresentationSurface*`, however to actually present to a window or to some other Direct Composition Visual, we have to add those parts as well,
for a window surface we need `IDCompositionDevice*`, `IDCompositionTarget*`, `IDCompositionVisual*` and using a previously obtained handle for the presentation surface we can make a Direct Composition visual (idk what the actual type is, so it will just be an `IUnknown*`),
we also need to keep track of an `ID3D11Device5*` for when we create the Swapchain.
```cpp
struct Surface
{
  HWND                  HWnd;
  ID3D11Device5*        D3D11Device;
  IPresentationManager* Manager;
  HANDLE                SurfaceHandle;
  IPresentationSurface* Surface;
  IDCompositionDevice*  DCompDevice;
  IDCompositionTarget*  DCompTarget;
  IDCompositionVisual*  DCompVisual;
  IUnknown*             DCompSurface;
};
```
*P.S. Make sure to validate all `HRESULT` and `VkResult`s returned from functions, I will skip this part for brevity*
To start off with we create the D3D11 Device, noting that it has to be a hardware device and that it supports BGRA textures as well as running single threaded with no threading optimizations (from the documentation provided at the top, this is required).
After creating the device we have to query for an `ID3D11Device5*` interface, because we need access to `ID3D11Fence*` further down the line (assuming we want a full `VkSwapchainKHR` implementation).
```cpp
ID3D11Device* d3d11Device = nullptr;
hr = D311CreateDevice(
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
hr = d3d11Device->QueryInterface(&surface.D3D11Device);
d3d11Device->Release();
```
Now that we have the D3D11 Device we will create the `IDCompositionDevice*`, `IPresentationManager*`, `IPresentationSurface*` and the `IUnknown*` for the Direct Composition Surface.
```cpp
IDXGIDevice*          dxgiDevice          = nullptr;
IPresentationFactory* presentationFactory = nullptr;
hr = surface.D3D11Device->QueryInterface(&dxgiDevice);
hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), (void**) &surface.DCompDevice);
dxgiDevice->Release();
hr = CreatePresentationFactory(surface.D3D11Device, __uuidof(IPresentationFactory), (void**) &presentationFactory);
if (!presentationFactory->IsPresentationSupported())
{
  presentationFactory->Release();
  // Not supported
  return;
}
hr = presentationFactory->CreatePresentationManager(&surface.Manager);
presentationFactory->Release();
hr = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surface.SurfaceHandle);
hr = surface.Manager->CreatePresentationSurface(surface.SurfaceHandle, &surface.Surface);
hr = surface.DCompDevice->CreateSurfaceFromHandle(surface.SurfaceHandle, &surface.DCompSurface);
```
Now we can create the `IDCompositionTarget*` and `IDCompositionVisual*` and set them up to be used.
```cpp
hr = surface.DCompDevice->CreateTargetForHwnd(surface.HWnd, TRUE, &surface.DCompTarget);
hr = surface.DCompDevice->CreateVisual(&surface.DCompVisual);
hr = surface.DCompVisual->SetContent(surface.DCompSurface);
hr = surface.DCompTarget->SetRoot(surface.DCompVisual);
hr = surface.DCompDevice->Commit();
hr = surface.DCompDevice->WaitForCommitCompletion(); // Not really necessary
```
Don't forget to also `CloseHandle` all `HANDLE` types and `->Release()` all COM interface pointers.

## Create a Composition Swapchain capable swapchain
Now that we have a surface, we need to actually build up the swapchain, how you build it up is always up to you, I will only go over the minmum requirements to have something present using FiFo presentation.
I won't go over swapchain recreation, however I can say that it is relatively easy to implement a "Staggered" recreation for the buffers themselves, as each buffer is completely independent of the other buffers.
Each buffer for the swapchain will need the following things:
```cpp
struct SwapchainBuffer
{
  // D3D side
  ID3D11Texture2D*     Texture;
  HANDLE               TextureHandle;
  IPresentationBuffer* PresentationBuffer;
  ID3D11Fence*         PresentFence;
  HANDLE               PresentFenceHandle;
  UINT64               PresentFenceValue;

  // VK side
  VkImage        vkImage;
  VkDeviceMemory vkImageMemory;
  VkSemaphore    vkTimeline;

  // State
  std::atomic_uint8_t State; // It might not be necessary for your uses, however the implementation I provide here is a FiFo only implementation
};
```
The PresentFence and vkTimeline variables are only necessary if you want a full `VkSwapchainKHR` implementation.
The Swapchain will need the following things:
```cpp
struct Swapchain
{
  Surface* Surface;

  std::atomic_uint8_t UsableBufferCount;
  uint32_t            BufferCount;
  uint32_t            BufferIndex;
  SwapchainBuffer     Buffers[MaxBufferCount];
  uint32_t            PresentQueue[MaxBufferCount];
  HANDLE              Events[3 + MaxBufferCount]; // [0]: Lost, [1]: Terminate, [2]: OnBufferRetire, [3,...]: OnBufferRendered
  ID3D11Fence*        RetireFence;

  UINT                  Width;
  UINT                  Height;
  DXGI_FORMAT           Format;
  DXGI_COLOR_SPACE_TYPE ColorSpace;
  DXGI_ALPHA_MODE       AlphaMode;

  std::mutex       Mtx;
  std::atomic_bool PresentThreadRunning; // These two can be made into globals internally in a driver, however then you're left to dealing with all the present ready signals on a separate thread.
  std::thread      PresentThread;
};
```
To get started you can use the following table to convert from vulkan enums to DXGI enums
| VkFormat | DXGI_FORMAT |
| --- | --- |
| VK_FORMAT_B8G8R8A8_UNORM | DXGI_FORMAT_B8G8R8A8_UNORM |
| VK_FORMAT_R8G8B8A8_UNORM | DXGI_FORMAT_R8G8B8A8_UNORM |
| VK_FORMAT_R16G16B16A16_SFLOAT | DXGI_FORMAT_R16G16B16A16_FLOAT |
| VK_FORMAT_A2B10G10R10_UNORM_PACK32 | DXGI_FORMAT_R10G10B10A2_UNORM |

| VkColorSpaceKHR | DXGI_COLOR_SPACE_TYPE |
| --- | --- |
| VK_COLOR_SPACE_SRGB_NONLINEAR_KHR | DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 |
| VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_KHR | DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 |
| VK_COLOR_SPACE_HDR10_ST2084_EXT | DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 |

| VkCompositeAlphaFlagBits | DXGI_ALPHA_MODE |
| --- | --- |
| VK_COMPOSITE_ALPHA_OPAQUE_BIT | DXGI_ALPHA_MODE_IGNORE |
| VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT | DXGI_ALPHA_MODE_PREMULTIPLIED |
| VK_COPMOSITE_ALPHA_POST_MULTIPLIED_BIT | DXGI_ALPHA_MODE_STRAIGHT |

First we need to get an event handle for when the `IPresentationManager*` is lost, which will only happen in very undesired situations like extremely low on memory, driver updates or similar.
We also need a Retiring Fence and an event, which is a fence that gets signaled once a buffer is retired.
We also need to make an event for terminating the thread (Mostly necessary for an event thread since the present thread will get signaled on every VBlank).
We also need an event for every buffer that gets signaled when the present is ready.
```cpp
hr = surface.Manager->GetLostEvent(&swapchain.Events[0]);
hr = surface.Manager->GetPresentRetiringFence(__uuidof(ID3D11Fence), (void**) &swapchain.RetireFence);
swapchain.Events[1] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
swapchain.Events[2] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
for (uint32_t i = 0; i < swapchain.BufferCount; ++i)
  swapchain.Events[3 + i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
```
Now for each buffer we will need to create a shared texture, add it to the `IPresentationManager*`, create a shared fence, and then create the vulkan counterparts.
```cpp
IDXGIResource* dxgiResource = nullptr;
D3D11_TEXTURE2D_DESC textureDesc {
  .Width          = swapchain.Width,
  .Height         = swapchain.Height,
  .MipLevels      = 1,
  .ArraySize      = 1,
  .Format         = swapchain.Format,
  .SampleDesc     = { 1, 0 },
  .Usage          = D3D11_USAGE_DEFAULT,
  .BindFlags      = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
  .CPUAccessFlags = 0,
  .MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE
};
hr = surface.D3D11Device->CreateTexture2D(&textureDesc, nullptr, &buffer.Texture);
hr = surface.Manager->AddBufferFromResource(buffer.Texture, &buffer.PresentationBuffer);
hr = buffer.QueryInterface(&dxgiResource);
hr = dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &buffer.TextureHandle);
dxgiResource->Release();
hr = surface.D3D11Device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**) &buffer.PresentFence);
hr = buffer.PresentFence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &buffer.PresentFenceHandle);

VkMemoryWin32HandlePropertiesKHR handleProps {
  .sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR
};
vr = vkGetMemoryWin32HandlePropertiesKHR(vkDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, buffer.TextureHandle, &handleProps);

VkExternalMemoryImageCreateInfo emiCreateInfo {
  .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
  .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
};
VkImageCreateInfo iCreateInfo {
  .sType                  = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
  .pNext                  = &emiCreateInfo,
  .imageType              = VK_IMAGE_TYPE_2D,
  .format                 = imageFormat,
  .extent                 = { (uint32_t) swapchain.Width, (uint32_t) swapchain.Height, 1 },
  .mipLevels              = 1,
  .arrayLayers            = 1,
  .samples                = VK_SAMPLE_COUNT_1_BIT,
  .tililng                = VK_IMAGE_TILING_OPTIMAL,
  .usage                  = imageUsage,
  .sharingMode            = imageSharingMode,
  .queueFamilyIndexCount  = queueFamilyIndexCount,
  .pQueueFamilyIndexCount = pQueueFamilyIndexCount
  .initialLayout          = VK_IMAGE_LAYOUT_UNDEFINED
};
vr = vkCreateImage(vkDevice, &iCreateInfo, pAllocator, &buffer.vkImage);

VkMemoryRequirements mReq {};
vkGetImageMemoryRequirements(vkDevice, buffer.vkImage, &mReq);

VkImportMemoryWin32HandleInfoKHR imHandleInfo {
  .sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
  .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
  .handle     = buffer.TextureHandle
};
VkMemoryAllocationInfo mAllocInfo {
  .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATION_INFO,
  .pNext           = &imHandleInfo,
  .allocationSize  = mReq.size,
  .memoryTypeIndex = (uint32_t) std::countr_zero(handleProps.memoryTypeBits)
};
vr = vkAllocateMemory(vkDevice, &mAllocInfo, pAllocator, &buffer.vkImageMemory);
vr = vkBindImageMemory(vkDevice, buffer.vkImage, buffer.vkImageMemory, 0);

VkSemaphoreTypeCreateInfo stCreateInfo {
  .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
  .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE
};
VkSemaphoreCreateInfo sCreateInfo {
  .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  .pNext = &stCreateInfo
};
vr = vkCreateSemaphore(vkDevice, &sCreateInfo, pAllocator, &buffer.vkTimeline);

VkImportSemaphoreWin32HandleInfoKHR isHandleInfo {
  .sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
  .semaphore  = buffer.vkTimeline,
  .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE_BIT,
  .handle     = buffer.PresentFenceHandle
};
vr = vkImportSemaphoreWin32HandleKHR(vkDevice, &isHandleInfo);

buffer.State = 0; // Make sure the state of the buffer is in a Renderable state

swapchain.PresentQueue[bufferIndex] = ~0U; // Initialize all present queue entries to ~0U signifying no presents are made
```
Now we should have the swapchain created, we just need to set the thread of the swapchain to a presentation thread, which I'll ommit here, just assume `PresentThreadRunning = true` and the thread is set.
Also don't forget to destroy, release and close all handles created if there's a failure or it's requested to be destroyed.
When the swapchain is to be destroyed signal the Terminate event.

## Acquire an image
For acquiring an image I will assume there's always an infinite timeout, as it simplifies things a little bit with the atomic waits.
First we make sure there's at least one usable buffer by waiting for a non zero value stored in `Swapchain::UsableBufferCount`, after making sure there has to be at least one buffer available, we will then iterate from `Swapchain::BufferIndex` until we reach the starting point to try and find a Renderable buffer.
Once we have found a buffer with a Renderable state we transition it to a Rendering state and remove it from the `Swapchain::UsableBufferCount`.
And we return the index to that buffer, potentially signaling a semaphore and fence immediately.
```cpp
swapchain.UsableBufferCount.wait(0);
uint32_t startIndex = swapchain.BufferIndex;
swapchain.Mtx.lock();
do
{
  uint32_t currentIndex = swapchain.BufferIndex;
  swapchain.BufferIndex = (currentIndex + 1) % swapchain.BufferCount;

  auto& buffer = swapchain.Buffers[currentIndex];
  if (buffer.State != 0) // We represent Renderable buffers with a state of 0.
    continue;

  buffer.State = 1; // We represent Rendering buffers with a state of 1.
  --swapchain.UsableBufferCount;
  swapchain.Mtx.unlock();
  *pImageIndex = currentIndex;
  // Since we generally need to signal a semaphore about when an image is available we just do a basic vkQueueSubmit2 only signaling the semaphore, however it isn't really needed for FiFo as the acquisition will wait until there's a buffer immediately available.
  return;
}
while (swapchain.BufferIndex != startIndex);
swapchain.Mtx.unlock();
```

## Presenting an image
To present an image we will transition a Rendering buffer into a Presentable buffer either immediately or after an OnBufferRendered event (only needed if the present has wait semaphores).
When there are no waitSemaphores we can immediately transition to a Presentable buffer and add the present to the end of the present queue.
Otherwise we have to transition to a Waiting state and make a queue submit that waits for the semaphores and signals the `SwapchainBuffer::vkTimeline` semaphore with a value of `++SwapchainBuffer::PresentFenceValue`, then we tell the `Swapchain::PresentFence` to signal our OnBufferRendered event when its value is the same.
```cpp
auto& buffer = swapchain.Buffers[imageIndex];
if (!waitSemaphoreCount)
{
  buffer.State = 3; // We represent Presentable buffers with a state of 3.
  swapchain.Mtx.lock();
  for (uint32_t i = 0; i < swapchain->BufferCount; ++i)
  {
    if (swapchain->PresentQueue[i] == ~0U)
    {
      swapchain->PresentQueue[i] = imageIndex;
      break;
    }
  }
  swapchain.Mtx.unlock();
}
else
{
  buffer.State = 2; // We represent Waiting buffers with a state of 2.

  VkSemaphoreSubmitInfo* waits; // I'll leave the implementation of this to the reader :) Just fill each semaphore with the waitSemaphore, additionally the values with waitValues (if timeline semaphores are preferred)
  VkSemaphoreSubmitInfo signal {
    .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore = buffer.vkTimeline,
    .value     = ++buffer.PresentFenceValue,
    .stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
  };
  VkSubmitInfo2 submit {
    .sType                     = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .waitSemaphoreInfoCount    = waitSemaphoreCount,
    .pWaitSemaphoreInfos       = waits,
    .signalSemaphoreInfoCount  = 1,
    .pSignalSemaphoreInfoCount = &signal
  };
  vr = vkQueueSubmit2(vkQueue, 1, &submit, nullptr);
  hr = buffer.PresentFence->SetEventOnCompletion(buffer.PresentFenceValue, swapchain.Events[3 + imageIndex]);
}
```

## Present Thread
Now that we can acquire images and present images, we have to truly present on the VBlanks, so we will use a separate thread to ensure we get that to happen, to do that we will use `DCompositionWaitForCompositorClock`.
When we receive an OnBufferRetire event we will iterate over every buffer to see if it can be made available which only happens if the State is Presenting and the `IPresentationBuffer::IsAvailable` returns true. In that case we transition the buffer to a Renderable and add it back to `Swapchain::UsableBufferCount`.
When we receive an OnBufferRendered event we will transition that buffer to a Presentable buffer and add the present to the end of the present queue.
```cpp
while (swapchain.PresentThreadRunning)
{
  DWORD eventIndex = DCompositionWaitForCompositorClock(3 + swapchain.BufferCount, swapchain.Events, INFINITE);
  if (eventIndex < WAIT_OBJECT_0 || eventIndex > WAIT_OBJECT_0 + 3 + swapchain.BufferCount)
    break; // We got some error from WaitForMultipleObjects used internally in DCompositionWaitForCompositorClock.
  if (eventIndex == WAIT_OBJECT_0) // IPresentationManager* Lost event
    break;
  if (eventIndex == WAIT_OBJECT_0 + 1) // Terminate event
    break;

  if (eventIndex == WAIT_OBJECT_0 + 2) // OnBufferRetire
  {
    ResetEvent(swapchain.Events[2]);
    for (uint32_t i = 0; i < swapchain.BufferCount; ++i)
    {
      auto& buffer = swapchain.Buffers[i];
      if (buffer.State != 4) // We represent Presenting buffers with a state of 4.
        continue;
      boolean available = FALSE;
      hr = buffer.PresentationBuffer->IsAvailable(&available);
      if (available)
      {
        buffer.State = 0; // Transition to a Renderable buffer
        ++swapchain.UsableBufferCount;
        swapchain.UsableBufferCount.notify_one();
      }
    }
    continue;
  }
  if (eventIndex < WAIT_OBJECT_0 + 3 + swapchain.BufferCount)
  {
    uint32_t imageIndex = (uint32_t) (eventIndex - WAIT_OBJECT_0 - 3);
    swapchain.Mtx.lock();
    auto& buffer = swapchain.Buffers[imageIndex];
    buffer.State = 3; // We represent Presentable buffers with a state of 3.
    for (uint32_t i = 0; i < swapchain.BufferCount; ++i)
    {
      if (swapchain.PresentQueue[i] == ~0U)
      {
        swapchain.PresentQueue[i] = imageIndex;
        break;
      }
    }
    swapchain.Mtx.unlock();
    continue;
  }

  // VBlank event
  swapchain.Mtx.lock();
  uint32_t imageIndex = swapchain.PresentQueue[0]; // Pop the first present and shift everything left, shifting in a ~0U at the end.
  for (uint32_t i = 1; i < swapchain.BufferCount; ++i)
    swapchain.PresentQueue[i - 1] = swapchain.PresentQueue[i];
  swapchain.PresentQueue[swapchan.BufferCount - 1] = ~0U;
  swapchain.Mtx.unlock();
  if (imageIndex != ~0U)
    continue; // Skip VBlank as nothing is presentable

  auto& buffer = swapchain.Buffers[imageIndex];
  buffer.State = 4; // We represent Presenting buffers with a state of 4.
  RECT rect {
    .right  = (LONG) swapchain.Width,
    .bottom = (LONG) swapchain.Height,
  };
  hr = surface.Surface->SetSourceRect(&rect);
  hr = surface.Surface->SetAlphaMode(swapchain.AlphaMode);
  hr = surface.Surface->SetColorSpace(swapchain.ColorSpace);
  hr = surface.Surface->SetBuffer(buffer.PresentationBuffer);
  SystemInterruptTime time { 0 };
  hr = surface.Manager->SetTargetTime(time);
  UINT64 id = surface.Manager->GetNextPresentId();
  hr = surface.Manager->Present();
  hr = swapchain.RetireFence->SetEventOnCompletion(id, swapchain.Events[2]);
}
```
Now we have everything and it should be possible to use this as if it was a normal `VkSwapchainKHR` (with separate functions probably, as it wouldn't be dispatchable). With minor tweaks it could also be converted into a `VK_PRESENT_MODE_IMMEDIATE` by cancelling all previous presents on a present were we force wait on the waitSemaphores on the CPU.
`VK_PRESENT_MAILBOX_KHR` is also relatively straight forward to implement using this backbone structure, for MAILBOX you want Presentable buffers to also be acquirable, you might also want to allow multiple renders to the same buffer after eachother assuming they wait for the previous render to finish, but I don't think it is all that necessary.
Although MAILBOX also does require a separate thread from the PresentThread, as the OnBufferRendered events will run faster than the VBlank events so it will never get the VBlank events, a solution would be to use the raw VBlank event with WaitForMultipleObjects directly, although I have no idea where one could get the raw VBlank event.


















