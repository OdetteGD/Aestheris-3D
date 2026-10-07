#include "VulkanRenderer.h"
#include "engine/assets/ObjMeshLoader.h"
#include "engine/core/Std140.h"
#include "engine/shader/ShaderResourceManager.h"
#include "engine/world/DemoWorldInitializer.h"
#include <android/native_window.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <filesystem>
#include <cmath>
#include <cstdint>
#include "engine/core/AetherisLog.h"

namespace aetheris {

namespace {
struct alignas(16) PostPushConstants final {
    float exposure{1.15f};
    float invWidth{};
    float invHeight{};
    float bloomStrength{0.22f};
};

struct DemoMaterial final {
    Vec4 color{};
    float metallic{};
    float roughness{};
    float ao{};
};

constexpr std::array<DemoMaterial, kMaxDemoMeshes> kMaterials = {{
    {{0.18f, 0.28f, 0.42f, 1.0f}, 0.00f, 0.86f, 1.00f},
    {{0.22f, 0.28f, 0.36f, 1.0f}, 0.08f, 0.68f, 1.00f},
    {{0.16f, 0.36f, 0.52f, 1.0f}, 0.02f, 0.54f, 1.00f},
    {{0.28f, 0.40f, 0.52f, 1.0f}, 0.00f, 0.74f, 1.00f},
    {{0.60f, 0.27f, 0.07f, 1.0f}, 0.00f, 0.62f, 1.00f},
    {{0.34f, 0.38f, 0.44f, 1.0f}, 0.00f, 0.70f, 1.00f},
    {{0.22f, 0.26f, 0.32f, 1.0f}, 0.00f, 0.80f, 1.00f}
}};

#if defined(NDEBUG)
constexpr bool kValidation = false;
#else
constexpr bool kValidation = true;
#endif
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
}

bool VulkanRenderer::Initialize(ANativeWindow* w) {
    if (initialized_ || !w) return false;
    AETHERIS_VK_LOGI("Initialize: native window=%p", static_cast<void*>(w));
    window_ = w;
    ANativeWindow_acquire(window_);

    if (!CreateInstance() ||
        !CreateSurface() ||
        !PickGPU() ||
        !CreateDevice() ||
        !resources_.Initialize(
            gpu_,
            device_,
            projectRoot_.empty()
                ? std::filesystem::path("cache/pipelines/aetheris_vk.bin")
                : projectRoot_ / "cache/pipelines/aetheris_vk.bin") ||
        !CreateFrames() ||
        !CreateFrameUniformBuffer() ||
        !CreateShaderModules() ||
        !CreateDescriptorLayouts() ||
        !CreateDefaultIBL() ||
        !CreateDemoMeshes() ||
        !CreateSwapchain() ||
        !CreateGBufferAttachments() ||
        !CreateHDRTarget() ||
        !CreatePasses() ||
        !CreateViews() ||
        !CreateFramebuffers() ||
        !CreateDescriptorPoolAndSets() ||
        !CreatePipelines()) {
        AETHERIS_VK_LOGE("Vulkan initialization failed");
        Shutdown();
        return false;
    }

    initialized_ = true;
    UpdateCamera();
    AETHERIS_VK_LOGI("Initialize complete: %ux%u, swapchain=%zu",
                      extent_.width, extent_.height, images_.size());
    return true;
}

bool VulkanRenderer::CreateInstance() {
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> e(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, e.data());

    std::vector<const char*> x = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    if (kValidation) x.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    for (auto name : x) {
        bool ok = std::any_of(
            e.begin(),
            e.end(),
            [&](const auto& a) { return std::strcmp(a.extensionName, name) == 0; });
        if (!ok) return false;
    }

    uint32_t supportedApi = VK_API_VERSION_1_1;
    PFN_vkEnumerateInstanceVersion enumerateVersion =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerateVersion) {
        uint32_t runtimeApi = VK_API_VERSION_1_0;
        if (enumerateVersion(&runtimeApi) == VK_SUCCESS) {
            supportedApi = std::min(runtimeApi, VK_API_VERSION_1_2);
        }
    }

    if (supportedApi < VK_API_VERSION_1_1) return false;

    VkApplicationInfo a{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    a.pApplicationName = "Aetheris";
    a.pEngineName = "Aetheris";
    a.apiVersion = supportedApi;

    AETHERIS_VK_LOGI("Creating Vulkan instance (validation=%d, api=%u.%u)",
                      kValidation ? 1 : 0, VK_VERSION_MAJOR(supportedApi), VK_VERSION_MINOR(supportedApi));

    VkInstanceCreateInfo c{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    c.pApplicationInfo = &a;
    c.enabledExtensionCount = static_cast<uint32_t>(x.size());
    c.ppEnabledExtensionNames = x.data();

    if (kValidation) {
        c.enabledLayerCount = 1;
        c.ppEnabledLayerNames = &kValidationLayer;
    }
    const VkResult result = vkCreateInstance(&c, nullptr, &instance_);
    if (result != VK_SUCCESS) AETHERIS_VK_LOGE("vkCreateInstance failed: %s", VkResultName(result));
    return result == VK_SUCCESS;
}

bool VulkanRenderer::CreateSurface() {
    VkAndroidSurfaceCreateInfoKHR c{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    c.window = window_;
    const VkResult result = vkCreateAndroidSurfaceKHR(instance_, &c, nullptr, &surface_);
    if (result != VK_SUCCESS) AETHERIS_VK_LOGE("vkCreateAndroidSurfaceKHR failed: %s", VkResultName(result));
    else AETHERIS_VK_LOGI("Android Vulkan surface created: 0x%llx", static_cast<unsigned long long>(VkHandleValue(surface_)));
    return result == VK_SUCCESS;
}

bool VulkanRenderer::PickGPU() {
    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(instance_, &n, nullptr) != VK_SUCCESS || !n) return false;

    std::vector<VkPhysicalDevice> d(n);
    vkEnumeratePhysicalDevices(instance_, &n, d.data());

    int best = -1;
    for (auto g : d) {
        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(g, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> ex(en);
        vkEnumerateDeviceExtensionProperties(g, nullptr, &en, ex.data());

        if (!std::any_of(ex.begin(), ex.end(), [](const auto& a) {
                return std::strcmp(a.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            })) {
            continue;
        }

        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> q(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, q.data());

        uint32_t family = UINT32_MAX;
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 p = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g, i, surface_, &p);
            if (p && (q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                family = i;
                break;
            }
        }

        if (family == UINT32_MAX) continue;

        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(g, &p);

        int score =
            (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500 : 250) +
            (p.apiVersion >= VK_API_VERSION_1_2 ? 100 : 0);

        uint32_t f = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g, surface_, &f, nullptr);
        score += f ? 100 : 0;

        if (score > best) {
            best = score;
            gpu_ = g;
            family_ = family;
        }
    }
    return best >= 0;
}

bool VulkanRenderer::CreateDevice() {
    float priority = 1.0f;

    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family_;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;

    const char* ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceFeatures f{};
    VkDeviceCreateInfo c{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    c.pQueueCreateInfos = &q;
    c.queueCreateInfoCount = 1;
    c.enabledExtensionCount = 1;
    c.ppEnabledExtensionNames = ext;
    c.pEnabledFeatures = &f;

    const VkResult result = vkCreateDevice(gpu_, &c, nullptr, &device_);
    if (result != VK_SUCCESS) {
        AETHERIS_VK_LOGE("vkCreateDevice failed: %s", VkResultName(result));
        return false;
    }
    vkGetDeviceQueue(device_, family_, 0, &queue_);
    AETHERIS_VK_LOGI("Logical device ready: queueFamily=%u", family_);
    return true;
}

bool VulkanRenderer::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR cap{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &cap) != VK_SUCCESS) return false;
    if (cap.currentExtent.width == 0 || cap.currentExtent.height == 0) return false;

    uint32_t fn = 0;
    uint32_t mn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &fn, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &mn, nullptr);

    if (fn == 0 || mn == 0) return false;

    std::vector<VkSurfaceFormatKHR> fs(fn);
    std::vector<VkPresentModeKHR> ms(mn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &fn, fs.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &mn, ms.data());

