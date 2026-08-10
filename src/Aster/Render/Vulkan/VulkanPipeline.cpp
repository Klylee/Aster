#include "VulkanPipeline.h"
#include "VulkanUtil.h"

#include <array>
#include <cstring>
#include <functional>
#include <iostream>

#ifndef ASTER_SHADOW_MAP_BACKFACE_CULLING
#define ASTER_SHADOW_MAP_BACKFACE_CULLING 1
#endif

using VulkanUtil::CheckVkResult;

namespace aster
{
using VulkanUtil::CreateShaderModule;
using VulkanUtil::FindMemoryType;
using VulkanUtil::ReadFile;

// 创建 host-visible + coherent 的统一缓冲（相机 / 灯光 UBO）
static bool CreateUbo(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                      VkBuffer &outBuffer, VkDeviceMemory &outMemory, void *&outMapped)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReq);
    uint32_t memType = FindMemoryType(physicalDevice, memReq.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == std::numeric_limits<uint32_t>::max())
        return false;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memType;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS ||
        vkBindBufferMemory(device, outBuffer, outMemory, 0) != VK_SUCCESS)
        return false;

    vkMapMemory(device, outMemory, 0, size, 0, &outMapped);
    return true;
}

bool VulkanPipeline::Create(VkDevice device, VkPhysicalDevice physicalDevice,
                            VkRenderPass renderPass,
                            const std::string &vertSpv, const std::string &fragSpv,
                            size_t pushConstantSize,
                            const VkVertexInputBindingDescription *bindings, uint32_t bindingCount,
                            const VkVertexInputAttributeDescription *attrs, uint32_t attrCount,
                            bool enableDepthTest, bool enableBlend)
{
    this->device = device;
    this->physicalDevice = physicalDevice;
    this->renderPass_ = renderPass;
    this->pushConstantSize_ = pushConstantSize;

    // 保存顶点输入布局，供阴影深度管线（EnableShadowMap）复用
    vertexBindings_.assign(bindings, bindings + bindingCount);
    vertexAttrs_.assign(attrs, attrs + attrCount);

    // ---- 着色器模块 ----
    std::vector<char> vertCode = ReadFile(vertSpv);
    std::vector<char> fragCode = ReadFile(fragSpv);
    if (vertCode.empty() || fragCode.empty())
    {
        std::cerr << "[VulkanPipeline] Shader files not found: " << vertSpv << " / " << fragSpv
                  << std::endl;
        return false;
    }
    vertModule = CreateShaderModule(device, vertCode);
    fragModule = CreateShaderModule(device, fragCode);

    // ---- 描述符集布局：set 0 ----
    //   binding 0  = 相机 UBO（顶点阶段）
    //   binding 1  = 灯光 UBO（片元阶段）
    //   binding 2  = 2D shadow maps 数组（方向光/聚光，片元阶段）
    //   binding 3  = 阴影 UBO 数组（片元阶段）
    //   binding 4  = 点光源 cubemap shadow map（片元阶段）
    //   binding 5  = 点光源阴影 UBO（片元阶段）
    //   binding 6  = 环境 cubemap（天空盒 / 反射，片元阶段）
    //   binding 7  = 漫反射 irradiance cubemap（片元阶段）
    //   binding 8  = 高光预过滤 cubemap（片元阶段）
    //   binding 9  = BRDF LUT（片元阶段）
    //   binding 10 = 环境 UBO（强度/模式/材质/相机，片元阶段）
    VkDescriptorSetLayoutBinding layoutBindings[11]{};
    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[2].binding = 2;
    layoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[2].descriptorCount = MAX_2D_SHADOW_MAPS;
    layoutBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[3].binding = 3;
    layoutBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[3].descriptorCount = 1;
    layoutBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[4].binding = 4;
    layoutBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[4].descriptorCount = 1;
    layoutBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[5].binding = 5;
    layoutBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[5].descriptorCount = 1;
    layoutBindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[6].binding = 6;
    layoutBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[6].descriptorCount = 1;
    layoutBindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[7].binding = 7;
    layoutBindings[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[7].descriptorCount = 1;
    layoutBindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[8].binding = 8;
    layoutBindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[8].descriptorCount = 1;
    layoutBindings[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[9].binding = 9;
    layoutBindings[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[9].descriptorCount = 1;
    layoutBindings[9].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[10].binding = 10;
    layoutBindings[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[10].descriptorCount = 1;
    layoutBindings[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 11;
    setLayoutInfo.pBindings = layoutBindings;
    if (vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create descriptor set layout" << std::endl;
        Destroy();
        return false;
    }

    // ---- 管线布局：描述集 + push constants ----
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = (uint32_t)pushConstantSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create pipeline layout" << std::endl;
        Destroy();
        return false;
    }

    // ---- 相机 / 灯光 / 阴影 UBO 缓冲（host-visible + coherent） ----
    if (!CreateUbo(device, physicalDevice, sizeof(CameraUBO), uboBuffer, uboMemory, uboMapped))
    {
        std::cerr << "[VulkanPipeline] Failed to create camera UBO" << std::endl;
        Destroy();
        return false;
    }
    if (!CreateUbo(device, physicalDevice, sizeof(CameraUBO), shadowCamBuffer, shadowCamMemory, shadowCamMapped))
    {
        std::cerr << "[VulkanPipeline] Failed to create shadow camera UBO" << std::endl;
        Destroy();
        return false;
    }
    if (!CreateUbo(device, physicalDevice, sizeof(LightUBO), lightsBuffer, lightsMemory, lightsMapped))
    {
        std::cerr << "[VulkanPipeline] Failed to create lights UBO" << std::endl;
        Destroy();
        return false;
    }
    if (!CreateUbo(device, physicalDevice, sizeof(ShadowUBO), shadowUboBuffer, shadowUboMemory, shadowUboMapped))
    {
        std::cerr << "[VulkanPipeline] Failed to create shadow UBO" << std::endl;
        Destroy();
        return false;
    }
    if (!CreateUbo(device, physicalDevice, sizeof(PointShadowUBO), pointShadowUboBuffer, pointShadowUboMemory, pointShadowUboMapped))
    {
        std::cerr << "[VulkanPipeline] Failed to create point shadow UBO" << std::endl;
        Destroy();
        return false;
    }
    // 环境 UBO（环境贴图参数）
    if (!CreateUbo(device, physicalDevice, sizeof(EnvUBO), envUboBuffer, envUboMemory, envUboMapped))
    {
        std::cerr << "[VulkanPipeline] Failed to create environment UBO" << std::endl;
        Destroy();
        return false;
    }

    // ---- 描述符池 + 描述符集 ----
    // 每个描述符集含 5 个 UBO（相机/灯光/阴影/点阴影/环境）与 6 个 CIS（2D 阴影数组 +
    // 点阴影 cubemap + 环境 cubemap + irradiance + 预过滤 + BRDF LUT）；
    // 主集 + 阴影集共 2 套。容量按 2 套计，保证分配不超。
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 5 * 2; // 主集 + 阴影集各 5 个 UBO
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = (MAX_2D_SHADOW_MAPS + 1 + 4) * 2; // 2D shadow maps + 点阴影 + 4 个环境贴图，主/阴影各一份

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2; // 主描述符集 + 阴影深度 pass 描述符集
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create descriptor pool" << std::endl;
        Destroy();
        return false;
    }

    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = descriptorPool;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &setAllocInfo, &descriptorSet) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to allocate descriptor set" << std::endl;
        Destroy();
        return false;
    }
    // 阴影深度 pass 专用描述符集（binding 0 = 灯光视角相机 UBO）
    if (vkAllocateDescriptorSets(device, &setAllocInfo, &shadowDescriptorSet) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to allocate shadow descriptor set" << std::endl;
        Destroy();
        return false;
    }

    // 每套描述符集 5 个 UBO：binding 0(相机)/1(灯光)/3(阴影)/5(点阴影)/10(环境)
    VkDescriptorBufferInfo bufferInfos[10]{};
    // 主描述符集
    bufferInfos[0].buffer = uboBuffer;
    bufferInfos[0].offset = 0;
    bufferInfos[0].range = sizeof(CameraUBO);
    bufferInfos[1].buffer = lightsBuffer;
    bufferInfos[1].offset = 0;
    bufferInfos[1].range = sizeof(LightUBO);
    bufferInfos[2].buffer = shadowUboBuffer;
    bufferInfos[2].offset = 0;
    bufferInfos[2].range = sizeof(ShadowUBO);
    bufferInfos[3].buffer = pointShadowUboBuffer;
    bufferInfos[3].offset = 0;
    bufferInfos[3].range = sizeof(PointShadowUBO);
    bufferInfos[4].buffer = envUboBuffer;
    bufferInfos[4].offset = 0;
    bufferInfos[4].range = sizeof(EnvUBO);
    // 阴影深度 pass 描述符集：binding 0 = 灯光视角相机 UBO，其余共享
    bufferInfos[5].buffer = shadowCamBuffer;
    bufferInfos[5].offset = 0;
    bufferInfos[5].range = sizeof(CameraUBO);
    bufferInfos[6].buffer = lightsBuffer;
    bufferInfos[6].offset = 0;
    bufferInfos[6].range = sizeof(LightUBO);
    bufferInfos[7].buffer = shadowUboBuffer;
    bufferInfos[7].offset = 0;
    bufferInfos[7].range = sizeof(ShadowUBO);
    bufferInfos[8].buffer = pointShadowUboBuffer;
    bufferInfos[8].offset = 0;
    bufferInfos[8].range = sizeof(PointShadowUBO);
    bufferInfos[9].buffer = envUboBuffer;
    bufferInfos[9].offset = 0;
    bufferInfos[9].range = sizeof(EnvUBO);

    VkWriteDescriptorSet writes[10]{};
    for (int s = 0; s < 2; ++s)
    {
        const VkDescriptorBufferInfo *bi = &bufferInfos[s * 5];
        const int dstBindings[5] = {0, 1, 3, 5, 10};
        for (int w = 0; w < 5; ++w)
        {
            writes[s * 5 + w].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[s * 5 + w].dstSet = (s == 0) ? descriptorSet : shadowDescriptorSet;
            writes[s * 5 + w].dstBinding = dstBindings[w];
            writes[s * 5 + w].dstArrayElement = 0;
            writes[s * 5 + w].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[s * 5 + w].descriptorCount = 1;
            writes[s * 5 + w].pBufferInfo = &bi[w];
        }
    }
    vkUpdateDescriptorSets(device, 10, writes, 0, nullptr);

    // ---- 图形管线 ----
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = bindingCount;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = attrCount;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = enableDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = enableDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = enableBlend ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create graphics pipeline" << std::endl;
        Destroy();
        return false;
    }

    return true;
}

