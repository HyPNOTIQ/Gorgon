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
constexpr auto MAX_PENDING_FRAMES = 3u;

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

const auto deviceExtensions = {
	vk::KHRSwapchainExtensionName,

	// TODO
	//vk::EXTSwapchainMaintenance1ExtensionName,
	//vk::EXTSurfaceMaintenance1ExtensionName, 
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

	const auto RequiredExtensions = GetRequiredExtensions();

#ifndef VKCONFIG
	const auto DebugUtilsMessengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT{
		.messageSeverity = ~vk::DebugUtilsMessageSeverityFlagsEXT(),
		.messageType = ~vk::DebugUtilsMessageTypeFlagsEXT() ^ vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding,
		.pfnUserCallback = DebugMessageFunc,
	};
#endif // !VKCONFIG

	const auto Instance = [&] {
		const auto ApplicationInfo = vk::ApplicationInfo{
			.apiVersion = VK_API_VERSION_1_3, // for VK_KHR_dynamic_rendering
		};

		const auto CreateInfo = vk::InstanceCreateInfo{
#ifndef VKCONFIG
			.pNext = &DebugUtilsMessengerCreateInfo,
#endif // !VKCONFIG
			.pApplicationInfo = &ApplicationInfo,
		}.setPEnabledExtensionNames(RequiredExtensions);

		return vk::raii::Instance(Context, CreateInfo);
	}();

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

	// TODO: handle separate graphics and present case
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


	const auto PhysicalDeviceSwapchainMaintenance1FeaturesEXT = vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT{
		.swapchainMaintenance1 = true,
	};

	const auto PhysicalDeviceDynamicRenderingFeatures = vk::PhysicalDeviceDynamicRenderingFeatures{
		.pNext = const_cast<vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT*>(&PhysicalDeviceSwapchainMaintenance1FeaturesEXT),
		.dynamicRendering = true,
	};

	const auto DeviceCreateInfo = vk::DeviceCreateInfo{
		.pNext = &PhysicalDeviceDynamicRenderingFeatures,
	}
	.setQueueCreateInfos(queueCreateInfos)
	.setPEnabledExtensionNames(deviceExtensions)
	;

	const auto Device = PhysicalDevice.createDevice(DeviceCreateInfo);

	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Device);

	const auto SurfaceCapabilities = PhysicalDevice.getSurfaceCapabilitiesKHR(Surface);

	const auto MinImageCount = [&] {
		const auto DesiredMinImageCount = SurfaceCapabilities.minImageCount + 1;
		const auto& maxImageCount = SurfaceCapabilities.maxImageCount;

		// https://registry.khronos.org/vulkan/specs/latest/man/html/VkSurfaceCapabilitiesKHR.html
		// maxImageCount is the maximum number of images the specified device supports for a swapchain created for the surface,
		// and will be either 0, or greater than or equal to minImageCount.
		// A value of 0 means that there is no limit on the number of images,
		// though there may be limits related to the total amount of memory used by presentable images.
		return maxImageCount ? std::min(DesiredMinImageCount, maxImageCount) : DesiredMinImageCount;
	};

	const auto ImageExtent = [&] {
		// https://registry.khronos.org/vulkan/specs/latest/man/html/VkSurfaceCapabilitiesKHR.html
		// currentExtent is the current width and height of the surface, or the special value (0xFFFFFFFF, 0xFFFFFFFF) indicating
		// that the surface size will be determined by the extent of a swapchain targeting the surface.
		constexpr auto SpecialValue = std::numeric_limits<uint32_t>::max();
		const auto isSpecialValue = SurfaceCapabilities.currentExtent == vk::Extent2D{SpecialValue, SpecialValue};

		const auto SpecialValueCaseImageExtent = [&] {
			int width, height;
			glfwGetFramebufferSize(Window, &width, &height);

			const auto actualExtent = vk::Extent2D{
				.width = uint32_t(width),
				.height = uint32_t(height),
			};

			const auto& MinImageExtent = SurfaceCapabilities.minImageExtent;
			const auto& MaxImageExtent = SurfaceCapabilities.maxImageExtent;

			return vk::Extent2D{
				.width = std::clamp(actualExtent.height, MinImageExtent.height, MinImageExtent.height),
				.height = std::clamp(actualExtent.width, MinImageExtent.width, MinImageExtent.width),
			};
		};

		return isSpecialValue ? SpecialValueCaseImageExtent() : SurfaceCapabilities.currentExtent;

		return vk::Extent2D{};
	}();

	const auto SurfaceFormat = [&] {
		const auto SurfaceFormats = PhysicalDevice.getSurfaceFormatsKHR(Surface);
		for (const auto& format: SurfaceFormats) {
			if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear && format.format == vk::Format::eB8G8R8A8Srgb)
			{
				return format;
			}
		}

		return SurfaceFormats[0];
	}();

	const auto PresentMode = [&] {
		const auto PresentModes = PhysicalDevice.getSurfacePresentModesKHR(Surface);

		const auto it = std::ranges::find(PresentModes, vk::PresentModeKHR::eMailbox);
		return it != PresentModes.cend() ? *it : vk::PresentModeKHR::eFifo; // TODO: check C++26 "deref_or_default"
	};

	const auto SwapchainCreateInfo = vk::SwapchainCreateInfoKHR{
		.surface = *Surface,
		.minImageCount = MinImageCount(),
		.imageFormat = SurfaceFormat.format,
		.imageColorSpace = SurfaceFormat.colorSpace,
		.imageExtent = ImageExtent,
		.imageArrayLayers = 1,
		.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
		.imageSharingMode = vk::SharingMode::eExclusive,
		.preTransform = SurfaceCapabilities.currentTransform,
		.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		.presentMode = PresentMode(),
		.clipped = true,
	};

	const auto Swapchain = Device.createSwapchainKHR(SwapchainCreateInfo);

	const auto GraphicsQueue = Device.getQueue(GraphicsQueueFamilyIndex, 0);

	// TODO
	const auto PresentQueue = Device.getQueue(GraphicsQueueFamilyIndex, 0);

	const auto CommandPool = [&] {
		const auto CreateInfo = vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
			.queueFamilyIndex = GraphicsQueueFamilyIndex,
		};

		return Device.createCommandPool(CreateInfo);
	}();

	const auto CommandBuffers = [&] {
		const auto CreateInfo = vk::CommandBufferAllocateInfo{
			.commandPool = *CommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = MAX_PENDING_FRAMES,
		};

		return Device.allocateCommandBuffers(CreateInfo);
	}();

	// TODO: https://en.cppreference.com/w/cpp/container/inplace_vector.html
	const auto acquireNextImageSemaphores = [&] {
		std::vector<vk::raii::Semaphore> Result;
		Result.reserve(MAX_PENDING_FRAMES);

		const auto CreateInfo = vk::SemaphoreCreateInfo{};

		std::generate_n(std::back_inserter(Result), MAX_PENDING_FRAMES, [&] { return Device.createSemaphore(CreateInfo); });

		return Result;
	}();

	const auto renderFinishedSemaphores = [&] {
		std::vector<vk::raii::Semaphore> Result;
		Result.reserve(MAX_PENDING_FRAMES);

		const auto CreateInfo = vk::SemaphoreCreateInfo{};

		std::generate_n(std::back_inserter(Result), MAX_PENDING_FRAMES, [&] { return Device.createSemaphore(CreateInfo); });

		return Result;
	}();

	const auto presentFences = [&] {
		std::vector<vk::raii::Fence> Result;
		Result.reserve(MAX_PENDING_FRAMES);

		const auto CreateInfo = vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled, };

		std::generate_n(std::back_inserter(Result), MAX_PENDING_FRAMES, [&] { return Device.createFence(CreateInfo); });

		return Result;
	}();

	const auto swapChainImages = Swapchain.getImages();

	const auto swapChainImageViews = [&] {
		const auto createImageViewL = [&](const vk::Image& image) {
			const auto createInfo = vk::ImageViewCreateInfo{
				.image = image,
				.viewType = vk::ImageViewType::e2D,
				.format = SurfaceFormat.format,
				.components = vk::ComponentMapping{},
				.subresourceRange = vk::ImageSubresourceRange{
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.levelCount = 1,
					.layerCount = 1,
				}
			};

			return Device.createImageView(createInfo);
		};

		return swapChainImages
			| std::views::transform(createImageViewL)
			| std::ranges::to<std::vector>();
	}();

	auto currentFrame = 0u;
	while (not StopRenderThread.load())
	{
		//fmt::println(std::clog, "Render Thread");

		const auto& acquireNextImageSemaphore = acquireNextImageSemaphores[currentFrame];
		const auto& renderFinishedSemaphore = renderFinishedSemaphores[currentFrame];

		const auto& presentFence = presentFences[currentFrame];
		Device.waitForFences({ *presentFence }, true, std::numeric_limits<uint64_t>::max());
		Device.resetFences({ *presentFence });

		const auto NextImage = [&] {
			const auto Result = Swapchain.acquireNextImage(
				std::numeric_limits<uint64_t>::max(),
				acquireNextImageSemaphore
			);

			assert(Result.first == vk::Result::eSuccess);
			return Result.second;
		}();

		const auto& CommandBuffer = CommandBuffers[currentFrame];
		CommandBuffer.reset();

		CommandBuffer.begin(vk::CommandBufferBeginInfo{});

		const auto ColorAttachments = {
			vk::RenderingAttachmentInfo{
				.imageView = swapChainImageViews[NextImage],
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = { .color = std::to_array({0.5f, 0.5f, 0.5f, 1.0f}) },
			},
		};

		const auto RenderingInfo = vk::RenderingInfo{
			.renderArea = vk::Rect2D{ .extent = ImageExtent, },
			.layerCount = 1,
		}.setColorAttachments(ColorAttachments);

		const auto toAttachmentBarrier = vk::ImageMemoryBarrier{
			.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
			.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
			.image = swapChainImages[NextImage],
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.levelCount = 1,
				.layerCount = 1,
			},
		};

		CommandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, // dependencyFlags
			{}, // memoryBarriers
			{}, // bufferMemoryBarriers
			{ toAttachmentBarrier } // imageMemoryBarriers
		);

		CommandBuffer.beginRendering(RenderingInfo);
		CommandBuffer.endRendering();

		swapChainImageViews[NextImage].getDevice();

		// TODO: check VK_KHR_synchronization2
		const auto toPresentBarrier = vk::ImageMemoryBarrier{
			.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
			.oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.newLayout = vk::ImageLayout::ePresentSrcKHR,
			.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
			.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
			.image = swapChainImages[NextImage],
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.levelCount = 1,
				.layerCount = 1,
			},
		};

		CommandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, // dependencyFlags
			{}, // memoryBarriers
			{}, // bufferMemoryBarriers
			{ toPresentBarrier } // imageMemoryBarriers
		);

		CommandBuffer.end();

		const auto waitSemaphores = { *acquireNextImageSemaphore };
		const auto CommandBuffers = { *CommandBuffer };
		const auto SignalSemaphores = { *renderFinishedSemaphore };
		const auto WaitDstStageMask = { vk::PipelineStageFlags{ vk::PipelineStageFlagBits::eColorAttachmentOutput } };
		
		const auto SubmitInfo = vk::SubmitInfo{}
			.setWaitSemaphores(waitSemaphores)
			.setWaitDstStageMask(WaitDstStageMask)
			.setCommandBuffers(CommandBuffers)
			.setSignalSemaphores(SignalSemaphores)
			;

		// TODO: check https://registry.khronos.org/vulkan/specs/latest/man/html/vkQueueSubmit2.html
		GraphicsQueue.submit(SubmitInfo);

		const auto Swapchains = { *Swapchain };
		const auto ImageIndices = { NextImage };

		const auto PresentFences = { *presentFence };
		const auto presentFenceInfo = vk::SwapchainPresentFenceInfoEXT{}.setFences(PresentFences);

		PresentQueue.presentKHR(
			vk::PresentInfoKHR{
				.pNext = &presentFenceInfo,
			}
			.setWaitSemaphores(SignalSemaphores)
			.setSwapchains(Swapchains)
			.setImageIndices(ImageIndices)
		);

		currentFrame = (currentFrame + 1) % MAX_PENDING_FRAMES;
	}

	// TODO
	//Device.waitForFences(
	//	presentFences
	//		| std::views::transform([](const auto& fence) { return *fence; })
	//		| std::ranges::to<std::array<vk::Fence, MAX_PENDING_FRAMES>>(),
	//	true,
	//	std::numeric_limits<uint64_t>::max()
	//);

	Device.waitIdle();
}

int main() {
	glfwSetErrorCallback(GlfwErrorCallback);

	if (glfwInit() not_eq GLFW_TRUE)
	{
		fmt::println(std::cerr, "Failed to initialize GLFW");
		return EXIT_FAILURE;
	}

	const auto GlfwGuard = gsl::finally([] { glfwTerminate(); });

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow* const Window = glfwCreateWindow(WIDTH, HEIGHT, "Gorgon", nullptr, nullptr);
	if (not Window) {
		fmt::println(std::cerr, "Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	const auto GlfwWindowGuard = gsl::finally([&] { glfwDestroyWindow(Window); });

	const auto RenderThread = std::jthread(RenderThreadFunc, Window);

	while (not glfwWindowShouldClose(Window))
	{
		//fmt::println(std::clog, "Main Thread");

		glfwPollEvents();
	}

	StopRenderThread.store(true);

	return EXIT_SUCCESS;
}