    VkSurfaceFormatKHR fmt = fs.front();
    for (const auto& f : fs) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            fmt = f;
            break;
        }
    }

    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : ms) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            mode = m;
            break;
        }
    }

    uint32_t count = std::max(3u, cap.minImageCount);
    if (cap.maxImageCount) count = std::min(count, cap.maxImageCount);

    VkSwapchainCreateInfoKHR c{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    c.surface = surface_;
    c.minImageCount = count;
    c.imageFormat = fmt.format;
    c.imageColorSpace = fmt.colorSpace;
    c.imageExtent = cap.currentExtent;
    c.imageArrayLayers = 1;
    c.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    c.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    c.preTransform = cap.currentTransform;
    if (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        c.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        c.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    } else if (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        c.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else {
        c.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    if ((cap.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        AETHERIS_VK_LOGE("Surface does not support color-attachment swapchain images");
        return false;
    }
    c.presentMode = mode;
    c.clipped = VK_TRUE;

    const VkResult result = vkCreateSwapchainKHR(device_, &c, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        AETHERIS_VK_LOGE("vkCreateSwapchainKHR failed: %s", VkResultName(result));
        return false;
    }

    format_ = fmt.format;
    extent_ = cap.currentExtent;

    uint32_t in = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &in, nullptr);
    images_.resize(in);
    const VkResult imageResult = vkGetSwapchainImagesKHR(device_, swapchain_, &in, images_.data());
    if (imageResult == VK_SUCCESS) {
        AETHERIS_VK_LOGI("Swapchain created: %ux%u images=%u format=%d presentMode=%d",
                          extent_.width, extent_.height, in, static_cast<int>(format_), static_cast<int>(mode));
    } else {
        AETHERIS_VK_LOGE("vkGetSwapchainImagesKHR failed: %s", VkResultName(imageResult));
    }
    return imageResult == VK_SUCCESS;
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const noexcept {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(gpu_, &memory);
    for (uint32_t i=0;i<memory.memoryTypeCount;++i)
        if((typeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&properties)==properties)return i;
    return UINT32_MAX;
}
bool VulkanRenderer::CreateAttachmentImage(VkFormat format,VkImageUsageFlags usage,VkImage& image,VkDeviceMemory& memory,VkImageView& view,VkImageAspectFlags aspect){
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={extent_.width,extent_.height,1};ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;ci.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(device_,&ci,nullptr,&image)!=VK_SUCCESS)return false;VkMemoryRequirements req{};vkGetImageMemoryRequirements(device_,image,&req);
    uint32_t mt=FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);if(mt==UINT32_MAX)mt=FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(mt==UINT32_MAX){vkDestroyImage(device_,image,nullptr);image={};return false;}VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=mt;
    if(vkAllocateMemory(device_,&ai,nullptr,&memory)!=VK_SUCCESS){vkDestroyImage(device_,image,nullptr);image={};return false;}if(vkBindImageMemory(device_,image,memory,0)!=VK_SUCCESS){vkFreeMemory(device_,memory,nullptr);vkDestroyImage(device_,image,nullptr);memory={};image={};return false;}
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={aspect,0,1,0,1};if(vkCreateImageView(device_,&vi,nullptr,&view)!=VK_SUCCESS){vkFreeMemory(device_,memory,nullptr);vkDestroyImage(device_,image,nullptr);memory={};image={};return false;}return true;
}
bool VulkanRenderer::CreateGBufferAttachments(){
    const VkImageUsageFlags u=VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if(!CreateAttachmentImage(VK_FORMAT_R16G16B16A16_SFLOAT,u,gbufferImages_[0],gbufferMemory_[0],gbufferViews_[0],VK_IMAGE_ASPECT_COLOR_BIT)||
       !CreateAttachmentImage(VK_FORMAT_A2B10G10R10_UNORM_PACK32,u,gbufferImages_[1],gbufferMemory_[1],gbufferViews_[1],VK_IMAGE_ASPECT_COLOR_BIT)||
       !CreateAttachmentImage(VK_FORMAT_R8G8B8A8_UNORM,u,gbufferImages_[2],gbufferMemory_[2],gbufferViews_[2],VK_IMAGE_ASPECT_COLOR_BIT)){DestroyGBufferAttachments();return false;}
    const VkFormat candidates[]={VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D16_UNORM};depthFormat_=VK_FORMAT_UNDEFINED;
    for(VkFormat f:candidates){VkFormatProperties p{};vkGetPhysicalDeviceFormatProperties(gpu_,f,&p);if(p.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT){depthFormat_=f;break;}}
    if(depthFormat_==VK_FORMAT_UNDEFINED){DestroyGBufferAttachments();return false;}const VkImageUsageFlags du=VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(!CreateAttachmentImage(depthFormat_,du,depthImage_,depthMemory_,depthView_,VK_IMAGE_ASPECT_DEPTH_BIT)){DestroyGBufferAttachments();return false;}return true;
}
void VulkanRenderer::DestroyGBufferAttachments() noexcept{if(!device_)return;for(size_t i=0;i<3;++i){if(gbufferViews_[i])vkDestroyImageView(device_,gbufferViews_[i],nullptr);if(gbufferImages_[i])vkDestroyImage(device_,gbufferImages_[i],nullptr);if(gbufferMemory_[i])vkFreeMemory(device_,gbufferMemory_[i],nullptr);gbufferViews_[i]={};gbufferImages_[i]={};gbufferMemory_[i]={};}if(depthView_)vkDestroyImageView(device_,depthView_,nullptr);if(depthImage_)vkDestroyImage(device_,depthImage_,nullptr);if(depthMemory_)vkFreeMemory(device_,depthMemory_,nullptr);depthView_={};depthImage_={};depthMemory_={};}
bool VulkanRenderer::CreatePasses() {
    std::array<VkAttachmentDescription, 5> attachments{};

    constexpr VkFormat gFormats[3] = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_UNORM
    };

    for (uint32_t i = 0; i < 3; ++i) {
        attachments[i].format = gFormats[i];
        attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    attachments[3].format = depthFormat_;
    attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachments[4].format = hdrFormat_;
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const std::array<VkAttachmentReference, 3> colors = {{
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    }};
    const VkAttachmentReference depth{
        3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };
    const std::array<VkAttachmentReference, 3> inputs = {{
        {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    }};
    const VkAttachmentReference hdr{
        4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription geometry{};
    geometry.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    geometry.colorAttachmentCount = 3;
    geometry.pColorAttachments = colors.data();
    geometry.pDepthStencilAttachment = &depth;

    VkSubpassDescription lighting{};
    lighting.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    lighting.inputAttachmentCount = 3;
    lighting.pInputAttachments = inputs.data();
    lighting.colorAttachmentCount = 1;
    lighting.pColorAttachments = &hdr;

    const std::array<VkSubpassDependency, 3> deps = {{
        {
            VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
        },
        {
            0, 1,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
        },
        {
            1, VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
        }
    }};

    const std::array<VkSubpassDescription, 2> subpasses = {{
        geometry, lighting
    }};

    VkRenderPassCreateInfo createInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
    };
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    createInfo.pSubpasses = subpasses.data();
    createInfo.dependencyCount = static_cast<uint32_t>(deps.size());
    createInfo.pDependencies = deps.data();

    if (vkCreateRenderPass(
            device_, &createInfo, nullptr, &pass_) != VK_SUCCESS) {
        return false;
    }

    VkAttachmentDescription postAttachment{};
    postAttachment.format = format_;
    postAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    postAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    postAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    postAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    postAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    const VkAttachmentReference postColor{
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription postSubpass{};
    postSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    postSubpass.colorAttachmentCount = 1;
    postSubpass.pColorAttachments = &postColor;

    const std::array<VkSubpassDependency, 2> postDeps = {{
        {
            VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
        },
        {
            0, VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
        }
    }};

    VkRenderPassCreateInfo postInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
    };
    postInfo.attachmentCount = 1;
    postInfo.pAttachments = &postAttachment;
    postInfo.subpassCount = 1;
    postInfo.pSubpasses = &postSubpass;
    postInfo.dependencyCount = static_cast<uint32_t>(postDeps.size());
    postInfo.pDependencies = postDeps.data();

    if (vkCreateRenderPass(
            device_, &postInfo, nullptr, &postPass_) != VK_SUCCESS) {
        vkDestroyRenderPass(device_, pass_, nullptr);
        pass_ = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanRenderer::CreateViews() {
    views_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        c.image = images_[i];
        c.viewType = VK_IMAGE_VIEW_TYPE_2D;
        c.format = format_;
        c.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        c.subresourceRange.levelCount = 1;
        c.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &c, nullptr, &views_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

bool VulkanRenderer::CreateFramebuffers() {
    framebuffers_.resize(views_.size());
    postFramebuffers_.resize(views_.size());

    for (size_t i = 0; i < views_.size(); ++i) {
        const std::array<VkImageView, 5> attachments = {
            gbufferViews_[0],
            gbufferViews_[1],
            gbufferViews_[2],
            depthView_,
            hdrView_
        };

        VkFramebufferCreateInfo mainInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        mainInfo.renderPass = pass_;
        mainInfo.attachmentCount = 5;
        mainInfo.pAttachments = attachments.data();
        mainInfo.width = extent_.width;
        mainInfo.height = extent_.height;
        mainInfo.layers = 1;

        if (vkCreateFramebuffer(
                device_, &mainInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            return false;
        }

        VkFramebufferCreateInfo postInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        postInfo.renderPass = postPass_;
        postInfo.attachmentCount = 1;
        postInfo.pAttachments = &views_[i];
        postInfo.width = extent_.width;
        postInfo.height = extent_.height;
        postInfo.layers = 1;

        if (vkCreateFramebuffer(
                device_, &postInfo, nullptr, &postFramebuffers_[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool VulkanRenderer::CreateFrames() {
    for (auto& f : frames_) {
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        p.queueFamilyIndex = family_;
        if (vkCreateCommandPool(device_, &p, nullptr, &f.pool) != VK_SUCCESS) return false;

        VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        a.commandPool = f.pool;
        a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_, &a, &f.cmd) != VK_SUCCESS) return false;

        VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device_, &s, nullptr, &f.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &s, nullptr, &f.renderFinished) != VK_SUCCESS) {
            return false;
        }

        VkFenceCreateInfo z{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        z.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(device_, &z, nullptr, &f.fence) != VK_SUCCESS) return false;
    }
    return true;
}



bool VulkanRenderer::BeginFrame() {
    if (!initialized_ || begun_ || !swapchain_ || !surface_) return false;

    Frame& frame = frames_[frame_];

    if (vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;

    const VkResult acquire = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &image_);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR || acquire == VK_SUBOPTIMAL_KHR) {
        AETHERIS_VK_LOGW("Acquire returned %s; rebuilding swapchain", VkResultName(acquire));
        RecreateSwapchain(nullptr);
        return false;
    }

    if (acquire != VK_SUCCESS) {
        AETHERIS_VK_LOGE("vkAcquireNextImageKHR failed: %s", VkResultName(acquire));
        return false;
    }

    vkResetFences(device_, 1, &frame.fence);
    vkResetCommandPool(device_, frame.pool, 0);

    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };

    if (vkBeginCommandBuffer(frame.cmd, &beginInfo) != VK_SUCCESS)
        return false;

    UpdateCamera();

    std::array<VkClearValue, 5> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clears[1].color = {{0.5f, 0.5f, 1.0f, 0.55f}};
    clears[2].color = {{0.10f, 0.13f, 0.18f, 1.0f}};
    clears[3].depthStencil = {1.0f, 0};
    clears[4].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo renderPassBegin{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
    };

    renderPassBegin.renderPass = pass_;
    renderPassBegin.framebuffer = framebuffers_[image_];
    renderPassBegin.renderArea.extent = extent_;
    renderPassBegin.clearValueCount = static_cast<uint32_t>(clears.size());
    renderPassBegin.pClearValues = clears.data();

    vkCmdBeginRenderPass(
        frame.cmd,
        &renderPassBegin,
        VK_SUBPASS_CONTENTS_INLINE
    );

    VkViewport viewport{
        0.0f, 0.0f,
        static_cast<float>(extent_.width),
        static_cast<float>(extent_.height),
        0.0f, 1.0f
    };

    VkRect2D scissor{{0, 0}, extent_};

    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);

    begun_ = true;
    mainRenderPassActive_ = true;
    frameRecorded_ = false;

    return true;
}

void VulkanRenderer::DrawRenderQueue(
    const RenderQueue& queue,
    std::span<const Transform> transforms
) {
    if (!begun_ || !mainRenderPassActive_ || frameRecorded_)
        return;

    if (!UpdateFrameUniforms())
        return;

    Frame& frame = frames_[frame_];

    vkCmdBindPipeline(
        frame.cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        geometryPipeline_
    );

    uint32_t boundMesh = UINT32_MAX;

    for (const RenderItem& item : queue.Items()) {
        if (item.meshId >= kMaxDemoMeshes ||
            item.transformIndex >= transforms.size()) {
            continue;
        }

        const MeshGpu& mesh = demoMeshes_[item.meshId];

        if (!mesh.vertex.buffer ||
            !mesh.index.buffer ||
            mesh.indexCount == 0) {
            continue;
        }

        if (boundMesh != item.meshId) {
            const VkBuffer vertexBuffer = mesh.vertex.buffer;
            const VkDeviceSize offset = 0;

            vkCmdBindVertexBuffers(
                frame.cmd, 0, 1, &vertexBuffer, &offset);

            vkCmdBindIndexBuffer(
                frame.cmd,
                mesh.index.buffer,
                0,
                VK_INDEX_TYPE_UINT32
            );

            boundMesh = item.meshId;
        }

        struct alignas(16) GeometryPush final {
            Mat4 viewProj{};
            Mat4 model{};
        };

        static_assert(sizeof(GeometryPush) == 128);

        const Transform& transform =
            transforms[item.transformIndex];

        GeometryPush constants{};
        constants.viewProj = viewProj_;
        constants.model = MakeModel(transform);

        vkCmdPushConstants(
            frame.cmd,
            geometryLayout_,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(constants),
            &constants
        );

        vkCmdDrawIndexed(
            frame.cmd,
            mesh.indexCount,
            1,
            0,
            0,
            0
        );
    }

    vkCmdNextSubpass(
        frame.cmd,
        VK_SUBPASS_CONTENTS_INLINE
    );

    vkCmdBindPipeline(
        frame.cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        lightingPipeline_
    );

    const uint32_t dynamicOffset =
        static_cast<uint32_t>(frame_ * frameUboStride_);

    vkCmdBindDescriptorSets(
        frame.cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        lightingLayout_,
        0,
        1,
        &lightingInputSet_,
        0,
        nullptr
    );

    vkCmdBindDescriptorSets(
        frame.cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        lightingLayout_,
        1,
        1,
        &lightingFrameSet_,
        1,
        &dynamicOffset
    );

    // Full-screen triangle invokes deferred_lighting_mobile.frag.
    vkCmdDraw(frame.cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(frame.cmd);
    mainRenderPassActive_ = false;

    VkImageMemoryBarrier hdrBarrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
    };

    hdrBarrier.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    hdrBarrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;
    hdrBarrier.oldLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrBarrier.newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrBarrier.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.image = hdrImage_;
    hdrBarrier.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, 1, 0, 1
    };

    vkCmdPipelineBarrier(
        frame.cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &hdrBarrier
    );

    VkClearValue postClear{};
    postClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo postBegin{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
    };

    postBegin.renderPass = postPass_;
    postBegin.framebuffer = postFramebuffers_[image_];
    postBegin.renderArea.extent = extent_;
    postBegin.clearValueCount = 1;
    postBegin.pClearValues = &postClear;

    vkCmdBeginRenderPass(
        frame.cmd,
        &postBegin,
        VK_SUBPASS_CONTENTS_INLINE
    );

    VkViewport viewport{
        0.0f, 0.0f,
        static_cast<float>(extent_.width),
        static_cast<float>(extent_.height),
        0.0f, 1.0f
    };

    VkRect2D scissor{{0, 0}, extent_};

    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);

    vkCmdBindPipeline(
        frame.cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        postPipeline_
    );

    vkCmdBindDescriptorSets(
        frame.cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        postLayout_,
        0,
        1,
        &postSet_,
        0,
        nullptr
    );

    const PostPushConstants post{
        1.15f,
        1.0f / std::max(1.0f, static_cast<float>(extent_.width)),
        1.0f / std::max(1.0f, static_cast<float>(extent_.height)),
        0.22f
    };

    vkCmdPushConstants(
        frame.cmd,
        postLayout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(post),
        &post
    );

    vkCmdDraw(frame.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(frame.cmd);

    frameRecorded_ = true;
}

void VulkanRenderer::EndFrame() {
    if (!begun_) return;

    Frame& frame = frames_[frame_];

    if (mainRenderPassActive_) {
        vkCmdEndRenderPass(frame.cmd);
        mainRenderPassActive_ = false;
    }

    if (vkEndCommandBuffer(frame.cmd) != VK_SUCCESS) {
        begun_ = false;
        frameRecorded_ = false;
        return;
    }

    const VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO
    };

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frame.renderFinished;

    const VkResult submit =
        vkQueueSubmit(
            queue_,
            1,
            &submitInfo,
            frame.fence
        );

    if (submit != VK_SUCCESS) {
        AETHERIS_VK_LOGE(
            "vkQueueSubmit failed: %s",
            VkResultName(submit)
        );
        begun_ = false;
        frameRecorded_ = false;
        return;
    }

    VkPresentInfoKHR presentInfo{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
    };

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &image_;

    const VkResult present =
        vkQueuePresentKHR(
            queue_,
            &presentInfo
        );

    if (present == VK_ERROR_OUT_OF_DATE_KHR ||
        present == VK_SUBOPTIMAL_KHR) {
        AETHERIS_VK_LOGW(
            "Present returned %s; rebuilding swapchain",
            VkResultName(present)
        );
        RecreateSwapchain(nullptr);
    } else if (present != VK_SUCCESS) {
        AETHERIS_VK_LOGE(
            "vkQueuePresentKHR failed: %s",
            VkResultName(present)
        );
    }

    frame_ = (frame_ + 1) % Frames;
    begun_ = false;
    frameRecorded_ = false;
}

void VulkanRenderer::DestroySwapchain() noexcept {
    if (!device_) return;

    vkDeviceWaitIdle(device_);

    DestroyPipelines();
    DestroyDescriptors();

    for (VkFramebuffer fb : postFramebuffers_)
        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);

    for (VkFramebuffer fb : framebuffers_)
        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);

    if (postPass_)
        vkDestroyRenderPass(device_, postPass_, nullptr);

    if (pass_)
        vkDestroyRenderPass(device_, pass_, nullptr);

    if (hdrView_)
        vkDestroyImageView(device_, hdrView_, nullptr);

    if (hdrImage_)
        vkDestroyImage(device_, hdrImage_, nullptr);

    if (hdrMemory_)
        vkFreeMemory(device_, hdrMemory_, nullptr);

    hdrView_ = VK_NULL_HANDLE;
    hdrImage_ = VK_NULL_HANDLE;
    hdrMemory_ = VK_NULL_HANDLE;

    DestroyGBufferAttachments();

    for (VkImageView view : views_)
        if (view) vkDestroyImageView(device_, view, nullptr);

    if (swapchain_)
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);

    postPass_ = VK_NULL_HANDLE;
    pass_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;

    framebuffers_.clear();
    postFramebuffers_.clear();
    views_.clear();
    images_.clear();

    image_ = UINT32_MAX;
}

