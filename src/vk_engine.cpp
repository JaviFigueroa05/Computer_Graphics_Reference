#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_images.h"
#include "vk_pipelines.h"

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

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
    init_descriptors();
    init_pipelines();
    init_imgui();
    
    init_default_data();

    _isInitialized = true;
}

VkEngine::~VkEngine()
{
    if (_isInitialized)
    {
        vkDeviceWaitIdle(_device);
        for (auto& frame : _frames) {}
        _mainDeletionQueue.flush();

		for (int i = 0; i < FRAME_OVERLAP; i++) {
            _frames[i]._frameDeletionQueue.flush();
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device ,_frames[i]._swapchainSemaphore, nullptr);
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
    // Wait for last frame to finish
    {
        check_vk_result(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
        get_current_frame()._frameDeletionQueue.flush();
        check_vk_result(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    }
    // Acquire the next image
    uint32_t swapchainImageIndex;
    {
    	check_vk_result(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex));
    }
    // Start command buffer recording
	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
    {
        check_vk_result(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        _drawExtent.width = _drawImage.imageExtent.width;
	    _drawExtent.height = _drawImage.imageExtent.height;
        check_vk_result(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    }
    // Clear Screen
    {
        // draw to the image
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        draw_background(cmd);
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        draw_geometry(cmd);
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // move image to swapchain
        vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);
        vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);
    	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        // register to command buffer
        check_vk_result(vkEndCommandBuffer(cmd));
    }
    // Submit command buffer
    {
        VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);	
        VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
        VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);	
        VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo,&signalInfo,&waitInfo);	
        check_vk_result(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));
    }
    // Present frame
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = nullptr;
        presentInfo.pSwapchains = &_swapchain;
        presentInfo.swapchainCount = 1;
        presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pImageIndices = &swapchainImageIndex;
        check_vk_result(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

        _frameNumber++;
    }
}

void VkEngine::draw_background(VkCommandBuffer cmd)
{
    ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

    // bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	// bind the descriptor set containing the draw image for the compute pipeline
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);


    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void VkEngine::draw_geometry(VkCommandBuffer cmd)
{
    //begin a render pass  connected to our draw image
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, nullptr);
	vkCmdBeginRendering(cmd, &renderInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

	//set dynamic viewport and scissor
	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = _drawExtent.width;
	viewport.height = _drawExtent.height;
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = _drawExtent.width;
	scissor.extent.height = _drawExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	//launch a draw command to draw 3 vertices
	vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

	GPUDrawPushConstants push_constants;
	push_constants.worldMatrix = glm::mat4{ 1.f };
	push_constants.vertexBuffer = rectangle.vertexBufferAddress;

	vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
	vkCmdBindIndexBuffer(cmd, rectangle.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);

	vkCmdEndRendering(cmd);
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
        
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("background")) {
			
			ComputeEffect& selected = backgroundEffects[currentBackgroundEffect];
		
			ImGui::Text("Selected effect: %s", selected.name);
		
			ImGui::SliderInt("Effect Index", &currentBackgroundEffect,0, backgroundEffects.size() - 1);
		
			ImGui::InputFloat4("data1",(float*)& selected.data.data1);
			ImGui::InputFloat4("data2",(float*)& selected.data.data2);
			ImGui::InputFloat4("data3",(float*)& selected.data.data3);
			ImGui::InputFloat4("data4",(float*)& selected.data.data4);
		}
		ImGui::End();

        ImGui::Render();

        draw();
    }
}

