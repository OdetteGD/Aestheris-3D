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

    auto& f = frames_[frame_];
    if (vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;

    VkResult r = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &image_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        AETHERIS_VK_LOGW("Acquire returned %s; recreating swapchain", VkResultName(r));
        RecreateSwapchain(nullptr);
        return false;
    }
    if (r != VK_SUCCESS) {
        AETHERIS_VK_LOGE("vkAcquireNextImageKHR failed: %s", VkResultName(r));
        return false;
    }

    vkResetFences(device_, 1, &f.fence);
    vkResetCommandPool(device_, f.pool, 0);
    if (!Record(f.cmd, image_)) return false;

    begun_ = true;
    return true;
}

void VulkanRenderer::DrawRenderQueue(const RenderQueue&) {
    // RenderGraph passes bind pipelines and issue draws here. No allocations per frame.
}

void VulkanRenderer::EndFrame() {
    if (!begun_) return;

    auto& f = frames_[frame_];
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    s.waitSemaphoreCount = 1;
    s.pWaitSemaphores = &f.imageAvailable;
    s.pWaitDstStageMask = &stage;
    s.commandBufferCount = 1;
    s.pCommandBuffers = &f.cmd;
    s.signalSemaphoreCount = 1;
    s.pSignalSemaphores = &f.renderFinished;

    const VkResult submitResult = vkQueueSubmit(queue_, 1, &s, f.fence);
    if (submitResult != VK_SUCCESS) {
        AETHERIS_VK_LOGE("vkQueueSubmit failed: %s", VkResultName(submitResult));
        begun_ = false;
        return;
    }

    VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    p.waitSemaphoreCount = 1;
    p.pWaitSemaphores = &f.renderFinished;
    p.swapchainCount = 1;
    p.pSwapchains = &swapchain_;
    p.pImageIndices = &image_;

    VkResult r = vkQueuePresentKHR(queue_, &p);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        AETHERIS_VK_LOGW("Present returned %s; rebuilding swapchain", VkResultName(r));
        RecreateSwapchain(nullptr);
    } else if (r != VK_SUCCESS) {
        AETHERIS_VK_LOGE("vkQueuePresentKHR failed: %s", VkResultName(r));
    }

    frame_ = (frame_ + 1) % Frames;
    begun_ = false;
}

void VulkanRenderer::DestroySwapchain() noexcept {
    if (!device_) return;

    vkDeviceWaitIdle(device_);
    for (auto x : fb_) if (x) vkDestroyFramebuffer(device_, x, nullptr);
    for (auto x : views_) if (x) vkDestroyImageView(device_, x, nullptr);
    if (pass_) vkDestroyRenderPass(device_, pass_, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);

    fb_.clear();
    views_.clear();
    images_.clear();
    pass_ = {};
    swapchain_ = {};
    DestroyGBufferAttachments();
}

bool VulkanRenderer::RecreateSwapchain(ANativeWindow* w) {
    if (!device_) return false;

    AETHERIS_VK_LOGI("RecreateSwapchain begin: newWindow=%p currentWindow=%p",
                      static_cast<void*>(w), static_cast<void*>(window_));
    // Device-idle is the hard synchronization boundary for all swapchain-dependent resources.
    vkDeviceWaitIdle(device_);
    DestroySwapchain();

    if (w && (w != window_ || !surface_)) {
        if (surface_) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = {};
        }
        if (window_) {
            ANativeWindow_release(window_);
            window_ = nullptr;
        }
        window_ = w;
        ANativeWindow_acquire(window_);
        if (!CreateSurface()) return false;
    }

    if (!surface_) return false;
    const bool ok = CreateSwapchain() &&
                    CreateGBufferAttachments() &&
                    CreatePass() &&
                    CreateViews() &&
                    CreateFramebuffers();
    AETHERIS_VK_LOGI("RecreateSwapchain complete: result=%d extent=%ux%u", ok ? 1 : 0, extent_.width, extent_.height);
    return ok;
}

void VulkanRenderer::ReleaseSurface() noexcept {
    if (!device_) return;
    AETHERIS_VK_LOGI("ReleaseSurface begin");

    begun_ = false;
    DestroySwapchain();

    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = {};
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
                : projectRoot_ / "cache/pipelines/aetheris_vk.bin");

        for (auto& f : frames_) {
            if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
            if (f.renderFinished) vkDestroySemaphore(device_, f.renderFinished, nullptr);
            if (f.imageAvailable) vkDestroySemaphore(device_, f.imageAvailable, nullptr);
            if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);
        }

        DestroySwapchain();
        resources_.Shutdown();
        vkDestroyDevice(device_, nullptr);
    }

    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (window_) ANativeWindow_release(window_);

    instance_ = {};
    surface_ = {};
    gpu_ = {};
    device_ = {};
    queue_ = {};
    window_ = {};
    family_ = UINT32_MAX;
    initialized_ = begun_ = false;
}

uint64_t VulkanRenderer::CreateOffscreenRenderTarget(uint32_t, uint32_t) { return 0; }
bool VulkanRenderer::ResizeOffscreenRenderTarget(uint64_t, uint32_t, uint32_t) { return false; }
uint64_t VulkanRenderer::GetOffscreenColorHandle(uint64_t) const noexcept { return 0; }

} // namespace aetheris
