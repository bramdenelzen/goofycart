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
#include "shadow_vert.h"
#include "ui_frag.h"
#include "ui_vert.h"

#include <stb_easy_font.h>

#include "font.h"

#include <algorithm>
#include <cstdlib>
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
    const VkDescriptorSetLayoutBinding bindings[3] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl.bindingCount = 3;
    dsl.pBindings = bindings;
    GK_VK_CHECK(vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &set_layout_));

    const VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
                                                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * kFramesInFlight}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kFramesInFlight;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = pool_sizes;
    GK_VK_CHECK(vkCreateDescriptorPool(device_, &dp, nullptr, &descriptor_pool_));

    // Set 1: one 2D texture per loaded model part.
    const VkDescriptorSetLayoutBinding tex_binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo tdsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    tdsl.bindingCount = 1;
    tdsl.pBindings = &tex_binding;
    GK_VK_CHECK(vkCreateDescriptorSetLayout(device_, &tdsl, nullptr, &texture_set_layout_));
    const VkDescriptorPoolSize tex_pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
    VkDescriptorPoolCreateInfo tdp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    tdp.maxSets = kMaxTextures;
    tdp.poolSizeCount = 1;
    tdp.pPoolSizes = &tex_pool_size;
    GK_VK_CHECK(vkCreateDescriptorPool(device_, &tdp, nullptr, &texture_pool_));
    const uint8_t white[4] = {255, 255, 255, 255};
    if (upload_texture(1, 1, white) != 0) return false;

    {   // Depth-compare sampler for the shadow map; outside it counts as lit.
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = VK_FILTER_LINEAR;
        sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sc.compareEnable = VK_TRUE;
        sc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        GK_VK_CHECK(vkCreateSampler(device_, &sc, nullptr, &shadow_sampler_));
    }
    for (auto& f : frames_) {
        f.shadow = create_image({kShadowSize, kShadowSize}, depth_format_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT);
        if (!f.shadow.image) return false;
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
        f.particles = create_buffer(sizeof(Vertex) * 6 * kMaxParticles, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        if (!f.particles.buffer) return false;
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool = descriptor_pool_;
        da.descriptorSetCount = 1;
        da.pSetLayouts = &set_layout_;
        GK_VK_CHECK(vkAllocateDescriptorSets(device_, &da, &f.set));
        VkDescriptorBufferInfo bi{f.uniforms.buffer, 0, sizeof(FrameUniforms)};
        VkDescriptorImageInfo ii{sampler_, textures_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo si{shadow_sampler_, f.shadow.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        w[2].dstSet = f.set;
        w[2].dstBinding = 2;
        w[2].descriptorCount = 1;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2].pImageInfo = &si;
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
        vkUpdateDescriptorSets(device_, 3, w, 0, nullptr);
    }

    if (!create_pipeline()) return false;
    if (!create_shadow_pipeline()) return false;
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
        destroy_buffer(f.particles);
        vkDestroySemaphore(device_, f.acquired, nullptr);
        vkDestroyFence(device_, f.fence, nullptr);
        vkDestroyCommandPool(device_, f.pool, nullptr);
    }
    for (auto& m : meshes_) {
        destroy_buffer(m.vertices);
        destroy_buffer(m.indices);
    }
    meshes_.clear();
    for (auto& t : textures2d_) destroy_image(t.image);
    textures2d_.clear();
    destroy_offscreen();
    destroy_swapchain();
    vkDestroyPipeline(device_, mesh_pipeline_, nullptr);
    vkDestroyPipeline(device_, mesh_pipeline_two_sided_, nullptr);
    vkDestroyPipeline(device_, mesh_pipeline_blend_, nullptr);
    vkDestroyPipeline(device_, mesh_pipeline_add_, nullptr);
    vkDestroyPipeline(device_, shadow_pipeline_, nullptr);
    vkDestroySampler(device_, shadow_sampler_, nullptr);
    for (auto& f : frames_) destroy_image(f.shadow);
    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    vkDestroyPipeline(device_, ui_pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, ui_layout_, nullptr);
    destroy_image(textures_);
    vkDestroySampler(device_, sampler_, nullptr);
    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    vkDestroyDescriptorPool(device_, texture_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, texture_set_layout_, nullptr);
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
    composite_extent_ = win;
    composite_ = create_image(composite_extent_, color_format_,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
    return offscreen_color_.image && offscreen_depth_.image && composite_.image;
}

void Renderer::destroy_offscreen() {
    destroy_image(offscreen_color_);
    destroy_image(offscreen_depth_);
    destroy_image(composite_);
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

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mat4) + 2 * sizeof(vec4)};
    const VkDescriptorSetLayout layouts[2] = {set_layout_, texture_set_layout_};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 2;
    pl.pSetLayouts = layouts;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    const VkResult r2 = r == VK_SUCCESS ? vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &mesh_pipeline_two_sided_) : r;
    // See-through variant: alpha blending, depth tested but not written.
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    ds.depthWriteEnable = VK_FALSE;
    const VkResult r3 = r2 == VK_SUCCESS ? vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &mesh_pipeline_blend_) : r2;
    // Additive: light piles up (sparks, flames), depth tested, not written.
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    const VkResult r4 = r3 == VK_SUCCESS ? vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &mesh_pipeline_add_) : r3;
    GK_VK_CHECK(r4);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    GK_VK_CHECK(r);
    GK_VK_CHECK(r2);
    GK_VK_CHECK(r3);
    return true;
}

bool Renderer::create_shadow_pipeline() {
    VkShaderModule vs = make_shader(device_, k_shadow_vert, sizeof(k_shadow_vert));
    if (!vs) return false;
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // leaves, awnings and flags are single sheets
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.5f;
    rs.depthBiasSlopeFactor = 2.0f;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.depthAttachmentFormat = depth_format_;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &rendering;
    gp.stageCount = 1;
    gp.pStages = &stage;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dy;
    gp.layout = pipeline_layout_;
    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &shadow_pipeline_);
    vkDestroyShaderModule(device_, vs, nullptr);
    GK_VK_CHECK(r);
    return true;
}

