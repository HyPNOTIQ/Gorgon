#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "fmt/ostream.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <ranges>
#include <gsl/gsl>

#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
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

#define VKCONFIG

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

const std::vector<const char*> deviceExtensions = {
	vk::KHRSwapchainExtensionName
};

std::vector<const char*> GetRequiredExtensions() {
	uint32_t GlfwExtensionCount;
	const auto GlfwExtensions = glfwGetRequiredInstanceExtensions(&GlfwExtensionCount);

	auto Extensions = std::vector<const char*>(GlfwExtensions, GlfwExtensions + GlfwExtensionCount);

#if !defined(VKCONFIG) && !defined(NDEBUG)
	Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif // !defined(VKCONFIG) && !defined(NDEBUG)

	return Extensions;
}

void RenderThreadFunc(GLFWwindow* const Window)
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
		.enabledExtensionCount = uint32_t(RequiredExtensions.size()),
		.ppEnabledExtensionNames = RequiredExtensions.data(),
	};

	const auto Instance = vk::raii::Instance(Context, InstanceCreateInfo);

	const auto Surface = [&] {
		VkSurfaceKHR surface;
		glfwCreateWindowSurface(*Instance, Window, nullptr, &surface);

		return vk::raii::SurfaceKHR(Instance, surface);
	}();

#ifndef VKCONFIG
	const auto DebugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(Instance, DebugUtilsMessengerCreateInfo);
#endif // !VKCONFIG

	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Instance);
	const auto PhysicalDevices = Instance.enumeratePhysicalDevices();
	const auto& PhysicalDevice = PhysicalDevices[0];

	const auto QueueFamilyProperties = PhysicalDevice.getQueueFamilyProperties();

	// TODO: separate graphics and present case
	const uint32_t GraphicsQueueFamilyIndex = [&] {
		for (const auto [index, value] : std::views::enumerate(QueueFamilyProperties)) {
			const auto QueueFamilyIndex = uint32_t(index);

			if (value.queueFlags & vk::QueueFlagBits::eGraphics &&
				glfwGetPhysicalDevicePresentationSupport(*Instance, *PhysicalDevice, QueueFamilyIndex) == GLFW_TRUE)
			{
				return QueueFamilyIndex;
			}
		}

		assert(false);
		return uint32_t{};
	}();

	const auto queuePriority = 1.0f;
	const auto GraphicsQueueCreateInfo = vk::DeviceQueueCreateInfo{
		.queueFamilyIndex = GraphicsQueueFamilyIndex,
		.queueCount = 1,
		.pQueuePriorities = &queuePriority,
	};

	const std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos = { GraphicsQueueCreateInfo };

	const auto DeviceCreateInfo = vk::DeviceCreateInfo{
		.queueCreateInfoCount = uint32_t(queueCreateInfos.size()),
		.pQueueCreateInfos = queueCreateInfos.data(),
		.enabledExtensionCount = uint32_t(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data()
	};

	const auto Device = PhysicalDevice.createDevice(DeviceCreateInfo);

	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Device);

	//const auto SurfaceFormat = [&] {
	//	PhysicalDevice.getSurfaceFormatsKHR(Surface);
	//}();

	const auto SwapchainCreateInfo = vk::SwapchainCreateInfoKHR{
		.surface = *Surface,
		//.imageFormat = 
	};

	const auto Swapchain = Device.createSwapchainKHR(SwapchainCreateInfo);

	const auto GraphicsQueue = Device.getQueue(GraphicsQueueFamilyIndex, 0);

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

	const auto RenderThread = std::jthread(RenderThreadFunc, Window);

	while (!glfwWindowShouldClose(Window))
	{
		//fmt::println(std::clog, "Main Thread");
		glfwPollEvents();
	}

	StopRenderThread.store(true);

	return EXIT_SUCCESS;
}
