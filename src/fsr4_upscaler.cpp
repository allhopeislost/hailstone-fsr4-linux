// SPDX-License-Identifier: MIT
// AMD FSR 4 as a Hailstone upscaler provider (sdk/hailstone_upscaler.h).
//
// The adapter loads the native fsr4-vk runtime beside it and translates Hailstone's
// frame contract to the FidelityFX API. The runtime is built from the pinned
// ebuwiz/fsr4-vk submodule; no AMD DLL is loaded at run time.
//
// Hailstone supplies linear HDR colour, reverse-Z depth, render-resolution motion
// vectors and an optional reactive mask. FSR runs its own automatic exposure and
// RCAS sharpening. Hailstone's own frame generation remains independent of this
// provider and continues to use the same motion and depth.

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#if !defined(_WIN32)
// The SDK annotates these exports unconditionally. We only look them up by name.
#define __declspec(x)
#endif
#include <ffx_api.h>
#include <ffx_api_types.h>
#include <ffx_upscale.h>
#include <vk/ffx_api_vk.h>
#if !defined(_WIN32)
#undef __declspec
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hailstone_upscaler.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

const HailstoneUpscalerHost* g_host = nullptr;

void say(int level, const std::string& line) noexcept {
    if (!g_host || !g_host->log) return;
    try {
        g_host->log(g_host->host, level, line.c_str());
    } catch (...) {
    }
}

bool ok(ffxReturnCode_t result, const char* what) {
    if (result == FFX_API_RETURN_OK) return true;
    say(HAILSTONE_UPSCALER_LOG_ERROR,
        std::string(what) + " failed (FFX return " + std::to_string(result) + ")");
    return false;
}

#if defined(_WIN32)
constexpr const char* kRuntime = "amd_fidelityfx_upscaler_vk.dll";
#else
constexpr const char* kRuntime = "amd_fidelityfx_upscaler_vk.so";
#endif

void* g_library = nullptr;
PfnFfxCreateContext g_create = nullptr;
PfnFfxDestroyContext g_destroy = nullptr;
PfnFfxQuery g_query = nullptr;
PfnFfxDispatch g_dispatch = nullptr;

void* load_runtime(const std::string& path) {
#if defined(_WIN32)
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? static_cast<std::size_t>(length) : 0, L'\0');
    if (length > 0) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), length);
    return LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* symbol(const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(g_library), name));
#else
    return dlsym(g_library, name);
#endif
}

void set_runtime_log_path(const std::string& path) {
#if defined(_WIN32)
    SetEnvironmentVariableA("FSR4_VK_LOG_PATH", path.c_str());
#else
    setenv("FSR4_VK_LOG_PATH", path.c_str(), 1);
#endif
}

bool load_api() {
    g_library = load_runtime(std::string(g_host->folder) + "/" + kRuntime);
    if (!g_library) {
#if defined(_WIN32)
        const DWORD error = GetLastError();
        say(HAILSTONE_UPSCALER_LOG_ERROR,
            std::string(kRuntime) + " did not load from " + g_host->folder + " (Windows error " +
                std::to_string(error) + ")");
#else
        const char* error = dlerror();
        say(HAILSTONE_UPSCALER_LOG_ERROR,
            std::string(kRuntime) + " did not load from " + g_host->folder +
                (error ? std::string(": ") + error : ""));
#endif
        return false;
    }
    g_create = reinterpret_cast<PfnFfxCreateContext>(symbol("ffxCreateContext"));
    g_destroy = reinterpret_cast<PfnFfxDestroyContext>(symbol("ffxDestroyContext"));
    g_query = reinterpret_cast<PfnFfxQuery>(symbol("ffxQuery"));
    g_dispatch = reinterpret_cast<PfnFfxDispatch>(symbol("ffxDispatch"));
    if (!g_create || !g_destroy || !g_query || !g_dispatch) {
        say(HAILSTONE_UPSCALER_LOG_ERROR, std::string(kRuntime) + " lacks the FFX API functions");
        return false;
    }
    return true;
}

PFN_vkGetInstanceProcAddr gipa() {
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(g_host->get_instance_proc_addr);
}

