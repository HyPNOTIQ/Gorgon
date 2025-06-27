#include <GLFW/glfw3.h>
#include "fmt/ostream.h"
#include <iostream>
//#include <VkBootstrap.h>
#include <thread>
#include <atomic>
#include <glm/glm.hpp>
#include <gsl/gsl>
//#define VOLK_IMPLEMENTATION
//#include <volk.h>

#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
//#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

constexpr auto WIDTH = 800u;
constexpr auto HEIGHT = 600u;

void GlfwErrorCallback(
	const int ErrorCode,
	const char* const Description)
{
	fmt::println(std::cerr, "GLFW Error {}: {}", ErrorCode, Description);
}

//const std::vector<const char*> validationLayers = {
//	"VK_LAYER_KHRONOS_validation"
//};

//#define VKCONFIG

#ifndef VKCONFIG
VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugMessageFunc(
	const vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	const vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
	const vk::DebugUtilsMessengerCallbackDataEXT* const pCallbackData,
	void* /*pUserData*/)
{
	if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
	{
		fmt::println(std::clog, "{}", pCallbackData->pMessage);
	}

	return VK_FALSE;
}
#endif // !VKCONFIG

std::atomic<bool> StopRenderThread(false);

std::vector<const char*> GetRequiredExtensions() {
	uint32_t GlfwExtensionCount;
	const auto GlfwExtensions = glfwGetRequiredInstanceExtensions(&GlfwExtensionCount);

	auto Extensions = std::vector<const char*>(GlfwExtensions, GlfwExtensions + GlfwExtensionCount);

#if !defined(VKCONFIG) && !defined(NDEBUG)
	Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif // !defined(VKCONFIG) && !defined(NDEBUG)

	return Extensions;
}

void RenderThreadFunc()
{
	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	const auto Context = vk::raii::Context();

	const auto ApplicationInfo = vk::ApplicationInfo{
		.apiVersion = VK_API_VERSION_1_0,
	};

	const auto RequiredExtensions = GetRequiredExtensions();

#ifndef VKCONFIG
	const auto DebugUtilsMessengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT{
		.messageSeverity = ~vk::DebugUtilsMessageSeverityFlagsEXT(),
		.messageType = ~vk::DebugUtilsMessageTypeFlagsEXT() ^ vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding,
		.pfnUserCallback = DebugMessageFunc,
	};
#endif // !VKCONFIG

	const auto InstanceCreateInfo = vk::InstanceCreateInfo{
#ifndef VKCONFIG
		.pNext = &DebugUtilsMessengerCreateInfo,
#endif // !VKCONFIG
		.pApplicationInfo = &ApplicationInfo,
		//.enabledLayerCount = static_cast<decltype(VkInstanceCreateInfo::enabledLayerCount)>(validationLayers.size()),
		//.ppEnabledLayerNames = validationLayers.data(),
		.enabledExtensionCount = static_cast<decltype(VkInstanceCreateInfo::enabledExtensionCount)>(RequiredExtensions.size()),
		.ppEnabledExtensionNames = RequiredExtensions.data(),
	};

	const auto Instance = vk::raii::Instance(Context, InstanceCreateInfo);

#ifndef VKCONFIG
	const auto DebugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(Instance, DebugUtilsMessengerCreateInfo);
#endif // !VKCONFIG

	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Instance);
	const auto PhysicalDevices = Instance.enumeratePhysicalDevices();

	//const vk::DeviceCreateInfo DeviceCreateInfo = VkDeviceCreateInfo{
	//	.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	//};

	//vk::raii::Device Device = PhysicalDevices[0].createDevice(DeviceCreateInfo);
	// 
	//VULKAN_HPP_DEFAULT_DISPATCHER.init(*Device);

	while (!StopRenderThread.load())
	{
		//fmt::println(std::clog, "Render Thread");
	}
}

int main() {
	glfwSetErrorCallback(GlfwErrorCallback);

	if (glfwInit() != GLFW_TRUE)
	{
		fmt::println(std::cerr, "Failed to initialize GLFW");
		return EXIT_FAILURE;
	}

	const auto GlfwGuard = gsl::finally([] { glfwTerminate(); });

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow* const Window = glfwCreateWindow(WIDTH, HEIGHT, "Gorgon", nullptr, nullptr);
	if (!Window) {
		fmt::println(std::cerr, "Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	const auto GlfwWindowGuard = gsl::finally([&] { glfwDestroyWindow(Window); });

	const auto RenderThread = std::jthread(RenderThreadFunc);

	while (!glfwWindowShouldClose(Window))
	{
		//fmt::println(std::clog, "Main Thread");
		glfwPollEvents();
	}

	StopRenderThread.store(true);

	return EXIT_SUCCESS;
}
