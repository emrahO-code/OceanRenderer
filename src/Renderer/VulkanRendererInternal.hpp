#pragma once

#include "Renderer/VulkanRenderer.hpp"

#include "Camera/Camera.hpp"
#include "Math/Profiler.hpp"
#include "Ocean/TessendorfOcean.hpp"
#include "Renderer/RenderSettings.hpp"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace water {

void check(VkResult result, const char* operation);

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    [[nodiscard]] bool complete() const { return graphics && present; }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct Vertex {
    glm::vec3 position{};
    glm::vec2 uv{};
};

struct Buffer {
    VkBuffer handle{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize size{};
    void* mapped{};
};

struct Image {
    VkImage handle{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};

struct MeshBuffers {
    Buffer vertex;
    Buffer index;
    std::uint32_t indexCount{};
    std::uint32_t resolution{};
};

struct alignas(16) SceneUniform {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraTime{};
    glm::vec4 sunTurbidity{};
    glm::vec4 skyA{};
    glm::vec4 skyB{};
    glm::vec4 skyC{};
    glm::vec4 skyD{};
    glm::vec4 skyE{};
    glm::vec4 zenith{};
    glm::vec4 zeroThetaSun{};
    glm::vec4 rendering{};
    glm::vec4 absorption{};
    glm::vec4 scattering{};
    glm::vec4 backscattering{};
    glm::vec4 terrain{};
    glm::vec4 ocean{};
    glm::vec4 celestial{};
    glm::vec4 foam{};
    glm::vec4 foamColor{};
};

static_assert(sizeof(SceneUniform) == 480);
static_assert(offsetof(SceneUniform, cameraTime) == 192);
static_assert(offsetof(SceneUniform, rendering) == 336);
static_assert(offsetof(SceneUniform, ocean) == 416);
static_assert(offsetof(SceneUniform, foamColor) == 464);

struct PushConstants {
    glm::vec4 tileOffset{};
};

struct OceanComputePush {
    std::uint32_t resolution{};
    std::uint32_t stage{};
    std::uint32_t horizontal{};
    std::uint32_t sourceBuffer{};
    float time{};
    float patchLength{};
    float choppiness{};
};

static_assert(sizeof(OceanComputePush) == 28);
static_assert(offsetof(OceanComputePush, sourceBuffer) == 12);
static_assert(offsetof(OceanComputePush, time) == 16);
static_assert(offsetof(OceanComputePush, choppiness) == 24);

std::vector<char> readBinary(const std::string& path);
bool containsExtension(const std::vector<VkExtensionProperties>& extensions, const char* name);

struct VulkanRenderer::Impl {
    explicit Impl(GLFWwindow* nativeWindow);
    ~Impl();
    void createInstance();
    void createSurface();
    QueueFamilies findQueueFamilies(const VkPhysicalDevice candidate) const;
    SwapchainSupport querySwapchain(const VkPhysicalDevice candidate) const;
    bool deviceSuitable(const VkPhysicalDevice candidate) const;
    void selectPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createImageViews();
    std::uint32_t findMemoryType(
        const std::uint32_t filter, const VkMemoryPropertyFlags properties) const;
    void createBuffer(
        const VkDeviceSize size,
        const VkBufferUsageFlags usage,
        const VkMemoryPropertyFlags properties,
        Buffer& buffer,
        const bool map = false) const;
    void destroyBuffer(Buffer& buffer) const;
    void createImage(
        const std::uint32_t width,
        const std::uint32_t height,
        const VkFormat format,
        const VkImageUsageFlags usage,
        const VkImageAspectFlags aspect,
        Image& image,
        const VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const;
    VkImageView createImageView(
        const VkImage image,
        const VkFormat format,
        const VkImageAspectFlags aspect) const;
    void destroyImage(Image& image) const;
    VkFormat findDepthFormat() const;
    void createRenderPass();
    void createDepthResources();
    void createFramebuffers();
    void createDescriptorResources();
    VkShaderModule createShader(const std::string& filename) const;
    VkPipeline makePipeline(const bool water) const;
    void createPipelines();
    VkPipeline makeComputePipeline(const char* shaderName) const;
    void createComputeResources();
    void createCommandPool();
    void createUniformBuffer();
    void createSampler();
    void createSyncObjects();
    void createImGui();
    void destroyImGui();
    void createOceanResources(const TessendorfOcean& ocean);
    void destroyOceanResources();
    void imageBarrier(
        const VkCommandBuffer command,
        const VkImage image,
        const VkImageLayout oldLayout,
        const VkImageLayout newLayout,
        const VkPipelineStageFlags sourceStage,
        const VkPipelineStageFlags destinationStage,
        const VkAccessFlags sourceAccess,
        const VkAccessFlags destinationAccess);
    void computeOcean(
        const VkCommandBuffer command,
        const TessendorfOcean& ocean,
        const float simulationTime);
    static void updateSkyModel(SceneUniform& uniform);
    void updateUniform(
        const Camera& camera,
        const TessendorfOcean& ocean,
        const RenderSettings& settings,
        const float simulationTime) const;
    void recordCommands(
        const std::uint32_t imageIndex,
        const TessendorfOcean& ocean,
        const Camera& camera,
        const RenderSettings& settings,
        const float simulationTime);
    void render(
        const TessendorfOcean& ocean,
        const Camera& camera,
        const RenderSettings& settings,
        const float simulationTime);
    void destroySwapchain();
    void recreateSwapchain();
    bool drawInterface(
        OceanSettings& oceanSettings,
        RenderSettings& settings,
        const TessendorfOcean&,
        Camera& camera,
        const Profiler& profiler,
        const float fps) const;

    GLFWwindow* window{};
    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    std::uint32_t graphicsFamily{};
    std::uint32_t presentFamily{};
    VkQueue graphicsQueue{VK_NULL_HANDLE};
    VkQueue presentQueue{VK_NULL_HANDLE};

    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkFormat swapchainFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D extent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass{VK_NULL_HANDLE};
    VkSampleCountFlagBits msaaSamples{VK_SAMPLE_COUNT_1_BIT};
    Image colorImage;
    Image depthImage;

    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline skyPipeline{VK_NULL_HANDLE};
    VkPipeline waterPipeline{VK_NULL_HANDLE};
    VkDescriptorSetLayout oceanComputeDescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool oceanComputePool{VK_NULL_HANDLE};
    VkDescriptorSet oceanComputeSet{VK_NULL_HANDLE};
    VkPipelineLayout oceanComputeLayout{VK_NULL_HANDLE};
    VkPipeline oceanSpectrumPipeline{VK_NULL_HANDLE};
    VkPipeline oceanFftPipeline{VK_NULL_HANDLE};
    VkPipeline oceanFinalizePipeline{VK_NULL_HANDLE};

    VkCommandPool commandPool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> commandBuffers;
    VkSemaphore imageAvailable{VK_NULL_HANDLE};
    std::vector<VkSemaphore> renderFinished;
    VkFence frameFence{VK_NULL_HANDLE};

    Buffer uniformBuffer;
    std::vector<MeshBuffers> lodMeshes;
    Buffer spectrumBuffer;
    std::array<Buffer, 2> fftBuffers;
    Image displacementImage;
    Image normalImage;
    VkSampler sampler{VK_NULL_HANDLE};
    std::uint32_t oceanResolution{};
    std::uint64_t oceanRevision{};
    bool texturesInitialized{};
    VkQueryPool timestampPool{VK_NULL_HANDLE};
    float timestampPeriod{1.0f};
    double gpuFftMilliseconds{};
    double gpuRenderMilliseconds{};
    bool timestampResultsAvailable{};
    std::string deviceName;

    VkDescriptorPool imguiPool{VK_NULL_HANDLE};
    bool imguiInitialized{};
};

} // namespace water
