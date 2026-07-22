#include "Renderer/VulkanRendererInternal.hpp"

namespace water {

void VulkanRenderer::Impl::createRenderPass()
{
    VkAttachmentDescription color{};
    color.format = swapchainFormat;
    color.samples = msaaSamples;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = msaaSamples == VK_SAMPLE_COUNT_1_BIT
        ? VK_ATTACHMENT_STORE_OP_STORE
        : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = msaaSamples == VK_SAMPLE_COUNT_1_BIT
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = findDepthFormat();
    depth.samples = msaaSamples;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentReference colorReference{
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthReference{
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription resolve{};
    resolve.format = swapchainFormat;
    resolve.samples = VK_SAMPLE_COUNT_1_BIT;
    resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    const VkAttachmentReference resolveReference{
        2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    if (msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
        subpass.pResolveAttachments = &resolveReference;
    }

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array attachments{color, depth, resolve};
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = msaaSamples == VK_SAMPLE_COUNT_1_BIT
        ? 2u
        : static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    check(vkCreateRenderPass(device, &info, nullptr, &renderPass),
          "vkCreateRenderPass");
}

void VulkanRenderer::Impl::createDepthResources()
{
    if (msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
        createImage(
            extent.width, extent.height, swapchainFormat,
            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, colorImage, msaaSamples);
    }
    createImage(
        extent.width, extent.height, findDepthFormat(),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT, depthImage, msaaSamples);
}

void VulkanRenderer::Impl::createFramebuffers()
{
    framebuffers.resize(swapchainViews.size());
    for (std::size_t index = 0; index < swapchainViews.size(); ++index) {
        const std::array attachments{
            msaaSamples == VK_SAMPLE_COUNT_1_BIT
                ? swapchainViews[index]
                : colorImage.view,
            depthImage.view,
            swapchainViews[index]};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = renderPass;
        info.attachmentCount = msaaSamples == VK_SAMPLE_COUNT_1_BIT
            ? 2u
            : static_cast<std::uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.width = extent.width;
        info.height = extent.height;
        info.layers = 1;
        check(vkCreateFramebuffer(device, &info, nullptr, &framebuffers[index]),
              "vkCreateFramebuffer");
    }
}

void VulkanRenderer::Impl::createDescriptorResources()
{
    const std::array bindings{
        VkDescriptorSetLayoutBinding{
            0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{
            1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{
            2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(
              device, &layoutInfo, nullptr, &descriptorLayout),
          "vkCreateDescriptorSetLayout");

    const std::array sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
    VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
          "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = descriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &descriptorLayout;
    check(vkAllocateDescriptorSets(device, &allocation, &descriptorSet),
          "vkAllocateDescriptorSets");
}

VkShaderModule VulkanRenderer::Impl::createShader(const std::string& filename) const
{
    const auto bytecode = readBinary(
        std::string(WATER_SHADER_DIR) + "/" + filename + ".spv");
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytecode.size();
    info.pCode = reinterpret_cast<const std::uint32_t*>(bytecode.data());
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &info, nullptr, &module),
          "vkCreateShaderModule");
    return module;
}

VkPipeline VulkanRenderer::Impl::makePipeline(const bool water) const
{
    const auto vertexModule = createShader(water ? "water.vert" : "sky.vert");
    const auto fragmentModule = createShader(water ? "water.frag" : "sky.frag");
    const std::array stages{
        VkPipelineShaderStageCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
            VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main", nullptr},
        VkPipelineShaderStageCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
            VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main", nullptr}};

    const VkVertexInputBindingDescription binding{
        0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array attributes{
        VkVertexInputAttributeDescription{
            0, 0, VK_FORMAT_R32G32B32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(Vertex, position))},
        VkVertexInputAttributeDescription{
            1, 0, VK_FORMAT_R32G32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(Vertex, uv))}};
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (water) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = water ? VK_CULL_MODE_NONE : VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = msaaSamples;
    multisampling.sampleShadingEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = water ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = water ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisampling;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout;
    info.renderPass = renderPass;
    info.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, vertexModule, nullptr);
    vkDestroyShaderModule(device, fragmentModule, nullptr);
    check(result, "vkCreateGraphicsPipelines");
    return pipeline;
}

void VulkanRenderer::Impl::createPipelines()
{
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout),
          "vkCreatePipelineLayout");
    skyPipeline = makePipeline(false);
    waterPipeline = makePipeline(true);
}

void VulkanRenderer::Impl::createUniformBuffer()
{
    createBuffer(
        sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer, true);
    const VkDescriptorBufferInfo bufferInfo{
        uniformBuffer.handle, 0, sizeof(SceneUniform)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void VulkanRenderer::Impl::updateSkyModel(SceneUniform& uniform)
{
    const float t = uniform.sunTurbidity.w;
    const auto set = [](glm::vec4& destination, const glm::vec3 value) {
        destination = glm::vec4(value, 0.0f);
    };
    set(uniform.skyA, {
        0.1787f * t - 1.4630f, -0.0193f * t - 0.2592f,
        -0.0167f * t - 0.2608f});
    set(uniform.skyB, {
        -0.3554f * t + 0.4275f, -0.0665f * t + 0.0008f,
        -0.0950f * t + 0.0092f});
    set(uniform.skyC, {
        -0.0227f * t + 5.3251f, -0.0004f * t + 0.2125f,
        -0.0079f * t + 0.2102f});
    set(uniform.skyD, {
        0.1206f * t - 2.5771f, -0.0641f * t - 0.8989f,
        -0.0441f * t - 1.6537f});
    set(uniform.skyE, {
        -0.0670f * t + 0.3703f, -0.0033f * t + 0.0452f,
        -0.0109f * t + 0.0529f});

    const glm::vec3 sun = glm::normalize(glm::vec3(uniform.sunTurbidity));
    const float theta = std::acos(std::max(sun.y, 0.0f));
    const float theta2 = theta * theta;
    const float theta3 = theta2 * theta;
    const float t2 = t * t;
    const float chi =
        (4.0f / 9.0f - t / 120.0f) *
        (std::numbers::pi_v<float> - 2.0f * theta);
    const float luminance =
        (4.0453f * t - 4.9710f) * std::tan(chi) - 0.2155f * t + 2.4192f;
    const float x =
        (0.00165f * theta3 - 0.00375f * theta2 + 0.00209f * theta) * t2 +
        (-0.02903f * theta3 + 0.06377f * theta2 - 0.03202f * theta +
         0.00394f) * t +
        (0.11693f * theta3 - 0.21196f * theta2 + 0.06052f * theta +
         0.25886f);
    const float y =
        (0.00275f * theta3 - 0.00610f * theta2 + 0.00317f * theta) * t2 +
        (-0.04214f * theta3 + 0.08970f * theta2 - 0.04153f * theta +
         0.00516f) * t +
        (0.15346f * theta3 - 0.26756f * theta2 + 0.06670f * theta +
         0.26688f);
    uniform.zenith = glm::vec4(luminance, x, y, 0.0f);
    const float cosine = std::cos(theta);
    uniform.zeroThetaSun = glm::vec4(
        (glm::vec3(1.0f) + glm::vec3(uniform.skyA) *
            glm::exp(glm::vec3(uniform.skyB))) *
        (glm::vec3(1.0f) + glm::vec3(uniform.skyC) *
            glm::exp(glm::vec3(uniform.skyD) * theta) +
            glm::vec3(uniform.skyE) * cosine * cosine),
        0.0f);
}

void VulkanRenderer::Impl::updateUniform(
    const Camera& camera,
    const TessendorfOcean& ocean,
    const RenderSettings& settings,
    const float simulationTime) const
{
    SceneUniform uniform;
    const float aspect =
        static_cast<float>(extent.width) / static_cast<float>(extent.height);
    uniform.view = camera.view();
    uniform.projection = camera.projection(aspect);
    uniform.inverseViewProjection =
        glm::inverse(uniform.projection * uniform.view);
    uniform.cameraTime = glm::vec4(camera.position(), simulationTime);
    const float cosElevation = std::cos(settings.sunElevation);
    const glm::vec3 sun(
        cosElevation * std::cos(settings.sunAzimuth),
        std::sin(settings.sunElevation),
        cosElevation * std::sin(settings.sunAzimuth));
    uniform.sunTurbidity = glm::vec4(glm::normalize(sun), settings.turbidity);
    uniform.rendering = glm::vec4(
        settings.exposure, settings.waterDepth,
        settings.specularPower, settings.specularIntensity);
    uniform.absorption = glm::vec4(settings.absorption, settings.skyIntensity);
    uniform.scattering = glm::vec4(settings.scattering, 0.0f);
    uniform.backscattering = glm::vec4(settings.backscattering, 0.0f);
    uniform.terrain = glm::vec4(settings.terrainColor, 0.0f);
    uniform.ocean = glm::vec4(
        ocean.settings().patchLength, ocean.settings().heightScale,
        ocean.settings().choppiness, 1.0f);
    const glm::vec3 moon = -glm::normalize(sun);
    float daylight = std::clamp((sun.y + 0.20f) / 0.16f, 0.0f, 1.0f);
    daylight = daylight * daylight * (3.0f - 2.0f * daylight);
    uniform.celestial = glm::vec4(moon, daylight);
    uniform.foam = glm::vec4(
        settings.foamThreshold, settings.foamIntensity,
        settings.foamEnabled ? 1.0f : 0.0f,
        settings.lodEnabled ? 1.0f : 0.0f);
    uniform.foamColor = glm::vec4(settings.foamColor, 0.0f);
    updateSkyModel(uniform);
    std::memcpy(uniformBuffer.mapped, &uniform, sizeof(uniform));
}

void VulkanRenderer::Impl::recordCommands(
    const std::uint32_t imageIndex,
    const TessendorfOcean& ocean,
    const Camera& camera,
    const RenderSettings& settings,
    const float simulationTime)
{
    const auto command = commandBuffers[imageIndex];
    check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    computeOcean(command, ocean, simulationTime);
    vkCmdWriteTimestamp(
        command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampPool, 2);

    const std::array clearValues{
        VkClearValue{.color = {{0.01f, 0.03f, 0.06f, 1.0f}}},
        VkClearValue{.depthStencil = {1.0f, 0}}};
    VkRenderPassBeginInfo renderBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderBegin.renderPass = renderPass;
    renderBegin.framebuffer = framebuffers[imageIndex];
    renderBegin.renderArea.extent = extent;
    renderBegin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    renderBegin.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(command, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport viewport{
        0.0f, 0.0f,
        static_cast<float>(extent.width), static_cast<float>(extent.height),
        0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindDescriptorSets(
        command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &descriptorSet, 0, nullptr);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
    vkCmdDraw(command, 3, 1, 0, 0);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);
    const VkDeviceSize offset = 0;
    const float patchLength = ocean.settings().patchLength;
    const float centerX =
        std::floor(camera.position().x / patchLength) * patchLength;
    const float centerZ =
        std::floor(camera.position().z / patchLength) * patchLength;
    for (int z = -settings.tileRadius; z <= settings.tileRadius; ++z) {
        for (int x = -settings.tileRadius; x <= settings.tileRadius; ++x) {
            const int ring = std::max(std::abs(x), std::abs(z));
            const std::size_t lod = settings.lodEnabled
                ? static_cast<std::size_t>(std::clamp(ring - 1, 0, 2))
                : 0u;
            const auto& mesh = lodMeshes[lod];
            vkCmdBindVertexBuffers(
                command, 0, 1, &mesh.vertex.handle, &offset);
            vkCmdBindIndexBuffer(
                command, mesh.index.handle, 0, VK_INDEX_TYPE_UINT32);
            PushConstants push;
            push.tileOffset = glm::vec4(
                centerX + static_cast<float>(x) * patchLength,
                centerZ + static_cast<float>(z) * patchLength,
                static_cast<float>(lod), static_cast<float>(ring));
            vkCmdPushConstants(
                command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(push), &push);
            vkCmdDrawIndexed(command, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
    vkCmdEndRenderPass(command);
    vkCmdWriteTimestamp(
        command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool, 3);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
}

void VulkanRenderer::Impl::render(
    const TessendorfOcean& ocean,
    const Camera& camera,
    const RenderSettings& settings,
    const float simulationTime)
{
    if (oceanResolution != ocean.settings().resolution ||
        oceanRevision != ocean.revision()) {
        createOceanResources(ocean);
    }
    check(vkWaitForFences(
              device, 1, &frameFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
          "vkWaitForFences");
    if (timestampResultsAvailable) {
        std::array<std::uint64_t, 4> timestamps{};
        const auto queryResult = vkGetQueryPoolResults(
            device, timestampPool, 0,
            static_cast<std::uint32_t>(timestamps.size()),
            sizeof(timestamps), timestamps.data(),
            sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        if (queryResult == VK_SUCCESS) {
            gpuFftMilliseconds =
                static_cast<double>(timestamps[1] - timestamps[0]) *
                timestampPeriod / 1.0e6;
            gpuRenderMilliseconds =
                static_cast<double>(timestamps[3] - timestamps[2]) *
                timestampPeriod / 1.0e6;
        }
    }

    std::uint32_t imageIndex = 0;
    const auto acquire = vkAcquireNextImageKHR(
        device, swapchain, std::numeric_limits<std::uint64_t>::max(),
        imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        check(acquire, "vkAcquireNextImageKHR");
    }

    check(vkResetFences(device, 1, &frameFence), "vkResetFences");
    updateUniform(camera, ocean, settings, simulationTime);
    ImGui::Render();
    recordCommands(
        imageIndex, ocean, camera, settings, simulationTime);

    const VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffers[imageIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished[imageIndex];
    check(vkQueueSubmit(graphicsQueue, 1, &submit, frameFence), "vkQueueSubmit");
    timestampResultsAvailable = true;

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &imageIndex;
    const auto result = vkQueuePresentKHR(presentQueue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else {
        check(result, "vkQueuePresentKHR");
    }
}

} // namespace water