void VulkanPipeline::Destroy()
{
    if (!device)
        return;

    if (uboMapped)
    {
        vkUnmapMemory(device, uboMemory);
        uboMapped = nullptr;
    }
    if (uboMemory)
    {
        vkFreeMemory(device, uboMemory, nullptr);
        uboMemory = VK_NULL_HANDLE;
    }
    if (uboBuffer)
    {
        vkDestroyBuffer(device, uboBuffer, nullptr);
        uboBuffer = VK_NULL_HANDLE;
    }
    if (shadowCamMapped)
    {
        vkUnmapMemory(device, shadowCamMemory);
        shadowCamMapped = nullptr;
    }
    if (shadowCamMemory)
    {
        vkFreeMemory(device, shadowCamMemory, nullptr);
        shadowCamMemory = VK_NULL_HANDLE;
    }
    if (shadowCamBuffer)
    {
        vkDestroyBuffer(device, shadowCamBuffer, nullptr);
        shadowCamBuffer = VK_NULL_HANDLE;
    }
    if (lightsMapped)
    {
        vkUnmapMemory(device, lightsMemory);
        lightsMapped = nullptr;
    }
    if (lightsMemory)
    {
        vkFreeMemory(device, lightsMemory, nullptr);
        lightsMemory = VK_NULL_HANDLE;
    }
    if (lightsBuffer)
    {
        vkDestroyBuffer(device, lightsBuffer, nullptr);
        lightsBuffer = VK_NULL_HANDLE;
    }
    if (shadowUboMapped)
    {
        vkUnmapMemory(device, shadowUboMemory);
        shadowUboMapped = nullptr;
    }
    if (shadowUboMemory)
    {
        vkFreeMemory(device, shadowUboMemory, nullptr);
        shadowUboMemory = VK_NULL_HANDLE;
    }
    if (shadowUboBuffer)
    {
        vkDestroyBuffer(device, shadowUboBuffer, nullptr);
        shadowUboBuffer = VK_NULL_HANDLE;
    }
    if (pointShadowUboMapped)
    {
        vkUnmapMemory(device, pointShadowUboMemory);
        pointShadowUboMapped = nullptr;
    }
    if (pointShadowUboMemory)
    {
        vkFreeMemory(device, pointShadowUboMemory, nullptr);
        pointShadowUboMemory = VK_NULL_HANDLE;
    }
    if (pointShadowUboBuffer)
    {
        vkDestroyBuffer(device, pointShadowUboBuffer, nullptr);
        pointShadowUboBuffer = VK_NULL_HANDLE;
    }
    if (shadowSampler)
    {
        vkDestroySampler(device, shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
    }
    // 多张 2D shadow map（方向光/聚光）
    for (auto &fb : shadowFramebuffers)
    {
        if (fb)
            vkDestroyFramebuffer(device, fb, nullptr);
    }
    shadowFramebuffers.clear();
    for (auto &view : shadowImageViews)
    {
        if (view)
            vkDestroyImageView(device, view, nullptr);
    }
    shadowImageViews.clear();
    for (auto &mem : shadowImageMemories)
    {
        if (mem)
            vkFreeMemory(device, mem, nullptr);
    }
    shadowImageMemories.clear();
    for (auto &img : shadowImages)
    {
        if (img)
            vkDestroyImage(device, img, nullptr);
    }
    shadowImages.clear();
    shadowMapCount2D_ = 0;
    shadowMapEnabled_ = false;

    // 点光源 cubemap shadow map
    if (pointShadowSampler)
    {
        vkDestroySampler(device, pointShadowSampler, nullptr);
        pointShadowSampler = VK_NULL_HANDLE;
    }
    for (auto &fb : pointShadowFaceFramebuffers)
    {
        if (fb)
            vkDestroyFramebuffer(device, fb, nullptr);
    }
    pointShadowFaceFramebuffers.clear();
    for (auto &view : pointShadowFaceViews)
    {
        if (view)
            vkDestroyImageView(device, view, nullptr);
    }
    pointShadowFaceViews.clear();
    if (pointShadowImageView)
    {
        vkDestroyImageView(device, pointShadowImageView, nullptr);
        pointShadowImageView = VK_NULL_HANDLE;
    }
    if (pointShadowImageMemory)
    {
        vkFreeMemory(device, pointShadowImageMemory, nullptr);
        pointShadowImageMemory = VK_NULL_HANDLE;
    }
    if (pointShadowImage)
    {
        vkDestroyImage(device, pointShadowImage, nullptr);
        pointShadowImage = VK_NULL_HANDLE;
    }
    if (pointShadowRenderPass)
    {
        vkDestroyRenderPass(device, pointShadowRenderPass, nullptr);
        pointShadowRenderPass = VK_NULL_HANDLE;
    }
    if (pointShadowPipeline)
    {
        vkDestroyPipeline(device, pointShadowPipeline, nullptr);
        pointShadowPipeline = VK_NULL_HANDLE;
    }
    pointShadowMapEnabled_ = false;

    if (shadowRenderPass)
    {
        vkDestroyRenderPass(device, shadowRenderPass, nullptr);
        shadowRenderPass = VK_NULL_HANDLE;
    }
    if (shadowPipeline)
    {
        vkDestroyPipeline(device, shadowPipeline, nullptr);
        shadowPipeline = VK_NULL_HANDLE;
    }
    if (descriptorPool)
    {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    descriptorSet = VK_NULL_HANDLE;

    if (pipeline)
    {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (layout)
    {
        vkDestroyPipelineLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout)
    {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (vertModule)
    {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vertModule = VK_NULL_HANDLE;
    }
    if (fragModule)
    {
        vkDestroyShaderModule(device, fragModule, nullptr);
        fragModule = VK_NULL_HANDLE;
    }

    // ---- 环境贴图资源 ----
    if (envUboMapped)
    {
        vkUnmapMemory(device, envUboMemory);
        envUboMapped = nullptr;
    }
    if (envUboMemory)
    {
        vkFreeMemory(device, envUboMemory, nullptr);
        envUboMemory = VK_NULL_HANDLE;
    }
    if (envUboBuffer)
    {
        vkDestroyBuffer(device, envUboBuffer, nullptr);
        envUboBuffer = VK_NULL_HANDLE;
    }
    if (skyboxPipeline)
    {
        vkDestroyPipeline(device, skyboxPipeline, nullptr);
        skyboxPipeline = VK_NULL_HANDLE;
    }
    if (envSampler)
    {
        vkDestroySampler(device, envSampler, nullptr);
        envSampler = VK_NULL_HANDLE;
    }
    if (prefilteredSampler)
    {
        vkDestroySampler(device, prefilteredSampler, nullptr);
        prefilteredSampler = VK_NULL_HANDLE;
    }
    if (brdfSampler)
    {
        vkDestroySampler(device, brdfSampler, nullptr);
        brdfSampler = VK_NULL_HANDLE;
    }
    // 占位资源
    if (dummyCubeView)
    {
        vkDestroyImageView(device, dummyCubeView, nullptr);
        dummyCubeView = VK_NULL_HANDLE;
    }
    if (dummyCubeMemory)
    {
        vkFreeMemory(device, dummyCubeMemory, nullptr);
        dummyCubeMemory = VK_NULL_HANDLE;
    }
    if (dummyCubeImage)
    {
        vkDestroyImage(device, dummyCubeImage, nullptr);
        dummyCubeImage = VK_NULL_HANDLE;
    }
    if (dummyLutView)
    {
        vkDestroyImageView(device, dummyLutView, nullptr);
        dummyLutView = VK_NULL_HANDLE;
    }
    if (dummyLutMemory)
    {
        vkFreeMemory(device, dummyLutMemory, nullptr);
        dummyLutMemory = VK_NULL_HANDLE;
    }
    if (dummyLutImage)
    {
        vkDestroyImage(device, dummyLutImage, nullptr);
        dummyLutImage = VK_NULL_HANDLE;
    }
    // 真实环境 cubemap
    if (envImageView)
    {
        vkDestroyImageView(device, envImageView, nullptr);
        envImageView = VK_NULL_HANDLE;
    }
    if (envImageMemory)
    {
        vkFreeMemory(device, envImageMemory, nullptr);
        envImageMemory = VK_NULL_HANDLE;
    }
    if (envImage)
    {
        vkDestroyImage(device, envImage, nullptr);
        envImage = VK_NULL_HANDLE;
    }
    // irradiance cubemap
    if (irrImageView)
    {
        vkDestroyImageView(device, irrImageView, nullptr);
        irrImageView = VK_NULL_HANDLE;
    }
    if (irrImageMemory)
    {
        vkFreeMemory(device, irrImageMemory, nullptr);
        irrImageMemory = VK_NULL_HANDLE;
    }
    if (irrImage)
    {
        vkDestroyImage(device, irrImage, nullptr);
        irrImage = VK_NULL_HANDLE;
    }
    // 预过滤 cubemap
    if (prefilteredImageView)
    {
        vkDestroyImageView(device, prefilteredImageView, nullptr);
        prefilteredImageView = VK_NULL_HANDLE;
    }
    if (prefilteredImageMemory)
    {
        vkFreeMemory(device, prefilteredImageMemory, nullptr);
        prefilteredImageMemory = VK_NULL_HANDLE;
    }
    if (prefilteredImage)
    {
        vkDestroyImage(device, prefilteredImage, nullptr);
        prefilteredImage = VK_NULL_HANDLE;
    }
    // BRDF LUT
    if (brdfImageView)
    {
        vkDestroyImageView(device, brdfImageView, nullptr);
        brdfImageView = VK_NULL_HANDLE;
    }
    if (brdfImageMemory)
    {
        vkFreeMemory(device, brdfImageMemory, nullptr);
        brdfImageMemory = VK_NULL_HANDLE;
    }
    if (brdfImage)
    {
        vkDestroyImage(device, brdfImage, nullptr);
        brdfImage = VK_NULL_HANDLE;
    }
    environmentReady_ = false;

    device = VK_NULL_HANDLE;
}

void VulkanPipeline::UpdateCameraUBO(const glm::mat4 &view, const glm::mat4 &proj)
{
    if (!uboMapped)
        return;
    CameraUBO ubo{};
    ubo.view = view;
    ubo.projection = proj;
    std::memcpy(uboMapped, &ubo, sizeof(ubo));
}

void VulkanPipeline::UpdateShadowCameraUBO(const glm::mat4 &view, const glm::mat4 &proj)
{
    if (!shadowCamMapped)
        return;
    CameraUBO ubo{};
    ubo.view = view;
    ubo.projection = proj;
    std::memcpy(shadowCamMapped, &ubo, sizeof(ubo));
}

void VulkanPipeline::UpdateLightsUBO(const LightUBO &lights)
{
    if (!lightsMapped)
        return;

    std::memcpy(lightsMapped, &lights, sizeof(lights));
}

int VulkanPipeline::EnableShadowMaps(const std::string &shaderDir, uint32_t size, int count)
{
    if (!device || count <= 0)
        return 0;
    shadowExtent = {size, size};
    int created = 0;

    // ---- 深度渲染 pass（无颜色附件，方向光/聚光 2D shadow map 共用） ----
    if (!shadowRenderPass)
    {
        VkAttachmentDescription shadowDepth{};
        shadowDepth.format = VK_FORMAT_D32_SFLOAT;
        shadowDepth.samples = VK_SAMPLE_COUNT_1_BIT;
        shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        shadowDepth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        shadowDepth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        shadowDepth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        shadowDepth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference shadowDepthRef{};
        shadowDepthRef.attachment = 0;
        shadowDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &shadowDepthRef;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &shadowDepth;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &shadowRenderPass) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create shadow render pass" << std::endl;
            return 0;
        }
    }

    // ---- 采样器（线性过滤，clamp；非深度比较采样，直接采样深度值） ----
    if (!shadowSampler)
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create shadow sampler" << std::endl;
            return 0;
        }
    }

    // ---- 每盏 2D 阴影灯创建：深度图像 + 视图 + 帧缓冲 ----
    for (int i = 0; i < count; i++)
    {
        VkImage img = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer fb = VK_NULL_HANDLE;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = VK_FORMAT_D32_SFLOAT;
        imgInfo.extent = {size, size, 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &imgInfo, nullptr, &img) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create shadow image " << i << std::endl;
            break;
        }

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device, img, &memReq);
        uint32_t memType = FindMemoryType(physicalDevice, memReq.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memType == std::numeric_limits<uint32_t>::max())
        {
            std::cerr << "[VulkanPipeline] No device-local memory for shadow map" << std::endl;
            vkDestroyImage(device, img, nullptr);
            break;
        }
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memType;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &mem) != VK_SUCCESS ||
            vkBindImageMemory(device, img, mem, 0) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to allocate/bind shadow image memory" << std::endl;
            vkDestroyImage(device, img, nullptr);
            break;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = img;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create shadow image view " << i << std::endl;
            vkDestroyImage(device, img, nullptr);
            vkFreeMemory(device, mem, nullptr);
            break;
        }

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = shadowRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &view;
        fbInfo.width = size;
        fbInfo.height = size;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &fb) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create shadow framebuffer " << i << std::endl;
            vkDestroyImageView(device, view, nullptr);
            vkDestroyImage(device, img, nullptr);
            vkFreeMemory(device, mem, nullptr);
            break;
        }

        shadowImages.push_back(img);
        shadowImageMemories.push_back(mem);
        shadowImageViews.push_back(view);
        shadowFramebuffers.push_back(fb);
        created++;
    }

    if (created == 0)
        return 0;

    shadowMapCount2D_ = (int)shadowImages.size();
    shadowMapEnabled_ = true;

    // ---- 深度管线（0 颜色附件；复用 mesh.vert + depth.frag 与主管线布局） ----
    if (!shadowPipeline)
    {
        std::vector<char> vertCode = ReadFile(shaderDir + "/mesh.vert.spv");
        std::vector<char> fragCode = ReadFile(shaderDir + "/depth.frag.spv");
        if (vertCode.empty() || fragCode.empty())
        {
            std::cerr << "[VulkanPipeline] Shadow shaders missing: " << shaderDir << std::endl;
            return created;
        }
        VkShaderModule vertMod = CreateShaderModule(device, vertCode);
        VkShaderModule fragMod = CreateShaderModule(device, fragCode);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = (uint32_t)vertexBindings_.size();
        vertexInput.pVertexBindingDescriptions = vertexBindings_.data();
        vertexInput.vertexAttributeDescriptionCount = (uint32_t)vertexAttrs_.size();
        vertexInput.pVertexAttributeDescriptions = vertexAttrs_.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
#if ASTER_SHADOW_MAP_BACKFACE_CULLING
        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; // 方案 B：shadow map 只渲染背面（剔除朝向光源的正面）→ 几何上消除自遮挡
#else
        rasterizer.cullMode = VK_CULL_MODE_NONE; // 双面渲染
#endif
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        // 方案 A：slope-scaled depth bias → 按深度斜率自适应偏移（掠射角自动加大，正面几乎不偏移）
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.5f;
        rasterizer.depthBiasSlopeFactor = 1.75f;
        rasterizer.depthBiasClamp = 0.0f;
        rasterizer.depthClampEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 0; // 深度 pass 无颜色输出

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pInfo{};
        pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pInfo.stageCount = 2;
        pInfo.pStages = stages;
        pInfo.pVertexInputState = &vertexInput;
        pInfo.pInputAssemblyState = &inputAssembly;
        pInfo.pViewportState = &viewportState;
        pInfo.pRasterizationState = &rasterizer;
        pInfo.pMultisampleState = &multisampling;
        pInfo.pDepthStencilState = &depthStencil;
        pInfo.pColorBlendState = &colorBlending;
        pInfo.pDynamicState = &dyn;
        pInfo.layout = layout; // 复用主管线布局（描述集 + push constants）
        pInfo.renderPass = shadowRenderPass;
        pInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &shadowPipeline) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create shadow pipeline" << std::endl;
            vkDestroyShaderModule(device, vertMod, nullptr);
            vkDestroyShaderModule(device, fragMod, nullptr);
            return created;
        }
        vkDestroyShaderModule(device, vertMod, nullptr);
        vkDestroyShaderModule(device, fragMod, nullptr);
    }

    // ---- 描述符：binding 2 = 2D shadow maps 数组 ----
    std::vector<VkDescriptorImageInfo> imgDescs(shadowImageViews.size());
    for (size_t i = 0; i < shadowImageViews.size(); i++)
    {
        imgDescs[i].sampler = shadowSampler;
        imgDescs[i].imageView = shadowImageViews[i];
        imgDescs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 2;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = (uint32_t)imgDescs.size();
    write.pImageInfo = imgDescs.data();
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // 阴影深度 pass 描述符集同样需要绑定 2D shadow maps
    VkWriteDescriptorSet writeShadow = write;
    writeShadow.dstSet = shadowDescriptorSet;
    vkUpdateDescriptorSets(device, 1, &writeShadow, 0, nullptr);

    std::cout << "[VulkanPipeline] Shadow maps enabled (" << size << "x" << size
              << " x" << created << ")" << std::endl;
    return created;
}

