#include "gltf/loader.h"
#include "vk/vma.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

constexpr auto APP_NAME = "Gorgon";
constexpr auto WIDTH = 1440u;
constexpr auto HEIGHT = 900u;
constexpr auto MAX_PENDING_FRAMES = 2u;

namespace vk
{

	template <typename T>
	concept Chainable = requires(T t) {
	{ t.pNext } -> std::convertible_to<const void*>;
	};

template<typename... Ts>
	concept AllChainable = (Chainable<Ts> && ...);

	template <AllChainable... ChainElements>
decltype(auto) createStructureChain(ChainElements&&... elems)
	{
		return vk::StructureChain<std::remove_cvref_t<ChainElements>...>(std::forward<ChainElements>(elems)...);
	}

	const auto deviceExtensions = {
		vk::KHRSwapchainExtensionName,
	//vk::EXTVertexInputDynamicStateExtensionName,
	};

#ifndef VULKAN_CONFIGURATOR
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
#endif // !VULKAN_CONFIGURATOR

}

class FrameTimer {
public:
	using clock = std::chrono::steady_clock;

	FrameTimer() : frameNum(0), lastTime(clock::now()) {}

	void update() {
		auto now = clock::now();
		std::chrono::duration<float> dt = now - lastTime;
		lastTime = now;

		deltaTime = dt.count(); // in seconds
		frameNum++;
	}

	float getDeltaTime() const { return deltaTime; }
	uint64_t getFrameNum() const { return frameNum; }

private:
	clock::time_point lastTime;
	float deltaTime = 0.0f;
	uint64_t frameNum;
};

// TODO
struct RenderThreadConfig
{
	std::string_view gltfFile;
	GLFWwindow* const Window;
};

std::vector<const char*> GetRequiredExtensions() {
	uint32_t GlfwExtensionCount;
	const auto GlfwExtensions = glfwGetRequiredInstanceExtensions(&GlfwExtensionCount);

	auto Extensions = std::vector<const char*>(GlfwExtensions, GlfwExtensions + GlfwExtensionCount);

#if !defined(NDEBUG)
	Extensions.push_back(vk::EXTDebugUtilsExtensionName); // for debug object names
#endif // !defined(NDEBUG)

	return Extensions;
}

static inline void SetDebugUtilsObjectName(
	const vk::raii::Device& device,
	const vk::ObjectType objectType,
	const uint64_t objectHandle,
	const char* const ObjectName) {
#ifndef NDEBUG
	const auto PresentQueuedebugName = vk::DebugUtilsObjectNameInfoEXT{
		.objectType = objectType,
		.objectHandle = objectHandle,
		.pObjectName = ObjectName,
	};
	device.setDebugUtilsObjectNameEXT(PresentQueuedebugName);
#endif // !NDEBUG
}

void RenderThreadFunc(
	const std::stop_token& stop_token,
	const RenderThreadConfig& config)
{
#if defined(USE_RENDER_DOC) && 0
	static_assert(VK_API_VERSION < VK_API_VERSION_1_4, "Renderdoc does not support 1.4"); // https://github.com/baldurk/renderdoc/issues/3625
	RENDERDOC_API_1_1_2* rdoc_api;

	// TODO: use dynamic library loading
	//if (const auto renderdocLib = LoadLibraryA("renderdoc.dll"))
	//{
	//	const auto RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(renderdocLib, "RENDERDOC_GetAPI");
	//	RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
	//}

	rdoc_api->SetCaptureFilePathTemplate("renderdoc_captures");
#endif

	const auto &Window = config.Window;
	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	const auto context = vk::raii::Context();

	const auto RequiredExtensions = GetRequiredExtensions();

#ifndef VULKAN_CONFIGURATOR
	const auto DebugUtilsMessengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT{
		.messageSeverity = ~vk::DebugUtilsMessageSeverityFlagsEXT(),
		.messageType = ~vk::DebugUtilsMessageTypeFlagsEXT() ^ vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding, // TODO
		.pfnUserCallback = vk::DebugMessageFunc,
	};
#endif // !VULKAN_CONFIGURATOR

	const auto Instance = [&]
	{
		const auto ApplicationInfo = vk::ApplicationInfo{
			.apiVersion = VK_API_VERSION,
		};

		const auto CreateInfo = vk::InstanceCreateInfo{
#ifndef VULKAN_CONFIGURATOR
			.pNext = &DebugUtilsMessengerCreateInfo,
#endif // !VULKAN_CONFIGURATOR
			.pApplicationInfo = &ApplicationInfo,
		}.setPEnabledExtensionNames(RequiredExtensions);

		return context.createInstance(CreateInfo);
	}();
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Instance);

