#pragma once
#include "IAetherisRenderer.h"
#include <array>
#include <vector>
#include <vulkan/vulkan.h>
#include "engine/renderer/GPUResourceManager.h"
#include <filesystem>
#include <utility>
namespace aetheris {
class VulkanRenderer final:public IAetherisRenderer{
 std::filesystem::path projectRoot_{};
 GPUResourceManager resources_{};
 static constexpr uint32_t Frames=3;
 struct Frame{VkCommandPool pool{};VkCommandBuffer cmd{};VkSemaphore imageAvailable{};VkSemaphore renderFinished{};VkFence fence{};};
 VkInstance instance_{};VkPhysicalDevice gpu_{};VkDevice device_{};VkSurfaceKHR surface_{};VkQueue queue_{};
 VkSwapchainKHR swapchain_{};VkRenderPass pass_{};VkFormat format_{};VkExtent2D extent_{};
 std::vector<VkImage> images_;std::vector<VkImageView> views_;std::vector<VkFramebuffer> fb_;std::array<Frame,Frames> frames_{};
 ANativeWindow* window_{};uint32_t family_=UINT32_MAX,frame_=0,image_=UINT32_MAX;bool initialized_{},begun_{};
 bool CreateInstance(),CreateSurface(),PickGPU(),CreateDevice(),CreateSwapchain(),CreatePass(),CreateViews(),CreateFramebuffers(),CreateFrames();
 bool Record(VkCommandBuffer,uint32_t);void DestroySwapchain()noexcept;
public:
 explicit VulkanRenderer(std::filesystem::path projectRoot = {}) : projectRoot_(std::move(projectRoot)) {}
 ~VulkanRenderer()override{Shutdown();}
 bool Initialize(ANativeWindow*)override;bool BeginFrame()override;void EndFrame()override;bool RecreateSwapchain(ANativeWindow*)override;
 void ReleaseSurface() noexcept override;
 void DrawRenderQueue(const RenderQueue&)override;void Shutdown()noexcept override;
 uint64_t CreateOffscreenRenderTarget(uint32_t,uint32_t)override;bool ResizeOffscreenRenderTarget(uint64_t,uint32_t,uint32_t)override;uint64_t GetOffscreenColorHandle(uint64_t)const noexcept override;
};
}