bool VulkanPipeline::EnablePointShadowMap(const std::string &shaderDir, uint32_t size)
{
    if (!device)
        return false;
    pointShadowExtent = {size, size};

    // ---- 立方体贴图深度图像（D32，6 层，可采样） ----
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imgInfo.format = VK_FORMAT_D32_SFLOAT;
    imgInfo.extent = {size, size, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 6;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imgInfo, nullptr, &pointShadowImage) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create point shadow cube image" << std::endl;
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, pointShadowImage, &memReq);
    uint32_t memType = FindMemoryType(physicalDevice, memReq.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == std::numeric_limits<uint32_t>::max())
    {
        std::cerr << "[VulkanPipeline] No device-local memory for point shadow map" << std::endl;
        return false;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memType;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &pointShadowImageMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, pointShadowImage, pointShadowImageMemory, 0) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to allocate/bind point shadow memory" << std::endl;
        return false;
    }

    // ---- 采样用 cube 视图 + 渲染用 6 个 face 2D 视图 ----
    VkImageViewCreateInfo cubeViewInfo{};
    cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cubeViewInfo.image = pointShadowImage;
    cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubeViewInfo.format = VK_FORMAT_D32_SFLOAT;
    cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    cubeViewInfo.subresourceRange.baseMipLevel = 0;
    cubeViewInfo.subresourceRange.levelCount = 1;
    cubeViewInfo.subresourceRange.baseArrayLayer = 0;
    cubeViewInfo.subresourceRange.layerCount = 6;
    if (vkCreateImageView(device, &cubeViewInfo, nullptr, &pointShadowImageView) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create point shadow cube view" << std::endl;
        return false;
    }

    pointShadowFaceViews.resize(6);
    for (uint32_t f = 0; f < 6; f++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = pointShadowImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = f;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &pointShadowFaceViews[f]) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create point shadow face view " << f << std::endl;
            return false;
        }
    }

    // ---- 点光源采样器（线性，clamp） ----
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &pointShadowSampler) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create point shadow sampler" << std::endl;
        return false;
    }

    // ---- 深度渲染 pass（无颜色附件） ----
    VkAttachmentDescription shadowDepth{};
    shadowDepth.format = VK_FORMAT_D32_SFLOAT;
    shadowDepth.samples = VK_SAMPLE_COUNT_1_BIT;
    shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    shadowDepth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    shadowDepth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    shadowDepth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    shadowDepth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference shadowDepthRef{};
    shadowDepthRef.attachment = 0;
    shadowDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &shadowDepthRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &shadowDepth;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &rpInfo, nullptr, &pointShadowRenderPass) != VK_SUCCESS)
    {
        std::cerr << "[VulkanPipeline] Failed to create point shadow render pass" << std::endl;
        return false;
    }

    // ---- 6 个 face 帧缓冲 ----
    pointShadowFaceFramebuffers.resize(6);
    for (uint32_t f = 0; f < 6; f++)
    {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = pointShadowRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &pointShadowFaceViews[f];
        fbInfo.width = size;
        fbInfo.height = size;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &pointShadowFaceFramebuffers[f]) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create point shadow face framebuffer " << f << std::endl;
            return false;
        }
    }

    // ---- 深度管线（0 颜色附件；mesh.vert + depth_point.frag 与主管线布局） ----
    // 点光源 cubemap 需要写"到光源的归一化线性距离"，用专用 depth_point.frag
    {
        std::vector<char> vertCode = ReadFile(shaderDir + "/mesh.vert.spv");
        std::vector<char> fragCode = ReadFile(shaderDir + "/depth_point.frag.spv");
        if (vertCode.empty() || fragCode.empty())
        {
            std::cerr << "[VulkanPipeline] Point shadow shaders missing: " << shaderDir << std::endl;
            return false;
        }
        VkShaderModule vertMod = CreateShaderModule(device, vertCode);
        VkShaderModule fragMod = CreateShaderModule(device, fragCode);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = (uint32_t)vertexBindings_.size();
        vertexInput.pVertexBindingDescriptions = vertexBindings_.data();
        vertexInput.vertexAttributeDescriptionCount = (uint32_t)vertexAttrs_.size();
        vertexInput.pVertexAttributeDescriptions = vertexAttrs_.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        // 方案 B：shadow map 只渲染背面（剔除朝向光源的正面）→ 几何上消除自遮挡
        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        // 方案 A：slope-scaled depth bias → 按深度斜率自适应偏移
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.5f;
        rasterizer.depthBiasSlopeFactor = 1.75f;
        rasterizer.depthBiasClamp = 0.0f;
        rasterizer.depthClampEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 0; // 深度 pass 无颜色输出

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pInfo{};
        pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pInfo.stageCount = 2;
        pInfo.pStages = stages;
        pInfo.pVertexInputState = &vertexInput;
        pInfo.pInputAssemblyState = &inputAssembly;
        pInfo.pViewportState = &viewportState;
        pInfo.pRasterizationState = &rasterizer;
        pInfo.pMultisampleState = &multisampling;
        pInfo.pDepthStencilState = &depthStencil;
        pInfo.pColorBlendState = &colorBlending;
        pInfo.pDynamicState = &dyn;
        pInfo.layout = layout; // 复用主管线布局
        pInfo.renderPass = pointShadowRenderPass;
        pInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &pointShadowPipeline) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create point shadow pipeline" << std::endl;
            vkDestroyShaderModule(device, vertMod, nullptr);
            vkDestroyShaderModule(device, fragMod, nullptr);
            return false;
        }
        vkDestroyShaderModule(device, vertMod, nullptr);
        vkDestroyShaderModule(device, fragMod, nullptr);
    }

    // ---- 描述符：binding 4 = 点光源 cubemap ----
    VkDescriptorImageInfo imgDesc{};
    imgDesc.sampler = pointShadowSampler;
    imgDesc.imageView = pointShadowImageView;
    imgDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 4;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imgDesc;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // 阴影深度 pass 描述符集同样需要绑定点光源 cubemap
    VkWriteDescriptorSet writeShadow = write;
    writeShadow.dstSet = shadowDescriptorSet;
    vkUpdateDescriptorSets(device, 1, &writeShadow, 0, nullptr);

    pointShadowMapEnabled_ = true;
    std::cout << "[VulkanPipeline] Point shadow map (cubemap) enabled (" << size << "x" << size << ")" << std::endl;
    return true;
}

