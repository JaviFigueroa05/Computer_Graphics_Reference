#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"

#include <VkBootstrap.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <chrono>
#include <thread>

constexpr bool bUseValidationLayers = true;

VkEngine* loadedEngine = nullptr;

VkEngine& VkEngine::Get() { return *loadedEngine; }

VkEngine::VkEngine()
{
    // The engine is a singleton object.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // Initialize GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        abort();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    _window = glfwCreateWindow(1920, 1080, "Computer Graphics Reference", nullptr, nullptr);
    if (!_window)
    {
        printf("GLFW: Failed to create window\n");
        abort();
    }
    if (!glfwVulkanSupported())
    {
        printf("GLFW: Vulkan Not Supported\n");
        abort();
    }

    init_vulkan();
	init_swapchain();
	init_commands();
	init_sync_structures();
    
    _isInitialized = true;
}

VkEngine::~VkEngine()
{
    if (_isInitialized)
    {
        vkDeviceWaitIdle(_device);

		for (int i = 0; i < FRAME_OVERLAP; i++) {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
		}

        destroy_swapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);
		
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);

        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    loadedEngine = nullptr;
}

void VkEngine::draw()
{
    check_vk_result(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
	check_vk_result(vkResetFences(_device, 1, &get_current_frame()._renderFence));

    uint32_t swapchainImageIndex;
	check_vk_result(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex));

	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

	check_vk_result(vkResetCommandBuffer(cmd, 0));
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	check_vk_result(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

}

void VkEngine::run()
{
    while (!glfwWindowShouldClose(_window))
    {
        glfwPollEvents();

        if (stop_rendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        draw();
    }
}

void VkEngine::init_vulkan()
{
    // Create Vulkan Instance
    vkb::Instance vkb_inst;
    {
        vkb::InstanceBuilder builder;
        auto inst_ret = builder.set_app_name("Computer Graphics Reference")
            .request_validation_layers(bUseValidationLayers)
            .use_default_debug_messenger()
            .require_api_version(1, 3, 0)
            .build();

        vkb_inst = inst_ret.value();
        _instance = vkb_inst.instance;
        _debug_messenger = vkb_inst.debug_messenger;
    }
    // Create a surface for the window
    {
        VkResult err = glfwCreateWindowSurface(_instance, _window, nullptr, &_surface);
        check_vk_result(err);
    }
    // Create device
    vkb::Device vkbDevice;
    {
        VkPhysicalDeviceVulkan13Features features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features.dynamicRendering = true;
        features.synchronization2 = true;

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.bufferDeviceAddress = true;
        features12.descriptorIndexing = true;

        vkb::PhysicalDeviceSelector selector{ vkb_inst };
        vkb::PhysicalDevice physicalDevice = selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_surface(_surface)
            .select()
            .value();

        vkb::DeviceBuilder deviceBuilder{ physicalDevice };
        vkbDevice = deviceBuilder.build().value();

        _chosenGPU = physicalDevice.physical_device;
        _device = vkbDevice.device;
    }
    // Get the graphics queue
    {
        _graphicsQueue = vkbDevice
            .get_queue(vkb::QueueType::graphics)
            .value();
	    _graphicsQueueFamily = vkbDevice
            .get_queue_index(vkb::QueueType::graphics)
            .value();
    }

}

void VkEngine::init_swapchain()
{
    create_swapchain(_windowExtent.width, _windowExtent.height);
}

void VkEngine::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR {
            .format = _swapchainImageFormat, 
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR 
        })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	_swapchainExtent = vkbSwapchain.extent;
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VkEngine::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	for (int i = 0; i < _swapchainImageViews.size(); i++) {
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}

void VkEngine::init_commands()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	for (int i = 0; i < FRAME_OVERLAP; i++) {
		check_vk_result(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);
		
        check_vk_result(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
	}
}

void VkEngine::init_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		check_vk_result(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		check_vk_result(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
		check_vk_result(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
	}
}