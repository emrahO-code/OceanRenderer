#include "Renderer/VulkanRendererInternal.hpp"

namespace water {

VulkanRenderer::VulkanRenderer(GLFWwindow* window)
    : impl_(std::make_unique<Impl>(window))
{
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::drawInterface(
    OceanSettings& oceanSettings,
    RenderSettings& renderSettings,
    const TessendorfOcean& ocean,
    Camera& camera,
    const Profiler& profiler,
    const float framesPerSecond) const
{
    return impl_->drawInterface(
        oceanSettings, renderSettings, ocean, camera, profiler, framesPerSecond);
}

void VulkanRenderer::render(
    const TessendorfOcean& ocean,
    const Camera& camera,
    const RenderSettings& settings,
    const float simulationTime)
{
    impl_->render(ocean, camera, settings, simulationTime);
}

void VulkanRenderer::waitIdle()
{
    check(vkDeviceWaitIdle(impl_->device), "vkDeviceWaitIdle");
}

const std::string& VulkanRenderer::deviceName() const
{
    return impl_->deviceName;
}

double VulkanRenderer::gpuFftMilliseconds() const
{
    return impl_->gpuFftMilliseconds;
}

double VulkanRenderer::gpuRenderMilliseconds() const
{
    return impl_->gpuRenderMilliseconds;
}

bool VulkanRenderer::isInterfaceCapturingMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

} // namespace water
