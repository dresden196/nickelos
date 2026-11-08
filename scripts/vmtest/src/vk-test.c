/* NickelOS: the Vulkan devices an app sees (through the Vulkan loader and
 * the drivers registered in icd.d), with their driver names. Build: see
 * vmtest/gpu-test.sh. */
#include <stdio.h>
#include <vulkan/vulkan.h>

int main(void) {
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_2};
  VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
  VkInstance instance;
  VkResult r = vkCreateInstance(&info, NULL, &instance);
  if (r != VK_SUCCESS) {
    printf("vkCreateInstance failed: %d\n", r);
    return 1;
  }
  uint32_t n = 8;
  VkPhysicalDevice devices[8];
  vkEnumeratePhysicalDevices(instance, &n, devices);
  printf("%u Vulkan device(s)\n", n);
  for (uint32_t i = 0; i < n; ++i) {
    VkPhysicalDeviceDriverProperties driver = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &driver};
    vkGetPhysicalDeviceProperties2(devices[i], &props);
    printf("  %s (%s %s), Vulkan %u.%u\n", props.properties.deviceName,
           driver.driverName, driver.driverInfo,
           VK_API_VERSION_MAJOR(props.properties.apiVersion),
           VK_API_VERSION_MINOR(props.properties.apiVersion));
  }
  vkDestroyInstance(instance, NULL);
  return 0;
}
