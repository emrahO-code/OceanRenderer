#include "Renderer/VulkanRendererInternal.hpp"

namespace water {
namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        spdlog::error("Vulkan: {}", data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        spdlog::warn("Vulkan: {}", data->pMessage);
    } else {
        spdlog::debug("Vulkan: {}", data->pMessage);
    }
    return VK_FALSE;
}

} // namespace

void check(const VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " + std::to_string(result));
    }
}

std::vector<char> readBinary(const std::string& path)
{
    std::ifstream stream(path, std::ios::ate | std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open shader: " + path);
    const auto size = static_cast<std::size_t>(stream.tellg());
    std::vector<char> data(size);
    stream.seekg(0);
    stream.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

bool containsExtension(
    const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::ranges::any_of(
        extensions, [name](const auto& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
}

} // namespace water

namespace water {

VulkanRenderer::Impl::Impl(GLFWwindow* nativeWindow) : window(nativeWindow)
{
    createInstance();
    createSurface();
    selectPhysicalDevice();
    createDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createDescriptorResources();
    createPipelines();
    createComputeResources();
    createCommandPool();
    createDepthResources();
    createFramebuffers();
    createUniformBuffer();
    createSampler();
    createSyncObjects();
    createImGui();
}

VulkanRenderer::Impl::~Impl()
{
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
    destroyOceanResources();
    destroyImGui();
    destroySwapchain();
    if (oceanSpectrumPipeline) {
        vkDestroyPipeline(device, oceanSpectrumPipeline, nullptr);
    }
    if (oceanFftPipeline) {
        vkDestroyPipeline(device, oceanFftPipeline, nullptr);
    }
    if (oceanFinalizePipeline) {
        vkDestroyPipeline(device, oceanFinalizePipeline, nullptr);
    }
    if (oceanComputeLayout) {
        vkDestroyPipelineLayout(device, oceanComputeLayout, nullptr);
    }
    if (oceanComputePool) {
        vkDestroyDescriptorPool(device, oceanComputePool, nullptr);
    }
    if (oceanComputeDescriptorLayout) {
        vkDestroyDescriptorSetLayout(
            device, oceanComputeDescriptorLayout, nullptr);
    }
    if (timestampPool) vkDestroyQueryPool(device, timestampPool, nullptr);
    destroyBuffer(uniformBuffer);
    if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    if (imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(device, imageAvailable, nullptr);
    for (const auto semaphore : renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    if (frameFence != VK_NULL_HANDLE) vkDestroyFence(device, frameFence, nullptr);
    if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (debugMessenger != VK_NULL_HANDLE) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, debugMessenger, nullptr);
    }
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
}

void VulkanRenderer::Impl::createInstance()
{
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "WaterRenderer";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "WaterRenderer";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_2;

    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(
        glfwExtensions, glfwExtensions + glfwExtensionCount);

    std::uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());

    VkInstanceCreateFlags flags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (containsExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    std::vector<const char*> layers;
#if defined(WATER_ENABLE_VALIDATION) && !defined(NDEBUG)
    std::uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layerProperties(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layerProperties.data());
    const bool validationAvailable = std::ranges::any_of(
        layerProperties, [](const auto& layer) {
            return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });
    if (validationAvailable) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
        spdlog::warn("Vulkan validation layer is unavailable");
    }
#endif

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.flags = flags;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");

    if (!layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debugInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugCallback;
        const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create) check(create(instance, &debugInfo, nullptr, &debugMessenger),
                          "vkCreateDebugUtilsMessengerEXT");
    }
}

void VulkanRenderer::Impl::createSurface()
{
    check(glfwCreateWindowSurface(instance, window, nullptr, &surface),
          "glfwCreateWindowSurface");
}

QueueFamilies VulkanRenderer::Impl::findQueueFamilies(const VkPhysicalDevice candidate) const
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, properties.data());
    QueueFamilies result;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            result.graphics = index;
        }
        VkBool32 supportsPresentation = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            candidate, index, surface, &supportsPresentation);
        if (supportsPresentation) result.present = index;
        if (result.complete()) break;
    }
    return result;
}

SwapchainSupport VulkanRenderer::Impl::querySwapchain(const VkPhysicalDevice candidate) const
{
    SwapchainSupport result;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        candidate, surface, &result.capabilities);
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &count, nullptr);
    result.formats.resize(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        candidate, surface, &count, result.formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &count, nullptr);
    result.presentModes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        candidate, surface, &count, result.presentModes.data());
    return result;
}