namespace {
SdfFont g_font; // shared: ui_text_width is static
constexpr float kSdfPerEm = (128.0f / 8.0f) * 56.0f / 255.0f; // field units per em (font.cpp's bake)
vec4 unpack_rgba(uint32_t c) {
    return vec4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), float(c >> 24)) / 255.0f;
}
uint32_t pack_rgba(vec4 v) {
    const glm::uvec4 u(glm::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    return u.r | u.g << 8 | u.b << 16 | u.a << 24;
}
UiVertex flat_vertex(vec2 p, uint32_t c) {
    UiVertex v;
    v.pos = p;
    v.color = c;
    return v;
}
} // namespace

void Renderer::make_particle_texture() {
    // A 4 x 4 atlas of hand-drawn-style shapes (see Sprite): each a signed
    // distance (negative inside) drawn with a dark outline and baked
    // shading in the grey channel; the particle colour multiplies it, so
    // outlines come out as a dark shade of the particle's own colour.
    constexpr int S = 512, C = 128;
    constexpr float kLine = 0.07f; // outline width, in cell units (-1..1)
    std::vector<uint8_t> px(S * S * 4, 0);
    struct Px { float d, light; }; // signed distance and shading 0..1
    auto cell = [&](int index, bool outline, auto shape) {
        const int cx = (index % 4) * C, cy = (index / 4) * C;
        for (int y = 0; y < C; ++y)
            for (int x = 0; x < C; ++x) {
                float a = 0.0f, l = 0.0f;
                for (int sy = 0; sy < 3; ++sy)
                    for (int sx = 0; sx < 3; ++sx) {
                        const vec2 p((float(x) + (float(sx) + 0.5f) / 3.0f) / C * 2.0f - 1.0f, (float(y) + (float(sy) + 0.5f) / 3.0f) / C * 2.0f - 1.0f);
                        const Px q = shape(p);
                        if (q.d > 0.0f) continue;
                        a += 1.0f;
                        l += outline && q.d > -kLine ? 0.16f : q.light;
                    }
                uint8_t* o = &px[(size_t(cy + y) * S + size_t(cx + x)) * 4];
                const float lit = a > 0.0f ? l / a : 1.0f;
                o[0] = o[1] = o[2] = uint8_t(std::clamp(lit, 0.0f, 1.0f) * 255.0f);
                o[3] = uint8_t(a / 9.0f * 255.0f);
            }
    };
    const float pi = 3.14159265f;
    auto circle = [](vec2 p, vec2 c, float r) { return glm::length(p - c) - r; };
    // A cloud puff: overlapping round lobes, lit from the top left with a
    // darker underside and a few shadow creases.
    auto puff = [&](const std::vector<vec3>& lobes) {
        return [lobes, circle](vec2 p) {
            float d = 1e9f;
            for (const vec3& l : lobes) d = std::min(d, circle(p, vec2(l), l.z));
            const float up = glm::dot(p, glm::normalize(vec2(0.45f, 0.9f))); // + towards the lower right
            float light = up < 0.05f ? 1.0f : up < 0.45f ? 0.86f : 0.7f;
            // A crease inside each lobe on its lower side.
            for (const vec3& l : lobes) {
                const float r = glm::length(p - vec2(l));
                if (r < l.z * 0.85f && r > l.z * 0.72f && (p.y - l.y) > 0.25f * l.z) light = std::min(light, 0.66f);
            }
            return Px{d, light};
        };
    };
    cell(0, true, puff({{-0.3f, 0.2f, 0.42f}, {0.25f, 0.18f, 0.48f}, {0.0f, -0.25f, 0.5f}, {-0.48f, -0.12f, 0.3f}, {0.52f, -0.15f, 0.3f}}));
    cell(11, true, puff({{-0.2f, 0.25f, 0.45f}, {0.35f, 0.1f, 0.42f}, {-0.05f, -0.3f, 0.42f}, {0.4f, -0.4f, 0.25f}, {-0.55f, 0.0f, 0.28f}}));
    // Star: five chunky points, a lighter upper half.
    cell(1, true, [](vec2 p) {
        const float a = std::atan2(p.x, -p.y), r = glm::length(p);
        const float k = std::pow(0.5f + 0.5f * std::cos(5.0f * a), 1.6f); // 1 at a point
        const float edge = glm::mix(0.45f, 0.95f, k);
        return Px{(r - edge) * 0.75f, p.y < -0.05f ? 1.0f : 0.84f};
    });
    // Sparkle: a four-pointed glint with a round bright centre.
    cell(2, true, [](vec2 p) {
        const float a = std::atan2(p.y, p.x), r = glm::length(p);
        const float k = std::pow(std::abs(std::cos(2.0f * a)), 5.0f);
        const float edge = glm::mix(0.22f, 0.95f, k);
        return Px{(r - edge) * 0.7f, r < 0.32f ? 1.0f : 0.86f};
    });
    // Flame lick: a curved, tapering swoosh, tip at the top (-y), base at
    // the bottom; a hot core along the middle.
    cell(3, true, [](vec2 p) {
        const float t = std::clamp((p.y + 0.95f) / 1.9f, 0.0f, 1.0f); // 0 tip .. 1 base
        const float x0 = 0.35f * (1.0f - t) * (1.0f - t) - 0.12f;     // curls to the right near the tip
        const float w = 0.06f + 0.5f * std::sin(t * 2.4f) * t;
        const float d = std::max(std::abs(p.x - x0) - w, std::max(-0.95f - p.y, p.y - 0.9f));
        return Px{d, std::abs(p.x - x0) < w * 0.45f ? 1.0f : 0.78f};
    });
    // Solid square: decals and lines.
    cell(4, false, [](vec2 p) { return Px{std::max(std::abs(p.x), std::abs(p.y)) - 0.98f, 1.0f}; });
    // Ring: a shock wave.
    cell(5, true, [](vec2 p) { const float r = glm::length(p); return Px{std::abs(r - 0.78f) - 0.13f, 1.0f}; });
    // Dot: a disc with a highlight.
    cell(6, true, [](vec2 p) { return Px{glm::length(p) - 0.85f, glm::length(p - vec2(-0.25f, -0.25f)) < 0.3f ? 1.0f : 0.85f}; });
    // Confetti: a little card.
    cell(7, true, [](vec2 p) { return Px{std::max(std::abs(p.x) - 0.6f, std::abs(p.y) - 0.32f), p.y < 0.0f ? 1.0f : 0.82f}; });
    // Burst: a jagged impact star, white-hot in the middle.
    cell(8, true, [pi](vec2 p) {
        const float a = std::atan2(p.y, p.x), r = glm::length(p);
        const int n = 10;
        const float k = std::abs(std::fmod(a + 2.0f * pi, 2.0f * pi / n) / (2.0f * pi / n) - 0.5f) * 2.0f;
        const float jag = std::sin(a * 3.0f) * 0.08f + std::sin(a * 7.0f) * 0.05f; // irregular spikes
        const float edge = glm::mix(0.38f, 0.95f + jag, std::pow(k, 2.2f));
        return Px{(r - edge) * 0.7f, r < 0.32f ? 1.0f : r < 0.55f ? 0.9f : 0.78f};
    });
    // Streak: a thin wind line, fat at the head (bottom) and tapering away.
    cell(9, false, [](vec2 p) {
        const float t = std::clamp((p.y + 0.95f) / 1.9f, 0.0f, 1.0f);
        const float w = 0.03f + 0.22f * t * t;
        return Px{std::max(std::abs(p.x) - w, std::max(-0.95f - p.y, p.y - 0.95f)), 1.0f};
    });
    // Rock: a chipped chunk of dirt.
    cell(10, true, [pi](vec2 p) {
        const float a = std::atan2(p.y, p.x), r = glm::length(p);
        const float corners[6] = {0.82f, 0.62f, 0.9f, 0.7f, 0.85f, 0.6f};
        const float f = (a + pi) / (2.0f * pi) * 6.0f;
        const int i = int(f) % 6;
        const float edge = glm::mix(corners[i], corners[(i + 1) % 6], f - std::floor(f));
        return Px{(r - edge) * 0.9f, glm::dot(p, vec2(-0.7f, -0.7f)) > 0.0f ? 1.0f : 0.75f};
    });
    // Drop: a water drop, point at the top.
    cell(12, true, [](vec2 p) {
        const float base = glm::length(p - vec2(0.0f, 0.25f)) - 0.55f;
        const float cone = std::abs(p.x) - (p.y + 0.95f) * 0.5f;
        const float d = p.y < 0.25f ? std::max(cone, -0.95f - p.y) : base;
        return Px{std::min(base, std::max(cone, std::max(-0.95f - p.y, p.y - 0.25f))) + 0.0f * d, glm::length(p - vec2(-0.2f, 0.1f)) < 0.18f ? 1.0f : 0.85f};
    });
    // Tread: rubber laid down in streaks along the length (y), soft at the sides.
    cell(13, false, [](vec2 p) {
        auto h = [](float x) { return std::fmod(std::sin(x * 127.1f) * 43758.5453f, 1.0f); };
        const float band = std::floor((p.x + 1.0f) * 9.0f);
        const float streak = 0.45f + 0.55f * std::abs(h(band));
        const float side = 1.0f - glm::smoothstep(0.65f, 1.0f, std::abs(p.x));
        const float grain = std::abs(h(std::floor((p.x + 1.0f) * 40.0f) * 7.0f + std::floor((p.y + 1.0f) * 20.0f)));
        const float density = streak * side * (0.75f + 0.25f * grain);
        // Coverage by density: a sub-sample is inside when it falls under it.
        const float pick = std::abs(h(p.x * 911.0f + p.y * 377.0f));
        return Px{pick < density ? -1.0f : 1.0f, 1.0f};
    });
    if (const char* dump = std::getenv("GK_DUMP_PARTICLES")) stbi_write_png(dump, S, S, 4, px.data(), S * 4); // to check the art
    particle_texture_ = upload_texture(S, S, px.data());
}

