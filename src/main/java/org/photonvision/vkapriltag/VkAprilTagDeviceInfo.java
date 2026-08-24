package org.photonvision.vkapriltag;

/**
 * One entry per {@code apriltag_vulkan::vk::DeviceCaps} returned by the native library's
 * {@code Context::EnumerateDevices()}. {@code index} is the value to pass back into {@link
 * VkAprilTagJNI#create}'s {@code deviceIndex} to select this device.
 *
 * @param index raw index into {@code vkEnumeratePhysicalDevices} order; pass -1 elsewhere to let
 *     the library auto-select instead of picking a specific device
 * @param name the device's reported name, e.g. "Mali-G610 MP4"
 * @param description a human-readable summary from the native library's own {@code
 *     Context::DescribeDevice()}, including device type and driver API version
 * @param isCpuDevice true if this is a software (non-hardware) Vulkan implementation, e.g. Mesa
 *     lavapipe - excluded from auto-select by default
 * @param apiVersion the device's reported Vulkan API version, packed per {@code
 *     VK_MAKE_API_VERSION}
 */
public record VkAprilTagDeviceInfo(
        int index, String name, String description, boolean isCpuDevice, int apiVersion) {}
