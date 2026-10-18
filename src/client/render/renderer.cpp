#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "renderer.h"

#include "textures.h"

#include "gk/core/log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "mesh_frag.h"
#include "mesh_vert.h"
#include "ui_frag.h"
#include "ui_vert.h"

#include <stb_easy_font.h>

#include <algorithm>
#include <cstring>

namespace gk::render {
namespace {

#define GK_VK_CHECK(expr)                                                                \
    do {                                                                                 \
        VkResult r_ = (expr);                                                            \
        if (r_ != VK_SUCCESS) {                                                          \
            gk::log_error("Vulkan call failed ({}): {}", static_cast<int>(r_), #expr);   \
            return false;                                                                \
        }                                                                                \
    } while (0)

void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                   VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

VkShaderModule make_shader(VkDevice device, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

} // namespace

bool Renderer::init(SDL_Window* window, const RendererOptions& options) {
    window_ = window;
    options_ = options;

    // Use the Vulkan library SDL loaded for the window (on macOS that can be
    // the MoltenVK inside the app bundle), else look for the system loader.
    const auto sdl_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (sdl_gipa) volkInitializeCustom(sdl_gipa);
    else if (volkInitialize() != VK_SUCCESS) {
        log_error("No Vulkan loader found. Install your GPU driver (Windows/Linux) or the Vulkan SDK / MoltenVK (macOS).");
        return false;
    }

    Uint32 ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!sdl_exts) {
        log_error("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return false;
    }

    vkb::InstanceBuilder ib(vkGetInstanceProcAddr);
    ib.set_app_name("Gobby Kart").set_engine_name("gobbykart").require_api_version(1, 3, 0);
    const auto sys = vkb::SystemInfo::get_system_info(vkGetInstanceProcAddr);
    for (Uint32 i = 0; i < ext_count; ++i) {
        // SDL on macOS always asks for portability enumeration, which only the
        // Vulkan loader has; MoltenVK loaded directly (as in the app bundle)
        // does not. vk-bootstrap turns it on, with its flag, when it exists.
        if (std::strcmp(sdl_exts[i], VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) continue;
        if (sys && !sys->is_extension_available(sdl_exts[i])) {
            log_error("The Vulkan library has no {} (needed for the window)", sdl_exts[i]);
            return false;
        }
        ib.enable_extension(sdl_exts[i]);
    }
    if (options_.validation) ib.request_validation_layers(true).use_default_debug_messenger();
    auto inst = ib.build();
    if (!inst) {
        log_error("Vulkan instance creation failed: {}", inst.error().message());
        return false;
    }
    vkb_instance_ = inst.value();
    volkLoadInstance(vkb_instance_.instance);

    if (!SDL_Vulkan_CreateSurface(window_, vkb_instance_.instance, nullptr, &surface_)) {
        log_error("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    vkb::PhysicalDeviceSelector sel(vkb_instance_);
    auto phys = sel.set_minimum_version(1, 3)
                    .set_required_features_13(f13)
                    .set_surface(surface_)
                    .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                    .allow_any_gpu_device_type(true)
                    .select();
    if (!phys) {
        log_error("No Vulkan 1.3 GPU with dynamic rendering found: {}", phys.error().message());
        return false;
    }
    vkb_physical_ = phys.value();
    VkPhysicalDeviceFeatures f10{};
    f10.samplerAnisotropy = VK_TRUE;
    anisotropy_ = vkb_physical_.enable_features_if_present(f10);
    device_name_ = vkb_physical_.name;

    auto dev = vkb::DeviceBuilder(vkb_physical_).build();
    if (!dev) {
        log_error("Vulkan device creation failed: {}", dev.error().message());
        return false;
    }
    vkb_device_ = dev.value();
    device_ = vkb_device_.device;
    volkLoadDevice(device_);
    queue_ = vkb_device_.get_queue(vkb::QueueType::graphics).value();
    queue_family_ = vkb_device_.get_queue_index(vkb::QueueType::graphics).value();
    log_info("GPU: {}", device_name_);

    VmaVulkanFunctions funcs{};
    funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ai{};
    ai.vulkanApiVersion = VK_API_VERSION_1_3;
    ai.instance = vkb_instance_.instance;
    ai.physicalDevice = vkb_physical_.physical_device;
    ai.device = device_;
    ai.pVulkanFunctions = &funcs;
    GK_VK_CHECK(vmaCreateAllocator(&ai, &allocator_));

    if (!create_textures()) return false;

    // Descriptor layout: a uniform buffer with per-frame camera and lighting,
    // and the texture array.
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl.bindingCount = 2;
    dsl.pBindings = bindings;
    GK_VK_CHECK(vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &set_layout_));

    const VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
                                                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kFramesInFlight;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = pool_sizes;
    GK_VK_CHECK(vkCreateDescriptorPool(device_, &dp, nullptr, &descriptor_pool_));

    for (auto& f : frames_) {
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        cp.queueFamilyIndex = queue_family_;
        GK_VK_CHECK(vkCreateCommandPool(device_, &cp, nullptr, &f.pool));
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = f.pool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        GK_VK_CHECK(vkAllocateCommandBuffers(device_, &ca, &f.cmd));
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        GK_VK_CHECK(vkCreateFence(device_, &fc, nullptr, &f.fence));
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        GK_VK_CHECK(vkCreateSemaphore(device_, &sc, nullptr, &f.acquired));

        f.uniforms = create_buffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        if (!f.uniforms.buffer) return false;
        f.ui = create_buffer(sizeof(UiVertex) * kMaxUiVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        if (!f.ui.buffer) return false;
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool = descriptor_pool_;
        da.descriptorSetCount = 1;
        da.pSetLayouts = &set_layout_;
        GK_VK_CHECK(vkAllocateDescriptorSets(device_, &da, &f.set));
        VkDescriptorBufferInfo bi{f.uniforms.buffer, 0, sizeof(FrameUniforms)};
        VkDescriptorImageInfo ii{sampler_, textures_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        w[0].dstSet = f.set;
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &bi;
        w[1].dstSet = f.set;
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = &ii;
        vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
    }

    if (!create_pipeline()) return false;
    if (!create_ui_pipeline()) return false;
    if (!create_swapchain()) return false;
    if (!create_offscreen()) return false;
    return true;
}

void Renderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    for (auto& f : frames_) {
        finish_readback(f);
        destroy_buffer(f.readback);
        destroy_buffer(f.uniforms);
        destroy_buffer(f.ui);
        vkDestroySemaphore(device_, f.acquired, nullptr);
        vkDestroyFence(device_, f.fence, nullptr);
        vkDestroyCommandPool(device_, f.pool, nullptr);
    }
    for (auto& m : meshes_) {
        destroy_buffer(m.vertices);
        destroy_buffer(m.indices);
    }
    meshes_.clear();
    destroy_offscreen();
    destroy_swapchain();
    vkDestroyPipeline(device_, mesh_pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    vkDestroyPipeline(device_, ui_pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, ui_layout_, nullptr);
    destroy_image(textures_);
    vkDestroySampler(device_, sampler_, nullptr);
    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    vmaDestroyAllocator(allocator_);
    vkb::destroy_device(vkb_device_);
    vkb::destroy_surface(vkb_instance_, surface_);
    vkb::destroy_instance(vkb_instance_);
    device_ = VK_NULL_HANDLE;
}

bool Renderer::create_swapchain() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0) return false;

    vkb::SwapchainBuilder sb(vkb_device_);
    auto sc = sb.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                  .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                  .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                  .set_desired_extent(static_cast<uint32_t>(w), static_cast<uint32_t>(h))
                  .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                  .set_old_swapchain(vkb_swapchain_)
                  .build();
    if (!sc) {
        log_error("Swapchain creation failed: {}", sc.error().message());
        return false;
    }
    destroy_swapchain();
    vkb_swapchain_ = sc.value();
    swap_images_ = vkb_swapchain_.get_images().value();
    render_done_.resize(swap_images_.size());
    for (auto& s : render_done_) {
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device_, &sci, nullptr, &s) != VK_SUCCESS) return false;
    }
    swapchain_dirty_ = false;
    return true;
}

void Renderer::destroy_swapchain() {
    for (auto s : render_done_) vkDestroySemaphore(device_, s, nullptr);
    render_done_.clear();
    swap_images_.clear();
    if (vkb_swapchain_.swapchain) vkb::destroy_swapchain(vkb_swapchain_);
    vkb_swapchain_ = {};
}

bool Renderer::create_offscreen() {
    const VkExtent2D win = vkb_swapchain_.extent;
    uint32_t scale = 1;
    if (!options_.full_resolution && options_.target_height > 0)
        scale = std::max<uint32_t>(1, win.height / static_cast<uint32_t>(options_.target_height));
    pixel_scale_ = scale;
    offscreen_extent_ = {std::max<uint32_t>(1, win.width / scale), std::max<uint32_t>(1, win.height / scale)};
    offscreen_color_ = create_image(offscreen_extent_, color_format_,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
    offscreen_depth_ = create_image(offscreen_extent_, depth_format_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                    VK_IMAGE_ASPECT_DEPTH_BIT);
    return offscreen_color_.image && offscreen_depth_.image;
}

void Renderer::destroy_offscreen() {
    destroy_image(offscreen_color_);
    destroy_image(offscreen_depth_);
}

void Renderer::set_full_resolution(bool on) {
    if (options_.full_resolution == on) return;
    options_.full_resolution = on;
    swapchain_dirty_ = true;
}

bool Renderer::create_pipeline() {
    VkShaderModule vs = make_shader(device_, k_mesh_vert, sizeof(k_mesh_vert));
    VkShaderModule fs = make_shader(device_, k_mesh_frag, sizeof(k_mesh_frag));
    if (!vs || !fs) return false;

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4) + sizeof(vec4)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &set_layout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    GK_VK_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &pipeline_layout_));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[5] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, layer)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format_;
    rendering.depthAttachmentFormat = depth_format_;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &rendering;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dy;
    gp.layout = pipeline_layout_;
    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &mesh_pipeline_);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    GK_VK_CHECK(r);
    return true;
}