// Features the runtime can use when the device offers them. The structs stay
// valid until create, as required by Hailstone's provider contract.
struct Wanted {
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR cooperative{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    VkPhysicalDeviceShaderFloat8FeaturesEXT float8{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptors{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
    std::vector<const char*> extensions;
};
Wanted g_wanted;
bool g_prefer_fp8 = false;

bool has_extension(const std::vector<VkExtensionProperties>& all, const char* name) {
    return std::any_of(all.begin(), all.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

int instance_requirements(void*, const char* const** names, uint32_t* count, uint32_t* min_api) noexcept {
    *names = nullptr;
    *count = 0;
    *min_api = VK_MAKE_API_VERSION(0, 1, 3, 0);
    return 1;
}

int device_requirements(void*, void* instance_handle, void* physical_handle, const char* const** names,
                        uint32_t* count, void* features) noexcept {
    try {
        const auto instance = static_cast<VkInstance>(instance_handle);
        const auto physical = static_cast<VkPhysicalDevice>(physical_handle);
        const auto get_props =
            reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa()(instance, "vkGetPhysicalDeviceProperties"));
        const auto get_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            gipa()(instance, "vkGetPhysicalDeviceFeatures2"));
        const auto enum_ext = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            gipa()(instance, "vkEnumerateDeviceExtensionProperties"));
        if (!get_props || !get_features2 || !enum_ext) return 0;

        VkPhysicalDeviceProperties properties{};
        get_props(physical, &properties);
        if (properties.vendorID != 0x1002) {
            say(HAILSTONE_UPSCALER_LOG_INFO,
                std::string("FSR 4 needs an AMD GPU; this is ") + properties.deviceName);
            return 0;
        }

        uint32_t extension_count = 0;
        enum_ext(physical, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available(extension_count);
        if (extension_count) enum_ext(physical, nullptr, &extension_count, available.data());

        g_wanted = Wanted{};
        Wanted& wanted = g_wanted;
        VkPhysicalDeviceVulkan11Features have11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceVulkan12Features have12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features have13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        have.pNext = &have11;
        have11.pNext = &have12;
        have12.pNext = &have13;
        void** tail = &have13.pNext;

        const bool cooperative = has_extension(available, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
        const bool float8 = has_extension(available, VK_EXT_SHADER_FLOAT8_EXTENSION_NAME);
        const bool derivatives = has_extension(available, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
        const bool descriptors = has_extension(available, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        if (cooperative) {
            *tail = &wanted.cooperative;
            tail = &wanted.cooperative.pNext;
        }
        if (float8) {
            *tail = &wanted.float8;
            tail = &wanted.float8.pNext;
        }
        if (derivatives) {
            *tail = &wanted.derivatives;
            tail = &wanted.derivatives.pNext;
        }
        if (descriptors) {
            *tail = &wanted.descriptors;
            tail = &wanted.descriptors.pNext;
        }
        get_features2(physical, &have);

        const std::pair<const char*, VkBool32> shared[] = {
            {"shaderInt16", have.features.shaderInt16},
            {"shaderStorageImageWriteWithoutFormat", have.features.shaderStorageImageWriteWithoutFormat},
            {"storageBuffer16BitAccess", have11.storageBuffer16BitAccess},
            {"shaderFloat16", have12.shaderFloat16},
            {"shaderInt8", have12.shaderInt8},
            {"storageBuffer8BitAccess", have12.storageBuffer8BitAccess},
            {"vulkanMemoryModel", have12.vulkanMemoryModel},
            {"bufferDeviceAddress", have12.bufferDeviceAddress},
            {"runtimeDescriptorArray", have12.runtimeDescriptorArray},
        };
        for (const auto& [name, supported] : shared) {
            if (!supported) {
                say(HAILSTONE_UPSCALER_LOG_INFO, std::string("FSR 4 needs feature ") + name);
                return 0;
            }
        }

        wanted.cooperative.pNext = wanted.float8.pNext = wanted.derivatives.pNext =
            wanted.descriptors.pNext = nullptr;
        void* first = nullptr;
        void** link = &first;
        auto keep = [&](void* structure, void** next, const char* extension) {
            *link = structure;
            link = next;
            wanted.extensions.push_back(extension);
        };
        if (cooperative && wanted.cooperative.cooperativeMatrix) {
            wanted.cooperative.cooperativeMatrixRobustBufferAccess = VK_FALSE;
            keep(&wanted.cooperative, &wanted.cooperative.pNext, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
        }
        if (float8 && wanted.float8.shaderFloat8) {
            keep(&wanted.float8, &wanted.float8.pNext, VK_EXT_SHADER_FLOAT8_EXTENSION_NAME);
        }
        if (derivatives &&
            (wanted.derivatives.computeDerivativeGroupLinear || wanted.derivatives.computeDerivativeGroupQuads)) {
            keep(&wanted.derivatives, &wanted.derivatives.pNext,
                 VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
        }
        if (descriptors && wanted.descriptors.descriptorBuffer) {
            wanted.descriptors = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
            wanted.descriptors.descriptorBuffer = VK_TRUE;
            keep(&wanted.descriptors, &wanted.descriptors.pNext, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        }
        *link = nullptr;

        auto* f2 = static_cast<VkPhysicalDeviceFeatures2*>(features);
        auto* f11 = static_cast<VkPhysicalDeviceVulkan11Features*>(f2->pNext);
        auto* f12 = static_cast<VkPhysicalDeviceVulkan12Features*>(f11->pNext);
        auto* f13 = static_cast<VkPhysicalDeviceVulkan13Features*>(f12->pNext);
        f2->features.shaderInt16 = VK_TRUE;
        f2->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        f11->storageBuffer16BitAccess = VK_TRUE;
        f12->shaderFloat16 = VK_TRUE;
        f12->shaderInt8 = VK_TRUE;
        f12->storageBuffer8BitAccess = VK_TRUE;
        f12->vulkanMemoryModel = VK_TRUE;
        f12->bufferDeviceAddress = VK_TRUE;
        f12->runtimeDescriptorArray = VK_TRUE;
        f12->vulkanMemoryModelDeviceScope = have12.vulkanMemoryModelDeviceScope;
        f13->subgroupSizeControl = have13.subgroupSizeControl;
        f13->computeFullSubgroups = have13.computeFullSubgroups;
        f13->shaderIntegerDotProduct = have13.shaderIntegerDotProduct;
        if (first) {
            auto* last = reinterpret_cast<VkBaseOutStructure*>(f13);
            while (last->pNext) last = last->pNext;
            last->pNext = static_cast<VkBaseOutStructure*>(first);
        }

        g_prefer_fp8 = cooperative && wanted.cooperative.cooperativeMatrix && float8 &&
                       wanted.float8.shaderFloat8 && have13.subgroupSizeControl &&
                       have13.computeFullSubgroups;
        *names = wanted.extensions.data();
        *count = static_cast<uint32_t>(wanted.extensions.size());
        return 1;
    } catch (...) {
        say(HAILSTONE_UPSCALER_LOG_ERROR, "FSR 4 device probe failed");
        return 0;
    }
}

enum class VersionKind { Unknown, Auto, Int8, Fp8 };

VersionKind version_kind(std::string_view name) {
    if (name.find(" FP8") != std::string_view::npos) return VersionKind::Fp8;
    if (name.find(" INT8") != std::string_view::npos) return VersionKind::Int8;
    if (name.find("auto") != std::string_view::npos) return VersionKind::Auto;
    return VersionKind::Unknown;
}

struct VersionChoice {
    uint64_t id;
    VersionKind kind;
    std::string name;
};
std::vector<VersionChoice> g_versions;

constexpr uint64_t kMaximumVersions = 64;

void discover_versions() {
    g_versions.clear();
    ffxQueryDescGetVersions query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    uint64_t count = 0;
    query.outputCount = &count;
    if (!ok(g_query(nullptr, &query.header), "ffxQuery (versions)") || count == 0 || count > kMaximumVersions)
        return;

    std::vector<uint64_t> ids(count);
    std::vector<const char*> names(count);
    query.versionIds = ids.data();
    query.versionNames = names.data();
    if (!ok(g_query(nullptr, &query.header), "ffxQuery (versions)")) return;

    std::string listed;
    for (uint64_t index = 0; index < count; ++index) {
        const std::string name = names[index] ? names[index] : "";
        if (!name.empty()) {
            listed += (listed.empty() ? "" : ", ") + name;
            g_versions.push_back({ids[index], version_kind(name), name});
        }
    }
    say(HAILSTONE_UPSCALER_LOG_INFO, "FSR versions: " + listed);
}

std::vector<const VersionChoice*> ordered_versions() {
    std::vector<const VersionChoice*> ordered;
    auto add_kind = [&](VersionKind kind) {
        for (const auto& version : g_versions)
            if (version.kind == kind) ordered.push_back(&version);
    };
    if (g_prefer_fp8) {
        add_kind(VersionKind::Fp8);
        add_kind(VersionKind::Int8);
    } else {
        add_kind(VersionKind::Int8);
        add_kind(VersionKind::Fp8);
    }
    add_kind(VersionKind::Auto);
    return ordered;
}

VkDevice g_vk_device = VK_NULL_HANDLE;
VkPhysicalDevice g_physical_device = VK_NULL_HANDLE;
VkInstance g_vk_instance = VK_NULL_HANDLE;
PFN_vkGetDeviceProcAddr g_device_proc_addr = nullptr;
PFN_vkGetInstanceProcAddr g_instance_proc_addr = nullptr;
ffxContext g_context = nullptr;
bool g_reactive = false;
std::string g_version_name;

int create(void*, const HailstoneUpscalerDevice* device) noexcept {
    try {
        g_reactive = (device->flags & HAILSTONE_UPSCALER_DEVICE_REACTIVE) != 0;
        g_vk_device = static_cast<VkDevice>(device->device);
        g_physical_device = static_cast<VkPhysicalDevice>(device->physical_device);
        g_vk_instance = static_cast<VkInstance>(device->instance);
        g_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(device->get_device_proc_addr);
        g_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(device->get_instance_proc_addr);
        discover_versions();
        if (g_versions.empty()) {
            say(HAILSTONE_UPSCALER_LOG_INFO, "FSR 4 does not advertise a usable version");
            return 0;
        }
        return 1;
    } catch (...) {
        say(HAILSTONE_UPSCALER_LOG_ERROR, "FSR 4 provider creation failed");
        return 0;
    }
}

void destroy_context() noexcept {
    if (g_context && g_destroy) {
        try {
            g_destroy(&g_context, nullptr);
        } catch (...) {
        }
    }
    g_context = nullptr;
}

void ffx_message(uint32_t type, const wchar_t* message) noexcept {
    if (!message) return;
    try {
        std::string text;
        for (const wchar_t* p = message; *p; ++p) text += *p < 0x80 ? static_cast<char>(*p) : '?';
        say(type == FFX_API_MESSAGE_TYPE_ERROR ? HAILSTONE_UPSCALER_LOG_ERROR : HAILSTONE_UPSCALER_LOG_WARN,
            "FSR: " + text);
    } catch (...) {
    }
}

// fsr4-vk extends the original Vulkan backend descriptor with an optional
// instance block. That block lets the runtime query cooperative-matrix tiles
// through the same loader that created Hailstone's device.
struct FsrVulkanInstanceDesc {
    ffxCreateContextDescHeader header;
    VkInstance instance;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
};
constexpr ffxStructType_t kVulkanInstanceDescType = 4u;

bool create_context(const VersionChoice& version, uint32_t render_width, uint32_t render_height,
                    uint32_t view_width, uint32_t view_height) {
    destroy_context();
    ffxCreateBackendVKDesc_DEPRECATED backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_DEPRECATED;
    backend.vkDevice = g_vk_device;
    backend.vkPhysicalDevice = g_physical_device;
    backend.vkDeviceProcAddr = g_device_proc_addr;

    FsrVulkanInstanceDesc instance{};
    instance.header.type = kVulkanInstanceDescType;
    instance.instance = g_vk_instance;
    instance.getInstanceProcAddr = g_instance_proc_addr;

    ffxOverrideVersion override{};
    override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    override.versionId = version.id;

    ffxCreateContextDescUpscaleVersion api{};
    api.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    api.version = FFX_UPSCALER_VERSION;

    ffxCreateContextDescUpscale desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
                 FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    desc.maxRenderSize = {render_width, render_height};
    desc.maxUpscaleSize = {view_width, view_height};
    desc.fpMessage = ffx_message;
    desc.header.pNext = &backend.header;
    backend.header.pNext = &instance.header;
    instance.header.pNext = &override.header;
    override.header.pNext = &api.header;

    g_version_name = version.name;
    if (!ok(g_create(&g_context, &desc.header, nullptr), "ffxCreateContext (upscale)")) {
        g_context = nullptr;
        return false;
    }
    say(HAILSTONE_UPSCALER_LOG_INFO,
        "FSR " + g_version_name + ": " + std::to_string(render_width) + "x" +
            std::to_string(render_height) + " to " + std::to_string(view_width) + "x" +
            std::to_string(view_height));
    return true;
}

int resize(void*, uint32_t render_width, uint32_t render_height, uint32_t view_width,
           uint32_t view_height) noexcept {
    try {
        for (const VersionChoice* version : ordered_versions()) {
            if (create_context(*version, render_width, render_height, view_width, view_height)) return 1;
        }
        say(HAILSTONE_UPSCALER_LOG_ERROR, "FSR 4 could not create a context for this GPU");
        return 0;
    } catch (...) {
        destroy_context();
        say(HAILSTONE_UPSCALER_LOG_ERROR, "FSR 4 context creation failed");
        return 0;
    }
}

uint32_t surface_format(uint32_t vk_format) {
    switch (static_cast<VkFormat>(vk_format)) {
    case VK_FORMAT_R16G16B16A16_SFLOAT: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case VK_FORMAT_R8_UNORM: return FFX_API_SURFACE_FORMAT_R8_UNORM;
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return FFX_API_SURFACE_FORMAT_R32_FLOAT;
    case VK_FORMAT_D24_UNORM_S8_UINT: return FFX_API_SURFACE_FORMAT_R32_UINT;
    default: return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

FfxApiResource resource_of(const HailstoneUpscalerImage& image, bool output) {
    FfxApiResource resource{};
    if (!image.image) return resource;
    resource.resource = reinterpret_cast<void*>(static_cast<uintptr_t>(image.image));
    resource.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    resource.description.format = surface_format(image.format);
    resource.description.width = image.width;
    resource.description.height = image.height;
    resource.description.depth = 1;
    resource.description.mipCount = 1;
    resource.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
    resource.description.usage = output ? FFX_API_RESOURCE_USAGE_UAV
                           : (image.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? FFX_API_RESOURCE_USAGE_DEPTHTARGET
                                                                       : FFX_API_RESOURCE_USAGE_READ_ONLY;
    resource.state = output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ;
    return resource;
}

int evaluate(void*, const HailstoneUpscalerFrame* frame) noexcept {
    if (!g_context) return 0;
    try {
        ffxDispatchDescUpscale dispatch{};
        dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dispatch.commandList = frame->command_buffer;
        dispatch.color = resource_of(frame->color, false);
        dispatch.depth = resource_of(frame->depth, false);
        dispatch.motionVectors = resource_of(frame->motion, false);
        if (g_reactive && frame->reactive.image) dispatch.reactive = resource_of(frame->reactive, false);
        dispatch.output = resource_of(frame->output, true);
        dispatch.jitterOffset = {frame->jitter_x, frame->jitter_y};
        dispatch.motionVectorScale = {frame->motion_scale_x, frame->motion_scale_y};
        dispatch.renderSize = {frame->color.width, frame->color.height};
        dispatch.upscaleSize = {frame->output.width, frame->output.height};
        dispatch.enableSharpening = frame->sharpness > 0.0f;
        dispatch.sharpness = frame->sharpness;
        dispatch.frameTimeDelta = frame->frame_seconds * 1000.0f;
        dispatch.preExposure = 1.0f;
        dispatch.reset = (frame->flags & HAILSTONE_UPSCALER_FRAME_RESET) != 0;
        dispatch.cameraNear = frame->near_plane;
        dispatch.cameraFar = frame->far_plane;
        dispatch.cameraFovAngleVertical = frame->fov_y;
        dispatch.viewSpaceToMetersFactor = 1.0f;
        return ok(g_dispatch(&g_context, &dispatch.header), "ffxDispatch (upscale)") ? 1 : 0;
    } catch (...) {
        say(HAILSTONE_UPSCALER_LOG_ERROR, "FSR 4 dispatch failed");
        return 0;
    }
}

void destroy(void*) noexcept { destroy_context(); }

const HailstoneUpscaler g_table = {
    HAILSTONE_UPSCALER_ABI,
    sizeof(HailstoneUpscaler),
    "AMD FSR 4",
    HAILSTONE_UPSCALER_SHARPENS,
    0,
    instance_requirements,
    device_requirements,
    create,
    resize,
    evaluate,
    destroy,
};

} // namespace

HAILSTONE_UPSCALER_API const HailstoneUpscaler* hailstone_upscaler(const HailstoneUpscalerHost* host) noexcept {
    if (!host || host->abi < 1 || host->size < sizeof(HailstoneUpscalerHost) || !host->folder) return nullptr;
    g_host = host;
    try {
        if (!g_library && !load_api()) return nullptr;
        if (host->config_dir && *host->config_dir)
            set_runtime_log_path(std::string(host->config_dir) + "/fsr4-runtime.log");
        return &g_table;
    } catch (...) {
        say(HAILSTONE_UPSCALER_LOG_ERROR, "FSR 4 provider initialisation failed");
        return nullptr;
    }
}
