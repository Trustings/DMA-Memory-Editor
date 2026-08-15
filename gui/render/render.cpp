#include "render.hpp"

// Global variables
GLFWwindow* Render::glfwWindow = nullptr;

bool state = true;
bool menutoggle = true;

VkInstance Render::instance = VK_NULL_HANDLE;
VkPhysicalDevice Render::physicalDevice = VK_NULL_HANDLE;
VkDevice Render::device = VK_NULL_HANDLE;
VkQueue Render::graphicsQueue = VK_NULL_HANDLE;
VkQueue Render::presentQueue = VK_NULL_HANDLE;
VkSurfaceKHR Render::surface = VK_NULL_HANDLE;
VkSwapchainKHR Render::swapChain = VK_NULL_HANDLE;
VkFormat Render::swapChainImageFormat;
VkExtent2D Render::swapChainExtent;
std::vector<VkImage> Render::swapChainImages;
std::vector<VkImageView> Render::swapChainImageViews;
VkRenderPass Render::renderPass = VK_NULL_HANDLE;
VkPipelineLayout Render::pipelineLayout = VK_NULL_HANDLE;
VkPipeline Render::graphicsPipeline = VK_NULL_HANDLE;
std::vector<VkFramebuffer> Render::swapChainFramebuffers;
VkCommandPool Render::commandPool = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> Render::commandBuffers;
std::vector<VkSemaphore> Render::imageAvailableSemaphores;
std::vector<VkSemaphore> Render::renderFinishedSemaphores;
std::vector<VkFence> Render::inFlightFences;
std::vector<VkFence> Render::imagesInFlight;
size_t Render::currentFrame = 0;

const int MAX_FRAMES_IN_FLIGHT = 2;

void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_INSERT && action == GLFW_PRESS) {
        menutoggle = !menutoggle;
    }
}

void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    // Trigger swapchain recreation on resize
    // This will be handled in the render loop
}

void Render::CreateGLFWWindow() {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Make window resizable and full-screen
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);            // Make resizable
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);           // No window decorations
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);            // Always on top
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);           // Don't start maximized
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    // Create window with standard dimensions
    const int windowWidth = 800;
    const int windowHeight = 650;

    glfwWindow = glfwCreateWindow(windowWidth, windowHeight, "DMA-Memory-Editor", nullptr, nullptr);

    if (!glfwWindow) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Center window on primary monitor
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    int monitorX, monitorY;
    glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    glfwSetWindowPos(glfwWindow,
                     monitorX + (mode->width - windowWidth) / 2,
                     monitorY + (mode->height - windowHeight) / 2);

    // Set callbacks
    glfwSetKeyCallback(glfwWindow, glfwKeyCallback);
    glfwSetFramebufferSizeCallback(glfwWindow, glfwFramebufferSizeCallback);

    // Enable window resizing
    glfwSetWindowAttrib(glfwWindow, GLFW_RESIZABLE, GLFW_TRUE);
}

// Helper functions for Vulkan
void CreateInstance(VkInstance* instance) {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Key Of David";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // Get required extensions from GLFW
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = 0;

    VkResult result = vkCreateInstance(&createInfo, nullptr, instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

void PickPhysicalDevice(VkInstance instance, VkPhysicalDevice* physicalDevice) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            *physicalDevice = device;
            break;
        }
    }

    if (*physicalDevice == VK_NULL_HANDLE) {
        *physicalDevice = devices[0];
    }
}

uint32_t FindQueueFamily(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);

        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
            return i;
        }
    }
    return 0;
}

void CreateLogicalDevice(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkDevice* device, VkQueue* graphicsQueue, VkQueue* presentQueue) {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceFeatures deviceFeatures = {};

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    vkCreateDevice(physicalDevice, &createInfo, nullptr, device);
    vkGetDeviceQueue(*device, 0, 0, graphicsQueue);
    vkGetDeviceQueue(*device, 0, 0, presentQueue);
}

void CreateSwapChain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
    VkSwapchainKHR* swapChain, std::vector<VkImage>& swapChainImages,
    VkFormat* swapChainImageFormat, VkExtent2D* swapChainExtent) {

    // This function remains almost identical to your original
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    for (const auto& availableMode : presentModes) {
        if (availableMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = availableMode;
            break;
        }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == UINT32_MAX) {
        // Use GLFW to get window size instead of platform-specific code
        int width, height;
        glfwGetFramebufferSize(Render::glfwWindow, &width, &height);
        extent.width = static_cast<uint32_t>(width);
        extent.height = static_cast<uint32_t>(height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    vkCreateSwapchainKHR(device, &createInfo, nullptr, swapChain);

    vkGetSwapchainImagesKHR(device, *swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, *swapChain, &imageCount, swapChainImages.data());

    *swapChainImageFormat = surfaceFormat.format;
    *swapChainExtent = extent;
}

void CreateImageViews(VkDevice device, const std::vector<VkImage>& swapChainImages, VkFormat swapChainImageFormat,
    std::vector<VkImageView>& swapChainImageViews) {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]);
    }
}

