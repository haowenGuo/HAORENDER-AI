#pragma once

#include "rendering/render_backend.h"

#include <QOpenGLFunctions_3_3_Core>

#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

class QOpenGLShaderProgram;

namespace haorendergi {

class DxrRayTracer final : public IRenderBackend, protected QOpenGLFunctions_3_3_Core {
public:
    DxrRayTracer();
    ~DxrRayTracer() override;

    QString backendName() const override;
    bool initialize(QString* error_message) override;
    void shutdown() override;
    void resize(int framebuffer_width, int framebuffer_height) override;
    void uploadScene(const SceneModel& scene) override;
    RenderStats render(const FrameRenderSettings& settings) override;

private:
#ifdef _WIN32
    struct DxrVertex {
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        float nx = 0.0f;
        float ny = 1.0f;
        float nz = 0.0f;
        float tx = 1.0f;
        float ty = 0.0f;
        float tz = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint32_t material_index = 0;
        std::uint32_t padding0 = 0;
    };

    struct DxrMaterial {
        float base_color[4] = { 0.75f, 0.75f, 0.75f, 1.0f };
        float emissive[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float params[4] = { 0.0f, 0.65f, 1.0f, 0.0f };
        float base_color_rect[4] = {};
        float normal_rect[4] = {};
        float metallic_rect[4] = {};
        float roughness_rect[4] = {};
        float ao_rect[4] = {};
        float emissive_rect[4] = {};
    };

    struct DxrTextureAtlases {
        QImage base_color;
        QImage normal;
        QImage metallic;
        QImage roughness;
        QImage ao;
        QImage emissive;
    };

    struct DxrFrameConstants {
        float camera_position[4] = {};
        float camera_forward[4] = {};
        float camera_right[4] = {};
        float camera_up[4] = {};
        float frame_params[4] = {};
        float render_params[4] = {};
        float sun_direction[4] = {};
        float sun_color[4] = {};
        float area_light_corner[4] = {};
        float area_light_edge_u[4] = {};
        float area_light_edge_v[4] = {};
        float area_light_emissive[4] = {};
    };

    bool initializeD3D(QString* error_message);
    bool initializeDxr(QString* error_message);
    bool buildGlDisplay(QString* error_message);
    bool buildRootSignature(QString* error_message);
    bool compileRayLibrary(QString* error_message);
    bool buildStateObject(QString* error_message);
    bool buildAccelerationStructures(const std::vector<DxrVertex>& vertices,
                                     const std::vector<std::uint32_t>& indices,
                                     const std::vector<std::uint32_t>& primitive_materials,
                                     const std::vector<DxrMaterial>& materials,
                                     const DxrTextureAtlases& texture_atlases,
                                     QString* error_message);
    bool buildFallbackAccelerationStructures(QString* error_message);
    bool buildCornellScene(QString* error_message);
    bool rebuildLoadedScene(QString* error_message);
    void flattenScene(const SceneModel& scene,
                      std::vector<DxrVertex>* vertices,
                      std::vector<std::uint32_t>* indices,
                      std::vector<std::uint32_t>* primitive_materials,
                      std::vector<DxrMaterial>* materials,
                      DxrTextureAtlases* texture_atlases) const;
    bool uploadTextureAtlas(const QImage& atlas,
                            const QColor& fallback_color,
                            const QString& label,
                            Microsoft::WRL::ComPtr<ID3D12Resource>* target,
                            QString* error_message);
    bool uploadTextureAtlases(const DxrTextureAtlases& atlases, QString* error_message);
    bool buildOutputResources(QString* error_message);
    bool buildDescriptorHeap(QString* error_message);
    bool buildShaderTable(QString* error_message);
    bool dispatchDxr(const FrameRenderSettings& settings, QString* error_message);
    DxrFrameConstants makeFrameConstants(const FrameRenderSettings& settings) const;
    bool initializeGpuInterop(QString* error_message);
    void destroyGpuInterop();
    bool loadWglInterop(QString* error_message);
    bool drawGpuInteropImage(QString* error_message);
    void destroyGlDisplay();
    void releaseDxrResources();
    void resetAccumulation();
    bool ensureSceneMode(const FrameRenderSettings& settings, QString* error_message);
    std::uint64_t frameSignature(const FrameRenderSettings& settings) const;
    bool resetCommandList(QString* error_message);
    bool executeCommandList(QString* error_message);
    bool waitForGpu(QString* error_message);
    bool createBuffer(std::uint64_t byte_size,
                      D3D12_HEAP_TYPE heap_type,
                      D3D12_RESOURCE_FLAGS flags,
                      D3D12_RESOURCE_STATES initial_state,
                      Microsoft::WRL::ComPtr<ID3D12Resource>* resource,
                      QString* error_message) const;
    void transitionOutput(D3D12_RESOURCE_STATES new_state);
    void transitionSharedOutput(D3D12_RESOURCE_STATES new_state);
    std::uint64_t alignTo(std::uint64_t value, std::uint64_t alignment) const;