void VkEngine::init_vulkan()
{
    // Create Vulkan Instance
    vkb::Instance vkb_inst;
    {
        vkb::InstanceBuilder builder;
        auto inst_ret = builder
            .set_app_name("Computer Graphics Reference")
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
    // initialize the memory allocator
    {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = _chosenGPU;
        allocatorInfo.device = _device;
        allocatorInfo.instance = _instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        vmaCreateAllocator(&allocatorInfo, &_allocator);

        _mainDeletionQueue.push_function([this]() {
            vmaDestroyAllocator(_allocator);
        });
    }
}

void VkEngine::init_swapchain()
{
    create_swapchain(_windowExtent.width, _windowExtent.height);

    //draw image size will match the window
	VkExtent3D drawImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	//hardcoding the draw format to 32 bit float
	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

	//for the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	check_vk_result(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

	//add to deletion queues
	_mainDeletionQueue.push_function([this]() {
		vkDestroyImageView(_device, _drawImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
	});

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

    // immediate command buffer
    check_vk_result(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);
	check_vk_result(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));
    _mainDeletionQueue.push_function([this]() { 
	    vkDestroyCommandPool(_device, _immCommandPool, nullptr);
	});
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

    check_vk_result(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	_mainDeletionQueue.push_function([this]() { vkDestroyFence(_device, _immFence, nullptr); });
}

void VkEngine::init_descriptors()
{
	std::vector<DescriptorAllocator::PoolSizeRatio> sizes = 
        {{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }};
	globalDescriptorAllocator.init_pool(_device, 10, sizes);

	//make the descriptor set layout for our compute draw
	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

    //allocate a descriptor set for our draw image
	_drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);	

	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imgInfo.imageView = _drawImage.imageView;
	
	VkWriteDescriptorSet drawImageWrite = {};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWrite.pNext = nullptr;
	drawImageWrite.dstBinding = 0;
	drawImageWrite.dstSet = _drawImageDescriptors;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

    _mainDeletionQueue.push_function([this]() {
		globalDescriptorAllocator.destroy_pool(_device);
		vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
	});
}

void VkEngine::init_pipelines()
{
	init_background_pipelines();

    init_triangle_pipeline();
    init_mesh_pipeline();
}

void VkEngine::init_background_pipelines()
{
    // Create pipeline layout
    {
        VkPipelineLayoutCreateInfo computeLayout{};
        computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayout.pNext = nullptr;
        computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
        computeLayout.setLayoutCount = 1;

        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(ComputePushConstants) ;
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        computeLayout.pPushConstantRanges = &pushConstant;
        computeLayout.pushConstantRangeCount = 1;

        check_vk_result(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));
    }
    // Create compute pipeline
    {
        VkShaderModule gradientShader;
        if (!vkutil::load_shader_module("./gradient_color.comp.spv", _device, &gradientShader)) {
            std::cout << "Error when building the compute shader" << std::endl;
        }

        VkShaderModule skyShader;
        if (!vkutil::load_shader_module("./sky.comp.spv", _device, &skyShader)) {
            std::cout << "Error when building the compute shader" << std::endl;
        }

        VkPipelineShaderStageCreateInfo stageinfo{};
        stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageinfo.pNext = nullptr;
        stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageinfo.module = gradientShader;
        stageinfo.pName = "main";

        VkComputePipelineCreateInfo computePipelineCreateInfo{};
        computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.pNext = nullptr;
        computePipelineCreateInfo.layout = _gradientPipelineLayout;
        computePipelineCreateInfo.stage = stageinfo;

        ComputeEffect gradient;
        gradient.layout = _gradientPipelineLayout;
        gradient.name = "gradient";
        gradient.data = {};
        gradient.data.data1 = glm::vec4(1, 0, 0, 1);
        gradient.data.data2 = glm::vec4(0, 0, 1, 1);
        
        check_vk_result(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &gradient.pipeline));

        computePipelineCreateInfo.stage.module = skyShader;

        ComputeEffect sky;
        sky.layout = _gradientPipelineLayout;
        sky.name = "sky";
        sky.data = {};
        sky.data.data1 = glm::vec4(0.1, 0.2, 0.4 ,0.97);

        check_vk_result(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

        backgroundEffects.push_back(gradient);
        backgroundEffects.push_back(sky);

       	vkDestroyShaderModule(_device, gradientShader, nullptr);
        vkDestroyShaderModule(_device, skyShader, nullptr);
    	_mainDeletionQueue.push_function([=, this]() {
		    vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
		    vkDestroyPipeline(_device, sky.pipeline, nullptr);
		    vkDestroyPipeline(_device, gradient.pipeline, nullptr);
		});
    }
}