void Renderer::set_ui_icons(const std::vector<uint8_t>& icons, int size, int grid) {
    if (!g_font.loaded() || size != g_font.atlas_size()) return;
    // One texture, font on the left half, icons on the right.
    const int fs = g_font.atlas_size();
    std::vector<uint8_t> px(size_t(fs) * 2 * size_t(fs) * 4, 0);
    const auto& font = g_font.atlas_rgba();
    for (int y = 0; y < fs; ++y) {
        std::copy_n(font.begin() + ptrdiff_t(y) * fs * 4, size_t(fs) * 4, px.begin() + ptrdiff_t(y) * fs * 8);
        std::copy_n(icons.begin() + ptrdiff_t(y) * size * 4, size_t(size) * 4, px.begin() + ptrdiff_t(y) * fs * 8 + fs * 4);
    }
    font_texture_ = upload_texture(fs * 2, fs, px.data());
    font_u_scale_ = 0.5f;
    icon_grid_ = grid;
}

void Renderer::ui_icon(float x, float y, float size, int index, uint32_t tint) {
    if (!frame_active_ || icon_grid_ <= 0 || index < 0) return;
    const float c = 1.0f / float(icon_grid_), e = 0.5f / 512.0f;
    const float u0 = 0.5f + 0.5f * (float(index % icon_grid_) * c + e), u1 = 0.5f + 0.5f * (float(index % icon_grid_ + 1) * c - e);
    const float v0 = float(index / icon_grid_) * c + e, v1 = float(index / icon_grid_ + 1) * c - e;
    auto v = [&](float px, float py, float u, float vv) {
        UiVertex o;
        o.pos = {px, py};
        o.uv = {u, vv};
        o.color = tint;
        o.mode = 4;
        return o;
    };
    const UiVertex a = v(x, y, u0, v0), b = v(x + size, y, u1, v0), cc = v(x + size, y + size, u1, v1), d = v(x, y + size, u0, v1);
    ui_vertices_.insert(ui_vertices_.end(), {a, b, cc, a, cc, d});
}

