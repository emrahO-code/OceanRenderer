#include "Renderer/VulkanRendererInternal.hpp"

namespace water {

void VulkanRenderer::Impl::createImGui()
{
    const std::array poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64}};
    VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 256;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiPool),
          "vkCreateDescriptorPool(ImGui)");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(9.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.082f, 0.086f, 0.96f);
    colors[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.25f, 0.72f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.14f, 0.145f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.19f, 0.195f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.23f, 0.235f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.16f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.21f, 0.23f, 0.23f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.27f, 0.27f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.11f, 0.115f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.29f, 0.30f, 1.0f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.15f, 0.24f, 0.25f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.38f, 0.72f, 0.71f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.32f, 0.62f, 0.62f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.15f, 0.17f, 0.17f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.30f, 0.30f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.38f, 0.38f, 1.0f);
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = instance;
    info.PhysicalDevice = physicalDevice;
    info.Device = device;
    info.QueueFamily = graphicsFamily;
    info.Queue = graphicsQueue;
    info.DescriptorPool = imguiPool;
    info.RenderPass = renderPass;
    info.MinImageCount = std::max(2u, static_cast<std::uint32_t>(swapchainImages.size()));
    info.ImageCount = static_cast<std::uint32_t>(swapchainImages.size());
    info.MSAASamples = msaaSamples;
    info.CheckVkResultFn = [](const VkResult result) {
        if (result != VK_SUCCESS) {
            spdlog::error(
                "ImGui Vulkan error {}", static_cast<int>(result));
        }
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        throw std::runtime_error("ImGui Vulkan backend initialization failed");
    }
    ImGui_ImplVulkan_CreateFontsTexture();
    imguiInitialized = true;
}

void VulkanRenderer::Impl::destroyImGui()
{
    if (!imguiInitialized) return;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device, imguiPool, nullptr);
    imguiPool = VK_NULL_HANDLE;
    imguiInitialized = false;
}