void CreateRenderPass(VkDevice device, VkFormat swapChainImageFormat, VkRenderPass* renderPass) {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    vkCreateRenderPass(device, &renderPassInfo, nullptr, renderPass);
}

void CreateFramebuffers(VkDevice device, const std::vector<VkImageView>& swapChainImageViews,
    VkRenderPass renderPass, VkExtent2D swapChainExtent,
    std::vector<VkFramebuffer>& swapChainFramebuffers) {
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]);
    }
}

void CreateCommandPool(VkDevice device, VkCommandPool* commandPool) {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    vkCreateCommandPool(device, &poolInfo, nullptr, commandPool);
}

void CreateCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
    std::vector<VkCommandBuffer>& commandBuffers) {
    commandBuffers.resize(commandBufferCount);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = commandBufferCount;

    vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());

    /*
    // Record initial command buffers
    for (size_t i = 0; i < commandBuffers.size(); i++) {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = Render::renderPass;
        renderPassInfo.framebuffer = Render::swapChainFramebuffers[i];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = Render::swapChainExtent;

        VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(commandBuffers[i]);

        vkEndCommandBuffer(commandBuffers[i]);
    }
    */
}

void RecreateSwapChain() {
    vkDeviceWaitIdle(Render::device);

    // Cleanup old swapchain
    for (auto framebuffer : Render::swapChainFramebuffers) {
        vkDestroyFramebuffer(Render::device, framebuffer, nullptr);
    }
    for (auto imageView : Render::swapChainImageViews) {
        vkDestroyImageView(Render::device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(Render::device, Render::swapChain, nullptr);

    // Recreate - remove the GLFWwindow parameter
    CreateSwapChain(Render::physicalDevice, Render::device, Render::surface, &Render::swapChain,
        Render::swapChainImages, &Render::swapChainImageFormat, &Render::swapChainExtent);
    CreateImageViews(Render::device, Render::swapChainImages, Render::swapChainImageFormat, Render::swapChainImageViews);
    CreateFramebuffers(Render::device, Render::swapChainImageViews, Render::renderPass, Render::swapChainExtent, Render::swapChainFramebuffers);

    // Recreate command buffers
    vkFreeCommandBuffers(Render::device, Render::commandPool, static_cast<uint32_t>(Render::commandBuffers.size()), Render::commandBuffers.data());
    CreateCommandBuffers(Render::device, Render::commandPool, static_cast<uint32_t>(Render::swapChainFramebuffers.size()), Render::commandBuffers);
}

void CreateSyncObjects(VkDevice device, std::vector<VkSemaphore>& imageAvailableSemaphores,
    std::vector<VkSemaphore>& renderFinishedSemaphores,
    std::vector<VkFence>& inFlightFences, std::vector<VkFence>& imagesInFlight) {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    imagesInFlight.resize(Render::swapChainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]);
    }
}

bool Render::InitVulkan()
{
    try {
        // Create GLFW window for both Windows and Linux
        CreateGLFWWindow();

        // Initialize Vulkan using your existing methods
        CreateInstance(&instance);
        PickPhysicalDevice(instance, &physicalDevice);

        // Create surface using GLFW (replaces platform-specific surface creation)
        if (glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface!");
        }

        uint32_t queueFamilyIndex = FindQueueFamily(physicalDevice, surface);
        CreateLogicalDevice(physicalDevice, queueFamilyIndex, &device, &graphicsQueue, &presentQueue);

        // Create swapchain (no platform-specific parameters needed)
        CreateSwapChain(Render::physicalDevice, Render::device, Render::surface, &Render::swapChain,
            Render::swapChainImages, &Render::swapChainImageFormat, &Render::swapChainExtent);

        CreateImageViews(device, swapChainImages, swapChainImageFormat, swapChainImageViews);
        CreateRenderPass(device, swapChainImageFormat, &renderPass);
        CreateFramebuffers(device, swapChainImageViews, renderPass, swapChainExtent, swapChainFramebuffers);
        CreateCommandPool(device, &commandPool);
        CreateCommandBuffers(device, commandPool, static_cast<uint32_t>(swapChainFramebuffers.size()), commandBuffers);
        CreateSyncObjects(device, imageAvailableSemaphores, renderFinishedSemaphores, inFlightFences, imagesInFlight);

        // Initialize ImGui
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        // Font loading - cross-platform approach
#ifdef _WIN32
        imGuiMenu::normalText = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 15.f);
        imGuiMenu::titleText = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 16.f);
        imGuiMenu::subTitleText = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 15.f);
        imGuiMenu::highlightText = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanai.ttf", 13.f);
        imGuiMenu::espNameText = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 15.f);