#ifndef VULKAN_CONFIGURATOR
	const auto _DebugUtilsMessenger = Instance.createDebugUtilsMessengerEXT(DebugUtilsMessengerCreateInfo);
#endif // !VULKAN_CONFIGURATOR

	auto Surface = [&]
	{
		VkSurfaceKHR surface;

		if (const auto result = glfwCreateWindowSurface(*Instance, Window, nullptr, &surface); result != VK_SUCCESS)
		{
			// TODO
			assert(false);
		}

		// construct directly from surface handle created by glfw
		return vk::raii::SurfaceKHR(Instance, surface);
	}();

	auto PhysicalDevices = Instance.enumeratePhysicalDevices();

	const auto &PhysicalDevice = PhysicalDevices[0]; // TODO: select physical device

	const auto queueFamilyIndices = [&]
	{
		const auto QueueFamilyProperties = PhysicalDevice.getQueueFamilyProperties2();

		struct QueueFamilyIndices
		{
			uint32_t Graphics;
			uint32_t Present;
			uint32_t Transfer;
		};

		const auto supportsGraphics = [&](const vk::QueueFamilyProperties2 &properties)
		{
			return bool(properties.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics);
		};

		const auto supportsPresent = [&](const uint32_t index)
		{
			return glfwGetPhysicalDevicePresentationSupport(*Instance, *PhysicalDevice, index) == GLFW_TRUE;
		};

		// Find separate graphics and present queues
		const auto findSeparate = [&] -> std::optional<QueueFamilyIndices>
		{
			const auto graphicsQueues = [&]
			{
				// TODO
				const auto filter = [&](const std::pair<size_t, vk::QueueFamilyProperties2> &pair)
				{
					return supportsGraphics(pair.second);
				};

				const auto transform = [](const std::pair<size_t, vk::QueueFamilyProperties2> &pair)
				{
					return static_cast<uint32_t>(pair.first);
				};

				return QueueFamilyProperties | std::views::enumerate | std::views::filter(filter) | std::views::transform(transform) | std::ranges::to<std::vector<uint32_t>>();
			}();

			const auto presentQueues = [&]
			{
				return std::views::iota(0u, QueueFamilyProperties.size()) | std::views::filter(supportsPresent) | std::ranges::to<std::vector<uint32_t>>();
			}();

			for (const uint32_t graphicsQueueIndex : graphicsQueues)
			{
				for (const uint32_t presentQueueIndex : presentQueues)
				{
					if (graphicsQueueIndex != presentQueueIndex)
					{
						return std::make_optional(
							QueueFamilyIndices{
								.Graphics = graphicsQueueIndex,
								.Present = presentQueueIndex,
								.Transfer = graphicsQueueIndex // TODO: find transfer queue
							});
					}
				}
			}

			return std::nullopt;
		};

		const auto find = [&] -> std::optional<QueueFamilyIndices>
		{
			for (const auto &[index, value] : std::views::enumerate(QueueFamilyProperties))
			{
				const auto queueFamilyIndex = static_cast<uint32_t>(index);

				if (supportsGraphics(value) && supportsPresent(queueFamilyIndex))
				{
					return std::make_optional(QueueFamilyIndices{
						queueFamilyIndex,
						queueFamilyIndex,
						queueFamilyIndex});
				}
			}

			// TODO
			assert(false);
			return std::nullopt;
		};

		constexpr auto forceSeparate = true;

		// TODO
		return forceSeparate ? findSeparate().value_or(find().value()) : find().value();
	}();

	const auto Device = [&]
	{
		constexpr auto queuePriority = 1.0f;
		const auto transform = [&](const uint32_t index)
		{
			return vk::DeviceQueueCreateInfo{
				.queueFamilyIndex = index,
				.queueCount = 1,
				.pQueuePriorities = &queuePriority,
			};
		};

		// TODO: use std::inplace_vector C++26
		std::vector<uint32_t> queueIndices = {queueFamilyIndices.Graphics, queueFamilyIndices.Present};
		auto [first, last] = std::ranges::unique(queueIndices);
		queueIndices.erase(first, last);

		const auto queueCreateInfos = queueIndices | std::views::transform(transform) | std::ranges::to<vku::small::vector<vk::DeviceQueueCreateInfo, MAX_PENDING_FRAMES>>();

		const auto features = vk::PhysicalDeviceFeatures{
			.fillModeNonSolid = true,
		};

		const auto DeviceCreateInfoChain = createStructureChain(
			vk::DeviceCreateInfo{.pEnabledFeatures = &features}
				.setQueueCreateInfos(queueCreateInfos)
				.setPEnabledExtensionNames(vk::deviceExtensions),
			vk::PhysicalDeviceVulkan11Features{},
			vk::PhysicalDeviceVulkan12Features{
				//.descriptorIndexing = true,
				//.shaderSampledImageArrayNonUniformIndexing = true,
				.descriptorBindingSampledImageUpdateAfterBind = true,
				.descriptorBindingPartiallyBound = true,
				.descriptorBindingVariableDescriptorCount = true,
				.runtimeDescriptorArray = true,
				.timelineSemaphore = true,
			},
			vk::PhysicalDeviceVulkan13Features{
				.synchronization2 = true,
				.dynamicRendering = true,
			},
			vk::PhysicalDeviceVulkan14Features{},
			vk::PhysicalDeviceVertexAttributeRobustnessFeaturesEXT{
				.vertexAttributeRobustness = true,
			},
			vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT{
				.swapchainMaintenance1 = true,
			});

		return PhysicalDevice.createDevice(DeviceCreateInfoChain.get<vk::DeviceCreateInfo>());
	}();

	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Device);

	const auto vma = [&]
	{
		const auto createInfo = VulkanMemoryAllocator::CreateInfo{
			.instance = Instance,
			.physicalDevice = PhysicalDevice,
			.device = Device,
		};

		return VulkanMemoryAllocator(createInfo);
	}();

	const auto DeviceIdleGuard = gsl::finally([&]
											  { Device.waitIdle(); });

	const auto GraphicsQueue = Device.getQueue(queueFamilyIndices.Graphics, 0);
	const auto PresentQueue = Device.getQueue(queueFamilyIndices.Present, 0);
	const auto TransferQueue = Device.getQueue(queueFamilyIndices.Transfer, 0);

	// TODO: create question on Vulkan-hpp github about vk::ObjectType
	if (queueFamilyIndices.Graphics != queueFamilyIndices.Present)
	{
		SetDebugUtilsObjectName(
			Device,
			vk::ObjectType::eQueue,
			static_cast<uint64_t>(reinterpret_cast<uintptr_t>(VkQueue(*GraphicsQueue))),
			"GraphicsQueue");

		SetDebugUtilsObjectName(
			Device,
			vk::ObjectType::eQueue,
			static_cast<uint64_t>(reinterpret_cast<uintptr_t>(VkQueue(*PresentQueue))),
			"PresentQueue");
	}
	else
	{
		SetDebugUtilsObjectName(
			Device,
			vk::ObjectType::eQueue,
			static_cast<uint64_t>(reinterpret_cast<uintptr_t>(VkQueue(*GraphicsQueue))),
			"GraphicsPresentQueue");
	}

	const auto SurfaceCapabilities = PhysicalDevice.getSurfaceCapabilitiesKHR(Surface);

	const auto MinImageCount = [&]
	{
		const auto DesiredMinImageCount = SurfaceCapabilities.minImageCount + 1;
		const auto maxImageCount = SurfaceCapabilities.maxImageCount;

		// https://registry.khronos.org/vulkan/specs/latest/man/html/VkSurfaceCapabilitiesKHR.html
		// maxImageCount is the maximum number of images the specified device supports for a swapchain created for the surface,
		// and will be either 0, or greater than or equal to minImageCount.
		// A value of 0 means that there is no limit on the number of images,
		// though there may be limits related to the total amount of memory used by presentable images.
		return maxImageCount ? std::min(DesiredMinImageCount, maxImageCount) : DesiredMinImageCount;
	};

	// TODO: check vk::SurfaceFormat2KHR
	const auto surfaceExtent = [&]
	{
		// https://registry.khronos.org/vulkan/specs/latest/man/html/VkSurfaceCapabilitiesKHR.html
		// currentExtent is the current width and height of the surface, or the special value (0xFFFFFFFF, 0xFFFFFFFF) indicating
		// that the surface size will be determined by the extent of a swapchain targeting the surface.
		constexpr auto SpecialValue = std::numeric_limits<uint32_t>::max();
		const auto isSpecialValue = SurfaceCapabilities.currentExtent == vk::Extent2D{SpecialValue, SpecialValue};

		const auto SpecialValueCaseImageExtent = [&]
		{
			int width, height;
			glfwGetFramebufferSize(Window, &width, &height);

			const auto actualExtent = vk::Extent2D{
				.width = static_cast<uint32_t>(width),
				.height = static_cast<uint32_t>(height),
			};

			const auto &MinImageExtent = SurfaceCapabilities.minImageExtent;
			const auto &MaxImageExtent = SurfaceCapabilities.maxImageExtent;

			// TODO
			return vk::Extent2D{
				.width = std::clamp(actualExtent.height, MinImageExtent.height, MinImageExtent.height),
				.height = std::clamp(actualExtent.width, MinImageExtent.width, MinImageExtent.width),
			};
		};

		return isSpecialValue ? SpecialValueCaseImageExtent() : SurfaceCapabilities.currentExtent;

		return vk::Extent2D{};
	}();

	const auto SurfaceFormat = [&]
	{
		const auto SurfaceFormats = PhysicalDevice.getSurfaceFormatsKHR(Surface);
		for (const auto &format : SurfaceFormats)
		{
			if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear && format.format == vk::Format::eB8G8R8A8Srgb)
			{
				return format;
			}
		}

		return SurfaceFormats[0];
	}();

	const auto PresentMode = [&]
	{
		const auto PresentModes = PhysicalDevice.getSurfacePresentModesKHR(Surface);

		const auto it = std::ranges::find(PresentModes, vk::PresentModeKHR::eMailbox);
		return it != PresentModes.cend() ? *it : vk::PresentModeKHR::eFifo; // TODO: check C++26 "deref_or_default"
	};

	const auto SwapchainCreateInfo = vk::SwapchainCreateInfoKHR{
		.surface = *Surface,
		.minImageCount = MinImageCount(),
		.imageFormat = SurfaceFormat.format,
		.imageColorSpace = SurfaceFormat.colorSpace,
		.imageExtent = surfaceExtent,
		.imageArrayLayers = 1,
		.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
		.imageSharingMode = vk::SharingMode::eExclusive,
		.preTransform = SurfaceCapabilities.currentTransform,
		.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		.presentMode = PresentMode(),
		.clipped = true,
	};

	const auto Swapchain = Device.createSwapchainKHR(SwapchainCreateInfo);

	const auto swapchainImages = Swapchain.getImages();

	const auto swapchainImageViews = [&]
	{
		const auto createImageView = [&](const vk::Image &image)
		{
			const auto createInfo = vk::ImageViewCreateInfo{
				.image = image,
				.viewType = vk::ImageViewType::e2D,
				.format = SurfaceFormat.format,
				.subresourceRange = COLOR_SUBRESOURCE_RANGE,
			};

			return Device.createImageView(createInfo);
		};

		return swapchainImages | std::views::transform(createImageView) | std::ranges::to<std::vector>();
	}();

	const auto depthFormat = [&]
	{
		vk::Format result;

		const auto candidates = {
			//vk::Format::eD32Sfloat,
			//vk::Format::eD32SfloatS8Uint,
			vk::Format::eD24UnormS8Uint,
			vk::Format::eD16Unorm,
			vk::Format::eD16UnormS8Uint,
		};

		for (const auto format : candidates)
		{
			const auto props = PhysicalDevice.getFormatProperties2(format);

			if (props.formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
			{
				result = format;
				break;
			}
		}

		return result; // TODO
	}();

	const auto depthImage = [&]
	{
		const auto extent = vk::Extent3D{
			.width = surfaceExtent.width,
			.height = surfaceExtent.height,
			.depth = 1,
		};

		const auto createInfo = vk::ImageCreateInfo{
			.imageType = vk::ImageType::e2D,
			.format = depthFormat,
			.extent = extent,
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eOptimal,
			.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
			.sharingMode = vk::SharingMode::eExclusive,
		};

		const auto vmaFlags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

		return vma.createImage(createInfo, vmaFlags);
	}();

	const auto depthImageView = [&]
	{
		const auto subresourceRange = vk::ImageSubresourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eDepth,
			.levelCount = 1,
			.layerCount = 1,
		};

		const auto createInfo = vk::ImageViewCreateInfo{
			.image = *depthImage,
			.viewType = vk::ImageViewType::e2D,
			.format = depthFormat,
			.subresourceRange = subresourceRange,
		};

		return Device.createImageView(createInfo);
	}();

	const auto frameSynchronizations = [&]
	{
		struct FrameSynchronization
		{
			vk::raii::Semaphore acquireNextImage;
			vk::raii::Semaphore prePresent;
			vk::raii::Semaphore timeline;
			vk::raii::Fence present;
		};

		// TODO
		// vku::small::vector<FrameSynchronization, MAX_PENDING_FRAMES> Result;

		std::vector<FrameSynchronization> Result;
		Result.reserve(MAX_PENDING_FRAMES);

		const auto generateFunc = [&]
		{
			return FrameSynchronization{
				.acquireNextImage = [&]
				{ return Device.createSemaphore({}); }(),
				.prePresent = [&]
				{ return Device.createSemaphore({}); }(),
				.timeline = [&]
				{
						const auto typeCreateInfo = vk::SemaphoreTypeCreateInfo{
							.semaphoreType = vk::SemaphoreType::eTimeline,
						};

						const auto CreateInfo = vk::SemaphoreCreateInfo{ .pNext = &typeCreateInfo };
						return Device.createSemaphore(CreateInfo); }(),
				.present = [&]
				{ return Device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}); }(),
			};
		};

		// TODO: rewrite without temp container usage std::ranges::generate_n
		// TODO: https://en.cppreference.com/w/cpp/container/inplace_vector.html
		// return std::views::iota(0u, MAX_PENDING_FRAMES)
		//	| std::views::transform([&](auto) { return generateFunc(); })
		//	| std::ranges::to<vku::small::vector<FrameSynchronization, MAX_PENDING_FRAMES>>();

		std::generate_n(std::back_inserter(Result), MAX_PENDING_FRAMES, generateFunc);

		return Result;
	}();

	const auto graphicsCommandPool = [&]
	{
		const auto CreateInfo = vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
			.queueFamilyIndex = queueFamilyIndices.Graphics,
		};

		return Device.createCommandPool(CreateInfo);
	}();

	const auto presentCommandPool = [&]
	{
		const auto CreateInfo = vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
			.queueFamilyIndex = queueFamilyIndices.Present,
		};

		return Device.createCommandPool(CreateInfo);
	}();

	const auto transferCommandPool = [&]
	{
		const auto CreateInfo = vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
			.queueFamilyIndex = queueFamilyIndices.Transfer,
		};

		return Device.createCommandPool(CreateInfo);
	}();

	const auto graphicsCommandBuffers = [&]
	{
		const auto CreateInfo = vk::CommandBufferAllocateInfo{
			.commandPool = *graphicsCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = MAX_PENDING_FRAMES,
		};

		return Device.allocateCommandBuffers(CreateInfo);
	}();

	const auto presentCommandBuffers = [&]
	{
		const auto CreateInfo = vk::CommandBufferAllocateInfo{
			.commandPool = *presentCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = MAX_PENDING_FRAMES,
		};

		return Device.allocateCommandBuffers(CreateInfo);
	}();

	const auto transferCommandBuffer = [&]
	{
		const auto CreateInfo = vk::CommandBufferAllocateInfo{
			.commandPool = *transferCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1,
		};

		return std::move(Device.allocateCommandBuffers(CreateInfo).front());
	}();

	auto gltfLoader = [&]
	{
		const auto createInfo = gltf::Loader::CreateInfo{
			.device = Device,
			.vma = vma,
			.transferCommandBuffer = transferCommandBuffer,
			.transferQueue = TransferQueue,
			.surfaceFormat = SurfaceFormat.format,
			.depthFormat = depthFormat,
		};

		return gltf::Loader(createInfo);
	}();

	const auto gltfModel = gltfLoader.loadFromFile(config.gltfFile);

	float dolly = 0.5f;					   // distance from center
	float azimuth = 0.0f;				   // horizontal angle (radians)
	float altitude = glm::radians(30.0f);  // vertical angle (radians)
	float spinSpeed = glm::radians(45.0f); // radians per second

	auto frameTimer = FrameTimer();
	while (not stop_token.stop_requested())
	{
		// fmt::println(std::clog, "Render Thread");
		const auto frameNumber = frameTimer.getFrameNum();
		const auto frameIndex = frameNumber % MAX_PENDING_FRAMES;

		enum FrameTimeline : uint64_t
		{
			eRender = 1,
			ePrePresent,
			eMax = ePrePresent // TODO: use constexpr magic_enum
		};

		const auto getTimelineValue = [&](const FrameTimeline timelineValue)
		{
			return FrameTimeline::eMax * frameNumber + timelineValue;
		};

		const auto &frameSynchronization = frameSynchronizations[frameIndex];

		// TODO handle result value
		{
			const auto result = Device.waitForFences(*frameSynchronization.present, true, UINT64_MAX_VALUE);
			assert(result == vk::Result::eSuccess);
			Device.resetFences(*frameSynchronization.present);
		}

		const auto NextImage = [&]
		{
			const auto Result = Swapchain.acquireNextImage(
				UINT64_MAX_VALUE,
				frameSynchronization.acquireNextImage);

			// TODO handle result value
			assert(Result.first == vk::Result::eSuccess);
			return Result.second;
		}();

		const auto &swapchainImage = swapchainImages[NextImage];
		// render
		{
			const auto &commandBuffer = graphicsCommandBuffers[frameIndex];
			commandBuffer.reset();

			// record command buffer
			{
				commandBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

				{
					const auto imageMemoryBarriers = {
						// to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
						vk::ImageMemoryBarrier2{
							//.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput, // TODO: ?
							.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
							.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
							.newLayout = vk::ImageLayout::eColorAttachmentOptimal,
							.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
							.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
							.image = swapchainImage,
							.subresourceRange = COLOR_SUBRESOURCE_RANGE,
						},
						// to VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
						vk::ImageMemoryBarrier2{
							.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
							.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
							.newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
							.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
							.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
							.image = *depthImage,
							.subresourceRange = {
								.aspectMask = vk::ImageAspectFlagBits::eDepth,
								.levelCount = 1,
								.layerCount = 1,
							},
						},
					};

					const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(imageMemoryBarriers);
					commandBuffer.pipelineBarrier2(dependencyInfo);
				}

				const auto colorAttachment = vk::RenderingAttachmentInfo{
					.imageView = swapchainImageViews[NextImage],
					.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
					.loadOp = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = {.color = std::to_array({0.1f, 0.1f, 0.1f, 1.0f})},
				};

				const auto depthAttachment = vk::RenderingAttachmentInfo{
					.imageView = *depthImageView,
					.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
					.loadOp = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = {.depthStencil = {.depth = 1.0f}},
				};

				const auto renderingInfo = vk::RenderingInfo{
					.renderArea = vk::Rect2D{.extent = surfaceExtent},
					.layerCount = 1,
					.pDepthAttachment = &depthAttachment,
				}
											   .setColorAttachments(colorAttachment);

				commandBuffer.beginRendering(renderingInfo);

				// TODO: temp solution, rework == add input
				const auto createViewProj = [&]
				{
					azimuth += spinSpeed * frameTimer.getDeltaTime();

					// view matrix
					constexpr auto target = glm::vec3(0);
					constexpr auto up = glm::vec3(0.0f, 1.0f, 0.0f);

					const auto altitude_sin = glm::sin(altitude);
					const auto altitude_cos = glm::cos(altitude);
					const auto azimuth_sin = glm::sin(azimuth);
					const auto azimuth_cos = glm::cos(azimuth);

					const auto x = dolly * altitude_cos * azimuth_sin;
					const auto y = dolly * altitude_sin;
					const auto z = dolly * altitude_cos * azimuth_cos;

					const auto position = glm::vec3(x, y, z);

					const auto view = glm::lookAt(position, target, up);

					// projection matrix
					const auto width = static_cast<float>(surfaceExtent.width);
					const auto height = static_cast<float>(surfaceExtent.height);
					constexpr float fovY = glm::radians(45.0f);
					const float aspect = width / height;
					constexpr float nearPlane = 0.01f;
					constexpr float farPlane = 100.0f;

					const auto proj = glm::perspective(fovY, aspect, nearPlane, farPlane);

					return proj * view;
				};

				const auto viewProj = createViewProj();

				{
					const auto drawInfo = gltf::Model::DrawInfo{
						.sceneIndex = 0, // TODO
						.viewProj = viewProj,
						.commandBuffer = commandBuffer,
						.surfaceExtent = surfaceExtent,
					};

					gltfModel.Draw(drawInfo);
				}

				commandBuffer.endRendering();

				// to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
				{
					const auto imageMemoryBarrier = vk::ImageMemoryBarrier2{
						.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
						.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
						.oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
						.newLayout = vk::ImageLayout::ePresentSrcKHR,
						.srcQueueFamilyIndex = queueFamilyIndices.Graphics,
						.dstQueueFamilyIndex = queueFamilyIndices.Present,
						.image = swapchainImage,
						.subresourceRange = COLOR_SUBRESOURCE_RANGE,
					};

					const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(imageMemoryBarrier);
					commandBuffer.pipelineBarrier2(dependencyInfo);
				}

				commandBuffer.end();
			}

			const auto waitSemaphoresInfo = vk::SemaphoreSubmitInfo{
				.semaphore = *frameSynchronization.acquireNextImage,
				.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			};

			const auto commandBufferInfo = vk::CommandBufferSubmitInfo{.commandBuffer = *commandBuffer};

			const auto signalSemaphoreInfo = vk::SemaphoreSubmitInfo{
				.semaphore = *frameSynchronization.timeline,
				.value = FrameTimeline::eMax * frameNumber + FrameTimeline::eRender,
				.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
			};

			const auto submitInfo = vk::SubmitInfo2{}
										.setWaitSemaphoreInfos(waitSemaphoresInfo)
										.setCommandBufferInfos(commandBufferInfo)
										.setSignalSemaphoreInfos(signalSemaphoreInfo);

			GraphicsQueue.submit2(submitInfo);
		}

		// pre-present
		{
			const auto &commandBuffer = presentCommandBuffers[frameIndex];

			commandBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

			// image ownership transfer from graphics to present
			const auto toPresentBarrier = vk::ImageMemoryBarrier2{
				.oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.newLayout = vk::ImageLayout::ePresentSrcKHR,
				.srcQueueFamilyIndex = queueFamilyIndices.Graphics,
				.dstQueueFamilyIndex = queueFamilyIndices.Present,
				.image = swapchainImage,
				.subresourceRange = COLOR_SUBRESOURCE_RANGE,
			};

			const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(toPresentBarrier);
			commandBuffer.pipelineBarrier2(dependencyInfo);

			commandBuffer.end();

			const auto waitSemaphoresInfo = vk::SemaphoreSubmitInfo{
				.semaphore = *frameSynchronization.timeline,
				.value = getTimelineValue(FrameTimeline::eRender),
				.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
			};

			const auto commandBufferInfo = vk::CommandBufferSubmitInfo{.commandBuffer = *commandBuffer};

			const auto signalSemaphoreInfo = {
				vk::SemaphoreSubmitInfo{
					.semaphore = *frameSynchronization.timeline,
					.value = getTimelineValue(FrameTimeline::ePrePresent),
					.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
				},
				vk::SemaphoreSubmitInfo{
					.semaphore = *frameSynchronization.prePresent,
					.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
				},
			};

			const uint64_t waitSemaphoreValues = getTimelineValue(FrameTimeline::eRender);
			const uint64_t signalSemaphoreValues = getTimelineValue(FrameTimeline::ePrePresent);

			const auto submitInfo = vk::SubmitInfo2{}
										.setWaitSemaphoreInfos(waitSemaphoresInfo)
										.setCommandBufferInfos(commandBufferInfo)
										.setSignalSemaphoreInfos(signalSemaphoreInfo);

			PresentQueue.submit2(submitInfo);
		}

		// present
		{
			const auto presentFenceInfo = vk::SwapchainPresentFenceInfoEXT{}.setFences(*frameSynchronization.present);

			const auto presentInfoChain = createStructureChain(
				vk::PresentInfoKHR{}
					.setWaitSemaphores(*frameSynchronization.prePresent)
					.setSwapchains(*Swapchain)
					.setImageIndices(NextImage),
				vk::SwapchainPresentFenceInfoEXT{}.setFences(*frameSynchronization.present));

			// TODO: swapchain recreation
			{
				const auto result = PresentQueue.presentKHR(presentInfoChain.get<vk::PresentInfoKHR>());
				assert(result == vk::Result::eSuccess);
			}
		}

		frameTimer.update();
	}

	// wait for present fences before shutdown
	{
		const auto fences = frameSynchronizations | std::views::transform([](const auto &frameSynchronization)
																		  { return *frameSynchronization.present; }) |
							std::ranges::to<vku::small::vector<vk::Fence, MAX_PENDING_FRAMES>>();

		const auto result = Device.waitForFences(fences, true, UINT64_MAX_VALUE);
		assert(result == vk::Result::eSuccess);
	}
}

