#include "Renderer/VulkanRendererInternal.hpp"

namespace water {

VkPipeline VulkanRenderer::Impl::makeComputePipeline(const char* shaderName) const
{
    const auto module = createShader(shaderName);
    VkPipelineShaderStageCreateInfo stage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = stage;
    info.layout = oceanComputeLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result = vkCreateComputePipelines(
        device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    check(result, "vkCreateComputePipelines");
    return pipeline;
}

void VulkanRenderer::Impl::createComputeResources()
{
    const std::array bindings{
        VkDescriptorSetLayoutBinding{
            0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{
            1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{
            2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{
            3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{
            4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo descriptorInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount =
        static_cast<std::uint32_t>(bindings.size());
    descriptorInfo.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(
              device, &descriptorInfo, nullptr,
              &oceanComputeDescriptorLayout),
          "vkCreateDescriptorSetLayout(ocean compute)");

    const std::array poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
    VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount =
        static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    check(vkCreateDescriptorPool(
              device, &poolInfo, nullptr, &oceanComputePool),
          "vkCreateDescriptorPool(ocean compute)");
    VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = oceanComputePool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &oceanComputeDescriptorLayout;
    check(vkAllocateDescriptorSets(
              device, &allocation, &oceanComputeSet),
          "vkAllocateDescriptorSets(ocean compute)");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = sizeof(OceanComputePush);
    VkPipelineLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &oceanComputeDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    check(vkCreatePipelineLayout(
              device, &layoutInfo, nullptr, &oceanComputeLayout),
          "vkCreatePipelineLayout(ocean compute)");

    oceanSpectrumPipeline = makeComputePipeline("ocean_spectrum.comp");
    oceanFftPipeline = makeComputePipeline("ocean_fft.comp");
    oceanFinalizePipeline = makeComputePipeline("ocean_finalize.comp");

    VkQueryPoolCreateInfo queryInfo{
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 4;
    check(vkCreateQueryPool(device, &queryInfo, nullptr, &timestampPool),
          "vkCreateQueryPool");
}

void VulkanRenderer::Impl::createOceanResources(const TessendorfOcean& ocean)
{
    vkDeviceWaitIdle(device);
    destroyOceanResources();
    const auto resolution = ocean.settings().resolution;
    oceanResolution = resolution;
    oceanRevision = ocean.revision();
    const auto sampleCount =
        static_cast<std::size_t>(resolution) * resolution;
    const auto seedBytes = sampleCount * sizeof(glm::vec4);
    const auto transformBytes =
        sampleCount * 9u * sizeof(glm::vec2);
    createBuffer(
        seedBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        spectrumBuffer, true);
    std::memcpy(
        spectrumBuffer.mapped,
        ocean.spectralSeeds().data(), seedBytes);
    for (auto& buffer : fftBuffers) {
        createBuffer(
            transformBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer);
    }
    createImage(
        resolution, resolution, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, displacementImage);
    createImage(
        resolution, resolution, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, normalImage);

    const auto createMesh = [this](const std::uint32_t meshResolution) {
        std::vector<Vertex> vertices;
        vertices.reserve(
            static_cast<std::size_t>(meshResolution + 1) *
            (meshResolution + 1));
        for (std::uint32_t y = 0; y <= meshResolution; ++y) {
            for (std::uint32_t x = 0; x <= meshResolution; ++x) {
                const glm::vec2 uv(
                    static_cast<float>(x) / meshResolution,
                    static_cast<float>(y) / meshResolution);
                vertices.push_back({
                    glm::vec3(uv.x - 0.5f, 0.0f, uv.y - 0.5f), uv});
            }
        }
        std::vector<std::uint32_t> indices;
        indices.reserve(
            static_cast<std::size_t>(meshResolution) *
            meshResolution * 6);
        const auto stride = meshResolution + 1;
        for (std::uint32_t y = 0; y < meshResolution; ++y) {
            for (std::uint32_t x = 0; x < meshResolution; ++x) {
                const auto topLeft = y * stride + x;
                const auto bottomLeft = (y + 1) * stride + x;
                indices.insert(indices.end(), {
                    topLeft, bottomLeft, topLeft + 1,
                    topLeft + 1, bottomLeft, bottomLeft + 1});
            }
        }
        MeshBuffers mesh;
        mesh.resolution = meshResolution;
        mesh.indexCount = static_cast<std::uint32_t>(indices.size());
        createBuffer(
            vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mesh.vertex, true);
        std::memcpy(mesh.vertex.mapped, vertices.data(), mesh.vertex.size);
        createBuffer(
            indices.size() * sizeof(std::uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mesh.index, true);
        std::memcpy(mesh.index.mapped, indices.data(), mesh.index.size);
        lodMeshes.push_back(mesh);
    };
    for (std::uint32_t level = 0; level < 3; ++level) {
        createMesh(std::max(16u, resolution >> level));
    }

    const std::array imageInfos{
        VkDescriptorImageInfo{
            sampler, displacementImage.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkDescriptorImageInfo{
            sampler, normalImage.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (std::uint32_t index = 0; index < writes.size(); ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = descriptorSet;
        writes[index].dstBinding = index + 1;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[index].pImageInfo = &imageInfos[index];
    }
    vkUpdateDescriptorSets(
        device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

    const std::array bufferInfos{
        VkDescriptorBufferInfo{
            spectrumBuffer.handle, 0, spectrumBuffer.size},
        VkDescriptorBufferInfo{
            fftBuffers[0].handle, 0, fftBuffers[0].size},
        VkDescriptorBufferInfo{
            fftBuffers[1].handle, 0, fftBuffers[1].size}};
    const std::array storageImageInfos{
        VkDescriptorImageInfo{
            VK_NULL_HANDLE, displacementImage.view,
            VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{
            VK_NULL_HANDLE, normalImage.view,
            VK_IMAGE_LAYOUT_GENERAL}};
    std::array<VkWriteDescriptorSet, 5> computeWrites{};
    for (std::uint32_t index = 0; index < 3; ++index) {
        computeWrites[index].sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[index].dstSet = oceanComputeSet;
        computeWrites[index].dstBinding = index;
        computeWrites[index].descriptorCount = 1;
        computeWrites[index].descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[index].pBufferInfo = &bufferInfos[index];
    }
    for (std::uint32_t index = 0; index < 2; ++index) {
        auto& write = computeWrites[index + 3];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = oceanComputeSet;
        write.dstBinding = index + 3;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &storageImageInfos[index];
    }
    vkUpdateDescriptorSets(
        device, static_cast<std::uint32_t>(computeWrites.size()),
        computeWrites.data(), 0, nullptr);
    texturesInitialized = false;
}

void VulkanRenderer::Impl::destroyOceanResources()
{
    for (auto& mesh : lodMeshes) {
        destroyBuffer(mesh.vertex);
        destroyBuffer(mesh.index);
    }
    lodMeshes.clear();
    destroyBuffer(spectrumBuffer);
    for (auto& buffer : fftBuffers) destroyBuffer(buffer);
    destroyImage(displacementImage);
    destroyImage(normalImage);
    oceanResolution = 0;
    oceanRevision = 0;
    texturesInitialized = false;
}

void VulkanRenderer::Impl::imageBarrier(
    const VkCommandBuffer command,
    const VkImage image,
    const VkImageLayout oldLayout,
    const VkImageLayout newLayout,
    const VkPipelineStageFlags sourceStage,
    const VkPipelineStageFlags destinationStage,
    const VkAccessFlags sourceAccess,
    const VkAccessFlags destinationAccess)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    vkCmdPipelineBarrier(
        command, sourceStage, destinationStage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanRenderer::Impl::computeOcean(
    const VkCommandBuffer command,
    const TessendorfOcean& ocean,
    const float simulationTime)
{
    const auto oldLayout = texturesInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    const auto sourceStage = texturesInitialized
        ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const auto sourceAccess = texturesInitialized
        ? VK_ACCESS_SHADER_READ_BIT
        : VkAccessFlags{0};
    for (const auto image : {
             displacementImage.handle, normalImage.handle}) {
        imageBarrier(
            command, image, oldLayout, VK_IMAGE_LAYOUT_GENERAL,
            sourceStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            sourceAccess, VK_ACCESS_SHADER_WRITE_BIT);
    }

    vkCmdResetQueryPool(command, timestampPool, 0, 4);
    vkCmdWriteTimestamp(
        command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPool, 0);
    vkCmdBindDescriptorSets(
        command, VK_PIPELINE_BIND_POINT_COMPUTE, oceanComputeLayout,
        0, 1, &oceanComputeSet, 0, nullptr);

    OceanComputePush push{};
    push.resolution = oceanResolution;
    push.time = simulationTime;
    push.patchLength = ocean.settings().patchLength;
    push.choppiness = ocean.settings().choppiness;
    vkCmdBindPipeline(
        command, VK_PIPELINE_BIND_POINT_COMPUTE, oceanSpectrumPipeline);
    vkCmdPushConstants(
        command, oceanComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    const auto sampleCount = oceanResolution * oceanResolution;
    vkCmdDispatch(command, (sampleCount + 255u) / 256u, 1, 1);

    VkMemoryBarrier computeBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeBarrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    const auto insertComputeBarrier = [&] {
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
    };
    insertComputeBarrier();

    vkCmdBindPipeline(
        command, VK_PIPELINE_BIND_POINT_COMPUTE, oceanFftPipeline);
    const auto stageCount =
        static_cast<std::uint32_t>(std::countr_zero(oceanResolution));
    const auto butterflyCount = sampleCount / 2u * 9u;
    for (std::uint32_t axis = 0; axis < 2; ++axis) {
        for (std::uint32_t stage = 0; stage < stageCount; ++stage) {
            push.stage = stage;
            push.horizontal = axis == 0 ? 1u : 0u;
            vkCmdPushConstants(
                command, oceanComputeLayout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0, sizeof(push), &push);
            vkCmdDispatch(
                command, (butterflyCount + 255u) / 256u, 1, 1);
            insertComputeBarrier();
            push.sourceBuffer ^= 1u;
        }
    }

    vkCmdBindPipeline(
        command, VK_PIPELINE_BIND_POINT_COMPUTE, oceanFinalizePipeline);
    vkCmdPushConstants(
        command, oceanComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(
        command,
        (oceanResolution + 15u) / 16u,
        (oceanResolution + 15u) / 16u,
        1);
    vkCmdWriteTimestamp(
        command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPool, 1);

    for (const auto image : {
             displacementImage.handle, normalImage.handle}) {
        imageBarrier(
            command, image, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    texturesInitialized = true;
}

} // namespace water