bool Renderer::create_ui_pipeline() {
    VkShaderModule vs = make_shader(device_, k_ui_vert, sizeof(k_ui_vert));
    VkShaderModule fs = make_shader(device_, k_ui_frag, sizeof(k_ui_frag));
    if (!vs || !fs) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vec2)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    GK_VK_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &ui_layout_));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(UiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, pos)},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UiVertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format_;
    rendering.depthAttachmentFormat = depth_format_;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &rendering;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dy;
    gp.layout = ui_layout_;
    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &ui_pipeline_);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    GK_VK_CHECK(r);
    return true;
}

void Renderer::ui_rect(float x, float y, float w, float h, uint32_t color) {
    if (!frame_active_) return;
    const UiVertex a{{x, y}, color}, b{{x + w, y}, color}, c{{x + w, y + h}, color}, d{{x, y + h}, color};
    ui_vertices_.insert(ui_vertices_.end(), {a, b, c, a, c, d});
}

void Renderer::ui_quad(vec2 a, vec2 b, vec2 c, vec2 d, uint32_t color) {
    if (!frame_active_) return;
    const UiVertex va{a, color}, vb{b, color}, vc{c, color}, vd{d, color};
    ui_vertices_.insert(ui_vertices_.end(), {va, vb, vc, va, vc, vd});
}

