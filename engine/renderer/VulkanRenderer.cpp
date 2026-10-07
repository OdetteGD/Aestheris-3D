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

} // namespace aetheris
