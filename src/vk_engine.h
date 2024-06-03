#pragma once

#include "vk_types.h"

constexpr unsigned int FRAME_OVERLAP = 2;
struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

    VkSemaphore _swapchainSemaphore, _renderSemaphore;
	VkFence _renderFence;
};

class VkEngine {
public:
    bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1700 , 900 };

	struct GLFWwindow* _window{ nullptr };

    VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	VkDevice _device;
	VkSurfaceKHR _surface;

    VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

    FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
    
    VkEngine();
    ~VkEngine();

	static VkEngine& Get();

    void draw();
    void run();

private:
	void init_vulkan();
	
    void init_swapchain();
    void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();

	void init_commands();
	void init_sync_structures();
};