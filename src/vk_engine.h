#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)();
		}
		deletors.clear();
	}
};

constexpr unsigned int FRAME_OVERLAP = 2;
struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

    VkSemaphore _swapchainSemaphore, _renderSemaphore;
	VkFence _renderFence;
    
    DeletionQueue _frameDeletionQueue;
};

struct ComputePushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
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

    DeletionQueue _mainDeletionQueue;

    VmaAllocator _allocator;

    AllocatedImage _drawImage;
	VkExtent2D _drawExtent;

    DescriptorAllocator globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipelineLayout _gradientPipelineLayout;

	VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;

	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{0};

	    
    VkEngine();
    ~VkEngine();

	static VkEngine& Get();

    void draw();
    void run();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

private:
	void init_vulkan();
	
    void init_swapchain();
    void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();

	void init_commands();
	void init_sync_structures();

    void init_descriptors();

    void draw_background(VkCommandBuffer cmd);

    void init_pipelines();
	void init_background_pipelines();

	void init_imgui();
	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
};