bool VulkanRenderer::Impl::drawInterface(
    OceanSettings& oceanSettings,
    RenderSettings& settings,
    const TessendorfOcean&,
    Camera& camera,
    const Profiler& profiler,
    const float fps) const
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    bool rebuild = false;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float settingsWidth =
        std::clamp(display.x * 0.55f, 340.0f, 420.0f);
    const float performanceWidth =
        std::clamp(
            display.x - settingsWidth - 30.0f, 280.0f, 420.0f);
    ImGui::SetNextWindowPos(ImVec2(10.0f, 20.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(
        ImVec2(settingsWidth, std::min(720.0f, display.y - 40.0f)),
        ImGuiCond_Once);
    const bool settingsExpanded = ImGui::Begin("Settings");

    if (settingsExpanded) {
    if (ImGui::CollapsingHeader("Day / night", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Automatic cycle", &settings.automaticDayCycle);
        ImGui::SliderFloat(
            "Time of day", &settings.timeOfDay, 0.0f, 24.0f, "%.2f h");
        const float viewAzimuth =
            std::atan2(camera.forward().z, camera.forward().x);
        if (ImGui::Button("Dawn")) {
            settings.automaticDayCycle = false;
            settings.timeOfDay = 6.5f;
            settings.sunAzimuth = viewAzimuth - 0.25f;
            settings.sunElevation = 0.07f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Noon")) {
            settings.automaticDayCycle = false;
            settings.timeOfDay = 12.0f;
            settings.sunAzimuth = viewAzimuth;
            settings.sunElevation = 1.05f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sunset")) {
            settings.automaticDayCycle = false;
            settings.timeOfDay = 18.0f;
            settings.sunAzimuth = viewAzimuth;
            settings.sunElevation = 0.055f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Midnight")) {
            settings.automaticDayCycle = false;
            settings.timeOfDay = 0.0f;
            settings.sunAzimuth = viewAzimuth + 3.14159265359f;
            settings.sunElevation = -0.7f;
        }
        if (settings.automaticDayCycle) {
            ImGui::SliderFloat(
                "Day length", &settings.dayLengthMinutes,
                0.5f, 30.0f, "%.1f min");
        } else {
            ImGui::SliderAngle(
                "Sun azimuth", &settings.sunAzimuth, -180.0f, 180.0f);
            ImGui::SliderAngle(
                "Sun elevation", &settings.sunElevation, -89.0f, 89.0f);
        }
    }

    if (ImGui::CollapsingHeader("Tessendorf spectrum", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* const resolutions[] = {"64", "128", "256", "512"};
        int resolutionIndex =
            oceanSettings.resolution == 64 ? 0 :
            oceanSettings.resolution == 128 ? 1 :
            oceanSettings.resolution == 512 ? 3 : 2;
        if (ImGui::Combo("FFT resolution", &resolutionIndex, resolutions, 4)) {
            oceanSettings.resolution = 64u << resolutionIndex;
            rebuild = true;
        }
        ImGui::SliderFloat(
            "Patch length", &oceanSettings.patchLength,
            128.0f, 1024.0f, "%.0f m");
        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat(
            "Wind speed", &oceanSettings.windSpeed, 1.0f, 40.0f, "%.1f m/s");
        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat2(
            "Wind direction", &oceanSettings.windDirection.x, -1.0f, 1.0f);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            oceanSettings.windDirection =
                glm::length(oceanSettings.windDirection) > 0.05f
                    ? glm::normalize(oceanSettings.windDirection)
                    : glm::vec2(1.0f, 0.0f);
            rebuild = true;
        }
        float spectrum = oceanSettings.phillipsAmplitude * 1.0e7f;
        if (ImGui::SliderFloat("Spectrum energy", &spectrum, 0.1f, 8.0f)) {
            oceanSettings.phillipsAmplitude = spectrum * 1.0e-7f;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            rebuild = true;
        }
        ImGui::SliderFloat(
            "Short-wave damping", &oceanSettings.smallWaveDamping,
            0.02f, 0.5f);
        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat(
            "Animation period", &oceanSettings.animationPeriod,
            30.0f, 360.0f, "%.0f s");
        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat(
            "Choppiness", &oceanSettings.choppiness, -2.2f, 0.0f);
        ImGui::SliderFloat(
            "Height scale", &oceanSettings.heightScale, 0.05f, 3.0f);
        if (ImGui::Button("Reset ocean defaults")) {
            oceanSettings = OceanSettings{};
            rebuild = true;
        }
    }

    if (ImGui::CollapsingHeader("Optics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Turbidity", &settings.turbidity, 1.7f, 6.0f);
        ImGui::SliderFloat("Exposure", &settings.exposure, 0.2f, 2.5f);
        ImGui::SliderFloat(
            "Water depth", &settings.waterDepth, 2.0f, 200.0f);
        ImGui::SliderFloat(
            "Specular power", &settings.specularPower, 16.0f, 500.0f);
        ImGui::SliderFloat(
            "Specular intensity", &settings.specularIntensity, 0.0f, 4.0f);
        ImGui::SliderFloat(
            "Sky intensity", &settings.skyIntensity, 0.0f, 2.0f);
        ImGui::ColorEdit3("Terrain", &settings.terrainColor.x);
        static int waterType = 0;
        const char* const waterTypes[] = {
            "Clear open ocean", "Coastal water", "Turbid harbor"};
        if (ImGui::Combo("Water preset", &waterType, waterTypes, 3)) {
            constexpr std::array absorptionPresets{
                glm::vec3(0.420f, 0.063f, 0.019f),
                glm::vec3(0.510f, 0.120f, 0.250f),
                glm::vec3(0.920f, 0.630f, 1.600f)};
            constexpr std::array scatteringPresets{
                glm::vec3(0.032f, 0.037f, 0.042f),
                glm::vec3(0.180f, 0.219f, 0.250f),
                glm::vec3(1.540f, 1.824f, 2.000f)};
            settings.absorption = absorptionPresets[waterType];
            settings.scattering = scatteringPresets[waterType];
            settings.backscattering =
                0.01829f * settings.scattering + 0.00006f;
        }
        ImGui::SliderFloat3(
            "Absorption RGB", &settings.absorption.x,
            0.001f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::SliderFloat3(
                "Scattering RGB", &settings.scattering.x,
                0.001f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
            settings.backscattering =
                0.01829f * settings.scattering + 0.00006f;
        }
        if (ImGui::Button("Reset optics defaults")) {
            const RenderSettings defaults;
            settings.sunAzimuth = defaults.sunAzimuth;
            settings.sunElevation = defaults.sunElevation;
            settings.timeOfDay = defaults.timeOfDay;
            settings.dayLengthMinutes = defaults.dayLengthMinutes;
            settings.automaticDayCycle = defaults.automaticDayCycle;
            settings.turbidity = defaults.turbidity;
            settings.exposure = defaults.exposure;
            settings.waterDepth = defaults.waterDepth;
            settings.specularPower = defaults.specularPower;
            settings.specularIntensity = defaults.specularIntensity;
            settings.skyIntensity = defaults.skyIntensity;
            settings.terrainColor = defaults.terrainColor;
            settings.absorption = defaults.absorption;
            settings.scattering = defaults.scattering;
            settings.backscattering = defaults.backscattering;
            waterType = 0;
        }
    }

    if (ImGui::CollapsingHeader("Foam", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable foam", &settings.foamEnabled);
        ImGui::SliderFloat(
            "Breaking threshold", &settings.foamThreshold, 0.1f, 1.1f);
        ImGui::SliderFloat(
            "Foam intensity", &settings.foamIntensity, 0.0f, 3.0f);
        ImGui::ColorEdit3("Foam color", &settings.foamColor.x);
    }

    if (ImGui::CollapsingHeader("World")) {
        ImGui::SliderInt("Tile radius", &settings.tileRadius, 1, 6);
        ImGui::Checkbox("Distance LOD", &settings.lodEnabled);
        ImGui::Checkbox("Pause simulation", &settings.paused);
        float speed = camera.speed();
        if (ImGui::SliderFloat("Camera speed", &speed, 1.0f, 100.0f)) {
            camera.setSpeed(speed);
        }
        const auto position = camera.position();
        ImGui::Text(
            "Position: %.1f, %.1f, %.1f", position.x, position.y, position.z);
    }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(
        ImVec2(settingsWidth + 20.0f, 20.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(
        ImVec2(
            performanceWidth,
            std::min(440.0f, display.y - 40.0f)),
        ImGuiCond_Once);
    const bool performanceExpanded = ImGui::Begin("Performance");

    if (performanceExpanded) {
        static std::array<float, 180> frameHistory{};
        static std::size_t frameCursor{};
        const float frameMilliseconds =
            1000.0f / std::max(fps, 0.01f);
        frameHistory[frameCursor++ % frameHistory.size()] =
            frameMilliseconds;

        ImGui::Text("FPS");
        ImGui::SameLine(190.0f);
        ImGui::Text("%.0f", fps);
        ImGui::Text("CPU frame time");
        ImGui::SameLine(190.0f);
        ImGui::Text("%.2f ms", frameMilliseconds);
        ImGui::Spacing();
        ImGui::PlotLines(
            "##frame history", frameHistory.data(),
            static_cast<int>(frameHistory.size()),
            static_cast<int>(frameCursor % frameHistory.size()),
            nullptr, 0.0f, 33.3f, ImVec2(-1.0f, 92.0f));

        ImGui::SeparatorText("GPU");
        ImGui::Text("Compute FFT");
        ImGui::SameLine(190.0f);
        ImGui::Text("%.3f ms", gpuFftMilliseconds);
        ImGui::Text("Scene + interface");
        ImGui::SameLine(190.0f);
        ImGui::Text("%.3f ms", gpuRenderMilliseconds);
        ImGui::SeparatorText("Configuration");
        ImGui::Text("Device");
        ImGui::SameLine(190.0f);
        ImGui::TextUnformatted(deviceName.c_str());
        ImGui::Text("FFT resolution");
        ImGui::SameLine(190.0f);
        ImGui::Text(
            "%u x %u", oceanSettings.resolution,
            oceanSettings.resolution);
        ImGui::Text(
            "Mesh LOD: %u / %u / %u | MSAA: %ux",
            lodMeshes.empty() ? 0u : lodMeshes[0].resolution,
            lodMeshes.size() < 2 ? 0u : lodMeshes[1].resolution,
            lodMeshes.size() < 3 ? 0u : lodMeshes[2].resolution,
            static_cast<unsigned>(msaaSamples));
        if (ImGui::BeginTable("timings", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Scope");
            ImGui::TableSetupColumn("Average ms");
            ImGui::TableHeadersRow();
            for (const auto& sample : profiler.samples()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(sample.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f", sample.smoothedMilliseconds);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    return rebuild;
}

} // namespace water