void VulkanPipeline::UpdateShadowLight(int idx, const glm::mat4 &lightViewProj, bool enabled,
                                       int lightIndex, int lightType)
{
    if (!shadowUboMapped || idx < 0 || idx >= MAX_2D_SHADOW_MAPS)
        return;
    // 读取-修改-写回（保持其它灯数据）
    ShadowUBO ubo{};
    std::memcpy(&ubo, shadowUboMapped, sizeof(ubo));
    ubo.shadowLightCount = std::max(ubo.shadowLightCount, idx + 1);
    ubo.lights[idx].lightViewProj = lightViewProj;
    ubo.lights[idx].lightIndex = lightIndex;
    ubo.lights[idx].enabled = enabled ? 1 : 0;
    ubo.lights[idx].lightType = lightType;
    std::memcpy(shadowUboMapped, &ubo, sizeof(ubo));
}

void VulkanPipeline::UpdatePointShadow(const glm::vec3 &lightPos, float farPlane, bool enabled)
{
    if (!pointShadowUboMapped)
        return;
    PointShadowUBO ubo{};
    ubo.lightPos = glm::vec4(lightPos, 1.0f);
    ubo.farPlane = farPlane;
    ubo.enabled = enabled ? 1.0f : 0.0f;
    std::memcpy(pointShadowUboMapped, &ubo, sizeof(ubo));
}