#elif defined(__linux__)
        // Try multiple common Linux font paths
        const char* linuxFontPaths[] = {
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            nullptr
        };

        // Load fonts with fallback paths
        for (int i = 0; linuxFontPaths[i] != nullptr; i++) {
            if (imGuiMenu::normalText == nullptr) {
                imGuiMenu::normalText = io.Fonts->AddFontFromFileTTF(linuxFontPaths[i], 15.f);
            }
            if (imGuiMenu::titleText == nullptr) {
                imGuiMenu::titleText = io.Fonts->AddFontFromFileTTF(linuxFontPaths[i], 16.f);
            }
        }

        // If no fonts loaded, use default
        if (imGuiMenu::normalText == nullptr) {
            imGuiMenu::normalText = io.Fonts->AddFontDefault();
            imGuiMenu::titleText = io.Fonts->AddFontDefault();
            imGuiMenu::subTitleText = io.Fonts->AddFontDefault();
            imGuiMenu::highlightText = io.Fonts->AddFontDefault();
            imGuiMenu::espNameText = io.Fonts->AddFontDefault();
        } else {
            imGuiMenu::subTitleText = imGuiMenu::titleText;
            imGuiMenu::highlightText = imGuiMenu::normalText;
            imGuiMenu::espNameText = imGuiMenu::titleText;
        }
#endif

        ImGui::StyleColorsDark();

        // Initialize ImGui with GLFW+Vulkan backend (common for both platforms)
        ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = instance;
        init_info.PhysicalDevice = physicalDevice;
        init_info.Device = device;
        init_info.QueueFamily = queueFamilyIndex;
        init_info.Queue = graphicsQueue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = VK_NULL_HANDLE;
        init_info.DescriptorPoolSize = 1000;
        init_info.Allocator = nullptr;
        init_info.MinImageCount = 2;
        init_info.ImageCount = static_cast<uint32_t>(swapChainImages.size());
        init_info.CheckVkResultFn = nullptr;

        init_info.PipelineInfoMain.RenderPass = Render::renderPass;
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&init_info);

        return true;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Failed to initialize Vulkan: %s\n", e.what());
        return false;
    }
}

void Render::RenderLoop(void (*func)()) {
    using clock = std::chrono::high_resolution_clock;
    auto lastTime = clock::now();
    int frameCount = 0;
    float fps = 0.0f;

    while (!glfwWindowShouldClose(glfwWindow)) {
        glfwPollEvents();

        auto now = clock::now();
        frameCount++;
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= 1.0f) {
            fps = frameCount / elapsed;
            frameCount = 0;
            lastTime = now;
        }

        // Input handling through GLFW
        ImGuiIO& io = ImGui::GetIO();
        double mouseX, mouseY;
        glfwGetCursorPos(glfwWindow, &mouseX, &mouseY);
        io.MousePos = ImVec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
        io.MouseDown[0] = glfwGetMouseButton(glfwWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        // Handle Insert key toggle through GLFW
        if (glfwGetKey(glfwWindow, GLFW_KEY_INSERT) == GLFW_PRESS) {
            static bool insertPressed = false;
            if (!insertPressed) {
                menutoggle = !menutoggle;
                insertPressed = true;
            }
        } else {
            static bool insertPressed = false;
            insertPressed = false;
        }

        // Menu toggle handling with GLFW
        if (glfwGetKey(glfwWindow, GLFW_KEY_INSERT) == GLFW_PRESS) {
                   static bool insertPressed = false;
                   if (!insertPressed) {
                       menutoggle = !menutoggle;
                       insertPressed = true;

                       // Optional: Focus window when menu becomes visible
                       if (menutoggle) {
                           glfwFocusWindow(glfwWindow);
                       }
                   }
               } else {
                   static bool insertPressed = false;
                   insertPressed = false;
               }

        // Vulkan frame rendering (common code - unchanged)
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
            imageAvailableSemaphores[currentFrame],
            VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapChain();
            continue;
        }

        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        // Reset command buffer
        vkResetCommandBuffer(commandBuffers[imageIndex], 0);

        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);

        // Start render pass
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChainExtent;

        VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 0.0f}} };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // ImGui frame setup with GLFW backend
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Call user function
        func();

        // Draw FPS in top-right corner
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "FPS: %.1f", fps);
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(io.DisplaySize.x - 100.0f, 10.0f), IM_COL32(255, 255, 0, 255), buffer);

        // Render menu
        if (menutoggle) {
            imGuiMenu::renderMenu(state);
        }

        // Render ImGui
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffers[imageIndex]);

        // End render pass and command buffer
        vkCmdEndRenderPass(commandBuffers[imageIndex]);
        vkEndCommandBuffer(commandBuffers[imageIndex]);

        // Submit to queue
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

        VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

        // Present
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapChain();
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(device);
}