bool VulkanRenderer::RecreateSwapchain(ANativeWindow* w) {
    if (!device_) return false;

    AETHERIS_VK_LOGI(
        "RecreateSwapchain begin: new=%p old=%p",
        static_cast<void*>(w),
        static_cast<void*>(window_)
    );

    vkDeviceWaitIdle(device_);
    DestroySwapchain();

    if (w && (w != window_ || !surface_)) {
        if (surface_) {
            vkDestroySurfaceKHR(
                instance_,
                surface_,
                nullptr
            );
            surface_ = VK_NULL_HANDLE;
        }

        if (window_) {
            ANativeWindow_release(window_);
            window_ = nullptr;
        }

        window_ = w;
        ANativeWindow_acquire(window_);

        if (!CreateSurface())
            return false;
    }

    if (!surface_)
        return false;

    const bool ok =
        CreateSwapchain() &&
        CreateGBufferAttachments() &&
        CreateHDRTarget() &&
        CreatePasses() &&
        CreateViews() &&
        CreateFramebuffers() &&
        CreateDescriptorPoolAndSets() &&
        CreatePipelines();

    if (ok)
        UpdateCamera();

    AETHERIS_VK_LOGI(
        "RecreateSwapchain complete: result=%d extent=%ux%u",
        ok ? 1 : 0,
        extent_.width,
        extent_.height
    );

    return ok;
}

