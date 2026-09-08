#include "render.hpp"
#include "menu.hpp"

#include <cstdio>

GLFWwindow* Render::glfwWindow = nullptr;

bool state       = true;
bool menutoggle  = true;

VkInstance                   Render::instance                 = VK_NULL_HANDLE;
VkPhysicalDevice             Render::physicalDevice           = VK_NULL_HANDLE;
VkDevice                     Render::device                   = VK_NULL_HANDLE;
VkQueue                      Render::graphicsQueue            = VK_NULL_HANDLE;
VkQueue                      Render::presentQueue             = VK_NULL_HANDLE;
VkSurfaceKHR                 Render::surface                  = VK_NULL_HANDLE;
VkSwapchainKHR               Render::swapChain                = VK_NULL_HANDLE;
VkFormat                     Render::swapChainImageFormat     = VK_FORMAT_UNDEFINED;
VkExtent2D                   Render::swapChainExtent          = { 0, 0 };
std::vector<VkImage>         Render::swapChainImages;
std::vector<VkImageView>     Render::swapChainImageViews;
VkRenderPass                 Render::renderPass               = VK_NULL_HANDLE;
std::vector<VkFramebuffer>   Render::swapChainFramebuffers;
VkCommandPool                Render::commandPool              = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> Render::commandBuffers;
std::vector<VkSemaphore>     Render::imageAvailableSemaphores;
std::vector<VkSemaphore>     Render::renderFinishedSemaphores;
std::vector<VkFence>         Render::inFlightFences;
std::vector<VkFence>         Render::imagesInFlight;
size_t                       Render::currentFrame             = 0;
uint32_t                     Render::queueFamilyIndex         = 0;

static const int MAX_FRAMES_IN_FLIGHT = 2;

static bool g_imguiVulkanReady = false;
static bool g_imguiGlfwReady   = false;
static bool g_imguiContext     = false;

// ---------------------------------------------------------------------------
// GLFW
// ---------------------------------------------------------------------------

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    if (key == GLFW_KEY_INSERT && action == GLFW_PRESS) {
        menutoggle = !menutoggle;
    }
}

void Render::CreateGLFWWindow() {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    const int windowWidth  = 800;
    const int windowHeight = 650;

    glfwWindow = glfwCreateWindow(windowWidth, windowHeight, "DMA-Memory-Editor", nullptr, nullptr);
    if (!glfwWindow) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    if (primaryMonitor) {
        int monitorX = 0, monitorY = 0;
        glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);
        const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
        if (mode) {
            glfwSetWindowPos(glfwWindow,
                             monitorX + (mode->width - windowWidth) / 2,
                             monitorY + (mode->height - windowHeight) / 2);
        }
    }

    // ImGui's GLFW backend chains to whatever callback is already installed,
    // so this still fires once per press.
    glfwSetKeyCallback(glfwWindow, glfwKeyCallback);
}

// ---------------------------------------------------------------------------
// Vulkan setup
// ---------------------------------------------------------------------------

static void CreateInstance(VkInstance* instance) {
    VkApplicationInfo appInfo = {};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "DMA-Memory-Editor";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "No Engine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions) {
        throw std::runtime_error("GLFW reported no required Vulkan extensions "
                                 "(is a Vulkan loader installed?)");
    }

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount       = 0;

    if (vkCreateInstance(&createInfo, nullptr, instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

static void PickPhysicalDevice(VkInstance instance, VkPhysicalDevice* physicalDevice) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        // devices[0] on an empty vector was undefined behaviour.
        throw std::runtime_error("No Vulkan-capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& candidate : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            *physicalDevice = candidate;
            return;
        }
    }

    *physicalDevice = devices[0];
}

static bool FindQueueFamily(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                            uint32_t* outIndex) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);

        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
            *outIndex = i;
            return true;
        }
    }

    // Returning 0 and hoping used to be the behaviour here; on a GPU whose
    // family 0 does not support present that produced a device that could
    // never show a frame.
    return false;
}

static void CreateLogicalDevice(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                                VkDevice* device, VkQueue* graphicsQueue, VkQueue* presentQueue) {
    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures deviceFeatures = {};

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = 1;
    createInfo.pQueueCreateInfos       = &queueCreateInfo;
    createInfo.pEnabledFeatures        = &deviceFeatures;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    // These used to ask for family 0 unconditionally, ignoring the family we
    // just created the queue on.
    vkGetDeviceQueue(*device, queueFamilyIndex, 0, graphicsQueue);
    vkGetDeviceQueue(*device, queueFamilyIndex, 0, presentQueue);
}