bool VulkanRenderer::Impl::deviceSuitable(const VkPhysicalDevice candidate) const
{
    const auto families = findQueueFamilies(candidate);
    if (!families.complete()) return false;
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data());
    if (!containsExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return false;
    const auto support = querySwapchain(candidate);
    return !support.formats.empty() && !support.presentModes.empty();
}

void VulkanRenderer::Impl::selectPhysicalDevice()
{
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan physical device found");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    for (const auto candidate : devices) {
        if (!deviceSuitable(candidate)) continue;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (physicalDevice == VK_NULL_HANDLE ||
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physicalDevice = candidate;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("No Vulkan device supports graphics + presentation");
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    deviceName = properties.deviceName;
    timestampPeriod = properties.limits.timestampPeriod;
    const auto sampleCounts =
        properties.limits.framebufferColorSampleCounts &
        properties.limits.framebufferDepthSampleCounts;
    msaaSamples =
        (sampleCounts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT :
        (sampleCounts & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT :
                                                VK_SAMPLE_COUNT_1_BIT;
    spdlog::info("Vulkan device: {}", properties.deviceName);
    spdlog::info("Water MSAA: {}x", static_cast<unsigned>(msaaSamples));
}

void VulkanRenderer::Impl::createDevice()
{
    const auto families = findQueueFamilies(physicalDevice);
    graphicsFamily = *families.graphics;
    presentFamily = *families.present;
    const std::set<std::uint32_t> uniqueFamilies{graphicsFamily, presentFamily};
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (const auto family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        queues.push_back(queue);
    }

    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &extensionCount, available.data());
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    constexpr const char* portabilitySubset = "VK_KHR_portability_subset";
    if (containsExtension(available, portabilitySubset)) {
        extensions.push_back(portabilitySubset);
    }

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = supported.samplerAnisotropy;

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
    info.pQueueCreateInfos = queues.data();
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.pEnabledFeatures = &features;
    check(vkCreateDevice(physicalDevice, &info, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void VulkanRenderer::Impl::createSwapchain()
{
    const auto support = querySwapchain(physicalDevice);
    const auto preferred = std::ranges::find_if(
        support.formats,
        [](const auto& candidate) {
            return candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
                candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    const VkSurfaceFormatKHR chosenFormat =
        preferred != support.formats.end() ? *preferred : support.formats.front();

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (std::ranges::find(support.presentModes, VK_PRESENT_MODE_MAILBOX_KHR) !=
        support.presentModes.end()) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    if (support.capabilities.currentExtent.width !=
        std::numeric_limits<std::uint32_t>::max()) {
        extent = support.capabilities.currentExtent;
    } else {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        extent = {
            std::clamp(
                static_cast<std::uint32_t>(width),
                support.capabilities.minImageExtent.width,
                support.capabilities.maxImageExtent.width),
            std::clamp(
                static_cast<std::uint32_t>(height),
                support.capabilities.minImageExtent.height,
                support.capabilities.maxImageExtent.height)};
    }

    std::uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosenFormat.format;
    info.imageColorSpace = chosenFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array queueFamilies{graphicsFamily, presentFamily};
    if (graphicsFamily != presentFamily) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = queueFamilies.data();
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = support.capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain),
          "vkCreateSwapchainKHR");

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
    swapchainFormat = chosenFormat.format;
}

void VulkanRenderer::Impl::createImageViews()
{
    swapchainViews.resize(swapchainImages.size());
    for (std::size_t index = 0; index < swapchainImages.size(); ++index) {
        swapchainViews[index] = createImageView(
            swapchainImages[index], swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

std::uint32_t VulkanRenderer::Impl::findMemoryType(
    const std::uint32_t filter, const VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((filter & (1u << index)) &&
            (memory.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    throw std::runtime_error("No suitable Vulkan memory type");
}

void VulkanRenderer::Impl::createBuffer(
    const VkDeviceSize size,
    const VkBufferUsageFlags usage,
    const VkMemoryPropertyFlags properties,
    Buffer& buffer,
    const bool map) const
{
    buffer.size = size;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &info, nullptr, &buffer.handle), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    check(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory),
          "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0),
          "vkBindBufferMemory");
    if (map) {
        check(vkMapMemory(device, buffer.memory, 0, size, 0, &buffer.mapped),
              "vkMapMemory");
    }
}

void VulkanRenderer::Impl::destroyBuffer(Buffer& buffer) const
{
    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
    if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

void VulkanRenderer::Impl::createImage(
    const std::uint32_t width,
    const std::uint32_t height,
    const VkFormat format,
    const VkImageUsageFlags usage,
    const VkImageAspectFlags aspect,
    Image& image,
    const VkSampleCountFlagBits samples) const
{
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = format;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = usage;
    info.samples = samples;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device, &info, nullptr, &image.handle), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image.handle, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &allocation, nullptr, &image.memory),
          "vkAllocateMemory(image)");
    check(vkBindImageMemory(device, image.handle, image.memory, 0),
          "vkBindImageMemory");
    image.view = createImageView(image.handle, format, aspect);
}

VkImageView VulkanRenderer::Impl::createImageView(
    const VkImage image,
    const VkFormat format,
    const VkImageAspectFlags aspect) const
{
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspect;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    check(vkCreateImageView(device, &info, nullptr, &view), "vkCreateImageView");
    return view;
}

void VulkanRenderer::Impl::destroyImage(Image& image) const
{
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.handle) vkDestroyImage(device, image.handle, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = {};
}

VkFormat VulkanRenderer::Impl::findDepthFormat() const
{
    for (const auto format : {
             VK_FORMAT_D32_SFLOAT,
             VK_FORMAT_D32_SFLOAT_S8_UINT,
             VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        if (properties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
    }
    throw std::runtime_error("No depth-buffer format available");
}

void VulkanRenderer::Impl::createCommandPool()
{
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = graphicsFamily;
    check(vkCreateCommandPool(device, &info, nullptr, &commandPool),
          "vkCreateCommandPool");
    commandBuffers.resize(swapchainImages.size());
    VkCommandBufferAllocateInfo allocation{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = commandPool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount =
        static_cast<std::uint32_t>(commandBuffers.size());
    check(vkAllocateCommandBuffers(device, &allocation, commandBuffers.data()),
          "vkAllocateCommandBuffers");
}

void VulkanRenderer::Impl::createSampler()
{
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.anisotropyEnable = features.samplerAnisotropy;
    info.maxAnisotropy = features.samplerAnisotropy
        ? std::min(8.0f, properties.limits.maxSamplerAnisotropy)
        : 1.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler");
}

void VulkanRenderer::Impl::createSyncObjects()
{
    const VkSemaphoreCreateInfo semaphore{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateSemaphore(device, &semaphore, nullptr, &imageAvailable),
          "vkCreateSemaphore");
    renderFinished.resize(swapchainImages.size());
    for (auto& renderSemaphore : renderFinished) {
        check(vkCreateSemaphore(device, &semaphore, nullptr, &renderSemaphore),
              "vkCreateSemaphore");
    }
    check(vkCreateFence(device, &fence, nullptr, &frameFence), "vkCreateFence");
}

void VulkanRenderer::Impl::destroySwapchain()
{
    for (const auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();
    destroyImage(colorImage);
    destroyImage(depthImage);
    if (skyPipeline) vkDestroyPipeline(device, skyPipeline, nullptr);
    if (waterPipeline) vkDestroyPipeline(device, waterPipeline, nullptr);
    skyPipeline = VK_NULL_HANDLE;
    waterPipeline = VK_NULL_HANDLE;
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
    for (const auto view : swapchainViews) vkDestroyImageView(device, view, nullptr);
    swapchainViews.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void VulkanRenderer::Impl::recreateSwapchain()
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }
    vkDeviceWaitIdle(device);
    destroyImGui();
    for (const auto semaphore : renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    renderFinished.clear();
    if (!commandBuffers.empty()) {
        vkFreeCommandBuffers(
            device, commandPool,
            static_cast<std::uint32_t>(commandBuffers.size()),
            commandBuffers.data());
        commandBuffers.clear();
    }
    destroySwapchain();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createPipelines();
    createDepthResources();
    createFramebuffers();
    commandBuffers.resize(swapchainImages.size());
    VkCommandBufferAllocateInfo allocation{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = commandPool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount =
        static_cast<std::uint32_t>(commandBuffers.size());
    check(vkAllocateCommandBuffers(device, &allocation, commandBuffers.data()),
          "vkAllocateCommandBuffers");
    const VkSemaphoreCreateInfo semaphore{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished.resize(swapchainImages.size());
    for (auto& renderSemaphore : renderFinished) {
        check(vkCreateSemaphore(device, &semaphore, nullptr, &renderSemaphore),
              "vkCreateSemaphore");
    }
    createImGui();
}

} // namespace water