void Renderer::ui_text(float x, float y, const char* text, uint32_t color, float scale, bool shadow) {
    if (!frame_active_ || !text) return;
    const float off = std::max(1.0f, std::floor(scale * 0.3f));
    if (shadow) ui_text(x + off, y + off, text, rgba(0, 0, 0, uint8_t((color >> 24) * 3 / 4)), scale, false);
    struct EasyVertex {
        float x, y, z;
        unsigned char c[4];
    };
    static EasyVertex buffer[4 * 1000];
    unsigned char col[4] = {0, 0, 0, 0};
    const int quads = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(text), col, buffer, sizeof(buffer));
    for (int q = 0; q < quads; ++q) {
        const EasyVertex* v = &buffer[q * 4];
        UiVertex out[4];
        for (int i = 0; i < 4; ++i) out[i] = {{x + v[i].x * scale, y + v[i].y * scale}, color};
        ui_vertices_.insert(ui_vertices_.end(), {out[0], out[1], out[2], out[0], out[2], out[3]});
    }
}

float Renderer::ui_text_width(const char* text, float scale) {
    return float(stb_easy_font_width(const_cast<char*>(text))) * scale;
}

Renderer::Buffer Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible) {
    Buffer b;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    VmaAllocationCreateInfo ac{};
    ac.usage = VMA_MEMORY_USAGE_AUTO;
    if (host_visible)
        ac.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(allocator_, &bi, &ac, &b.buffer, &b.alloc, &info) != VK_SUCCESS) {
        log_error("Buffer allocation of {} bytes failed", size);
        return {};
    }
    b.mapped = info.pMappedData;
    return b;
}