static void CreateSwapChain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
                            VkSwapchainKHR* swapChain, std::vector<VkImage>& swapChainImages,
                            VkFormat* swapChainImageFormat, VkExtent2D* swapChainExtent) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (formatCount == 0) {
        throw std::runtime_error("Surface reports no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount,
                                                  presentModes.data());
        for (const auto& availableMode : presentModes) {
            if (availableMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = availableMode;
                break;
            }
        }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == UINT32_MAX) {
        int width = 0, height = 0;
        glfwGetFramebufferSize(Render::glfwWindow, &width, &height);
        extent.width  = static_cast<uint32_t>(width);
        extent.height = static_cast<uint32_t>(height);

        if (extent.width  < capabilities.minImageExtent.width)  extent.width  = capabilities.minImageExtent.width;
        if (extent.width  > capabilities.maxImageExtent.width)  extent.width  = capabilities.maxImageExtent.width;
        if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;
        if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface          = surface;
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = surfaceFormat.format;
    createInfo.imageColorSpace  = surfaceFormat.colorSpace;
    createInfo.imageExtent      = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform     = capabilities.currentTransform;
    createInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode      = presentMode;
    createInfo.clipped          = VK_TRUE;
    createInfo.oldSwapchain     = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    vkGetSwapchainImagesKHR(device, *swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, *swapChain, &imageCount, swapChainImages.data());

    *swapChainImageFormat = surfaceFormat.format;
    *swapChainExtent      = extent;
}

static void CreateImageViews(VkDevice device, const std::vector<VkImage>& swapChainImages,
                             VkFormat swapChainImageFormat,
                             std::vector<VkImageView>& swapChainImageViews) {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image    = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format   = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel   = 0;
        createInfo.subresourceRange.levelCount     = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount     = 1;

        vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]);
    }
}

static void CreateRenderPass(VkDevice device, VkFormat swapChainImageFormat,
                             VkRenderPass* renderPass) {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format         = swapChainImageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments    = &colorAttachment;
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

static void CreateFramebuffers(VkDevice device, const std::vector<VkImageView>& swapChainImageViews,
                               VkRenderPass renderPass, VkExtent2D swapChainExtent,
                               std::vector<VkFramebuffer>& swapChainFramebuffers) {
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        VkImageView attachments[] = { swapChainImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments    = attachments;
        framebufferInfo.width           = swapChainExtent.width;
        framebufferInfo.height          = swapChainExtent.height;
        framebufferInfo.layers          = 1;

        vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]);
    }
}

static void CreateCommandPool(VkDevice device, uint32_t queueFamilyIndex,
                              VkCommandPool* commandPool) {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;   // was hardcoded to 0
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

static void CreateCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                 uint32_t commandBufferCount,
                                 std::vector<VkCommandBuffer>& commandBuffers) {
    commandBuffers.resize(commandBufferCount);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = commandBufferCount;

    vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
}

static void DestroySyncObjects() {
    for (auto s : Render::imageAvailableSemaphores) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(Render::device, s, nullptr);
    }
    for (auto s : Render::renderFinishedSemaphores) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(Render::device, s, nullptr);
    }
    for (auto f : Render::inFlightFences) {
        if (f != VK_NULL_HANDLE) vkDestroyFence(Render::device, f, nullptr);
    }
    Render::imageAvailableSemaphores.clear();
    Render::renderFinishedSemaphores.clear();
    Render::inFlightFences.clear();
    Render::imagesInFlight.clear();
}

static void CreateSyncObjects() {
    // imageAvailable is per frame-in-flight, but renderFinished must be per
    // swapchain *image*: signalling the same semaphore from two submissions
    // that target different images is a validation error and can deadlock
    // present.
    Render::imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    Render::renderFinishedSemaphores.resize(Render::swapChainImages.size(), VK_NULL_HANDLE);
    Render::inFlightFences.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    Render::imagesInFlight.assign(Render::swapChainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(Render::device, &semaphoreInfo, nullptr,
                          &Render::imageAvailableSemaphores[i]);
        vkCreateFence(Render::device, &fenceInfo, nullptr, &Render::inFlightFences[i]);
    }
    for (size_t i = 0; i < Render::swapChainImages.size(); i++) {
        vkCreateSemaphore(Render::device, &semaphoreInfo, nullptr,
                          &Render::renderFinishedSemaphores[i]);
    }
}

static void DestroySwapChainObjects() {
    for (auto framebuffer : Render::swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(Render::device, framebuffer, nullptr);
        }
    }
    Render::swapChainFramebuffers.clear();

    for (auto imageView : Render::swapChainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(Render::device, imageView, nullptr);
        }
    }
    Render::swapChainImageViews.clear();

    if (Render::swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(Render::device, Render::swapChain, nullptr);
        Render::swapChain = VK_NULL_HANDLE;
    }
}