void VulkanPipeline::UpdateSoftShadow(bool soft)
{
    if (!shadowUboMapped)
        return;
    ShadowUBO ubo{};
    std::memcpy(&ubo, shadowUboMapped, sizeof(ubo));
    ubo.softShadow = soft ? 1 : 0;
    std::memcpy(shadowUboMapped, &ubo, sizeof(ubo));
}

void VulkanPipeline::UpdateShadowDebugView(int mode)
{
    if (!shadowUboMapped)
        return;
    ShadowUBO ubo{};
    std::memcpy(&ubo, shadowUboMapped, sizeof(ubo));
    ubo.shadowDebugView = mode;
    std::memcpy(shadowUboMapped, &ubo, sizeof(ubo));
}

void VulkanPipeline::BeginShadowPass(VkCommandBuffer cmd, int idx)
{
    if (idx < 0 || idx >= (int)shadowFramebuffers.size())
        return;
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = shadowRenderPass;
    rpInfo.framebuffer = shadowFramebuffers[idx];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = shadowExtent;
    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, (float)shadowExtent.width, (float)shadowExtent.height, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, shadowExtent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanPipeline::BeginPointShadowPass(VkCommandBuffer cmd, int face)
{
    if (face < 0 || face >= 6)
        return;
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = pointShadowRenderPass;
    rpInfo.framebuffer = pointShadowFaceFramebuffers[face];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = pointShadowExtent;
    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, (float)pointShadowExtent.width, (float)pointShadowExtent.height, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, pointShadowExtent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanPipeline::EndShadowPass(VkCommandBuffer cmd)
{
    vkCmdEndRenderPass(cmd);
}

void VulkanPipeline::EndPointShadowPass(VkCommandBuffer cmd, int face)
{
    vkCmdEndRenderPass(cmd);
    if (face < 0 || face >= 6)
        return;

    // 保证该 face 深度写入对主 pass 的片元采样可见
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pointShadowImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = (uint32_t)face;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanPipeline::Bind(VkCommandBuffer cmd) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                            0, 1, &descriptorSet, 0, nullptr);
}

// ============================================================================
// 环境贴图（Environment Map / IBL）
// ============================================================================

namespace
{
// 创建 RGBA32F 图像（cubemap 或 2D），device-local，TRANSFER_DST + SAMPLED
bool CreateEnvImage(VkDevice device, VkPhysicalDevice physicalDevice,
                    bool cube, int size, int mips,
                    VkImage &outImage, VkDeviceMemory &outMem)
{
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    if (cube)
        info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    info.extent = {(uint32_t)size, (uint32_t)size, 1};
    info.mipLevels = (uint32_t)mips;
    info.arrayLayers = cube ? 6u : 1u;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &outImage) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, outImage, &mr);
    uint32_t mt = FindMemoryType(physicalDevice, mr.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == std::numeric_limits<uint32_t>::max())
    {
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS ||
        vkBindImageMemory(device, outImage, outMem, 0) != VK_SUCCESS)
    {
        if (outMem)
            vkFreeMemory(device, outMem, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        outMem = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// 创建 host-visible staging buffer（TRANSFER_SRC）
bool CreateStaging(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                   VkBuffer &buf, VkDeviceMemory &mem, void *&mapped)
{
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, nullptr, &buf) != VK_SUCCESS)
        return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, buf, &mr);
    uint32_t mt = FindMemoryType(physicalDevice, mr.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == std::numeric_limits<uint32_t>::max())
        return false;
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS ||
        vkBindBufferMemory(device, buf, mem, 0) != VK_SUCCESS)
        return false;
    vkMapMemory(device, mem, 0, size, 0, &mapped);
    return true;
}