void VulkanRenderer::ReleaseSurface() noexcept {
    if (!device_) return;

    AETHERIS_VK_LOGI("ReleaseSurface begin");

    begun_ = false;
    mainRenderPassActive_ = false;
    frameRecorded_ = false;

    DestroySwapchain();

    if (surface_) {
        vkDestroySurfaceKHR(
            instance_,
            surface_,
            nullptr
        );
        surface_ = VK_NULL_HANDLE;
    }

    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }

    AETHERIS_VK_LOGI("ReleaseSurface complete");
}

void VulkanRenderer::Shutdown() noexcept {
    if (device_) {
        vkDeviceWaitIdle(device_);

        resources_.SavePipelineCache(
            projectRoot_.empty()
                ? std::filesystem::path("cache/pipelines/aetheris_vk.bin")
                : projectRoot_ / "cache/pipelines/aetheris_vk.bin"
        );

        DestroySwapchain();
        DestroyDemoMeshes();

        if (frameUbo_.buffer)
            resources_.DestroyBuffer(frameUbo_);

        if (geometryVert_)
            vkDestroyShaderModule(device_, geometryVert_, nullptr);
        if (geometryFrag_)
            vkDestroyShaderModule(device_, geometryFrag_, nullptr);
        if (fullscreenVert_)
            vkDestroyShaderModule(device_, fullscreenVert_, nullptr);
        if (lightingFrag_)
            vkDestroyShaderModule(device_, lightingFrag_, nullptr);
        if (postFrag_)
            vkDestroyShaderModule(device_, postFrag_, nullptr);

        geometryVert_ = VK_NULL_HANDLE;
        geometryFrag_ = VK_NULL_HANDLE;
        fullscreenVert_ = VK_NULL_HANDLE;
        lightingFrag_ = VK_NULL_HANDLE;
        postFrag_ = VK_NULL_HANDLE;

        if (postLayout_)
            vkDestroyPipelineLayout(device_, postLayout_, nullptr);
        if (lightingLayout_)
            vkDestroyPipelineLayout(device_, lightingLayout_, nullptr);
        if (geometryLayout_)
            vkDestroyPipelineLayout(device_, geometryLayout_, nullptr);

        postLayout_ = VK_NULL_HANDLE;
        lightingLayout_ = VK_NULL_HANDLE;
        geometryLayout_ = VK_NULL_HANDLE;

        if (postSetLayout_)
            vkDestroyDescriptorSetLayout(device_, postSetLayout_, nullptr);
        if (lightingFrameLayout_)
            vkDestroyDescriptorSetLayout(device_, lightingFrameLayout_, nullptr);
        if (lightingInputLayout_)
            vkDestroyDescriptorSetLayout(device_, lightingInputLayout_, nullptr);

        postSetLayout_ = VK_NULL_HANDLE;
        lightingFrameLayout_ = VK_NULL_HANDLE;
        lightingInputLayout_ = VK_NULL_HANDLE;

        DestroyDefaultIBL();

        for (Frame& frame : frames_) {
            if (frame.fence)
                vkDestroyFence(device_, frame.fence, nullptr);

            if (frame.renderFinished)
                vkDestroySemaphore(device_, frame.renderFinished, nullptr);

            if (frame.imageAvailable)
                vkDestroySemaphore(device_, frame.imageAvailable, nullptr);

            if (frame.pool)
                vkDestroyCommandPool(device_, frame.pool, nullptr);

            frame = {};
        }

        resources_.Shutdown();

        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_) {
        vkDestroySurfaceKHR(
            instance_,
            surface_,
            nullptr
        );
        surface_ = VK_NULL_HANDLE;
    }

    if (instance_) {
        vkDestroyInstance(
            instance_,
            nullptr
        );
        instance_ = VK_NULL_HANDLE;
    }

    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }

    gpu_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    family_ = UINT32_MAX;
    initialized_ = false;
    begun_ = false;
    mainRenderPassActive_ = false;
    frameRecorded_ = false;
    frame_ = 0;
    image_ = UINT32_MAX;
}

uint64_t VulkanRenderer::CreateOffscreenRenderTarget(uint32_t, uint32_t) { return 0; }
bool VulkanRenderer::ResizeOffscreenRenderTarget(uint64_t, uint32_t, uint32_t) { return false; }
uint64_t VulkanRenderer::GetOffscreenColorHandle(uint64_t) const noexcept { return 0; }