bool Renderer::load_font(const std::string& path) {
    if (!g_font.load(path)) return false;
    font_texture_ = upload_texture(g_font.atlas_size(), g_font.atlas_size(), g_font.atlas_rgba().data());
    return font_texture_ != 0;
}

bool Renderer::create_ui_pipeline() {
    VkShaderModule vs = make_shader(device_, k_ui_vert, sizeof(k_ui_vert));
    VkShaderModule fs = make_shader(device_, k_ui_frag, sizeof(k_ui_frag));
    if (!vs || !fs) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vec2)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &texture_set_layout_; // the font atlas
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    GK_VK_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &ui_layout_));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(UiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[5] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, uv)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UiVertex, color)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(UiVertex, mode)},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiVertex, params)},
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
    rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED; // drawn on the composite, which has no depth
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

void Renderer::particle(vec3 pos, float size, vec4 color, bool additive, vec3 velocity, float stretch, Sprite sprite, float angle) {
    if (!frame_active_ || particles_.size() >= kMaxParticles || color.a <= 0.003f) return;
    // Right in front of the lens a sprite would fill the screen: fade it out.
    const float d = glm::distance(pos, cam_pos_);
    if (d < 1.2f + size) return;
    color.a *= std::clamp((d - 1.2f - size) / 2.5f, 0.0f, 1.0f);
    particles_.push_back({pos, velocity, size, stretch, angle, pack_rgba(color), additive, sprite});
}

namespace {
// Atlas cell (4 x 4) for a sprite, inset a texel.
void sprite_uv(Renderer::Sprite s, vec2 out[4]) {
    const int i = int(s);
    const float c = 0.25f, e = 1.5f / 512.0f;
    const float u0 = float(i % 4) * c + e, v0 = float(i / 4) * c + e, u1 = u0 + c - 2.0f * e, v1 = v0 + c - 2.0f * e;
    out[0] = {u0, v1};
    out[1] = {u1, v1};
    out[2] = {u1, v0};
    out[3] = {u0, v0};
}
} // namespace

void Renderer::decal(const vec3 p[4], const vec2 uv[4], const vec4 color[4], Sprite sprite) {
    if (!frame_active_ || decals_.size() >= 6 * 3000) return;
    vec2 cell[4];
    sprite_uv(sprite, cell);
    const vec2 lo(cell[0].x, cell[2].y), hi(cell[2].x, cell[0].y);
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k : order) {
        Vertex v{};
        v.pos = p[k];
        v.normal = {0, 1, 0};
        v.color = pack_rgba(color[k]);
        v.uv = lo + (hi - lo) * glm::clamp(uv[k], vec2(0.0f), vec2(1.0f));
        decals_.push_back(v);
    }
}

void Renderer::decal(vec3 a, vec3 b, vec3 c, vec3 d, vec4 color) {
    if (!frame_active_ || decals_.size() >= 6 * 3000) return;
    vec2 uv[4];
    sprite_uv(Sprite::Solid, uv);
    const vec2 mid = 0.5f * (uv[0] + uv[2]);
    const vec3 p[4] = {a, b, c, d};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k : order) {
        Vertex v{};
        v.pos = p[k];
        v.normal = {0, 1, 0};
        v.color = pack_rgba(color);
        v.uv = mid;
        decals_.push_back(v);
    }
}

void Renderer::ui_rect(float x, float y, float w, float h, uint32_t color) {
    if (!frame_active_) return;
    const UiVertex a = flat_vertex({x, y}, color), b = flat_vertex({x + w, y}, color), c = flat_vertex({x + w, y + h}, color),
                   d = flat_vertex({x, y + h}, color);
    ui_vertices_.insert(ui_vertices_.end(), {a, b, c, a, c, d});
}

void Renderer::ui_quad(vec2 a, vec2 b, vec2 c, vec2 d, uint32_t color) {
    if (!frame_active_) return;
    const UiVertex va = flat_vertex(a, color), vb = flat_vertex(b, color), vc = flat_vertex(c, color), vd = flat_vertex(d, color);
    ui_vertices_.insert(ui_vertices_.end(), {va, vb, vc, va, vc, vd});
}

void Renderer::ui_round_rect(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom, float feather) {
    if (!frame_active_ || w <= 0.0f || h <= 0.0f) return;
    // Grown by the feather so the soft edge has room.
    const float g = feather;
    const vec4 params(0.5f * w, 0.5f * h, std::min(radius, 0.5f * std::min(w, h)), feather);
    const vec2 c(x + 0.5f * w, y + 0.5f * h);
    auto v = [&](float px, float py, uint32_t col) {
        UiVertex u;
        u.pos = {px, py};
        u.uv = vec2(px, py) - c;
        u.color = col;
        u.mode = 2;
        u.params = params;
        return u;
    };
    const UiVertex a = v(x - g, y - g, top), b = v(x + w + g, y - g, top), cc = v(x + w + g, y + h + g, bottom),
                   d = v(x - g, y + h + g, bottom);
    ui_vertices_.insert(ui_vertices_.end(), {a, b, cc, a, cc, d});
}

void Renderer::ui_slant_rect(float x, float y, float w, float h, float slant, uint32_t top, uint32_t bottom, float feather) {
    if (!frame_active_ || w <= 0.0f || h <= 0.0f) return;
    const float g = feather;
    const vec2 c(x + 0.5f * w, y + 0.5f * h);
    const vec4 params(0.5f * w, 0.5f * h, 0.0f, feather);
    // Corners in the unslanted rectangle; positions lean, the shape test
    // (uv) does not, so edges stay crisp along the slant.
    auto v = [&](float px, float py, uint32_t col) {
        UiVertex u;
        u.pos = {px + slant * (c.y - py), py};
        u.uv = vec2(px, py) - c;
        u.color = col;
        u.mode = 2;
        u.params = params;
        return u;
    };
    const UiVertex a = v(x - g, y - g, top), b = v(x + w + g, y - g, top), cc = v(x + w + g, y + h + g, bottom), d = v(x - g, y + h + g, bottom);
    ui_vertices_.insert(ui_vertices_.end(), {a, b, cc, a, cc, d});
}

