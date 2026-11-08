/* NickelOS: Vulkan in an X11 window, the way DXVK and VKD3D (Wine, Proton)
 * and native Vulkan games on X11 present frames: VK_KHR_xcb_surface and a
 * swapchain, which Mesa implements with DRI3 and Present. Prints the GPU and
 * how many frames reach the window. Run inside the session
 * (nickel-session-exec).
 *   x-vk-test SECONDS
 * prints "device=..." (llvmpipe means software), "present-mode=...",
 * "images=N", then "fps=N" after presenting for SECONDS (FIFO: tied to the
 * display's refresh; MAILBOX/IMMEDIATE when offered: as fast as it goes).
 * Build: vmtest/build-test-client.sh x-vk-test xcb vulkan. */
#define _GNU_SOURCE
#define VK_USE_PLATFORM_XCB_KHR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#define CHECK(call)                                       \
  do {                                                    \
    VkResult r = (call);                                  \
    if (r != VK_SUCCESS) {                                \
      printf("failed: %s = %d\n", #call, r);              \
      exit(1);                                            \
    }                                                     \
  } while (0)

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 3;
  int screen_number;
  xcb_connection_t* conn = xcb_connect(NULL, &screen_number);
  if (xcb_connection_has_error(conn)) {
    printf("no display\n");
    return 1;
  }
  xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn));
  for (int i = 0; i < screen_number; i++)
    xcb_screen_next(&it);
  xcb_window_t window = xcb_generate_id(conn);
  xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, it.data->root, 0, 0,
                    640, 480, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    it.data->root_visual, 0, NULL);
  xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
                      XCB_ATOM_STRING, 8, strlen("x-vk-test"), "x-vk-test");
  xcb_map_window(conn, window);
  xcb_flush(conn);

  const char* instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                       VK_KHR_XCB_SURFACE_EXTENSION_NAME};
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_1};
  VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
      .enabledExtensionCount = 2,
      .ppEnabledExtensionNames = instance_extensions};
  VkInstance instance;
  CHECK(vkCreateInstance(&instance_info, NULL, &instance));
  VkXcbSurfaceCreateInfoKHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = conn,
      .window = window};
  VkSurfaceKHR surface;
  CHECK(vkCreateXcbSurfaceKHR(instance, &surface_info, NULL, &surface));

  // The first GPU that can present to the window (a real GPU before a
  // software one).
  uint32_t count = 0;
  CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
  VkPhysicalDevice devices[8];
  if (count > 8)
    count = 8;
  CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
  VkPhysicalDevice gpu = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkPhysicalDeviceProperties properties;
  for (int pass = 0; pass < 2 && !gpu; pass++) {
    for (uint32_t i = 0; i < count && !gpu; i++) {
      vkGetPhysicalDeviceProperties(devices[i], &properties);
      if (pass == 0 && properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
        continue;
      uint32_t families = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, NULL);
      for (uint32_t f = 0; f < families; f++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], f, surface, &present);
        if (present) {
          gpu = devices[i];
          queue_family = f;
          break;
        }
      }
    }
  }
  if (!gpu) {
    printf("no GPU can present to the window\n");
    return 1;
  }
  vkGetPhysicalDeviceProperties(gpu, &properties);
  printf("device=%s\n", properties.deviceName);

  float priority = 1;
  VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queue_family,
      .queueCount = 1,
      .pQueuePriorities = &priority};
  const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount = 1,
      .ppEnabledExtensionNames = device_extensions};
  VkDevice device;
  CHECK(vkCreateDevice(gpu, &device_info, NULL, &device));
  VkQueue queue;
  vkGetDeviceQueue(device, queue_family, 0, &queue);

  VkSurfaceCapabilitiesKHR caps;
  CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));
  uint32_t format_count = 1;
  VkSurfaceFormatKHR format;
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, &format);
  // The fastest present mode offered, as games choose it with vsync off.
  uint32_t mode_count = 8;
  VkPresentModeKHR modes[8];
  CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &mode_count,
                                                  modes));
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  for (uint32_t i = 0; i < mode_count; i++) {
    if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
      mode = modes[i];
    if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR &&
        mode != VK_PRESENT_MODE_MAILBOX_KHR)
      mode = modes[i];
  }
  printf("present-mode=%s\n",
         mode == VK_PRESENT_MODE_MAILBOX_KHR     ? "mailbox"
         : mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate"
                                                 : "fifo");
  VkExtent2D extent = caps.currentExtent;
  if (extent.width == 0xffffffff)
    extent = (VkExtent2D){640, 480};
  uint32_t min_images = caps.minImageCount + 1;
  if (caps.maxImageCount && min_images > caps.maxImageCount)
    min_images = caps.maxImageCount;
  VkSwapchainCreateInfoKHR swapchain_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = min_images,
      .imageFormat = format.format,
      .imageColorSpace = format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = caps.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = mode,
      .clipped = VK_TRUE};
  VkSwapchainKHR swapchain;
  CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
  uint32_t image_count = 0;
  CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL));
  VkImage images[16];
  if (image_count > 16)
    image_count = 16;
  CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));
  printf("images=%u\n", image_count);
  fflush(stdout);

  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family};
  VkCommandPool pool;
  CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
  VkCommandBuffer command_buffer;
  VkCommandBufferAllocateInfo allocate_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  CHECK(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer));
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore acquired, rendered;
  CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired));
  CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &rendered));
  VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                  .flags = VK_FENCE_CREATE_SIGNALED_BIT};
  VkFence fence;
  CHECK(vkCreateFence(device, &fence_info, NULL, &fence));

  int frames = 0;
  double start = now(), end = start + seconds;
  while (now() < end) {
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    CHECK(vkResetFences(device, 1, &fence));
    uint32_t index;
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
                                       VK_NULL_HANDLE, &index);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
      printf("failed: vkAcquireNextImageKHR = %d\n", r);
      return 1;
    }
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(command_buffer, &begin));
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier to_clear = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = images[index],
        .subresourceRange = range};
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         1, &to_clear);
    VkClearColorValue color = {
        {0.2f, 0.4f + 0.4f * (frames % 120) / 120.0f, 0.6f, 1}};
    vkCmdClearColorImage(command_buffer, images[index],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                         &range);
    VkImageMemoryBarrier to_present = to_clear;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
                         NULL, 1, &to_present);
    CHECK(vkEndCommandBuffer(command_buffer));
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .waitSemaphoreCount = 1,
                           .pWaitSemaphores = &acquired,
                           .pWaitDstStageMask = &wait_stage,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &command_buffer,
                           .signalSemaphoreCount = 1,
                           .pSignalSemaphores = &rendered};
    CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &rendered,
                                .swapchainCount = 1,
                                .pSwapchains = &swapchain,
                                .pImageIndices = &index};
    r = vkQueuePresentKHR(queue, &present);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
      printf("failed: vkQueuePresentKHR = %d\n", r);
      return 1;
    }
    frames++;
  }
  printf("fps=%.0f\n", frames / (now() - start));
  vkDeviceWaitIdle(device);
  return 0;
}
