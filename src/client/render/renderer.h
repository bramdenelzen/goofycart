#pragma once
#include "mesh.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <VkBootstrap.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace gk::render {

using MeshId = uint32_t;

struct UiVertex {
    vec2 pos;       // pixels in the internal (low-res) image, origin top-left
    uint32_t color; // RGBA8
};

struct Camera {
    vec3 position{0, 3, -6};
    vec3 target{0, 0.5f, 0};
    float fov_y_deg = 70.0f;
    float near_z = 0.1f;
    float far_z = 1000.0f;
};

struct SceneLighting {
    vec3 light_dir = glm::normalize(vec3(0.4f, 1.0f, 0.3f)); // towards the light
    float ambient = 0.45f;
    vec3 sky = {0.53f, 0.74f, 0.95f};
    float fog_density = 0.006f;
};

struct RendererOptions {
    bool full_resolution = false; // false = render low-res and integer-upscale
    int target_height = 300;      // internal height is window height / floor(window height / this)
    bool validation = false;
};

// Vulkan 1.3 forward renderer: one low-resolution offscreen colour + depth
// target drawn with dynamic rendering, then blitted to the swapchain with
// nearest filtering. Runs on Metal on macOS through MoltenVK/KosmicKrisp.
class Renderer {
public:
    bool init(SDL_Window* window, const RendererOptions& options);
    void shutdown();

    MeshId upload_mesh(const MeshData& mesh);

    // Per-frame: begin, queue draws, end. begin_frame returns false when the
    // frame must be skipped (minimised window, swapchain being rebuilt).
    bool begin_frame(const Camera& camera, const SceneLighting& lighting);
    void draw(MeshId mesh, const mat4& model, vec4 tint = vec4(1.0f));
    void end_frame();

    // 2D overlay in internal-resolution pixels, drawn after the 3D scene.
    void ui_rect(float x, float y, float w, float h, uint32_t color);
    void ui_quad(vec2 a, vec2 b, vec2 c, vec2 d, uint32_t color);
    // Text in the stb_easy_font face (about 6 x 7 px per glyph at scale 1).
    void ui_text(float x, float y, const char* text, uint32_t color, float scale = 1.0f, bool shadow = true);
    static float ui_text_width(const char* text, float scale = 1.0f);

    // Saves the next presented frame's low-res image to a PNG file.
    void request_screenshot(std::string path) { screenshot_path_ = std::move(path); }

    void set_full_resolution(bool on);
    bool full_resolution() const { return options_.full_resolution; }
    VkExtent2D internal_extent() const { return offscreen_extent_; }
    const std::string& device_name() const { return device_name_; }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        void* mapped = nullptr;
    };
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
    };
    struct GpuMesh {
        Buffer vertices, indices;
        uint32_t index_count = 0;
    };
    struct DrawCmd {
        MeshId mesh;
        mat4 model;
        vec4 tint;
    };
    struct FrameUniforms {
        mat4 view_proj;
        vec4 camera_pos;
        vec4 light_dir;
        vec4 fog_color;
        vec4 sky_color;
    };
    static constexpr uint32_t kFramesInFlight = 2;
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        Buffer uniforms;
        VkDescriptorSet set = VK_NULL_HANDLE;
        Buffer readback; // screenshot target
        Buffer ui;       // per-frame overlay vertices
        std::string readback_path;
        VkExtent2D readback_extent{};
    };

    bool create_swapchain();
    void destroy_swapchain();
    bool create_offscreen();
    void destroy_offscreen();
    bool create_pipeline();
    bool create_ui_pipeline();
    Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible);
    void destroy_buffer(Buffer& b);
    Image create_image(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void destroy_image(Image& img);
    void finish_readback(Frame& f);

    SDL_Window* window_ = nullptr;
    RendererOptions options_;
    std::string device_name_;

    vkb::Instance vkb_instance_;
    vkb::PhysicalDevice vkb_physical_;
    vkb::Device vkb_device_;
    vkb::Swapchain vkb_swapchain_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VmaAllocator allocator_ = nullptr;

    std::vector<VkImage> swap_images_;
    std::vector<VkSemaphore> render_done_; // one per swapchain image
    bool swapchain_dirty_ = false;

    VkFormat color_format_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;
    Image offscreen_color_, offscreen_depth_;
    VkExtent2D offscreen_extent_{};
    uint32_t pixel_scale_ = 1;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline mesh_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_layout_ = VK_NULL_HANDLE;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    static constexpr uint32_t kMaxUiVertices = 120000;
    std::vector<UiVertex> ui_vertices_;

    std::array<Frame, kFramesInFlight> frames_{};
    uint32_t frame_index_ = 0;
    uint32_t image_index_ = 0;
    bool frame_active_ = false;

    std::vector<GpuMesh> meshes_;
    std::vector<DrawCmd> draws_;
    vec3 clear_color_{0.5f, 0.7f, 0.9f};
    std::string screenshot_path_;
};

} // namespace gk::render