void Renderer::ui_vignette(float strength, uint32_t color) {
    if (!frame_active_) return;
    const VkExtent2D e = ui_extent();
    const vec2 half(0.5f * float(e.width), 0.5f * float(e.height));
    auto v = [&](float x, float y) {
        UiVertex u;
        u.pos = {x, y};
        u.uv = vec2(x, y) - half;
        u.color = color;
        u.mode = 3;
        u.params = vec4(half, strength, 0.0f);
        return u;
    };
    const float w = float(e.width), h = float(e.height);
    const UiVertex a = v(0, 0), b = v(w, 0), c = v(w, h), d = v(0, h);
    ui_vertices_.insert(ui_vertices_.end(), {a, b, c, a, c, d});
}

void Renderer::ui_glyphs(float x, float y, const char* text, uint32_t color, float em, float edge, float soft, uint32_t color_bottom,
                         float slant, float tracking) {
    const float baseline = y + g_font.cap_height() * em;
    const float cap_top = y;
    if (!color_bottom) color_bottom = color;
    float pen = x;
    for (const char* p = text; *p; ++p) {
        const SdfFont::Glyph& g = g_font.glyph(*p);
        if (g.valid) {
            const float x0 = pen + g.x0 * em, x1 = pen + g.x1 * em, y0 = baseline + g.y0 * em, y1 = baseline + g.y1 * em;
            auto v = [&](float px, float py, float u, float vv) {
                UiVertex o;
                o.pos = {px + slant * (baseline - py), py};
                o.uv = {u, vv};
                // Gradient from the cap top to the baseline.
                const float t = std::clamp((py - cap_top) / std::max(baseline - cap_top, 1e-3f), 0.0f, 1.0f);
                const vec4 ca = unpack_rgba(color), cb = unpack_rgba(color_bottom);
                o.color = pack_rgba(glm::mix(ca, cb, t));
                o.mode = 1;
                o.params = vec4(edge, soft, 0.0f, 0.0f);
                return o;
            };
            const float u0 = g.u0 * font_u_scale_, u1 = g.u1 * font_u_scale_;
            const UiVertex a = v(x0, y0, u0, g.v0), b = v(x1, y0, u1, g.v0), c = v(x1, y1, u1, g.v1), d = v(x0, y1, u0, g.v1);
            ui_vertices_.insert(ui_vertices_.end(), {a, b, c, a, c, d});
        }
        pen += (g.advance + tracking + (p[1] ? g_font.kern(p[0], p[1]) : 0.0f)) * em;
    }
}

void Renderer::ui_text_styled(float x, float y, const char* text, const TextStyle& st) {
    if (!frame_active_ || !text) return;
    if (!g_font.loaded()) {
        ui_text_ex(x, y, text, st.color, st.scale, st.outline, st.outline_px, st.shadow != 0, st.weight);
        return;
    }
    const float em = 7.0f * st.scale / std::max(g_font.cap_height(), 0.1f);
    const float fill_edge = 0.5f - kSdfPerEm * 0.02f * st.weight;
    const float out_t = std::min(st.outline_px / em, 0.11f);
    const float outer = st.outline_px > 0.0f ? fill_edge - kSdfPerEm * out_t : fill_edge;
    if (st.shadow) ui_glyphs(x + st.shadow_offset.x, y + st.shadow_offset.y, text, st.shadow, em, outer, 0.0f, 0, st.slant, st.tracking);
    if (st.outline_px > 0.0f) ui_glyphs(x, y, text, st.outline, em, outer, 0.0f, 0, st.slant, st.tracking);
    ui_glyphs(x, y, text, st.color, em, fill_edge, 0.0f, st.color_bottom, st.slant, st.tracking);
}

float Renderer::ui_text_width(const char* text, float scale, float tracking) {
    if (!g_font.loaded()) return float(stb_easy_font_width(const_cast<char*>(text))) * scale;
    const float em = 7.0f * scale / std::max(g_font.cap_height(), 0.1f);
    float w = 0.0f;
    for (const char* p = text; *p; ++p) w += (g_font.glyph(*p).advance + tracking + (p[1] ? g_font.kern(p[0], p[1]) : 0.0f)) * em;
    return w;
}

void Renderer::ui_text_ex(float x, float y, const char* text, uint32_t color, float scale, uint32_t outline, float outline_px,
                          bool shadow, float weight) {
    if (!frame_active_ || !text) return;
    if (!g_font.loaded()) {
        if (outline_px > 0.0f || shadow) ui_text_ex(x + 1.0f, y + 1.0f, text, outline, scale, 0, 0.0f, false, weight);
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
            for (int i = 0; i < 4; ++i) out[i] = flat_vertex({x + v[i].x * scale, y + v[i].y * scale}, color);
            ui_vertices_.insert(ui_vertices_.end(), {out[0], out[1], out[2], out[0], out[2], out[3]});
        }
        return;
    }
    TextStyle st;
    st.color = color;
    st.scale = scale;
    st.outline = outline;
    st.outline_px = outline_px;
    st.weight = weight;
    st.slant = kItalic;
    st.tracking = kTracking;
    if (shadow) {
        st.shadow = rgba(0, 0, 0, uint8_t((color >> 24) * 6 / 10));
        st.shadow_offset = vec2(std::max(1.0f, scale * 0.7f));
    }
    ui_text_styled(x, y, text, st);
}