static void RecreateSwapChain() {
    // A minimized window reports a 0x0 framebuffer, and creating a swapchain
    // with a zero extent is invalid. Idle until it comes back.
    int width = 0, height = 0;
    glfwGetFramebufferSize(Render::glfwWindow, &width, &height);
    while (width == 0 || height == 0) {
        if (glfwWindowShouldClose(Render::glfwWindow)) {
            return;
        }
        glfwWaitEvents();
        glfwGetFramebufferSize(Render::glfwWindow, &width, &height);
    }

    vkDeviceWaitIdle(Render::device);

    DestroySwapChainObjects();

    CreateSwapChain(Render::physicalDevice, Render::device, Render::surface, &Render::swapChain,
                    Render::swapChainImages, &Render::swapChainImageFormat,
                    &Render::swapChainExtent);
    CreateImageViews(Render::device, Render::swapChainImages, Render::swapChainImageFormat,
                     Render::swapChainImageViews);
    CreateFramebuffers(Render::device, Render::swapChainImageViews, Render::renderPass,
                       Render::swapChainExtent, Render::swapChainFramebuffers);

    if (!Render::commandBuffers.empty()) {
        vkFreeCommandBuffers(Render::device, Render::commandPool,
                             static_cast<uint32_t>(Render::commandBuffers.size()),
                             Render::commandBuffers.data());
    }
    CreateCommandBuffers(Render::device, Render::commandPool,
                         static_cast<uint32_t>(Render::swapChainFramebuffers.size()),
                         Render::commandBuffers);

    // The image count can change across a resize, so the per-image semaphores
    // have to be rebuilt too.
    DestroySyncObjects();
    CreateSyncObjects();
    Render::currentFrame = 0;

    if (g_imguiVulkanReady) {
        ImGui_ImplVulkan_SetMinImageCount(2);
    }
}

// ---------------------------------------------------------------------------
// Init / teardown
// ---------------------------------------------------------------------------

bool Render::InitVulkan()
{
    try {
        CreateGLFWWindow();

        CreateInstance(&instance);
        PickPhysicalDevice(instance, &physicalDevice);

        if (glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface!");
        }

        if (!FindQueueFamily(physicalDevice, surface, &queueFamilyIndex)) {
            throw std::runtime_error("No graphics queue family with present support");
        }

        CreateLogicalDevice(physicalDevice, queueFamilyIndex, &device, &graphicsQueue, &presentQueue);

        CreateSwapChain(physicalDevice, device, surface, &swapChain, swapChainImages,
                        &swapChainImageFormat, &swapChainExtent);
        CreateImageViews(device, swapChainImages, swapChainImageFormat, swapChainImageViews);
        CreateRenderPass(device, swapChainImageFormat, &renderPass);
        CreateFramebuffers(device, swapChainImageViews, renderPass, swapChainExtent,
                           swapChainFramebuffers);
        CreateCommandPool(device, queueFamilyIndex, &commandPool);
        CreateCommandBuffers(device, commandPool,
                             static_cast<uint32_t>(swapChainFramebuffers.size()), commandBuffers);
        CreateSyncObjects();

        ImGui::CreateContext();
        g_imguiContext = true;

        ImGuiIO& io = ImGui::GetIO();

#ifdef _WIN32
        imGuiMenu::normalText   = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 15.f);
        imGuiMenu::titleText    = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 16.f);
        imGuiMenu::subTitleText = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 15.f);
        imGuiMenu::highlightText= io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanai.ttf", 13.f);
#elif defined(__linux__)
        const char* linuxFontPaths[] = {
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            nullptr
        };

        for (int i = 0; linuxFontPaths[i] != nullptr; i++) {
            // Check the file exists first. Handing ImGui a path that is not
            // there makes it log a "Could not load font file!" error (and
            // assert in debug builds) for every distribution that happens not
            // to ship that particular font -- which is most of them, since
            // this list is tried in order.
            FILE* probe = fopen(linuxFontPaths[i], "rb");
            if (!probe) {
                continue;
            }
            fclose(probe);

            if (imGuiMenu::normalText == nullptr) {
                imGuiMenu::normalText = io.Fonts->AddFontFromFileTTF(linuxFontPaths[i], 15.f);
            }
            if (imGuiMenu::titleText == nullptr) {
                imGuiMenu::titleText = io.Fonts->AddFontFromFileTTF(linuxFontPaths[i], 16.f);
            }
            if (imGuiMenu::normalText && imGuiMenu::titleText) {
                break;
            }
        }