// 提交一次性命令（用于环境贴图上传），同步等待完成
bool SubmitOneTime(VkDevice device, VkQueue queue, VkCommandPool pool,
                   const std::function<void(VkCommandBuffer)> &fn)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &ai, &cmd) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        return false;
    }
    fn(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        return false;
    }

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fi, nullptr, &fence) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        return false;
    }
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VkResult r = vkQueueSubmit(queue, 1, &si, fence);
    if (r == VK_SUCCESS)
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    return r == VK_SUCCESS;
}

// 把整个图像（含 mips/layers）转换到 SHADER_READ_ONLY
void TransitionToReadable(VkDevice device, VkQueue queue, VkCommandPool pool,
                          VkImage image, int mips, uint32_t layers)
{
    SubmitOneTime(device, queue, pool, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = (uint32_t)mips;
        b.subresourceRange.layerCount = layers;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    });
}

// 把 CPU cubemap 数据（data[mip] = 6 层连续 RGBA32F，mip0 分辨率 baseSize，逐层减半）
// 上传到 device-local 图像
bool UploadCubeMap(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue,
                   VkCommandPool pool, const std::vector<std::vector<float>> &data,
                   int baseSize, int mips, VkImage image)
{
    int dataMips = (int)data.size();
    if (dataMips <= 0)
        return false;
    std::vector<VkDeviceSize> mipOffset(dataMips);
    VkDeviceSize total = 0;
    for (int m = 0; m < dataMips; m++)
    {
        int s = baseSize >> m;
        mipOffset[m] = total;
        total += (VkDeviceSize)6 * s * s * 4 * sizeof(float);
    }
    if (total == 0)
        return false;

    VkBuffer stage = VK_NULL_HANDLE;
    VkDeviceMemory stageMem = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!CreateStaging(device, physicalDevice, total, stage, stageMem, mapped))
        return false;

    {
        char *dst = (char *)mapped;
        for (int m = 0; m < dataMips; m++)
        {
            int s = baseSize >> m;
            size_t expect = (size_t)6 * s * s * 4 * sizeof(float);
            size_t copy = std::min<size_t>(expect, data[m].size() * sizeof(float));
            std::memcpy(dst + mipOffset[m], data[m].data(), copy);
        }
    }

    bool ok = SubmitOneTime(device, queue, pool, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = image;
        toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toDst.subresourceRange.levelCount = (uint32_t)dataMips;
        toDst.subresourceRange.layerCount = 6;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toDst);

        for (int m = 0; m < dataMips; m++)
        {
            int s = baseSize >> m;
            for (int l = 0; l < 6; l++)
            {
                VkBufferImageCopy region{};
                region.bufferOffset = mipOffset[m] + (VkDeviceSize)l * s * s * 4 * sizeof(float);
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)m, (uint32_t)l, 1};
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {(uint32_t)s, (uint32_t)s, 1};
                vkCmdCopyBufferToImage(cmd, stage, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       1, &region);
            }
        }

        VkImageMemoryBarrier toRead{};
        toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = image;
        toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toRead.subresourceRange.levelCount = (uint32_t)dataMips;
        toRead.subresourceRange.layerCount = 6;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toRead);
    });

    vkUnmapMemory(device, stageMem);
    vkFreeMemory(device, stageMem, nullptr);
    vkDestroyBuffer(device, stage, nullptr);
    return ok;
}