    using WglDxOpenDeviceNv = HANDLE(WINAPI*)(void*);
    using WglDxCloseDeviceNv = BOOL(WINAPI*)(HANDLE);
    using WglDxSetResourceShareHandleNv = BOOL(WINAPI*)(void*, HANDLE);
    using WglDxRegisterObjectNv = HANDLE(WINAPI*)(HANDLE, void*, unsigned int, unsigned int, unsigned int);
    using WglDxUnregisterObjectNv = BOOL(WINAPI*)(HANDLE, HANDLE);
    using WglDxLockObjectsNv = BOOL(WINAPI*)(HANDLE, int, HANDLE*);
    using WglDxUnlockObjectsNv = BOOL(WINAPI*)(HANDLE, int, HANDLE*);

    Microsoft::WRL::ComPtr<IDXGIFactory6> dxgi_factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device5> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> command_list_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<IDxcBlob> ray_library_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> global_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12StateObject> state_object_;
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> state_object_properties_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instance_desc_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> output_texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> accumulation_texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> moment_texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shared_output_texture_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d11_shared_texture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
    Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> material_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> primitive_material_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> base_color_atlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> normal_atlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> metallic_atlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> roughness_atlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ao_atlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emissive_atlas_;
    D3D12_RESOURCE_STATES output_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES shared_output_state_ = D3D12_RESOURCE_STATE_COMMON;
    HANDLE shared_output_handle_ = nullptr;
    bool shared_output_handle_is_nt_ = false;
    HANDLE wgl_interop_device_ = nullptr;
    HANDLE wgl_interop_object_ = nullptr;
    WglDxOpenDeviceNv wgl_dx_open_device_nv_ = nullptr;
    WglDxCloseDeviceNv wgl_dx_close_device_nv_ = nullptr;
    WglDxSetResourceShareHandleNv wgl_dx_set_resource_share_handle_nv_ = nullptr;
    WglDxRegisterObjectNv wgl_dx_register_object_nv_ = nullptr;
    WglDxUnregisterObjectNv wgl_dx_unregister_object_nv_ = nullptr;
    WglDxLockObjectsNv wgl_dx_lock_objects_nv_ = nullptr;
    WglDxUnlockObjectsNv wgl_dx_unlock_objects_nv_ = nullptr;
    std::uint64_t fence_value_ = 0;
    void* fence_event_ = nullptr;
    std::uint32_t shader_record_size_ = 0;
    std::uint64_t shader_table_raygen_offset_ = 0;
    std::uint64_t shader_table_miss_offset_ = 0;
    std::uint64_t shader_table_hit_offset_ = 0;
    std::uint64_t shader_table_shadow_hit_offset_ = 0;
    D3D12_RAYTRACING_TIER raytracing_tier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    std::uint32_t scene_vertex_count_ = 0;
    std::uint32_t scene_index_count_ = 0;
    std::uint32_t scene_triangle_count_ = 0;
    std::uint32_t scene_material_count_ = 0;
    std::uint64_t accumulation_frame_index_ = 0;
    std::uint64_t last_frame_signature_ = 0;
    bool scene_buffers_are_cornell_ = false;
    bool has_cached_scene_ = false;
    bool gpu_present_ready_ = false;
    QString gl_vendor_;
    QString gl_renderer_;
    QString dxgi_adapter_name_;
    QString gpu_present_mode_;
    SceneModel cached_scene_;
#endif

    int framebuffer_width_ = 1;
    int framebuffer_height_ = 1;
    bool initialized_ = false;
    bool output_dirty_ = true;
    QString scene_error_;
    float display_denoise_strength_ = 0.25f;
    QOpenGLShaderProgram* display_program_ = nullptr;
    unsigned int fullscreen_vao_ = 0;
    unsigned int gl_frame_texture_ = 0;
};

} // namespace haorendergi
