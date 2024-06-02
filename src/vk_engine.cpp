#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <chrono>
#include <thread>

constexpr bool bUseValidationLayers = false;

VkEngine* loadedEngine = nullptr;

VkEngine& VkEngine::Get() { return *loadedEngine; }

VkEngine::VkEngine()
{
    // The engine is a singleton object.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // Initialize GLFW
    if (!glfwInit())
        abort();
    // glfwSetErrorCallback(glfw_error_callback);
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
    
    // Init Successful
    _isInitialized = true;
}

VkEngine::~VkEngine()
{
    if (_isInitialized)
    {
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    loadedEngine = nullptr;
}

void VkEngine::draw()
{
    // Draw code here
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