void VkEngine::init_triangle_pipeline()
{
    VkShaderModule triangleFragShader;
	if (!vkutil::load_shader_module("./colored_triangle.frag.spv", _device, &triangleFragShader)) {
		std::cout << "Error when building the triangle fragment shader module"  << std::endl;
	}

	VkShaderModule triangleVertexShader;
	if (!vkutil::load_shader_module("./colored_triangle.vert.spv", _device, &triangleVertexShader)) {
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
	check_vk_result(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

    PipelineBuilder pipelineBuilder;

	pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
	pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.disable_depthtest();
	pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
	pipelineBuilder.set_depth_format(VK_FORMAT_UNDEFINED);

	_trianglePipeline = pipelineBuilder.build_pipeline(_device);

	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
		vkDestroyPipeline(_device, _trianglePipeline, nullptr);
	});
}

void VkEngine::init_mesh_pipeline()
{
    VkShaderModule triangleFragShader;
	if (!vkutil::load_shader_module("./colored_triangle.frag.spv", _device, &triangleFragShader)) {
		std::cout << "Error when building the triangle fragment shader module"  << std::endl;
	}

	VkShaderModule triangleVertexShader;
	if (!vkutil::load_shader_module("./colored_triangle_mesh.vert.spv", _device, &triangleVertexShader)) {
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	
    VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
	pipeline_layout_info.pPushConstantRanges = &bufferRange;
	pipeline_layout_info.pushConstantRangeCount = 1;

	check_vk_result(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_meshPipelineLayout));

    PipelineBuilder pipelineBuilder;

	pipelineBuilder._pipelineLayout = _meshPipelineLayout;
	pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depthtest();
	// pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
	pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
	pipelineBuilder.set_depth_format(VK_FORMAT_UNDEFINED);
 
	_meshPipeline = pipelineBuilder.build_pipeline(_device);

	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
	});
}

void VkEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    check_vk_result(vkResetFences(_device, 1, &_immFence));
	check_vk_result(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	check_vk_result(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	check_vk_result(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	check_vk_result(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	check_vk_result(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void VkEngine::init_imgui()
{
    // 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	check_vk_result(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();
 
	// this initializes imgui for GLFW 
    ImGui_ImplGlfw_InitForVulkan(_window, true);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;
	

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

	// add the destroy the imgui created structures
	_mainDeletionQueue.push_function([this, imguiPool]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
	});
}

void VkEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

AllocatedBuffer VkEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	AllocatedBuffer newBuffer;

	check_vk_result(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
		&newBuffer.info));

	return newBuffer;
}

void VkEngine::destroy_buffer(const AllocatedBuffer &buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

GPUMeshBuffers VkEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	newSurface.vertexBuffer = create_buffer(
        vertexBufferSize, 
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	VkBufferDeviceAddressInfo deviceAdressInfo{ 
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = newSurface.vertexBuffer.buffer 
    };

	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	newSurface.indexBuffer = create_buffer(
        indexBufferSize, 
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging = create_buffer(
        vertexBufferSize + indexBufferSize, 
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
        VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.allocation->GetMappedData();

	memcpy(data, vertices.data(), vertexBufferSize);
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
	});

	destroy_buffer(staging);

	return newSurface;
}

void VkEngine::init_default_data()
{
    std::array<Vertex,4> rect_vertices;

	rect_vertices[0].position = {0.5,-0.5, 0};
	rect_vertices[1].position = {0.5,0.5, 0};
	rect_vertices[2].position = {-0.5,-0.5, 0};
	rect_vertices[3].position = {-0.5,0.5, 0};

	rect_vertices[0].color = {0,0, 0,1};
	rect_vertices[1].color = { 0.5,0.5,0.5 ,1};
	rect_vertices[2].color = { 1,0, 0,1 };
	rect_vertices[3].color = { 0,1, 0,1 };

	std::array<uint32_t,6> rect_indices;

	rect_indices[0] = 0;
	rect_indices[1] = 1;
	rect_indices[2] = 2;

	rect_indices[3] = 2;
	rect_indices[4] = 1;
	rect_indices[5] = 3;

	rectangle = uploadMesh(rect_indices, rect_vertices);

	//delete the rectangle data on engine shutdown
	_mainDeletionQueue.push_function([&](){
		destroy_buffer(rectangle.indexBuffer);
		destroy_buffer(rectangle.vertexBuffer);
	});
}
