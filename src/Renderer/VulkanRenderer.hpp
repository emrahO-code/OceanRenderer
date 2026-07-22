#pragma once

#include <memory>
#include <string>

struct GLFWwindow;

namespace water {

class Camera;
class Profiler;
class TessendorfOcean;
struct OceanSettings;
struct RenderSettings;

struct RendererStats {
    std::string deviceName;
    double oceanComputeMilliseconds{};
    double sceneMilliseconds{};
};

class VulkanRenderer {
public:
    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool drawInterface(
        OceanSettings& oceanSettings,
        RenderSettings& renderSettings,
        const TessendorfOcean& ocean,
        Camera& camera,
        const Profiler& profiler,
        float framesPerSecond) const;
    void render(
        const TessendorfOcean& ocean,
        const Camera& camera,
        const RenderSettings& settings,
        float simulationTime);
    void waitIdle();

    [[nodiscard]] RendererStats stats() const;
    [[nodiscard]] bool isInterfaceCapturingMouse() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace water
