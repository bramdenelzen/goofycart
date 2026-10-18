#pragma once
#include "mesh.h"

#include <cmath>
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
using TextureId = uint32_t;

struct UiVertex {
    vec2 pos;       // pixels in the internal (low-res) image, origin top-left
    vec2 uv{0.0f};
    uint32_t color; // RGBA8
    uint32_t mode = 0; // 0 flat, 1 SDF text, 2 rounded rectangle (see ui.frag)
    vec4 params{0.0f};
};

struct Camera {
    vec3 position{0, 3, -6};
    vec3 target{0, 0.5f, 0};
    float fov_y_deg = 70.0f;
    float near_z = 0.1f;
    float far_z = 1000.0f;
};

struct SceneLighting {
    vec3 light_dir = glm::normalize(vec3(0.6f, 0.62f, 0.45f)); // towards the light: a mid-morning sun
    float ambient = 0.45f;
    vec3 sky = {0.53f, 0.74f, 0.95f};
    float fog_density = 0.006f;
    bool shadows = true;
    float sun = 1.0f; // strength of the direct light: 1 sun, ~0.35 moon
};

struct RendererOptions {
    bool full_resolution = true;  // false = render low-res and integer-upscale
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

    // A 2D RGBA8 texture with a full mip chain, for loaded models. Texture 0
    // is a 1x1 white, bound to meshes that use the detail array instead.
    TextureId upload_texture(int width, int height, const uint8_t* rgba);
    // `texture` > 0 draws the mesh with that texture (its UVs, no detail
    // array); `two_sided` skips back-face culling for thin leaves and cloth.
    // `unlit` draws the texture as is (shadows, glows).
    // `blended` draws it see-through by alpha, after the opaque meshes and
    // without writing depth (glass, shadows); draws with a tint alpha below
    // 1 go there too.
    // `glow`: the texture's alpha marks self-lit parts (0 = fully lit from within).
    MeshId upload_mesh(const MeshData& mesh, TextureId texture = 0, bool two_sided = false, bool unlit = false, bool blended = false,
                       bool glow = false);

    // Per-frame: begin, queue draws, end. begin_frame returns false when the
    // frame must be skipped (minimised window, swapchain being rebuilt).
    bool begin_frame(const Camera& camera, const SceneLighting& lighting);
    void draw(MeshId mesh, const mat4& model, vec4 tint = vec4(1.0f));
    void end_frame();

    // 2D overlay in ui_extent() pixels, drawn after the 3D scene.
    void ui_rect(float x, float y, float w, float h, uint32_t color);
    void ui_quad(vec2 a, vec2 b, vec2 c, vec2 d, uint32_t color);
    // A rounded rectangle, shaded from `top` to `bottom`; `feather` blurs the
    // edge (soft shadows and glows).
    void ui_round_rect(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom, float feather = 0.0f);
    void ui_round_rect(float x, float y, float w, float h, float radius, uint32_t color) { ui_round_rect(x, y, w, h, radius, color, color); }
    // Text, its cap height about 7 * scale pixels, `y` the top of the capitals.
    // `shadow` gives it a dark outline and a soft drop shadow.
    void ui_text(float x, float y, const char* text, uint32_t color, float scale = 1.0f, bool shadow = true);
    // Full control: outline colour and width in pixels (0 for none), drop
    // shadow, and weight (0 regular, 1 heavy).
    void ui_text_ex(float x, float y, const char* text, uint32_t color, float scale, uint32_t outline, float outline_px,
                    bool shadow, float weight = 0.5f);
    static float ui_text_width(const char* text, float scale = 1.0f);
    // The rounded UI face; without it text falls back to the built-in pixel font.
    bool load_font(const std::string& ttf_path);

    // Saves the next presented frame's low-res image to a PNG file.
    void request_screenshot(std::string path) { screenshot_path_ = std::move(path); }

    void set_full_resolution(bool on);
    bool full_resolution() const { return options_.full_resolution; }
    VkExtent2D internal_extent() const { return offscreen_extent_; }
    // The 2D overlay is laid out in a virtual space kUiHeight pixels high
    // (the width follows the aspect), scaled to the internal image: the HUD
    // keeps its size whatever the resolution.
    static constexpr uint32_t kUiHeight = 360;
    VkExtent2D ui_extent() const {
        const float h = float(std::max<uint32_t>(offscreen_extent_.height, 1));
        return {uint32_t(std::lround(float(offscreen_extent_.width) * float(kUiHeight) / h)), kUiHeight};
    }
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
        TextureId texture = 0;
        bool two_sided = false;
        bool unlit = false;
        bool blended = false;
        bool glow = false;
    };
    struct Texture2D {
        Image image;
        VkDescriptorSet set = VK_NULL_HANDLE;
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
        mat4 light_vp;
        vec4 shadow;
    };
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kShadowSize = 2048;
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
        Image shadow;    // the sun's depth map
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
    bool create_textures();
    bool upload_image_mips(Image& img, const std::vector<std::vector<uint8_t>>& mips, const std::vector<VkExtent2D>& extents,
                           uint32_t layers);

    Image textures_;                   // the texture array (textures.h)
    VkSampler sampler_ = VK_NULL_HANDLE;
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
    bool anisotropy_ = false;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline mesh_pipeline_ = VK_NULL_HANDLE;
    VkPipeline mesh_pipeline_two_sided_ = VK_NULL_HANDLE;
    VkPipeline mesh_pipeline_blend_ = VK_NULL_HANDLE;
    VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
    VkSampler shadow_sampler_ = VK_NULL_HANDLE;
    mat4 light_vp_{1.0f};
    bool create_shadow_pipeline();
    VkDescriptorSetLayout texture_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool texture_pool_ = VK_NULL_HANDLE;
    std::vector<Texture2D> textures2d_;
    VkPipelineLayout ui_layout_ = VK_NULL_HANDLE;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    static constexpr uint32_t kMaxUiVertices = 120000;
    static constexpr uint32_t kMaxTextures = 1024;
    std::vector<UiVertex> ui_vertices_;
    TextureId font_texture_ = 0;
    void ui_glyphs(float x, float y, const char* text, uint32_t color, float em, float edge, float soft);

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
