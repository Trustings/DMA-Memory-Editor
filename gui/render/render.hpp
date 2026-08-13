#pragma once
#include <string>
#include <vector>
#include <stdexcept>
//#include <format>
#include <chrono>
#include <vulkan/vulkan.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include <GLFW/glfw3.h>

#ifdef _WIN32
    #include <dwmapi.h>
#endif

#include "menu.hpp"

namespace imGuiMenu {
// Font declarations (already in menu.hpp)
}

// External variables
extern bool state;
extern bool menutoggle;

namespace Render
{
// Initial window size (will be resizable)
//constexpr int Width = 1920, Height = 1080;

// GLFW window
extern GLFWwindow* glfwWindow;

// Common functions
bool InitVulkan();
void RenderLoop(void (*func)());
void Cleanup();

// Window creation
void CreateGLFWWindow();

// Vulkan context variables
extern VkInstance instance;
extern VkPhysicalDevice physicalDevice;
extern VkDevice device;
extern VkQueue graphicsQueue;
extern VkQueue presentQueue;
extern VkSurfaceKHR surface;
extern VkSwapchainKHR swapChain;
extern VkFormat swapChainImageFormat;
extern VkExtent2D swapChainExtent;
extern std::vector<VkImage> swapChainImages;
extern std::vector<VkImageView> swapChainImageViews;
extern VkRenderPass renderPass;
extern VkPipelineLayout pipelineLayout;
extern VkPipeline graphicsPipeline;
extern std::vector<VkFramebuffer> swapChainFramebuffers;
extern VkCommandPool commandPool;
extern std::vector<VkCommandBuffer> commandBuffers;
extern std::vector<VkSemaphore> imageAvailableSemaphores;
extern std::vector<VkSemaphore> renderFinishedSemaphores;
extern std::vector<VkFence> inFlightFences;
extern std::vector<VkFence> imagesInFlight;
extern size_t currentFrame;
}