void Renderer::ui_text(float x, float y, const char* text, uint32_t color, float scale, bool shadow) {
    // The house style: italic, slightly spaced, a thin black outline and a
    // hard drop shadow, like an arcade racer's HUD.
    TextStyle st;
    st.color = color;
    st.scale = scale;
    st.slant = kItalic;
    st.tracking = kTracking;
    if (shadow) {
        st.outline = rgba(0, 0, 0, uint8_t(std::min(255u, (color >> 24) * 9 / 10)));
        st.outline_px = std::clamp(scale * 0.5f, 0.6f, 2.2f);
        st.shadow = rgba(0, 0, 0, uint8_t((color >> 24) * 6 / 10));
        st.shadow_offset = vec2(std::max(1.0f, scale * 0.7f), std::max(1.0f, scale * 0.7f));
    }
    ui_text_styled(x, y, text, st);
}

float Renderer::ui_text_width(const char* text, float scale) { return ui_text_width(text, scale, kTracking); }

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

    // Upload every layer's mip chain.
    std::vector<std::vector<uint8_t>> mips;
    std::vector<VkExtent2D> extents;
    size_t at = 0;
    for (uint32_t layer = 0; layer < TexCount; ++layer)
        for (uint32_t mip = 0; mip < uint32_t(kTexMips); ++mip) {
            const uint32_t size = uint32_t(kTexSize) >> mip;
            const size_t bytes = size_t(size) * size * 4;
            mips.emplace_back(pixels.begin() + long(at), pixels.begin() + long(at + bytes));
            extents.push_back({size, size});
            at += bytes;
        }
    return upload_image_mips(textures_, mips, extents, TexCount);
}

// Copies `mips` (layer-major, mip-minor, RGBA8) into `img` and leaves it
// ready for sampling.
bool Renderer::upload_image_mips(Image& img, const std::vector<std::vector<uint8_t>>& mips, const std::vector<VkExtent2D>& extents,
                                 uint32_t layers) {
    size_t total = 0;
    for (const auto& m : mips) total += m.size();
    Buffer staging = create_buffer(std::max<size_t>(total, 4), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    if (!staging.mapped) return false;
    std::vector<VkBufferImageCopy> copies;
    VkDeviceSize offset = 0;
    const uint32_t per_layer = uint32_t(mips.size() / layers);
    for (size_t i = 0; i < mips.size(); ++i) {
        std::memcpy(static_cast<uint8_t*>(staging.mapped) + offset, mips[i].data(), mips[i].size());
        VkBufferImageCopy c{};
        c.bufferOffset = offset;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(i) % per_layer, uint32_t(i) / per_layer, 1};
        c.imageExtent = {extents[i].width, extents[i].height, 1};
        copies.push_back(c);
        offset += VkDeviceSize(mips[i].size());
    }
    vmaFlushAllocation(allocator_, staging.alloc, 0, VK_WHOLE_SIZE);

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
    b.image = img.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, per_layer, 0, layers};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &b);
    vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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

TextureId Renderer::upload_texture(int width, int height, const uint8_t* rgba) {
    if (width <= 0 || height <= 0 || !rgba || textures2d_.size() >= kMaxTextures) return 0;
    // Mip chain, box filtered, down to 1x1.
    std::vector<std::vector<uint8_t>> mips;
    std::vector<VkExtent2D> extents;
    mips.emplace_back(rgba, rgba + size_t(width) * size_t(height) * 4);
    extents.push_back({uint32_t(width), uint32_t(height)});
    int w = width, h = height;
    while (w > 1 || h > 1) {
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        const std::vector<uint8_t>& src = mips.back();
        std::vector<uint8_t> dst(size_t(nw) * size_t(nh) * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(w - 1, 2 * x + dx), sy = std::min(h - 1, 2 * y + dy);
                            sum += src[(size_t(sy) * size_t(w) + size_t(sx)) * 4 + size_t(c)];
                        }
                    dst[(size_t(y) * size_t(nw) + size_t(x)) * 4 + size_t(c)] = uint8_t(sum / 4);
                }
        mips.push_back(std::move(dst));
        extents.push_back({uint32_t(nw), uint32_t(nh)});
        w = nw;
        h = nh;
    }
    Texture2D t;
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = VK_FORMAT_R8G8B8A8_UNORM;
    ic.extent = {uint32_t(width), uint32_t(height), 1};
    ic.mipLevels = uint32_t(mips.size());
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ac{};
    ac.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator_, &ic, &ac, &t.image.image, &t.image.alloc, nullptr) != VK_SUCCESS) {
        log_error("Texture allocation failed ({}x{})", width, height);
        return 0;
    }
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = t.image.image;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = ic.format;
    vc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, ic.mipLevels, 0, 1};
    if (vkCreateImageView(device_, &vc, nullptr, &t.image.view) != VK_SUCCESS) {
        destroy_image(t.image);
        return 0;
    }
    if (!upload_image_mips(t.image, mips, extents, 1)) {
        destroy_image(t.image);
        return 0;
    }
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = texture_pool_;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &texture_set_layout_;
    if (vkAllocateDescriptorSets(device_, &da, &t.set) != VK_SUCCESS) {
        destroy_image(t.image);
        return 0;
    }
    VkDescriptorImageInfo ii{sampler_, t.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = t.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    textures2d_.push_back(t);
    return TextureId(textures2d_.size() - 1);
}

void Renderer::destroy_image(Image& img) {
    if (img.view) vkDestroyImageView(device_, img.view, nullptr);
    if (img.image) vmaDestroyImage(allocator_, img.image, img.alloc);
    img = {};
}

