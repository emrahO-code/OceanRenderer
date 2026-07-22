#include "Application/Application.hpp"

#include "Camera/Camera.hpp"
#include "Math/Profiler.hpp"
#include "Ocean/TessendorfOcean.hpp"
#include "Renderer/RenderSettings.hpp"
#include "Renderer/VulkanRenderer.hpp"

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace water {
namespace {

void logGlfwError(const int code, const char* description)
{
    spdlog::error("GLFW {}: {}", code, description);
}

class GlfwContext {
public:
    GlfwContext()
    {
        glfwSetErrorCallback(logGlfwError);
        if (glfwInit() != GLFW_TRUE) throw std::runtime_error("GLFW initialization failed");
    }
    ~GlfwContext() { glfwTerminate(); }
};

struct WindowDeleter {
    void operator()(GLFWwindow* window) const { glfwDestroyWindow(window); }
};

} // namespace

void Application::run()
{
    const GlfwContext glfw;
    if (glfwVulkanSupported() != GLFW_TRUE) {
        throw std::runtime_error("GLFW reports no Vulkan loader or ICD");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    const std::unique_ptr<GLFWwindow, WindowDeleter> window(
        glfwCreateWindow(1600, 900, "Tessendorf Ocean — Vulkan", nullptr, nullptr));
    if (!window) throw std::runtime_error("Window creation failed");

    OceanSettings oceanSettings;
    RenderSettings renderSettings;
    Camera camera;
    Profiler profiler;
    TessendorfOcean ocean(oceanSettings);
    VulkanRenderer renderer(window.get());

    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    float simulationTime = 0.0f;
    double smoothedFrame = 1.0 / 60.0;

    while (glfwWindowShouldClose(window.get()) == GLFW_FALSE) {
        const ProfileScope frameScope(profiler, "Frame");
        glfwPollEvents();

        const auto now = Clock::now();
        const float delta = std::clamp(
            std::chrono::duration<float>(now - previous).count(), 0.0f, 0.1f);
        previous = now;
        smoothedFrame = smoothedFrame * 0.94 + static_cast<double>(delta) * 0.06;

        if (renderSettings.automaticDayCycle && !renderSettings.paused) {
            const float hoursPerSecond =
                24.0f / std::max(renderSettings.dayLengthMinutes * 60.0f, 1.0f);
            renderSettings.timeOfDay =
                std::fmod(renderSettings.timeOfDay + delta * hoursPerSecond, 24.0f);
            const float solarPhase =
                (renderSettings.timeOfDay - 6.0f) *
                (2.0f * std::numbers::pi_v<float> / 24.0f);
            renderSettings.sunElevation = std::sin(solarPhase) * 1.05f;
            renderSettings.sunAzimuth = solarPhase - 1.57079632679f;
        }

        const bool rebuild = renderer.drawInterface(
            oceanSettings, renderSettings, ocean, camera, profiler,
            static_cast<float>(1.0 / std::max(smoothedFrame, 1.0e-6)));

        const bool rightMouse =
            glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        const bool captureCamera =
            rightMouse && !renderer.isInterfaceCapturingMouse();
        glfwSetInputMode(
            window.get(), GLFW_CURSOR,
            captureCamera ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        camera.update(window.get(), delta, captureCamera);

        if (rebuild) {
            const ProfileScope scope(profiler, "Ocean rebuild");
            ocean.rebuild(oceanSettings);
        }
        if (!renderSettings.paused) simulationTime += delta;
        {
            const ProfileScope scope(profiler, "Vulkan frame");
            renderer.render(ocean, camera, renderSettings, simulationTime);
        }
    }

    renderer.waitIdle();
}

void Application::runBenchmark()
{
    const GlfwContext glfw;
    if (glfwVulkanSupported() != GLFW_TRUE) {
        throw std::runtime_error("GLFW reports no Vulkan loader or ICD");
    }

    constexpr int windowWidth = 1600;
    constexpr int windowHeight = 900;
    constexpr int warmupFrames = 30;
    constexpr int measuredFrames = 90;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    const std::unique_ptr<GLFWwindow, WindowDeleter> window(
        glfwCreateWindow(windowWidth, windowHeight, "WaterRenderer benchmark", nullptr, nullptr));
    if (!window) throw std::runtime_error("Window creation failed");

    OceanSettings oceanSettings;
    RenderSettings renderSettings;
    Camera camera;
    Profiler profiler;
    TessendorfOcean ocean(oceanSettings);
    VulkanRenderer renderer(window.get());

#ifdef NDEBUG
    constexpr const char* buildType = "Release";
#else
    constexpr const char* buildType = "Debug";
#endif

    std::cout << "| Device | Build | Window | FFT | Tile Radius | Avg FPS | "
                 "CPU Frame | GPU FFT | GPU Render |\n";
    std::cout << "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";

    using Clock = std::chrono::steady_clock;
    const std::array resolutions{128u, 256u, 512u};
    for (const auto resolution : resolutions) {
        oceanSettings.resolution = resolution;
        ocean.rebuild(oceanSettings);

        float simulationTime = 0.0f;
        auto previous = Clock::now();
        double cpuMilliseconds = 0.0;
        double gpuFftMilliseconds = 0.0;
        double gpuRenderMilliseconds = 0.0;

        for (int frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
            const auto frameStart = Clock::now();
            glfwPollEvents();

            const auto now = Clock::now();
            const float delta = std::clamp(
                std::chrono::duration<float>(now - previous).count(), 0.0f, 0.1f);
            previous = now;

            renderer.drawInterface(
                oceanSettings, renderSettings, ocean, camera, profiler, 0.0f);
            camera.update(window.get(), delta, false);
            simulationTime += delta;
            renderer.render(ocean, camera, renderSettings, simulationTime);

            const auto frameEnd = Clock::now();
            if (frame >= warmupFrames) {
                cpuMilliseconds +=
                    std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
                gpuFftMilliseconds += renderer.gpuFftMilliseconds();
                gpuRenderMilliseconds += renderer.gpuRenderMilliseconds();
            }
        }

        renderer.waitIdle();
        const double averageCpu = cpuMilliseconds / measuredFrames;
        const double averageFft = gpuFftMilliseconds / measuredFrames;
        const double averageRender = gpuRenderMilliseconds / measuredFrames;
        const double averageFps = 1000.0 / std::max(averageCpu, 0.001);

        std::cout << "| " << renderer.deviceName()
                  << " | " << buildType
                  << " | " << windowWidth << "x" << windowHeight
                  << " | " << resolution << "x" << resolution
                  << " | " << renderSettings.tileRadius
                  << " | " << std::fixed << std::setprecision(1) << averageFps
                  << " | " << std::setprecision(2) << averageCpu << " ms"
                  << " | " << std::setprecision(3) << averageFft << " ms"
                  << " | " << std::setprecision(3) << averageRender << " ms"
                  << " |\n";
    }
}

} // namespace water