#endif

        // Whatever happened above, every font pointer must be valid: ImGui
        // asserts on a null PushFont.
        if (imGuiMenu::normalText == nullptr) {
            imGuiMenu::normalText = io.Fonts->AddFontDefault();
        }
        if (imGuiMenu::titleText == nullptr) {
            imGuiMenu::titleText = imGuiMenu::normalText;
        }
        if (imGuiMenu::subTitleText == nullptr) {
            imGuiMenu::subTitleText = imGuiMenu::titleText;
        }
        if (imGuiMenu::highlightText == nullptr) {
            imGuiMenu::highlightText = imGuiMenu::normalText;
        }

        ImGui::StyleColorsDark();
        // Applied once, not rebuilt on every frame.
        imGuiMenu::setStyle();

        if (!ImGui_ImplGlfw_InitForVulkan(glfwWindow, true)) {
            throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
        }
        g_imguiGlfwReady = true;

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance           = instance;
        init_info.PhysicalDevice     = physicalDevice;
        init_info.Device             = device;
        init_info.QueueFamily        = queueFamilyIndex;
        init_info.Queue              = graphicsQueue;
        init_info.PipelineCache      = VK_NULL_HANDLE;
        init_info.DescriptorPool     = VK_NULL_HANDLE;
        init_info.DescriptorPoolSize = 1000;
        init_info.Allocator          = nullptr;
        init_info.MinImageCount      = 2;
        init_info.ImageCount         = static_cast<uint32_t>(swapChainImages.size());
        init_info.CheckVkResultFn    = nullptr;

        init_info.PipelineInfoMain.RenderPass  = renderPass;
        init_info.PipelineInfoMain.Subpass     = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        if (!ImGui_ImplVulkan_Init(&init_info)) {
            throw std::runtime_error("ImGui_ImplVulkan_Init failed");
        }
        g_imguiVulkanReady = true;

        return true;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Failed to initialize Vulkan: %s\n", e.what());
        Cleanup();
        return false;
    }
}

void Render::Cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    if (g_imguiVulkanReady) {
        ImGui_ImplVulkan_Shutdown();
        g_imguiVulkanReady = false;
    }
    if (g_imguiGlfwReady) {
        ImGui_ImplGlfw_Shutdown();
        g_imguiGlfwReady = false;
    }
    if (g_imguiContext) {
        ImGui::DestroyContext();
        g_imguiContext = false;
    }

    if (device != VK_NULL_HANDLE) {
        DestroySyncObjects();

        if (!commandBuffers.empty() && commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, commandPool,
                                 static_cast<uint32_t>(commandBuffers.size()),
                                 commandBuffers.data());
            commandBuffers.clear();
        }
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }

        DestroySwapChainObjects();

        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    if (glfwWindow) {
        glfwDestroyWindow(glfwWindow);
        glfwWindow = nullptr;
    }
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// Render loop
// ---------------------------------------------------------------------------

void Render::RenderLoop(void (*func)()) {
    using clock = std::chrono::high_resolution_clock;
    auto  lastTime   = clock::now();
    int   frameCount = 0;
    float fps        = 0.0f;

    while (!glfwWindowShouldClose(glfwWindow)) {
        glfwPollEvents();

        const auto now = clock::now();
        frameCount++;
        const float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= 1.0f) {
            fps        = frameCount / elapsed;
            frameCount = 0;
            lastTime   = now;
        }

        // Input is handled entirely by the ImGui GLFW backend. This used to
        // also poke io.MousePos/io.MouseDown by hand, which the backend then
        // overwrote, and toggled the menu from three places at once.

        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
        if (fbWidth == 0 || fbHeight == 0) {
            glfwWaitEvents();
            continue;
        }

        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
                                                imageAvailableSemaphores[currentFrame],
                                                VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            RecreateSwapChain();
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "vkAcquireNextImageKHR failed (%d)\n", (int)result);
            break;
        }

        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        vkResetCommandBuffer(commandBuffers[imageIndex], 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass        = renderPass;
        renderPassInfo.framebuffer       = swapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChainExtent;

        VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues    = &clearColor;

        vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (func) {
            func();
        }

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "FPS: %.1f", fps);
        // Foreground, not background: the menu window covers the whole
        // viewport, so anything on the background draw list is painted over
        // and the counter was never actually visible.
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(ImGui::GetIO().DisplaySize.x - 90.0f, 6.0f),
            IM_COL32(255, 255, 0, 255), buffer);

        if (menutoggle) {
            imGuiMenu::renderMenu(state);
        }

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffers[imageIndex]);

        vkCmdEndRenderPass(commandBuffers[imageIndex]);
        vkEndCommandBuffer(commandBuffers[imageIndex]);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore          waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
        VkPipelineStageFlags waitStages[]     = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores    = waitSemaphores;
        submitInfo.pWaitDstStageMask  = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffers[imageIndex];

        // Per-image, so two in-flight frames never signal the same semaphore.
        VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[imageIndex] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = signalSemaphores;

        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
            fprintf(stderr, "vkQueueSubmit failed\n");
            break;
        }

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = signalSemaphores;

        VkSwapchainKHR swapChains[] = { swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains    = swapChains;
        presentInfo.pImageIndices  = &imageIndex;

        result = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapChain();
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(device);
}