bool VulkanRenderer::CreateHDRTarget() {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(
        gpu_,
        hdrFormat_,
        &properties
    );

    if ((properties.optimalTilingFeatures &
         VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0 ||
        (properties.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        hdrFormat_ = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        vkGetPhysicalDeviceFormatProperties(
            gpu_,
            hdrFormat_,
            &properties
        );

        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0 ||
            (properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
            hdrFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    if (!CreateImageRaw(
            hdrFormat_,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            0,
            {extent_.width, extent_.height, 1},
            1,
            hdrImage_,
            hdrMemory_)) {
        return false;
    }

    if (!CreateImageViewRaw(
            hdrImage_,
            hdrFormat_,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            hdrView_)) {
        vkDestroyImage(device_, hdrImage_, nullptr);
        vkFreeMemory(device_, hdrMemory_, nullptr);
        hdrImage_ = VK_NULL_HANDLE;
        hdrMemory_ = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanRenderer::CreateShaderModules() {
    ShaderResourceManager shaders{};

    struct ShaderFile {
        const char* name;
        VkShaderModule* output;
    };

    const std::array<ShaderFile, 8> files = {{
        {"gbuffer_mobile.vert.spv", &geometryVert_},
        {"gbuffer_mobile.frag.spv", &geometryFrag_},
        {"fullscreen_triangle.vert.spv", &fullscreenVert_},
        {"deferred_lighting_mobile.frag.spv", &lightingFrag_},
        {"shadow_mobile.vert.spv", &shadowVert_},
        {"shadow_mobile.frag.spv", &shadowFrag_},
        {"bloom_downsample_mobile.frag.spv", &bloomDownFrag_},
        {"bloom_upsample_mobile.frag.spv", &bloomUpFrag_}
    }};

    const std::filesystem::path root =
        projectRoot_.empty()
            ? std::filesystem::path("assets/shaders")
            : projectRoot_ / "assets/shaders";

    for (const ShaderFile& file : files) {
        std::vector<uint32_t> words{};
        const auto path = root / file.name;

        if (!shaders.LoadSPIRV(path, words) ||
            !ShaderResourceManager::ValidateSPIRV(words)) {
            AETHERIS_VK_LOGE(
                "SPIR-V load failed: %s",
                path.string().c_str()
            );
            return false;
        }

        VkShaderModuleCreateInfo info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };
        info.codeSize =
            words.size() * sizeof(uint32_t);
        info.pCode =
            words.data();

        if (vkCreateShaderModule(
                device_,
                &info,
                nullptr,
                file.output
            ) != VK_SUCCESS) {
            AETHERIS_VK_LOGE(
                "vkCreateShaderModule failed: %s",
                file.name
            );
            return false;
        }
    }

    // The post shader is intentionally loaded last because Android's shader
    // compiler emits the canonical file name from the source stem.
    {
        std::vector<uint32_t> words{};
        const auto path =
            root /
            "post_aces_bloom_mobile.frag.spv";

        if (!shaders.LoadSPIRV(path, words) ||
            !ShaderResourceManager::ValidateSPIRV(words)) {
            AETHERIS_VK_LOGE(
                "SPIR-V load failed: %s",
                path.string().c_str()
            );
            return false;
        }

        VkShaderModuleCreateInfo info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };
        info.codeSize =
            words.size() * sizeof(uint32_t);
        info.pCode =
            words.data();

        if (vkCreateShaderModule(
                device_,
                &info,
                nullptr,
                &postFrag_
            ) != VK_SUCCESS) {
            return false;
        }
    }

    // Full-screen vertex shader is shared by lighting and both bloom filters.
    // Geometry and shadow stages never compile or load shaders at frame time.
    return true;
}

bool VulkanRenderer::CreateDescriptorLayouts() {
    std::array<VkDescriptorSetLayoutBinding, 3> inputBindings{};
    for (uint32_t i = 0; i < 3; ++i) {
        inputBindings[i].binding = i;
        inputBindings[i].descriptorType =
            VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        inputBindings[i].descriptorCount = 1;
        inputBindings[i].stageFlags =
            VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo inputInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    inputInfo.bindingCount = 3;
    inputInfo.pBindings = inputBindings.data();

    if (vkCreateDescriptorSetLayout(
            device_, &inputInfo, nullptr, &lightingInputLayout_) != VK_SUCCESS) {
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 4> frameBindings{};
    frameBindings[0] = {
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    };

    for (uint32_t i = 1; i < 4; ++i) {
        frameBindings[i] = {
            i,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            nullptr
        };
    }

    VkDescriptorSetLayoutCreateInfo frameInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    frameInfo.bindingCount = 4;
    frameInfo.pBindings = frameBindings.data();

    if (vkCreateDescriptorSetLayout(
            device_, &frameInfo, nullptr, &lightingFrameLayout_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayoutBinding postBinding{
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    };

    VkDescriptorSetLayoutCreateInfo postInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    postInfo.bindingCount = 1;
    postInfo.pBindings = &postBinding;

    if (vkCreateDescriptorSetLayout(
            device_, &postInfo, nullptr, &postSetLayout_) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange geometryPush{
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        128
    };

    VkPipelineLayoutCreateInfo geometryLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    geometryLayoutInfo.pushConstantRangeCount = 1;
    geometryLayoutInfo.pPushConstantRanges = &geometryPush;

    if (vkCreatePipelineLayout(
            device_, &geometryLayoutInfo, nullptr, &geometryLayout_) != VK_SUCCESS) {
        return false;
    }

    const std::array<VkDescriptorSetLayout, 2> lightingLayouts = {
        lightingInputLayout_,
        lightingFrameLayout_
    };

    VkPipelineLayoutCreateInfo lightingLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    lightingLayoutInfo.setLayoutCount = 2;
    lightingLayoutInfo.pSetLayouts = lightingLayouts.data();

    if (vkCreatePipelineLayout(
            device_, &lightingLayoutInfo, nullptr, &lightingLayout_) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange postPush{
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        16
    };

    VkPipelineLayoutCreateInfo postLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    postLayoutInfo.setLayoutCount = 1;
    postLayoutInfo.pSetLayouts = &postSetLayout_;
    postLayoutInfo.pushConstantRangeCount = 1;
    postLayoutInfo.pPushConstantRanges = &postPush;

    return vkCreatePipelineLayout(
        device_,
        &postLayoutInfo,
        nullptr,
        &postLayout_
    ) == VK_SUCCESS;
}

bool VulkanRenderer::CreateDescriptorPoolAndSets() {
    const std::array<VkDescriptorPoolSize, 3> sizes = {{
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 3},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}
    }};

    VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
    };
    poolInfo.maxSets = 3;
    poolInfo.poolSizeCount =
        static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();

    if (vkCreateDescriptorPool(
            device_,
            &poolInfo,
            nullptr,
            &descriptorPool_) != VK_SUCCESS) {
        return false;
    }

    const std::array<VkDescriptorSetLayout, 3> layouts = {{
        lightingInputLayout_,
        lightingFrameLayout_,
        postSetLayout_
    }};

    std::array<VkDescriptorSet, 3> sets{};

    VkDescriptorSetAllocateInfo allocateInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
    };
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 3;
    allocateInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(
            device_,
            &allocateInfo,
            sets.data()) != VK_SUCCESS) {
        return false;
    }

    lightingInputSet_ = sets[0];
    lightingFrameSet_ = sets[1];
    postSet_ = sets[2];

    std::array<VkDescriptorImageInfo, 3> inputImages{};
    for (uint32_t i = 0; i < 3; ++i) {
        inputImages[i].imageView = gbufferViews_[i];
        inputImages[i].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    std::array<VkWriteDescriptorSet, 3> inputWrites{};
    for (uint32_t i = 0; i < 3; ++i) {
        inputWrites[i] = {
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
        };
        inputWrites[i].dstSet = lightingInputSet_;
        inputWrites[i].dstBinding = i;
        inputWrites[i].descriptorCount = 1;
        inputWrites[i].descriptorType =
            VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        inputWrites[i].pImageInfo = &inputImages[i];
    }

    VkDescriptorBufferInfo frameInfo{
        frameUbo_.buffer,
        0,
        sizeof(std140::DeferredFrameBlock)
    };

    std::array<VkDescriptorImageInfo, 3> iblImages = {{
        {
            linearSampler_,
            irradianceView_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        },
        {
            linearSampler_,
            prefilteredView_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        },
        {
            linearSampler_,
            brdfView_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }
    }};

    std::array<VkWriteDescriptorSet, 4> frameWrites{};
    frameWrites[0] = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    frameWrites[0].dstSet = lightingFrameSet_;
    frameWrites[0].dstBinding = 0;
    frameWrites[0].descriptorCount = 1;
    frameWrites[0].descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    frameWrites[0].pBufferInfo = &frameInfo;

    for (uint32_t i = 0; i < 3; ++i) {
        frameWrites[i + 1] = {
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
        };
        frameWrites[i + 1].dstSet = lightingFrameSet_;
        frameWrites[i + 1].dstBinding = i + 1;
        frameWrites[i + 1].descriptorCount = 1;
        frameWrites[i + 1].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        frameWrites[i + 1].pImageInfo = &iblImages[i];
    }

    VkDescriptorImageInfo postImage{
        linearSampler_,
        hdrView_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet postWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    postWrite.dstSet = postSet_;
    postWrite.dstBinding = 0;
    postWrite.descriptorCount = 1;
    postWrite.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    postWrite.pImageInfo = &postImage;

    vkUpdateDescriptorSets(
        device_,
        static_cast<uint32_t>(inputWrites.size()),
        inputWrites.data(),
        0,
        nullptr
    );

    vkUpdateDescriptorSets(
        device_,
        static_cast<uint32_t>(frameWrites.size()),
        frameWrites.data(),
        0,
        nullptr
    );

    vkUpdateDescriptorSets(
        device_,
        1,
        &postWrite,
        0,
        nullptr
    );

    return true;
}

bool VulkanRenderer::CreatePipelines() {
    const VkPipelineShaderStageCreateInfo geometryStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            geometryVert_,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            geometryFrag_,
            "main",
            nullptr
        }
    };

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(DemoVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    const std::array<VkVertexInputAttributeDescription, 5> attributes = {{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DemoVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DemoVertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(DemoVertex, uv)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DemoVertex, baseColorMetallic)},
        {4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(DemoVertex, roughnessAO)}
    }};

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssembly.topology =
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisample.rasterizationSamples =
        VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    std::array<VkPipelineColorBlendAttachmentState, 3> gbufferBlend{};
    for (auto& state : gbufferBlend) {
        state.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo gbufferBlendState{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    gbufferBlendState.attachmentCount = 3;
    gbufferBlendState.pAttachments = gbufferBlend.data();

    const std::array<VkDynamicState, 2> dynamicStates = {{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    }};

    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo geometryInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };

    geometryInfo.stageCount = 2;
    geometryInfo.pStages = geometryStages;
    geometryInfo.pVertexInputState = &vertexInput;
    geometryInfo.pInputAssemblyState = &inputAssembly;
    geometryInfo.pViewportState = &viewportState;
    geometryInfo.pRasterizationState = &rasterization;
    geometryInfo.pMultisampleState = &multisample;
    geometryInfo.pDepthStencilState = &depth;
    geometryInfo.pColorBlendState = &gbufferBlendState;
    geometryInfo.pDynamicState = &dynamicState;
    geometryInfo.layout = geometryLayout_;
    geometryInfo.renderPass = pass_;
    geometryInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(
            device_,
            resources_.PipelineCache(),
            1,
            &geometryInfo,
            nullptr,
            &geometryPipeline_) != VK_SUCCESS) {
        AETHERIS_VK_LOGE("Geometry pipeline creation failed");
        return false;
    }

    const VkPipelineShaderStageCreateInfo fullscreenStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            fullscreenVert_,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            lightingFrag_,
            "main",
            nullptr
        }
    };

    VkPipelineVertexInputStateCreateInfo noVertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipelineRasterizationStateCreateInfo fullscreenRasterization =
        rasterization;
    fullscreenRasterization.cullMode =
        VK_CULL_MODE_NONE;

    VkPipelineDepthStencilStateCreateInfo noDepth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };

    VkPipelineColorBlendAttachmentState oneColor{};
    oneColor.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo oneColorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    oneColorBlend.attachmentCount = 1;
    oneColorBlend.pAttachments = &oneColor;

    VkGraphicsPipelineCreateInfo lightingInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    lightingInfo.stageCount = 2;
    lightingInfo.pStages = fullscreenStages;
    lightingInfo.pVertexInputState = &noVertexInput;
    lightingInfo.pInputAssemblyState = &inputAssembly;
    lightingInfo.pViewportState = &viewportState;
    lightingInfo.pRasterizationState = &fullscreenRasterization;
    lightingInfo.pMultisampleState = &multisample;
    lightingInfo.pDepthStencilState = &noDepth;
    lightingInfo.pColorBlendState = &oneColorBlend;
    lightingInfo.pDynamicState = &dynamicState;
    lightingInfo.layout = lightingLayout_;
    lightingInfo.renderPass = pass_;
    lightingInfo.subpass = 1;

    if (vkCreateGraphicsPipelines(
            device_,
            resources_.PipelineCache(),
            1,
            &lightingInfo,
            nullptr,
            &lightingPipeline_) != VK_SUCCESS) {
        AETHERIS_VK_LOGE("Deferred lighting pipeline creation failed");
        DestroyPipelines();
        return false;
    }

    const VkPipelineShaderStageCreateInfo postStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            fullscreenVert_,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            postFrag_,
            "main",
            nullptr
        }
    };

    VkGraphicsPipelineCreateInfo postInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    postInfo.stageCount = 2;
    postInfo.pStages = postStages;
    postInfo.pVertexInputState = &noVertexInput;
    postInfo.pInputAssemblyState = &inputAssembly;
    postInfo.pViewportState = &viewportState;
    postInfo.pRasterizationState = &fullscreenRasterization;
    postInfo.pMultisampleState = &multisample;
    postInfo.pDepthStencilState = &noDepth;
    postInfo.pColorBlendState = &oneColorBlend;
    postInfo.pDynamicState = &dynamicState;
    postInfo.layout = postLayout_;
    postInfo.renderPass = postPass_;
    postInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(
            device_,
            resources_.PipelineCache(),
            1,
            &postInfo,
            nullptr,
            &postPipeline_) != VK_SUCCESS) {
        AETHERIS_VK_LOGE("Post pipeline creation failed");
        DestroyPipelines();
        return false;
    }

    return true;
}