int main(const int argc, const char *const *argv)
{
	// Initialize the command line parser
	auto app = CLI::App();
	auto gltfFile = std::string_view();
	app.add_option("gltfFile", gltfFile, "Input glTF file")->required();

	CLI11_PARSE(app, argc, argv);

	// Initialize GLFW
	const auto GlfwErrorCallback = [](const int ErrorCode, const char *const Description)
	{
		fmt::println(std::cerr, "GLFW Error {}: {}", ErrorCode, Description);
	};

	glfwSetErrorCallback(GlfwErrorCallback);

	if (glfwInit() != GLFW_TRUE)
	{
		fmt::println(std::cerr, "Failed to initialize GLFW");
		return EXIT_FAILURE;
	}

	const auto GlfwGuard = gsl::finally([] { glfwTerminate(); });

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow *const Window = glfwCreateWindow(WIDTH, HEIGHT, APP_NAME, nullptr, nullptr);
	if (not Window)
	{
		fmt::println(std::cerr, "Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	const auto GlfwWindowGuard = gsl::finally([&]
											  { glfwDestroyWindow(Window); });

	const auto renderThreadConfig = RenderThreadConfig{
		.gltfFile = gltfFile,
		.Window = Window,
	};

	const auto renderThread = std::jthread(
		[](const std::stop_token &stop_token, const RenderThreadConfig &config)
		{
			RenderThreadFunc(stop_token, config);
		},
		renderThreadConfig
	);

	while (not glfwWindowShouldClose(Window))
	{
		glfwPollEvents();
	}

	return EXIT_SUCCESS;
}

// TODO: swapchain recreation
// TODO: slangc check flags for warnings
// TODO: slangc add shader optimization based on build config
// TODO: add IWYU
// TODO: add clang-format
// TODO: add clang-tidy
// TODO: check VK_KHR_present_id
// TODO: check VK_KHR_present_wait
// TODO: hot-reload shaders
// TODO: add support for VK_EXT_shader_object
// TODO: improve command line
// TODO: replace vku::small with std::inplace_vector C++26
// TODO: check glm intrinsics options
// TODO: check vkAcquireNextImage2KHR
// TODO: add support for gltf cameras
// TODO: multisampling
// TODO: https://www.khronos.org/blog/vk-ext-descriptor-buffer
// TODO: check MeshPrimitiveModes\glTF\MeshPrimitiveModes.gltf