MeshId Renderer::upload_mesh(const MeshData& mesh, TextureId texture, bool two_sided, bool unlit, bool blended, bool glow) {
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
    g.texture = texture < textures2d_.size() ? texture : 0;
    g.two_sided = two_sided;
    g.unlit = unlit;
    g.blended = blended;
    g.glow = glow && texture != 0;
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
    cam_right_ = vec3(view[0][0], view[1][0], view[2][0]);
    cam_up_ = vec3(view[0][1], view[1][1], view[2][1]);
    cam_pos_ = camera.position;
    time_ = float(double(SDL_GetTicks()) / 1000.0);
    FrameUniforms u{};
    u.view_proj = proj * view;
    u.camera_pos = vec4(camera.position, 1.0f);
    u.light_dir = vec4(glm::normalize(lighting.light_dir), lighting.ambient);
    u.fog_color = vec4(lighting.sky, lighting.fog_density);
    u.sky_color = vec4(lighting.zenith, 1.0f);
    {   // The sun's view: an orthographic box round the area ahead of the camera,
        // snapped to whole shadow-map texels so the edges don't crawl.
        const vec3 l = glm::normalize(lighting.light_dir);
        const vec3 ahead = camera.target + glm::normalize(vec3(camera.target - camera.position) * vec3(1, 0, 1) + vec3(1e-4f)) * 22.0f;
        const float half = 48.0f;
        mat4 lv = glm::lookAt(ahead + l * 150.0f, ahead, std::abs(l.y) > 0.98f ? vec3(0, 0, 1) : kUp);
        const float texel = 2.0f * half / float(kShadowSize);
        vec4 o = lv * vec4(0, 0, 0, 1);
        lv[3][0] -= std::fmod(o.x, texel);
        lv[3][1] -= std::fmod(o.y, texel);
        const mat4 lp = glm::ortho(-half, half, -half, half, 1.0f, 320.0f);
        light_vp_ = lp * lv;
        u.light_vp = light_vp_;
        u.shadow = vec4(lighting.shadows ? 1.0f : 0.0f, 1.0f / float(kShadowSize), lighting.sun, std::fmod(time_, 1000.0f));
    }
    std::memcpy(f.uniforms.mapped, &u, sizeof(u));
    vmaFlushAllocation(allocator_, f.uniforms.alloc, 0, VK_WHOLE_SIZE);
    clear_color_ = lighting.sky;
    sky_center_ = camera.position;
    sky_texture_ = lighting.sky_texture;

    draws_.clear();
    particles_.clear();
    decals_.clear();
    ui_vertices_.clear();
    frame_active_ = true;
    return true;
}