bool VulkanRenderer::CreateOneTimeCommand(
    VkCommandBuffer& commandBuffer
) {
    VkCommandBufferAllocateInfo info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    info.commandPool = frames_[0].pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;

    return vkAllocateCommandBuffers(
        device_,
        &info,
        &commandBuffer
    ) == VK_SUCCESS;
}

void VulkanRenderer::DestroyOneTimeCommand(
    VkCommandBuffer commandBuffer
) noexcept {
    if (!commandBuffer) return;

    vkFreeCommandBuffers(
        device_,
        frames_[0].pool,
        1,
        &commandBuffer
    );
}

bool VulkanRenderer::UploadImage(
    VkImage image,
    const void* data,
    VkDeviceSize bytes,
    const VkBufferImageCopy* copies,
    uint32_t copyCount
) {
    VkBufferCreateInfo stagingInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
    };
    stagingInfo.size = bytes;
    stagingInfo.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer staging{};
    VkDeviceMemory memory{};

    if (vkCreateBuffer(
            device_,
            &stagingInfo,
            nullptr,
            &staging
        ) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(
        device_,
        staging,
        &requirements
    );

    const uint32_t type =
        FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

    if (type == UINT32_MAX) {
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    VkMemoryAllocateInfo allocation{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
    };
    allocation.allocationSize =
        requirements.size;
    allocation.memoryTypeIndex =
        type;

    if (vkAllocateMemory(
            device_,
            &allocation,
            nullptr,
            &memory
        ) != VK_SUCCESS ||
        vkBindBufferMemory(
            device_,
            staging,
            memory,
            0
        ) != VK_SUCCESS) {
        if (memory)
            vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(
            device_,
            memory,
            0,
            bytes,
            0,
            &mapped
        ) != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    std::memcpy(
        mapped,
        data,
        static_cast<size_t>(bytes)
    );

    vkUnmapMemory(
        device_,
        memory
    );

    VkCommandBuffer commandBuffer{};
    if (!CreateOneTimeCommand(commandBuffer)) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    begin.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    bool success =
        vkBeginCommandBuffer(
            commandBuffer,
            &begin
        ) == VK_SUCCESS;

    if (success) {
        VkImageMemoryBarrier before{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
        };
        before.oldLayout =
            VK_IMAGE_LAYOUT_UNDEFINED;
        before.newLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        before.dstAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT;
        before.srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        before.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        before.image = image;
        before.subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, 1, 0,
            copyCount == 6 ? 6u : 1u
        };

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &before
        );

        vkCmdCopyBufferToImage(
            commandBuffer,
            staging,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            copyCount,
            copies
        );

        VkImageMemoryBarrier after = before;
        after.oldLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        after.newLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        after.srcAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT;
        after.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &after
        );

        success =
            vkEndCommandBuffer(
                commandBuffer
            ) == VK_SUCCESS;
    }

    VkResult submit = VK_ERROR_INITIALIZATION_FAILED;

    if (success) {
        VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO
        };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers =
            &commandBuffer;

        submit =
            vkQueueSubmit(
                queue_,
                1,
                &submitInfo,
                VK_NULL_HANDLE
            );

        if (submit == VK_SUCCESS)
            submit = vkQueueWaitIdle(queue_);
    }

    DestroyOneTimeCommand(
        commandBuffer
    );

    vkFreeMemory(
        device_,
        memory,
        nullptr
    );

    vkDestroyBuffer(
        device_,
        staging,
        nullptr
    );

    return success && submit == VK_SUCCESS;
}