void Renderer::destroy_buffer(Buffer& b) {
    if (b.buffer) vmaDestroyBuffer(allocator_, b.buffer, b.alloc);
    b = {};
}

Renderer::Image Renderer::create_image(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                                       VkImageAspectFlags aspect) {
    Image img;
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = format;
    ic.extent = {extent.width, extent.height, 1};
    ic.mipLevels = 1;
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = usage;
    VmaAllocationCreateInfo ac{};
    ac.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator_, &ic, &ac, &img.image, &img.alloc, nullptr) != VK_SUCCESS) {
        log_error("Image allocation failed");
        return {};
    }
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = img.image;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = format;
    vc.subresourceRange = {aspect, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vc, nullptr, &img.view) != VK_SUCCESS) {
        destroy_image(img);
        return {};
    }
    return img;
}

bool Renderer::create_textures() {
    const std::vector<uint8_t> pixels = make_texture_layers();
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = VK_FORMAT_R8G8B8A8_UNORM;
    ic.extent = {uint32_t(kTexSize), uint32_t(kTexSize), 1};
    ic.mipLevels = kTexMips;
    ic.arrayLayers = TexCount;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ac{};
    ac.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator_, &ic, &ac, &textures_.image, &textures_.alloc, nullptr) != VK_SUCCESS) {
        log_error("Texture allocation failed");
        return false;
    }
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = textures_.image;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vc.format = ic.format;
    vc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, uint32_t(kTexMips), 0, uint32_t(TexCount)};
    GK_VK_CHECK(vkCreateImageView(device_, &vc, nullptr, &textures_.view));

    // Trilinear, anisotropic where the GPU allows it.
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc.magFilter = VK_FILTER_LINEAR;
    sc.minFilter = VK_FILTER_LINEAR;
    sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sc.anisotropyEnable = anisotropy_ ? VK_TRUE : VK_FALSE;
    sc.maxAnisotropy = anisotropy_ ? 8.0f : 1.0f;
    sc.maxLod = float(kTexMips);
    GK_VK_CHECK(vkCreateSampler(device_, &sc, nullptr, &sampler_));

    // Upload through a staging buffer, once.
    Buffer staging = create_buffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    if (!staging.mapped) return false;
    std::memcpy(staging.mapped, pixels.data(), pixels.size());
    vmaFlushAllocation(allocator_, staging.alloc, 0, VK_WHOLE_SIZE);
    std::vector<VkBufferImageCopy> copies;
    VkDeviceSize offset = 0;
    for (uint32_t layer = 0; layer < TexCount; ++layer)
        for (uint32_t mip = 0; mip < uint32_t(kTexMips); ++mip) {
            const uint32_t size = uint32_t(kTexSize) >> mip;
            VkBufferImageCopy c{};
            c.bufferOffset = offset;
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
            c.imageExtent = {size, size, 1};
            copies.push_back(c);
            offset += VkDeviceSize(size) * size * 4;
        }

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cp.queueFamilyIndex = queue_family_;
    GK_VK_CHECK(vkCreateCommandPool(device_, &cp, nullptr, &pool));
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    GK_VK_CHECK(vkAllocateCommandBuffers(device_, &ca, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = textures_.image;
    b.subresourceRange = vc.subresourceRange;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &b);
    vkCmdCopyBufferToImage(cmd, staging.buffer, textures_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           uint32_t(copies.size()), copies.data());
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    GK_VK_CHECK(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
    vkQueueWaitIdle(queue_);
    vkDestroyCommandPool(device_, pool, nullptr);
    destroy_buffer(staging);
    return true;
}

void Renderer::destroy_image(Image& img) {
    if (img.view) vkDestroyImageView(device_, img.view, nullptr);
    if (img.image) vmaDestroyImage(allocator_, img.image, img.alloc);
    img = {};
}

MeshId Renderer::upload_mesh(const MeshData& mesh) {
    // Low-poly meshes are small, so host-visible memory is fine; a staging
    // path can come later if a track ever needs it.
    GpuMesh g;
    const VkDeviceSize vbytes = mesh.vertices.size() * sizeof(Vertex);
    const VkDeviceSize ibytes = mesh.indices.size() * sizeof(uint32_t);
    g.vertices = create_buffer(std::max<VkDeviceSize>(vbytes, 4), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    g.indices = create_buffer(std::max<VkDeviceSize>(ibytes, 4), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true);
    if (g.vertices.mapped && vbytes) std::memcpy(g.vertices.mapped, mesh.vertices.data(), vbytes);
    if (g.indices.mapped && ibytes) std::memcpy(g.indices.mapped, mesh.indices.data(), ibytes);
    vmaFlushAllocation(allocator_, g.vertices.alloc, 0, VK_WHOLE_SIZE);
    vmaFlushAllocation(allocator_, g.indices.alloc, 0, VK_WHOLE_SIZE);
    g.index_count = static_cast<uint32_t>(mesh.indices.size());
    meshes_.push_back(g);
    return static_cast<MeshId>(meshes_.size() - 1);
}

void Renderer::finish_readback(Frame& f) {
    if (f.readback_path.empty()) return;
    vmaInvalidateAllocation(allocator_, f.readback.alloc, 0, VK_WHOLE_SIZE);
    const int w = static_cast<int>(f.readback_extent.width), h = static_cast<int>(f.readback_extent.height);
    if (stbi_write_png(f.readback_path.c_str(), w, h, 4, f.readback.mapped, w * 4))
        log_info("Screenshot saved to {} ({}x{})", f.readback_path, w, h);
    else
        log_error("Could not write screenshot {}", f.readback_path);
    f.readback_path.clear();
}

bool Renderer::begin_frame(const Camera& camera, const SceneLighting& lighting) {
    Frame& f = frames_[frame_index_];
    vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX);
    finish_readback(f);

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0) return false; // minimised
    if (swapchain_dirty_ || static_cast<uint32_t>(w) != vkb_swapchain_.extent.width ||
        static_cast<uint32_t>(h) != vkb_swapchain_.extent.height) {
        vkDeviceWaitIdle(device_);
        destroy_offscreen();
        if (!create_swapchain() || !create_offscreen()) return false;
    }

    VkResult r = vkAcquireNextImageKHR(device_, vkb_swapchain_.swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE,
                                       &image_index_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty_ = true;
        return false;
    }
    if (r == VK_SUBOPTIMAL_KHR) swapchain_dirty_ = true;
    else if (r != VK_SUCCESS) {
        log_error("vkAcquireNextImageKHR failed ({})", static_cast<int>(r));
        return false;
    }

    vkResetFences(device_, 1, &f.fence);
    vkResetCommandPool(device_, f.pool, 0);

    const float aspect = float(offscreen_extent_.width) / float(offscreen_extent_.height);
    mat4 proj = glm::perspective(glm::radians(camera.fov_y_deg), aspect, camera.near_z, camera.far_z);
    proj[1][1] *= -1.0f; // Vulkan clip space has +Y down
    const mat4 view = glm::lookAt(camera.position, camera.target, kUp);
    FrameUniforms u{};
    u.view_proj = proj * view;
    u.camera_pos = vec4(camera.position, 1.0f);
    u.light_dir = vec4(glm::normalize(lighting.light_dir), lighting.ambient);
    u.fog_color = vec4(lighting.sky, lighting.fog_density);
    u.sky_color = vec4(lighting.sky, 1.0f);
    std::memcpy(f.uniforms.mapped, &u, sizeof(u));
    vmaFlushAllocation(allocator_, f.uniforms.alloc, 0, VK_WHOLE_SIZE);
    clear_color_ = lighting.sky;

    draws_.clear();
    ui_vertices_.clear();
    frame_active_ = true;
    return true;
}