void Renderer::draw(MeshId mesh, const mat4& model, vec4 tint) {
    if (frame_active_ && mesh < meshes_.size()) draws_.push_back({mesh, model, tint, fx_});
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

    {   // Shadow pass: opaque, lit meshes from the sun.
        image_barrier(cmd, f.shadow.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        VkRenderingAttachmentInfo sd{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        sd.imageView = f.shadow.view;
        sd.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        sd.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sd.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sd.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo sri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        sri.renderArea = {{0, 0}, {kShadowSize, kShadowSize}};
        sri.layerCount = 1;
        sri.pDepthAttachment = &sd;
        vkCmdBeginRendering(cmd, &sri);
        VkViewport sv{0, 0, float(kShadowSize), float(kShadowSize), 0.0f, 1.0f};
        VkRect2D ss{{0, 0}, {kShadowSize, kShadowSize}};
        vkCmdSetViewport(cmd, 0, 1, &sv);
        vkCmdSetScissor(cmd, 0, 1, &ss);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &f.set, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 1, 1, &textures2d_[0].set, 0, nullptr);
        struct {
            mat4 model;
            vec4 tint;
            glm::ivec4 flags;
        } sp{};
        for (const auto& d : draws_) {
            const GpuMesh& m = meshes_[d.mesh];
            if (!m.index_count || m.unlit || m.blended || d.tint.a < 0.99f) continue;
            sp.model = d.model;
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertices.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, m.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sp), &sp);
            vkCmdDrawIndexed(cmd, m.index_count, 1, 0, 0, 0);
        }
        vkCmdEndRendering(cmd);
        image_barrier(cmd, f.shadow.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

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
        glm::ivec4 flags; // x: 1 = textured (set 1) instead of the detail array
    } push;
    VkPipeline bound_pipeline = mesh_pipeline_;
    TextureId bound_texture = UINT32_MAX;
    {   // The sky first: a sphere just inside the far plane, round the camera.
        if (!sky_mesh_) {
            MeshBuilder b;
            b.icosphere(vec3(0.0f), 1.0f, rgba(255, 255, 255));
            sky_mesh_ = upload_mesh(b.take(), 0, true);
        }
        const GpuMesh& m = meshes_[sky_mesh_];
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_two_sided_);
        bound_pipeline = mesh_pipeline_two_sided_;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 1, 1, &textures2d_[sky_texture_].set, 0, nullptr);
        bound_texture = sky_texture_;
        push.model = glm::scale(glm::translate(mat4(1.0f), sky_center_), vec3(900.0f));
        push.tint = vec4(1.0f);
        push.flags = glm::ivec4(sky_texture_ ? 1 : 0, 0, 0, 1);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(cmd, m.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, m.index_count, 1, 0, 0, 0);
    }
    // Opaque meshes first, then the see-through ones in the order they came.
    for (int pass = 0; pass < 2; ++pass)
    for (const auto& d : draws_) {
        const GpuMesh& m = meshes_[d.mesh];
        if (!m.index_count) continue;
        const bool see_through = m.blended || d.tint.a < 0.99f;
        if (see_through != (pass == 1)) continue;
        const VkPipeline want = see_through ? mesh_pipeline_blend_ : m.two_sided ? mesh_pipeline_two_sided_ : mesh_pipeline_;
        if (want != bound_pipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
            bound_pipeline = want;
        }
        if (m.texture != bound_texture) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 1, 1, &textures2d_[m.texture].set, 0, nullptr);
            bound_texture = m.texture;
        }
        push.flags = glm::ivec4(m.texture ? 1 : 0, m.unlit ? 1 : 0, m.glow ? 1 : 0, d.fx);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(cmd, m.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        push.model = d.model;
        push.tint = d.tint;
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, m.index_count, 1, 0, 0, 0);
    }
    if ((!particles_.empty() || !decals_.empty()) && particle_texture_) {
        // Decals, then soft blended particles (back to front), then additive ones.
        std::stable_sort(particles_.begin(), particles_.end(), [&](const ParticleCmd& a, const ParticleCmd& b) {
            if (a.additive != b.additive) return !a.additive;
            return glm::distance(a.pos, cam_pos_) > glm::distance(b.pos, cam_pos_);
        });
        Vertex* out = static_cast<Vertex*>(f.particles.mapped);
        const uint32_t cap = 6 * kMaxParticles;
        uint32_t n = 0;
        for (const Vertex& v : decals_) {
            if (n >= cap) break;
            out[n++] = v;
        }
        uint32_t first_add = 0;
        bool add_seen = false;
        for (const ParticleCmd& p : particles_) {
            if (n + 6 > cap) break;
            if (p.additive && !add_seen) first_add = n, add_seen = true;
            vec3 rx = cam_right_ * p.size, ry = cam_up_ * p.size;
            if (p.angle != 0.0f) {
                const float cs = std::cos(p.angle), sn = std::sin(p.angle);
                const vec3 a = rx * cs + ry * sn, b = -rx * sn + ry * cs;
                rx = a, ry = b;
            }
            if (p.stretch > 0.0f && glm::length(p.vel) > 0.01f) {
                // A streak: long axis along the motion as seen on screen.
                const vec3 to = glm::normalize(p.pos - cam_pos_);
                vec3 along = p.vel - to * glm::dot(p.vel, to);
                if (glm::length(along) > 1e-3f) {
                    along = glm::normalize(along);
                    ry = along * (p.size + glm::length(p.vel) * p.stretch);
                    rx = glm::normalize(glm::cross(along, to)) * p.size;
                }
            }
            const vec3 c[4] = {p.pos - rx - ry, p.pos + rx - ry, p.pos + rx + ry, p.pos - rx + ry};
            vec2 uv[4];
            sprite_uv(p.sprite, uv);
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k : order) {
                Vertex v{};
                v.pos = c[k];
                v.normal = -cam_right_;
                v.color = p.color;
                v.uv = uv[k];
                out[n++] = v;
            }
        }
        if (!add_seen) first_add = n;
        vmaFlushAllocation(allocator_, f.particles.alloc, 0, VK_WHOLE_SIZE);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 1, 1, &textures2d_[particle_texture_].set, 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &f.particles.buffer, &offset);
        push.model = mat4(1.0f);
        push.tint = vec4(1.0f);
        push.flags = glm::ivec4(1, 1, 0, 0);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        if (first_add > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_blend_);
            vkCmdDraw(cmd, first_add, 1, 0, 0);
        }
        if (n > first_add) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_add_);
            vkCmdDraw(cmd, n - first_add, 1, first_add, 0);
        }
    }
    vkCmdEndRendering(cmd);

    // Scale the (possibly low-res) scene up into the full-size composite
    // with nearest filtering: the pixel look is for the game only.
    image_barrier(cmd, offscreen_color_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT);
    image_barrier(cmd, composite_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, composite_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    image_barrier(cmd, composite_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkExtent2D win = composite_extent_;
    {
        const int32_t dw = int32_t(offscreen_extent_.width * pixel_scale_), dh = int32_t(offscreen_extent_.height * pixel_scale_);
        const int32_t ox = (int32_t(win.width) - dw) / 2, oy = (int32_t(win.height) - dh) / 2;
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {int32_t(offscreen_extent_.width), int32_t(offscreen_extent_.height), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[0] = {ox, oy, 0};
        blit.dstOffsets[1] = {ox + dw, oy + dh, 1};
        vkCmdBlitImage(cmd, offscreen_color_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, composite_.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    }

    // The UI, at full resolution.
    image_barrier(cmd, composite_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    if (!ui_vertices_.empty()) {
        VkRenderingAttachmentInfo ui_color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ui_color.imageView = composite_.view;
        ui_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ui_color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ui_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo uri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        uri.renderArea = {{0, 0}, win};
        uri.layerCount = 1;
        uri.colorAttachmentCount = 1;
        uri.pColorAttachments = &ui_color;
        vkCmdBeginRendering(cmd, &uri);
        VkViewport uvp{0, 0, float(win.width), float(win.height), 0.0f, 1.0f};
        VkRect2D usc{{0, 0}, win};
        vkCmdSetViewport(cmd, 0, 1, &uvp);
        vkCmdSetScissor(cmd, 0, 1, &usc);
        const size_t count = std::min<size_t>(ui_vertices_.size(), kMaxUiVertices);
        std::memcpy(f.ui.mapped, ui_vertices_.data(), count * sizeof(UiVertex));
        vmaFlushAllocation(allocator_, f.ui.alloc, 0, VK_WHOLE_SIZE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_layout_, 0, 1, &textures2d_[font_texture_].set, 0, nullptr);
        const VkExtent2D ui = ui_extent();
        const vec2 inv(2.0f / float(ui.width), 2.0f / float(ui.height));
        vkCmdPushConstants(cmd, ui_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(inv), &inv);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &f.ui.buffer, &offset);
        vkCmdDraw(cmd, uint32_t(count), 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    // Composite to the swapchain, one to one.
    image_barrier(cmd, composite_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    image_barrier(cmd, swap, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    {
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {int32_t(win.width), int32_t(win.height), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[1] = {int32_t(win.width), int32_t(win.height), 1};
        vkCmdBlitImage(cmd, composite_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_NEAREST);
    }

    if (!screenshot_path_.empty()) {
        const VkDeviceSize bytes = VkDeviceSize(win.width) * win.height * 4;
        if (f.readback.buffer && (f.readback_extent.width != win.width || f.readback_extent.height != win.height))
            destroy_buffer(f.readback);
        if (!f.readback.buffer) f.readback = create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        if (f.readback.buffer) {
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {win.width, win.height, 1};
            vkCmdCopyImageToBuffer(cmd, composite_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.readback.buffer, 1, &copy);
            f.readback_extent = win;
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
