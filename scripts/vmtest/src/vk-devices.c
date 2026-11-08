/* NickelOS: which GPUs a program sees through Vulkan, and through which
 * drivers, built 32-bit or 64-bit (gcc -m32), for testing the prefix's
 * 32-bit graphics (hw/lib32-test.sh): a 32-bit game needs a 32-bit Vulkan
 * loader and a 32-bit driver for its GPU. Prints the word size, then one
 * line per GPU: "NAME: DRIVER DRIVER-INFO (TYPE)".
 *   vk-devices
 * Build: gcc [-m32] -o vk-devices vk-devices.c -lvulkan */
#include <stdio.h>
#include <vulkan/vulkan.h>

int main(void) {
  printf("%zu-bit\n", sizeof(void*) * 8);
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.apiVersion = VK_API_VERSION_1_2;
  VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  VkInstance instance;
  VkResult r = vkCreateInstance(&info, NULL, &instance);
  if (r != VK_SUCCESS) {
    printf("vkCreateInstance failed: %d\n", r);
    return 1;
  }
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, NULL);
  VkPhysicalDevice devices[16];
  if (count > 16) {
    count = 16;
  }
  vkEnumeratePhysicalDevices(instance, &count, devices);
  static const char* const kTypes[] = {"other", "integrated", "discrete",
                                       "virtual", "cpu"};
  for (uint32_t i = 0; i < count; ++i) {
    VkPhysicalDeviceDriverProperties driver = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &driver;
    vkGetPhysicalDeviceProperties2(devices[i], &props);
    printf("%s: %s %s (%s)\n", props.properties.deviceName, driver.driverName,
           driver.driverInfo,
           props.properties.deviceType <= 4
               ? kTypes[props.properties.deviceType]
               : "?");
  }
  vkDestroyInstance(instance, NULL);
  return 0;
}
