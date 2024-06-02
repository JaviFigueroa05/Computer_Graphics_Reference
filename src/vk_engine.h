#pragma once

#include "vk_types.h"

class VkEngine {
public:
    VkEngine();
    ~VkEngine();

    bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1700 , 900 };

	struct GLFWwindow* _window{ nullptr };

	static VkEngine& Get();

    void draw();
    void run();
};