// 把 CPU 2D RGBA32F 数据（size x size）上传到 device-local 图像
bool UploadImage2D(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue,
                   VkCommandPool pool, const std::vector<float> &data, int size,
                   VkImage image)
{
    VkDeviceSize total = (VkDeviceSize)size * size * 4 * sizeof(float);
    VkBuffer stage = VK_NULL_HANDLE;
    VkDeviceMemory stageMem = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!CreateStaging(device, physicalDevice, total, stage, stageMem, mapped))
        return false;
    size_t bytes = std::min<size_t>((size_t)total, data.size() * sizeof(float));
    std::memcpy(mapped, data.data(), bytes);

    bool ok = SubmitOneTime(device, queue, pool, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = image;
        toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toDst.subresourceRange.levelCount = 1;
        toDst.subresourceRange.layerCount = 1;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {(uint32_t)size, (uint32_t)size, 1};
        vkCmdCopyBufferToImage(cmd, stage, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);

        VkImageMemoryBarrier toRead{};
        toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = image;
        toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toRead.subresourceRange.levelCount = 1;
        toRead.subresourceRange.layerCount = 1;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toRead);
    });

    vkUnmapMemory(device, stageMem);
    vkFreeMemory(device, stageMem, nullptr);
    vkDestroyBuffer(device, stage, nullptr);
    return ok;
}
} // namespace

bool VulkanPipeline::CreateEnvironmentResources(VkQueue queue, VkCommandPool cmdPool,
                                                const std::string &shaderDir)
{
    if (!device)
        return false;

    // ---- 采样器 ----
    if (!envSampler)
    {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 1.0f;
        if (vkCreateSampler(device, &si, nullptr, &envSampler) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create env sampler" << std::endl;
            return false;
        }
    }
    if (!prefilteredSampler)
    {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;   // 预过滤 mip 链用 mipmapMode 线性插值
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 15.0f; // 实际 mip 数会钳制 LOD
        if (vkCreateSampler(device, &si, nullptr, &prefilteredSampler) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create prefiltered sampler" << std::endl;
            return false;
        }
    }
    if (!brdfSampler)
    {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 1.0f;
        if (vkCreateSampler(device, &si, nullptr, &brdfSampler) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create BRDF LUT sampler" << std::endl;
            return false;
        }
    }

    // ---- 占位资源（1x1），保证 binding 6-9 在真实数据上传前有效 ----
    if (!dummyCubeImage)
    {
        if (!CreateEnvImage(device, physicalDevice, true, 1, 1, dummyCubeImage, dummyCubeMemory))
        {
            std::cerr << "[VulkanPipeline] Failed to create dummy cube image" << std::endl;
            return false;
        }
        TransitionToReadable(device, queue, cmdPool, dummyCubeImage, 1, 6);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = dummyCubeImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 6;
        if (vkCreateImageView(device, &vi, nullptr, &dummyCubeView) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create dummy cube view" << std::endl;
            return false;
        }
    }
    if (!dummyLutImage)
    {
        if (!CreateEnvImage(device, physicalDevice, false, 1, 1, dummyLutImage, dummyLutMemory))
        {
            std::cerr << "[VulkanPipeline] Failed to create dummy LUT image" << std::endl;
            return false;
        }
        TransitionToReadable(device, queue, cmdPool, dummyLutImage, 1, 1);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = dummyLutImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vi, nullptr, &dummyLutView) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create dummy LUT view" << std::endl;
            return false;
        }
    }

    // ---- binding 6-9 写入占位描述符（主集 + 阴影集） ----
    {
        VkDescriptorImageInfo cubeInfos[3]{};
        for (int i = 0; i < 3; i++)
        {
            cubeInfos[i].sampler = envSampler;
            cubeInfos[i].imageView = dummyCubeView;
            cubeInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkDescriptorImageInfo lutInfo{};
        lutInfo.sampler = brdfSampler;
        lutInfo.imageView = dummyLutView;
        lutInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorSet sets[2] = {descriptorSet, shadowDescriptorSet};
        for (VkDescriptorSet set : sets)
        {
            VkWriteDescriptorSet writes[4]{};
            for (int i = 0; i < 3; i++)
            {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = 6 + i;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].descriptorCount = 1;
                writes[i].pImageInfo = &cubeInfos[i];
            }
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = set;
            writes[3].dstBinding = 9;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[3].descriptorCount = 1;
            writes[3].pImageInfo = &lutInfo;
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
        }
    }

    // ---- 天空盒管线（全屏三角形，无顶点输入；复用主管线布局/描述集） ----
    if (!skyboxPipeline)
    {
        std::vector<char> vertCode = ReadFile(shaderDir + "/skybox.vert.spv");
        std::vector<char> fragCode = ReadFile(shaderDir + "/skybox.frag.spv");
        if (vertCode.empty() || fragCode.empty())
        {
            std::cerr << "[VulkanPipeline] Skybox shaders missing: " << shaderDir
                      << " - skybox disabled (object IBL still works)" << std::endl;
            return true;
        }
        VkShaderModule vertMod = CreateShaderModule(device, vertCode);
        VkShaderModule fragMod = CreateShaderModule(device, fragCode);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        // 无顶点输入：全屏三角形用 gl_VertexIndex

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vpState{};
        vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpState.viewportCount = 1;
        vpState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rz{};
        rz.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rz.polygonMode = VK_POLYGON_MODE_FILL;
        rz.lineWidth = 1.0f;
        rz.cullMode = VK_CULL_MODE_NONE;
        rz.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rz.depthClampEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;   // LESS_OR_EQUAL：全屏三角形深度=1.0，通过
        ds.depthWriteEnable = VK_FALSE; // 不写深度，场景绘制在其上方
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        ba.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable = VK_FALSE;
        cb.attachmentCount = 1;
        cb.pAttachments = &ba;

        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsc{};
        dsc.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsc.dynamicStateCount = 2;
        dsc.pDynamicStates = dyn;

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vertexInput;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vpState;
        pi.pRasterizationState = &rz;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &ds;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &dsc;
        pi.layout = layout;
        pi.renderPass = renderPass_;
        pi.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &skyboxPipeline) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create skybox pipeline" << std::endl;
            skyboxPipeline = VK_NULL_HANDLE;
        }
        vkDestroyShaderModule(device, vertMod, nullptr);
        vkDestroyShaderModule(device, fragMod, nullptr);
    }

    return true;
}

