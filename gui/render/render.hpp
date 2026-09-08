#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <vulkan/vulkan.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

// This is a Vulkan application. Without GLFW_INCLUDE_NONE, glfw3.h pulls in
// GL/gl.h, which need not exist on a machine that has Vulkan headers but no
// OpenGL development package.
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

extern bool state;
extern bool menutoggle;

namespace Render
{

extern GLFWwindow* glfwWindow;

// Bring up GLFW, Vulkan and ImGui. Returns false on failure -- the caller
// must not enter RenderLoop() if this fails.
bool InitVulkan();

void RenderLoop(void (*func)());

// Destroy everything InitVulkan() created, in reverse order. Safe to call
// even if initialization failed part-way through.
void Cleanup();

void CreateGLFWWindow();

extern VkInstance                   instance;
extern VkPhysicalDevice             physicalDevice;
extern VkDevice                     device;
extern VkQueue                      graphicsQueue;
extern VkQueue                      presentQueue;
extern VkSurfaceKHR                 surface;
extern VkSwapchainKHR               swapChain;
extern VkFormat                     swapChainImageFormat;
extern VkExtent2D                   swapChainExtent;
extern std::vector<VkImage>         swapChainImages;
extern std::vector<VkImageView>     swapChainImageViews;
extern VkRenderPass                 renderPass;
extern std::vector<VkFramebuffer>   swapChainFramebuffers;
extern VkCommandPool                commandPool;
extern std::vector<VkCommandBuffer> commandBuffers;
extern std::vector<VkSemaphore>     imageAvailableSemaphores;
extern std::vector<VkSemaphore>     renderFinishedSemaphores;
extern std::vector<VkFence>         inFlightFences;
extern std::vector<VkFence>         imagesInFlight;
extern size_t                       currentFrame;
extern uint32_t                     queueFamilyIndex;

} // namespace Render