bool VulkanRenderer::UploadBufferToDeviceLocal(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    GpuBuffer& output
) {
    VkBufferCreateInfo stagingInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
    };
    stagingInfo.size = size;
    stagingInfo.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer staging{};
    VkDeviceMemory memory{};

    if (vkCreateBuffer(
            device_,
            &stagingInfo,
            nullptr,
            &staging
        ) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(
        device_,
        staging,
        &requirements
    );

    const uint32_t type =
        FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

    if (type == UINT32_MAX) {
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    VkMemoryAllocateInfo allocation{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
    };
    allocation.allocationSize =
        requirements.size;
    allocation.memoryTypeIndex =
        type;

    if (vkAllocateMemory(
            device_,
            &allocation,
            nullptr,
            &memory
        ) != VK_SUCCESS ||
        vkBindBufferMemory(
            device_,
            staging,
            memory,
            0
        ) != VK_SUCCESS) {
        if (memory)
            vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    void* mapped = nullptr;

    if (vkMapMemory(
            device_,
            memory,
            0,
            size,
            0,
            &mapped
        ) != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    std::memcpy(
        mapped,
        data,
        static_cast<size_t>(size)
    );

    vkUnmapMemory(
        device_,
        memory
    );

    VkBufferCreateInfo deviceInfo{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
    };
    deviceInfo.size = size;
    deviceInfo.usage =
        usage |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    deviceInfo.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE;

    output = resources_.CreateBuffer(
        deviceInfo,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (!output.buffer) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    VkCommandBuffer commandBuffer{};

    if (!CreateOneTimeCommand(commandBuffer)) {
        resources_.DestroyBuffer(output);
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    begin.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    bool success =
        vkBeginCommandBuffer(
            commandBuffer,
            &begin
        ) == VK_SUCCESS;

    if (success) {
        const VkBufferCopy copy{
            0,
            0,
            size
        };

        vkCmdCopyBuffer(
            commandBuffer,
            staging,
            output.buffer,
            1,
            &copy
        );

        success =
            vkEndCommandBuffer(
                commandBuffer
            ) == VK_SUCCESS;
    }

    VkResult submit =
        VK_ERROR_INITIALIZATION_FAILED;

    if (success) {
        VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO
        };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers =
            &commandBuffer;

        submit =
            vkQueueSubmit(
                queue_,
                1,
                &submitInfo,
                VK_NULL_HANDLE
            );

        if (submit == VK_SUCCESS)
            submit = vkQueueWaitIdle(queue_);
    }

    DestroyOneTimeCommand(commandBuffer);

    vkFreeMemory(
        device_,
        memory,
        nullptr
    );

    vkDestroyBuffer(
        device_,
        staging,
        nullptr
    );

    if (!success ||
        submit != VK_SUCCESS) {
        resources_.DestroyBuffer(output);
        return false;
    }

    return true;
}

bool VulkanRenderer::LoadOrFallbackMesh(
    DemoMeshSlot slot,
    DemoCpuMesh& mesh
) noexcept {
    const uint32_t index =
        static_cast<uint32_t>(slot);

    if (index >=
        static_cast<uint32_t>(DemoMeshSlot::Count)) {
        return false;
    }

    const DemoMaterial& material =
        kMaterials[index];

    const std::filesystem::path path =
        (projectRoot_.empty()
            ? std::filesystem::path{}
            : projectRoot_) /
        DemoWorldInitializer::AssetPath(slot);

    ObjMeshLoader loader{};

    if (loader.Load(
            path,
            material.color,
            material.metallic,
            material.roughness,
            material.ao,
            mesh
        )) {
        return true;
    }

    AETHERIS_VK_LOGW(
        "Falling back to procedural mesh slot=%u",
        index
    );

    DemoWorldInitializer::FallbackMesh(
        slot,
        mesh
    );

    return !mesh.Empty();
}

bool VulkanRenderer::CreateDemoMeshes() {
    constexpr uint32_t meshCount =
        static_cast<uint32_t>(DemoMeshSlot::Count);

    for (uint32_t i = 0;
         i < meshCount;
         ++i) {

        DemoCpuMesh cpu{};

        if (!LoadOrFallbackMesh(
                static_cast<DemoMeshSlot>(i),
                cpu
            )) {
            return false;
        }

        MeshGpu& gpu = demoMeshes_[i];

        if (!UploadBufferToDeviceLocal(
                cpu.vertices.data(),
                static_cast<VkDeviceSize>(
                    cpu.vertexCount *
                    sizeof(DemoVertex)
                ),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                gpu.vertex
            ) ||
            !UploadBufferToDeviceLocal(
                cpu.indices.data(),
                static_cast<VkDeviceSize>(
                    cpu.indexCount *
                    sizeof(uint32_t)
                ),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                gpu.index
            )) {
            return false;
        }

        gpu.indexCount =
            cpu.indexCount;
    }

    return true;
}

bool VulkanRenderer::CreateFrameUniformBuffer() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(
        gpu_,
        &properties
    );

    const VkDeviceSize alignment =
        std::max<VkDeviceSize>(
            properties.limits.minUniformBufferOffsetAlignment,
            16
        );

    const VkDeviceSize blockSize =
        sizeof(std140::DeferredFrameBlock);

    frameUboStride_ =
        (
            blockSize +
            alignment -
            1
        ) /
        alignment *
        alignment;

    VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
    };

    info.size =
        frameUboStride_ *
        Frames;

    info.usage =
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    info.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE;

    frameUbo_ =
        resources_.CreateBuffer(
            info,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

    return frameUbo_.buffer != VK_NULL_HANDLE;
}

bool VulkanRenderer::CreateDefaultIBL() {
    VkSamplerCreateInfo sampler{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
    };

    sampler.magFilter =
        VK_FILTER_LINEAR;
    sampler.minFilter =
        VK_FILTER_LINEAR;
    sampler.mipmapMode =
        VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.minLod = 0.0f;
    sampler.maxLod = 1.0f;

    if (vkCreateSampler(
            device_,
            &sampler,
            nullptr,
            &linearSampler_
        ) != VK_SUCCESS) {
        return false;
    }

    const std::array<uint8_t, 4> white = {
        255, 255, 255, 255
    };

    if (!CreateImageRaw(
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            {1,1,1},
            6,
            irradianceImage_,
            irradianceMemory_
        ) ||
        !CreateImageViewRaw(
            irradianceImage_,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_VIEW_TYPE_CUBE,
            VK_IMAGE_ASPECT_COLOR_BIT,
            6,
            irradianceView_
        )) {
        return false;
    }

    std::array<VkBufferImageCopy, 6> cubeCopies{};

    for (uint32_t face = 0; face < 6; ++face) {
        cubeCopies[face].imageSubresource =
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                face,
                1
            };
        cubeCopies[face].imageExtent =
            {1,1,1};
    }

    if (!UploadImage(
            irradianceImage_,
            white.data(),
            white.size(),
            cubeCopies.data(),
            6
        )) {
        return false;
    }

    if (!CreateImageRaw(
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            {1,1,1},
            6,
            prefilteredImage_,
            prefilteredMemory_
        ) ||
        !CreateImageViewRaw(
            prefilteredImage_,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_VIEW_TYPE_CUBE,
            VK_IMAGE_ASPECT_COLOR_BIT,
            6,
            prefilteredView_
        )) {
        return false;
    }

    if (!UploadImage(
            prefilteredImage_,
            white.data(),
            white.size(),
            cubeCopies.data(),
            6
        )) {
        return false;
    }

    const std::array<uint8_t, 2> brdf = {
        255, 0
    };

    if (!CreateImageRaw(
            VK_FORMAT_R8G8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            0,
            {1,1,1},
            1,
            brdfImage_,
            brdfMemory_
        ) ||
        !CreateImageViewRaw(
            brdfImage_,
            VK_FORMAT_R8G8_UNORM,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            brdfView_
        )) {
        return false;
    }

    VkBufferImageCopy brdfCopy{};
    brdfCopy.imageSubresource =
        {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0,
            0,
            1
        };
    brdfCopy.imageExtent =
        {1,1,1};

    return UploadImage(
        brdfImage_,
        brdf.data(),
        brdf.size(),
        &brdfCopy,
        1
    );
}

bool VulkanRenderer::UpdateFrameUniforms() noexcept {
    std140::DeferredFrameBlock frame{};

    frame.cameraPosition = {
        18.0f, 14.0f, 18.0f, 1.0f
    };

    frame.sunDirection = {
        -0.35f, -1.0f, -0.25f, 0.0f
    };

    frame.sunColor = {
        3.8f, 3.6f, 3.3f, 1.0f
    };

    frame.maxPrefilterMip = 0.0f;

    const VkDeviceSize offset =
        frame_ * frameUboStride_;

    void* mapped = nullptr;

    if (vkMapMemory(
            device_,
            frameUbo_.allocation.memory,
            frameUbo_.allocation.offset + offset,
            sizeof(frame),
            0,
            &mapped
        ) != VK_SUCCESS) {
        return false;
    }

    std::memcpy(
        mapped,
        &frame,
        sizeof(frame)
    );

    vkUnmapMemory(
        device_,
        frameUbo_.allocation.memory
    );

    return true;
}

Mat4 VulkanRenderer::Identity() noexcept {
    Mat4 result{};
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

Mat4 VulkanRenderer::Multiply(
    const Mat4& a,
    const Mat4& b
) noexcept {
    Mat4 result{};

    for (uint32_t column = 0;
         column < 4;
         ++column) {
        for (uint32_t row = 0;
             row < 4;
             ++row) {
            float value = 0.0f;

            for (uint32_t k = 0;
                 k < 4;
                 ++k) {
                value +=
                    a.m[k * 4 + row] *
                    b.m[column * 4 + k];
            }

            result.m[column * 4 + row] =
                value;
        }
    }

    return result;
}

Mat4 VulkanRenderer::MakeTranslation(
    const Vec4& p
) noexcept {
    Mat4 result = Identity();
    result.m[12] = p.x;
    result.m[13] = p.y;
    result.m[14] = p.z;
    return result;
}

Mat4 VulkanRenderer::MakeScale(
    const Vec4& s
) noexcept {
    Mat4 result = Identity();
    result.m[0] = s.x;
    result.m[5] = s.y;
    result.m[10] = s.z;
    return result;
}

Mat4 VulkanRenderer::MakeRotation(
    const Vec4& r
) noexcept {
    const float cx = std::cos(r.x);
    const float sx = std::sin(r.x);
    const float cy = std::cos(r.y);
    const float sy = std::sin(r.y);
    const float cz = std::cos(r.z);
    const float sz = std::sin(r.z);

    Mat4 rx = Identity();
    rx.m[5] = cx;
    rx.m[6] = sx;
    rx.m[9] = -sx;
    rx.m[10] = cx;

    Mat4 ry = Identity();
    ry.m[0] = cy;
    ry.m[2] = -sy;
    ry.m[8] = sy;
    ry.m[10] = cy;

    Mat4 rz = Identity();
    rz.m[0] = cz;
    rz.m[1] = sz;
    rz.m[4] = -sz;
    rz.m[5] = cz;

    return Multiply(
        Multiply(rz, ry),
        rx
    );
}

Mat4 VulkanRenderer::MakeModel(
    const Transform& transform
) noexcept {
    return Multiply(
        MakeTranslation(transform.position),
        Multiply(
            MakeRotation(transform.rotation),
            MakeScale(transform.scale)
        )
    );
}

Mat4 VulkanRenderer::MakeLookAt(
    float ex, float ey, float ez,
    float cx, float cy, float cz
) noexcept {
    float fx = cx - ex;
    float fy = cy - ey;
    float fz = cz - ez;

    {
        const float length =
            std::sqrt(fx * fx + fy * fy + fz * fz);
        if (length > 1e-6f) {
            fx /= length;
            fy /= length;
            fz /= length;
        }
    }

    float sx = fy;
    float sy = -fx;
    float sz = 0.0f;

    {
        const float length =
            std::sqrt(sx * sx + sy * sy + sz * sz);
        if (length > 1e-6f) {
            sx /= length;
            sy /= length;
            sz /= length;
        }
    }

    const float ux = sy * fz - sz * fy;
    const float uy = sz * fx - sx * fz;
    const float uz = sx * fy - sy * fx;

    Mat4 result = Identity();

    result.m[0] = sx;
    result.m[1] = ux;
    result.m[2] = -fx;

    result.m[4] = sy;
    result.m[5] = uy;
    result.m[6] = -fy;

    result.m[8] = sz;
    result.m[9] = uz;
    result.m[10] = -fz;

    result.m[12] =
        -(sx * ex + sy * ey + sz * ez);

    result.m[13] =
        -(ux * ex + uy * ey + uz * ez);

    result.m[14] =
        fx * ex + fy * ey + fz * ez;

    return result;
}

Mat4 VulkanRenderer::MakePerspective(
    float fovRadians,
    float aspect,
    float nearPlane,
    float farPlane
) noexcept {
    const float f =
        1.0f / std::tan(fovRadians * 0.5f);

    Mat4 result{};

    result.m[0] = f / aspect;
    result.m[5] = -f;

    result.m[10] =
        farPlane /
        (nearPlane - farPlane);

    result.m[11] = -1.0f;

    result.m[14] =
        (farPlane * nearPlane) /
        (nearPlane - farPlane);

    return result;
}

void VulkanRenderer::UpdateCamera() noexcept {
    if (extent_.width == 0 ||
        extent_.height == 0) {
        return;
    }

    const Mat4 view =
        MakeLookAt(
            18.0f, 14.0f, 18.0f,
            0.0f, 1.2f, 0.0f
        );

    const float aspect =
        static_cast<float>(extent_.width) /
        static_cast<float>(extent_.height);

    const Mat4 projection =
        MakePerspective(
            55.0f *
                3.14159265359f /
                180.0f,
            aspect,
            0.1f,
            150.0f
        );

    viewProj_ =
        Multiply(
            projection,
            view
        );
}


bool VulkanRenderer::CreateImageRaw(
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkExtent3D imageExtent,
    uint32_t arrayLayers,
    VkImage& image,
    VkDeviceMemory& memory
) {
    VkImageCreateInfo info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
    };

    info.flags = flags;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = imageExtent;
    info.mipLevels = 1;
    info.arrayLayers = arrayLayers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(
            device_, &info, nullptr, &image) != VK_SUCCESS) {
        image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(
        device_, image, &requirements);

    const uint32_t memoryType =
        FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

    if (memoryType == UINT32_MAX) {
        vkDestroyImage(device_, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocation{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
    };

    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;

    if (vkAllocateMemory(
            device_, &allocation, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(
            device_, image, memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
        vkDestroyImage(device_, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanRenderer::CreateImageViewRaw(
    VkImage image,
    VkFormat format,
    VkImageViewType type,
    VkImageAspectFlags aspect,
    uint32_t layers,
    VkImageView& view
) {
    VkImageViewCreateInfo info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
    };

    info.image = image;
    info.viewType = type;
    info.format = format;
    info.subresourceRange.aspectMask = aspect;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = layers;

    const VkResult result =
        vkCreateImageView(
            device_, &info, nullptr, &view);

    if (result != VK_SUCCESS)
        view = VK_NULL_HANDLE;

    return result == VK_SUCCESS;
}

void VulkanRenderer::DestroyPipelines() noexcept {
    if (!device_) return;

    if (postPipeline_)
        vkDestroyPipeline(device_, postPipeline_, nullptr);

    if (lightingPipeline_)
        vkDestroyPipeline(device_, lightingPipeline_, nullptr);

    if (geometryPipeline_)
        vkDestroyPipeline(device_, geometryPipeline_, nullptr);

    postPipeline_ = VK_NULL_HANDLE;
    lightingPipeline_ = VK_NULL_HANDLE;
    geometryPipeline_ = VK_NULL_HANDLE;
}

void VulkanRenderer::DestroyDescriptors() noexcept {
    if (!device_) return;

    if (descriptorPool_)
        vkDestroyDescriptorPool(
            device_, descriptorPool_, nullptr);

    descriptorPool_ = VK_NULL_HANDLE;
    lightingInputSet_ = VK_NULL_HANDLE;
    lightingFrameSet_ = VK_NULL_HANDLE;
    postSet_ = VK_NULL_HANDLE;
}

void VulkanRenderer::DestroyDemoMeshes() noexcept {
    for (MeshGpu& mesh : demoMeshes_) {
        resources_.DestroyBuffer(mesh.vertex);
        resources_.DestroyBuffer(mesh.index);
        mesh.indexCount = 0;
    }
}

void VulkanRenderer::DestroyDefaultIBL() noexcept {
    if (!device_) return;

    if (brdfView_)
        vkDestroyImageView(device_, brdfView_, nullptr);
    if (brdfImage_)
        vkDestroyImage(device_, brdfImage_, nullptr);
    if (brdfMemory_)
        vkFreeMemory(device_, brdfMemory_, nullptr);

    if (prefilteredView_)
        vkDestroyImageView(device_, prefilteredView_, nullptr);
    if (prefilteredImage_)
        vkDestroyImage(device_, prefilteredImage_, nullptr);
    if (prefilteredMemory_)
        vkFreeMemory(device_, prefilteredMemory_, nullptr);

    if (irradianceView_)
        vkDestroyImageView(device_, irradianceView_, nullptr);
    if (irradianceImage_)
        vkDestroyImage(device_, irradianceImage_, nullptr);
    if (irradianceMemory_)
        vkFreeMemory(device_, irradianceMemory_, nullptr);

    if (linearSampler_)
        vkDestroySampler(device_, linearSampler_, nullptr);

    brdfView_ = VK_NULL_HANDLE;
    brdfImage_ = VK_NULL_HANDLE;
    brdfMemory_ = VK_NULL_HANDLE;
    prefilteredView_ = VK_NULL_HANDLE;
    prefilteredImage_ = VK_NULL_HANDLE;
    prefilteredMemory_ = VK_NULL_HANDLE;
    irradianceView_ = VK_NULL_HANDLE;
    irradianceImage_ = VK_NULL_HANDLE;
    irradianceMemory_ = VK_NULL_HANDLE;
    linearSampler_ = VK_NULL_HANDLE;
}

} // namespace aetheris