void Renderer::draw(MeshId mesh, const mat4& model, vec4 tint) {
    if (frame_active_ && mesh < meshes_.size()) draws_.push_back({mesh, model, tint});
}

void Renderer::end_frame() {
    if (!frame_active_) return;
    frame_active_ = false;
    Frame& f = frames_[frame_index_];
    VkCommandBuffer cmd = f.cmd;
    VkImage swap = swap_images_[image_index_];

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // The previous frame's blit may still be reading the offscreen image; the
    // barrier's source scope covers that.
    image_barrier(cmd, offscreen_color_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, offscreen_depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = offscreen_color_.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{clear_color_.r, clear_color_.g, clear_color_.b, 1.0f}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = offscreen_depth_.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, offscreen_extent_};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &ri);

    VkViewport viewport{0, 0, float(offscreen_extent_.width), float(offscreen_extent_.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, offscreen_extent_};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &f.set, 0, nullptr);
    struct {
        mat4 model;
        vec4 tint;
    } push;
    for (const auto& d : draws_) {
        const GpuMesh& m = meshes_[d.mesh];
        if (!m.index_count) continue;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(cmd, m.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        push.model = d.model;
        push.tint = d.tint;
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, m.index_count, 1, 0, 0, 0);
    }
    if (!ui_vertices_.empty()) {
        const size_t count = std::min<size_t>(ui_vertices_.size(), kMaxUiVertices);
        std::memcpy(f.ui.mapped, ui_vertices_.data(), count * sizeof(UiVertex));
        vmaFlushAllocation(allocator_, f.ui.alloc, 0, VK_WHOLE_SIZE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
        const VkExtent2D ui = ui_extent();
        const vec2 inv(2.0f / float(ui.width), 2.0f / float(ui.height));
        vkCmdPushConstants(cmd, ui_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(inv), &inv);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &f.ui.buffer, &offset);
        vkCmdDraw(cmd, uint32_t(count), 1, 0, 0);
    }
    vkCmdEndRendering(cmd);

    // Upscale the low-res image to the swapchain with nearest filtering.
    image_barrier(cmd, offscreen_color_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT);
    image_barrier(cmd, swap, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, swap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    image_barrier(cmd, swap, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    const VkExtent2D win = vkb_swapchain_.extent;
    const int32_t dw = int32_t(offscreen_extent_.width * pixel_scale_), dh = int32_t(offscreen_extent_.height * pixel_scale_);
    const int32_t ox = (int32_t(win.width) - dw) / 2, oy = (int32_t(win.height) - dh) / 2;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int32_t(offscreen_extent_.width), int32_t(offscreen_extent_.height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {ox, oy, 0};
    blit.dstOffsets[1] = {ox + dw, oy + dh, 1};
    vkCmdBlitImage(cmd, offscreen_color_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    if (!screenshot_path_.empty()) {
        const VkDeviceSize bytes = VkDeviceSize(offscreen_extent_.width) * offscreen_extent_.height * 4;
        if (f.readback.buffer && (f.readback_extent.width != offscreen_extent_.width ||
                                  f.readback_extent.height != offscreen_extent_.height))
            destroy_buffer(f.readback);
        if (!f.readback.buffer) f.readback = create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        if (f.readback.buffer) {
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {offscreen_extent_.width, offscreen_extent_.height, 1};
            vkCmdCopyImageToBuffer(cmd, offscreen_color_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.readback.buffer,
                                   1, &copy);
            f.readback_extent = offscreen_extent_;
            f.readback_path = std::move(screenshot_path_);
        }
        screenshot_path_.clear();
    }

    image_barrier(cmd, swap, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = render_done_[image_index_];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = 1;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = &signal;
    vkQueueSubmit2(queue_, 1, &si, f.fence);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_done_[image_index_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &vkb_swapchain_.swapchain;
    pi.pImageIndices = &image_index_;
    const VkResult pr = vkQueuePresentKHR(queue_, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) swapchain_dirty_ = true;

    frame_index_ = (frame_index_ + 1) % kFramesInFlight;
}

} // namespace gk::render