bool VulkanPipeline::UploadEnvironmentMap(VkQueue queue, VkCommandPool cmdPool,
                                          const EnvironmentMap &env)
{
    if (!device || !env.IsValid())
        return false;

    int envSize = env.EnvCubeSize();
    int irrSize = env.IrradianceSize();
    int pfBase = env.PrefilteredBaseSize();
    int pfMips = env.PrefilteredMips();
    int lutSize = env.BRDFLutSize();

    if (!CreateEnvImage(device, physicalDevice, true, envSize, 1, envImage, envImageMemory) ||
        !CreateEnvImage(device, physicalDevice, true, irrSize, 1, irrImage, irrImageMemory) ||
        !CreateEnvImage(device, physicalDevice, true, pfBase, pfMips, prefilteredImage, prefilteredImageMemory) ||
        !CreateEnvImage(device, physicalDevice, false, lutSize, 1, brdfImage, brdfImageMemory))
    {
        std::cerr << "[VulkanPipeline] Failed to create environment images" << std::endl;
        return false;
    }
    envCubeSize_ = envSize;
    prefilteredMips_ = pfMips;

    // ---- 视图 ----
    auto createCubeView = [&](VkImage img, int mips, VkImageView &view) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = (uint32_t)mips;
        vi.subresourceRange.layerCount = 6;
        return vkCreateImageView(device, &vi, nullptr, &view) == VK_SUCCESS;
    };
    if (!createCubeView(envImage, 1, envImageView) ||
        !createCubeView(irrImage, 1, irrImageView) ||
        !createCubeView(prefilteredImage, pfMips, prefilteredImageView))
    {
        std::cerr << "[VulkanPipeline] Failed to create environment image views" << std::endl;
        return false;
    }
    {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = brdfImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vi, nullptr, &brdfImageView) != VK_SUCCESS)
        {
            std::cerr << "[VulkanPipeline] Failed to create BRDF LUT view" << std::endl;
            return false;
        }
    }

    // ---- 上传 ----
    if (!UploadCubeMap(device, physicalDevice, queue, cmdPool,
                       {env.EnvCube()}, envSize, 1, envImage) ||
        !UploadCubeMap(device, physicalDevice, queue, cmdPool,
                       {env.Irradiance()}, irrSize, 1, irrImage) ||
        !UploadCubeMap(device, physicalDevice, queue, cmdPool,
                       env.Prefiltered(), pfBase, pfMips, prefilteredImage) ||
        !UploadImage2D(device, physicalDevice, queue, cmdPool,
                       env.BRDFLUT(), lutSize, brdfImage))
    {
        std::cerr << "[VulkanPipeline] Failed to upload environment data" << std::endl;
        return false;
    }

    // ---- 更新描述符（主集 + 阴影集） ----
    {
        VkDescriptorImageInfo envInfo{envSampler, envImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo irrInfo{envSampler, irrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo pfInfo{prefilteredSampler, prefilteredImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo brdfInfo{brdfSampler, brdfImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo *infos[4] = {&envInfo, &irrInfo, &pfInfo, &brdfInfo};

        VkDescriptorSet sets[2] = {descriptorSet, shadowDescriptorSet};
        for (VkDescriptorSet set : sets)
        {
            VkWriteDescriptorSet writes[4]{};
            for (int i = 0; i < 4; i++)
            {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = 6 + i;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].descriptorCount = 1;
                writes[i].pImageInfo = infos[i];
            }
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
        }
    }

    environmentReady_ = true;
    std::cout << "[VulkanPipeline] Environment map uploaded: cube " << envSize
              << ", irradiance " << irrSize << ", prefiltered " << pfBase
              << " mips=" << pfMips << ", BRDF LUT " << lutSize << std::endl;
    return true;
}

void VulkanPipeline::UpdateEnvUBO(const EnvUBO &env)
{
    if (!envUboMapped)
        return;
    std::memcpy(envUboMapped, &env, sizeof(env));
}

void VulkanPipeline::RecordSkybox(VkCommandBuffer cmd, const glm::mat4 &invViewProj,
                                  float yaw, float exposure)
{
    if (!environmentReady_ || !skyboxPipeline)
        return;

    struct SkyboxPC
    {
        glm::mat4 invViewProj;
        glm::vec4 yaw; // x=方位角(弧度), y=曝光
    };
    SkyboxPC pc{};
    pc.invViewProj = invViewProj;
    pc.yaw = glm::vec4(yaw, exposure, 0.0f, 0.0f);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                            0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace aster

