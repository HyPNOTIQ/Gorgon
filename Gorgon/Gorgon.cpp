#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <fmt/ostream.h>
#include <iostream>
#include <thread>
#include <filesystem>
#include <atomic>
#include <ranges>
#include <gsl/gsl>

#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/utility/vk_small_containers.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

constexpr auto WIDTH = 800u;
constexpr auto HEIGHT = 600u;
constexpr auto MAX_PENDING_FRAMES = 2u;
constexpr auto UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();

void GlfwErrorCallback(
	const int ErrorCode,
	const char* const Description)
{
	fmt::println(std::cerr, "GLFW Error {}: {}", ErrorCode, Description);
}

#define VKCONFIG

namespace vk
{
	template <typename T>
	concept Chainable = requires(T t) {
		{ t.pNext } -> std::convertible_to<const void*>;
	};

	template<typename... Ts>
	concept AllChainable = (Chainable<Ts> && ...);

	template <AllChainable... ChainElements>
	auto createStructureChain(ChainElements const... elems)
	{
		return vk::StructureChain<ChainElements...>(elems...);
		//return vk::StructureChain<ChainElements...>(std::forward<ChainElements>(elems)...); // TODO: check && universal ref
	}

	const auto deviceExtensions = {
		vk::KHRSwapchainExtensionName,
	};

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
}

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

void RenderThreadFunc(std::stop_token stop_token, GLFWwindow* const Window)
{
	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	const auto Context = vk::raii::Context();

	const auto RequiredExtensions = GetRequiredExtensions();

#ifndef VKCONFIG
	const auto DebugUtilsMessengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT{
		.messageSeverity = ~vk::DebugUtilsMessageSeverityFlagsEXT(),
		.messageType = ~vk::DebugUtilsMessageTypeFlagsEXT() ^ vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding,
		.pfnUserCallback = vk::DebugMessageFunc,
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

#ifndef VKCONFIG
	const auto DebugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(Instance, DebugUtilsMessengerCreateInfo);
#endif // !VKCONFIG

	const auto Surface = [&] {
		VkSurfaceKHR surface;

		// TODO: handle return value
		glfwCreateWindowSurface(*Instance, Window, nullptr, &surface);

		return vk::raii::SurfaceKHR(Instance, surface);
	}();

	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Instance);
	const auto PhysicalDevices = Instance.enumeratePhysicalDevices();
	const auto& PhysicalDevice = PhysicalDevices[0];

	const auto queueFamilyIndices = [&] {
		const auto QueueFamilyProperties = PhysicalDevice.getQueueFamilyProperties2();

		struct QueueFamilyIndices
		{
			QueueFamilyIndices(const uint32_t graphicsVal, const uint32_t presentVal)
				: Graphics(graphicsVal), Present(presentVal), same(graphicsVal == presentVal) {}

			uint32_t Graphics;
			uint32_t Present;
			bool same;
		};

		const auto supportsGraphics = [&](const vk::QueueFamilyProperties2& properties) {
			return bool(properties.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics);
		};

		const auto supportsPresent = [&](const uint32_t index) {
			return glfwGetPhysicalDevicePresentationSupport(*Instance, *PhysicalDevice, index) == GLFW_TRUE;
		};

		const auto findSeparate = [&] -> std::optional<QueueFamilyIndices> {
			const auto graphicsQueues = [&] {
				const auto filter = [&](const std::pair<size_t, vk::QueueFamilyProperties2>& pair) {
					return supportsGraphics(pair.second);
				};

				const auto transform = [](const std::pair<size_t, vk::QueueFamilyProperties2>& pair) {
					return uint32_t(pair.first);
				};

				return QueueFamilyProperties
					| std::views::enumerate
					| std::views::filter(filter)
					| std::views::transform(transform)
					| std::ranges::to<std::vector<uint32_t>>();
			}();

			const auto presentQueues = [&] {
				return std::views::iota(0u, QueueFamilyProperties.size())
					| std::views::filter(supportsPresent)
					| std::ranges::to<std::vector<uint32_t>>();
			}();

			for (const uint32_t graphicsQueueIndex : graphicsQueues) {
				for (const uint32_t presentQueueIndex : presentQueues) {
					if (graphicsQueueIndex != presentQueueIndex) {

						return std::make_optional(QueueFamilyIndices(graphicsQueueIndex, presentQueueIndex));
					}
				}
			}

			return std::nullopt;
		};

		const auto find = [&] -> std::optional<QueueFamilyIndices> {
			for (const auto& [index, value] : std::views::enumerate(QueueFamilyProperties)) {
				const auto queueFamilyIndex = uint32_t(index);

				if (supportsGraphics(value) && supportsPresent(queueFamilyIndex)) {
					return std::make_optional(QueueFamilyIndices(queueFamilyIndex, queueFamilyIndex));
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

	const auto queuePriority = 1.0f;
	const auto GraphicsQueueCreateInfo = vk::DeviceQueueCreateInfo{
		.queueFamilyIndex = queueFamilyIndices.Graphics,
		.queueCount = 1,
		.pQueuePriorities = &queuePriority,
	};

	const auto PresentQueueCreateInfo = vk::DeviceQueueCreateInfo{
		.queueFamilyIndex = queueFamilyIndices.Present,
		.queueCount = 1,
		.pQueuePriorities = &queuePriority,
	};

	// TODO
	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos = { GraphicsQueueCreateInfo, PresentQueueCreateInfo };

	// Step 1: Sort the vector
	std::ranges::sort(queueCreateInfos);

	// Step 2: Use std::ranges::unique to move duplicates to the end
	auto [first, last] = std::ranges::unique(queueCreateInfos);

	// Step 3: Erase the duplicates
	queueCreateInfos.erase(first, last);

	//const auto queueCreateInfos = { GraphicsQueueCreateInfo, PresentQueueCreateInfo };

	const auto DeviceCreateInfoChain = createStructureChain(
		vk::DeviceCreateInfo{}
			.setQueueCreateInfos(queueCreateInfos)
			.setPEnabledExtensionNames(vk::deviceExtensions),
		vk::PhysicalDeviceFeatures2{
			.features = {
				//.geometryShader = true,
			}
		},
		vk::PhysicalDeviceVulkan12Features{
			.timelineSemaphore = true,
		},
		vk::PhysicalDeviceVulkan13Features{
			.synchronization2 = true,
			.dynamicRendering = true,
		},
		// check how to replace with core vulkan
		vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT{
			.swapchainMaintenance1 = true,
		}
	);

	const auto Device = PhysicalDevice.createDevice(DeviceCreateInfoChain.get<vk::DeviceCreateInfo>());
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*Device);

	const auto GraphicsQueue = Device.getQueue(queueFamilyIndices.Graphics, 0);
	const auto PresentQueue = Device.getQueue(queueFamilyIndices.Present, 0);

	// TODO: create question on Vulkan-hpp github about vk::ObjectType
	if (not queueFamilyIndices.same) {
		SetDebugUtilsObjectName(
			Device,
			vk::ObjectType::eQueue,
			static_cast<uint64_t>(reinterpret_cast<uintptr_t>(VkQueue(*GraphicsQueue))),
			"GraphicsQueue"
		);

		SetDebugUtilsObjectName(
			Device,
			vk::ObjectType::eQueue,
			static_cast<uint64_t>(reinterpret_cast<uintptr_t>(VkQueue(*PresentQueue))),
			"PresentQueue"
		);
	}
	else
	{
		SetDebugUtilsObjectName(
			Device,
			vk::ObjectType::eQueue,
			static_cast<uint64_t>(reinterpret_cast<uintptr_t>(VkQueue(*GraphicsQueue))),
			"GraphicsPresentQueue"
		);
	}

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

	const auto graphicsCommandPool = [&] {
		const auto CreateInfo = vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
			.queueFamilyIndex = queueFamilyIndices.Graphics,
		};

		return Device.createCommandPool(CreateInfo);
	}();

	const auto presentCommandPool = [&] {
		const auto CreateInfo = vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
			.queueFamilyIndex = queueFamilyIndices.Present,
		};

		return Device.createCommandPool(CreateInfo);
	}();

	const auto renderCommandBuffers = [&] {
		const auto CreateInfo = vk::CommandBufferAllocateInfo{
			.commandPool = *graphicsCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = MAX_PENDING_FRAMES,
		};

		return Device.allocateCommandBuffers(CreateInfo);
	}();

	const auto presentCommandBuffers = [&] {
		const auto CreateInfo = vk::CommandBufferAllocateInfo{
			.commandPool = *presentCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = MAX_PENDING_FRAMES,
		};

		return Device.allocateCommandBuffers(CreateInfo);
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


	const auto pipelineLayout = [&] {
		const auto createInfo = vk::PipelineLayoutCreateInfo{};
		return Device.createPipelineLayout(createInfo);
	};

	const auto createShaderModule = [&](const std::filesystem::path& path) {
		const auto loadSPIRV = [](const std::filesystem::path& path) {
			auto file = std::ifstream(path, std::ios::binary | std::ios::ate);

			if (!file) {
				assert(false);
			}

			const auto size = file.tellg();
			file.seekg(0, std::ios::beg);

			if (size % sizeof(uint32_t) != 0) {
				assert(false);
			}

			std::vector<uint32_t> buffer(size / sizeof(uint32_t));
			if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
				assert(false);
			}

			return buffer;
			};

		const auto code = loadSPIRV(path);

		const auto createInfo = vk::ShaderModuleCreateInfo{
		}.setCode(code);

		return Device.createShaderModule(createInfo);
	};

	// TODO: add support for VK_EXT_shader_object
	const auto fragShaderModule = createShaderModule("shaders/triangle.frag");
	const auto vertShaderModule = createShaderModule("shaders/triangle.vert");

	const auto graphicaPipeline = [&] {
		// Vertex input state
		const auto vertexInputState = vk::PipelineVertexInputStateCreateInfo{};

		// Input assembly
		const auto inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
			.topology = vk::PrimitiveTopology::eTriangleList,
			.primitiveRestartEnable = false
		};

		const auto viewport = vk::Viewport{
			.width = float(ImageExtent.width),
			.height = float(ImageExtent.height),
			.maxDepth = 1,
		};

		const auto scissor = vk::Rect2D{ .extent = ImageExtent };

		// Viewport state
		const auto viewportState = vk::PipelineViewportStateCreateInfo{}
			.setViewports(viewport)
			.setScissors(scissor);

		// Rasterization State
		const auto rasterizationState = vk::PipelineRasterizationStateCreateInfo{
			.polygonMode = vk::PolygonMode::eFill,
			.cullMode = vk::CullModeFlagBits::eBack,
			.frontFace = vk::FrontFace::eClockwise,
		};

		// Multisample state
		const auto multisampleState = vk::PipelineMultisampleStateCreateInfo{
			.rasterizationSamples = vk::SampleCountFlagBits::e1,
			.minSampleShading = 1.0f,
		};

		// Color blend state
		const auto colorBlendAttachmentState = vk::PipelineColorBlendAttachmentState{
			.colorWriteMask = ~vk::ColorComponentFlags(),
		};

		const auto colorBlendState = vk::PipelineColorBlendStateCreateInfo{
		}.setAttachments(colorBlendAttachmentState);

		// Dynamic state
		const auto dynamicStates = {
			vk::DynamicState::ePrimitiveTopology,
		};

		const auto dynamicState = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamicStates);

		// Stages
		const auto stages = {
			vk::PipelineShaderStageCreateInfo{
				.stage = vk::ShaderStageFlagBits::eFragment,
				.module = fragShaderModule,
				.pName = "main",
			},
			vk::PipelineShaderStageCreateInfo{
				.stage = vk::ShaderStageFlagBits::eVertex,
				.module = vertShaderModule,
				.pName = "main",
			},
		};

		const auto pipelineRendering = vk::PipelineRenderingCreateInfo{}
			.setColorAttachmentFormats(SurfaceFormat.format);

		const auto createInfo = vk::GraphicsPipelineCreateInfo{
			.pNext = &pipelineRendering,
			.pVertexInputState = &vertexInputState,
			.pInputAssemblyState = &inputAssemblyState,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizationState,
			.pMultisampleState = &multisampleState,
			.pColorBlendState = &colorBlendState,
			//.pDynamicState = &dynamicState,
		}.setStages(stages);

		return Device.createGraphicsPipeline(nullptr, createInfo);
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
		//vku::small::vector<FrameSynchronization, MAX_PENDING_FRAMES> Result;

		std::vector<FrameSynchronization> Result;
		Result.reserve(MAX_PENDING_FRAMES);

		const auto generateFunc = [&]
		{
			return FrameSynchronization{
				.acquireNextImage = [&] {
					const auto CreateInfo = vk::SemaphoreCreateInfo{};
					return Device.createSemaphore(CreateInfo);
				}(),
				.prePresent = [&] {
					const auto CreateInfo = vk::SemaphoreCreateInfo{};
					return Device.createSemaphore(CreateInfo);
				}(),
				.timeline = [&] {
					const auto typeCreateInfo = vk::SemaphoreTypeCreateInfo {
						.semaphoreType = vk::SemaphoreType::eTimeline,
						.initialValue = 0,
					};

					const auto CreateInfo = vk::SemaphoreCreateInfo{ .pNext = &typeCreateInfo };
					return Device.createSemaphore(CreateInfo);
				}(),
				.present = [&] {
					const auto CreateInfo = vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled };
					return Device.createFence(CreateInfo);
				}(),
			};
		};

		// TODO: rewrite without temp container usage std::ranges::generate_n
		// TODO: https://en.cppreference.com/w/cpp/container/inplace_vector.html
		//return std::views::iota(0u, MAX_PENDING_FRAMES)
		//	| std::views::transform([&](auto) { return generateFunc(); })
		//	| std::ranges::to<vku::small::vector<FrameSynchronization, MAX_PENDING_FRAMES>>();

		std::generate_n(std::back_inserter(Result), MAX_PENDING_FRAMES, generateFunc);

		return Result;
	}();

	auto frameNumber = 0u;
	while (not stop_token.stop_requested())
	//while (frameNumber < MAX_PENDING_FRAMES * 2)
	{
		//fmt::println(std::clog, "Render Thread");
		const auto frameIndex = frameNumber % MAX_PENDING_FRAMES;

		// TODO: create somesort of class with update(frameNumber) to simplify usage
		enum FrameTimeline : uint64_t {
			eRender = 1,
			ePrePresent,
			eMax = ePrePresent
		};

		const auto& frameSynchronization = frameSynchronizations[frameIndex];

		// TODO handle result value
		Device.waitForFences({ *frameSynchronization.present }, true, UINT64_MAX_VALUE);
		Device.resetFences({ *frameSynchronization.present });

		const auto NextImage = [&] {
			const auto Result = Swapchain.acquireNextImage(
				UINT64_MAX_VALUE,
				frameSynchronization.acquireNextImage
			);

			assert(Result.first == vk::Result::eSuccess);
			return Result.second;
		}();

		constexpr auto COLOR_SUBRESOURCE_RANGE = vk::ImageSubresourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.levelCount = 1,
			.layerCount = 1,
		};

		const auto& swapchainImage = swapChainImages[NextImage];
		// render
		{
			const auto& commandBuffer = renderCommandBuffers[frameIndex];
			commandBuffer.reset();
			

			// record command buffer
			{
				commandBuffer.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

				// to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				{
					const auto toAttachmentBarrier = vk::ImageMemoryBarrier2{
						.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
						.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
						.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
						.oldLayout = vk::ImageLayout::eUndefined,
						.newLayout = vk::ImageLayout::eColorAttachmentOptimal,
						.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
						.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
						.image = swapchainImage,
						.subresourceRange = COLOR_SUBRESOURCE_RANGE,
					};

					const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(toAttachmentBarrier);
					commandBuffer.pipelineBarrier2(dependencyInfo);
				}

				const auto colorAttachments = {
					vk::RenderingAttachmentInfo{
						.imageView = swapChainImageViews[NextImage],
						.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
						.loadOp = vk::AttachmentLoadOp::eClear,
						.storeOp = vk::AttachmentStoreOp::eStore,
						.clearValue = { .color = std::to_array({0.5f, 0.5f, 0.5f, 1.0f}) },
					},
				};

				const auto renderingInfo = vk::RenderingInfo{
					.renderArea = vk::Rect2D{ .extent = ImageExtent },
					.layerCount = 1,
				}.setColorAttachments(colorAttachments);

				commandBuffer.beginRendering(renderingInfo);

				commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicaPipeline);
				commandBuffer.draw(3, 1, 0, 0);

				commandBuffer.endRendering();

				// to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
				{
					const auto toPresentBarrier = vk::ImageMemoryBarrier2{
						.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
						.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
						.oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
						.newLayout = vk::ImageLayout::ePresentSrcKHR,
						.srcQueueFamilyIndex = queueFamilyIndices.Graphics,
						.dstQueueFamilyIndex = queueFamilyIndices.Present,
						.image = swapchainImage,
						.subresourceRange = COLOR_SUBRESOURCE_RANGE,
					};

					const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(toPresentBarrier);
					commandBuffer.pipelineBarrier2(dependencyInfo);
				}

				commandBuffer.end();
			}

			const auto waitSemaphoresInfo = vk::SemaphoreSubmitInfo{
				.semaphore = *frameSynchronization.acquireNextImage,
				.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			};

			const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *commandBuffer };

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
			const auto& commandBuffer = presentCommandBuffers[frameIndex];

			const auto waitSemaphoresInfo = vk::SemaphoreSubmitInfo{
				.semaphore = *frameSynchronization.timeline,
				.value = FrameTimeline::eMax * frameNumber + FrameTimeline::eRender,
				.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
			};

			const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *commandBuffer };

			const auto signalSemaphoreInfo = {
				vk::SemaphoreSubmitInfo{
					.semaphore = *frameSynchronization.timeline,
					.value = ePrePresent,
					.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
				},
				vk::SemaphoreSubmitInfo{
					.semaphore = *frameSynchronization.prePresent,
					.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
				},
			};

			const uint64_t waitSemaphoreValues = FrameTimeline::eMax * frameNumber + FrameTimeline::eRender;
			const uint64_t signalSemaphoreValues = FrameTimeline::eMax * frameNumber + FrameTimeline::ePrePresent;

			const auto submitInfo = vk::SubmitInfo2{}
				.setWaitSemaphoreInfos(waitSemaphoresInfo)
				.setCommandBufferInfos(commandBufferInfo)
				.setSignalSemaphoreInfos(signalSemaphoreInfo);

			commandBuffer.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

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

			PresentQueue.submit2(submitInfo);
		}

		// TODO: not needed for non separate queues
		// present
		{
			const auto presentFenceInfo = vk::SwapchainPresentFenceInfoEXT{}.setFences(*frameSynchronization.present);

			const auto presentInfoChain = createStructureChain(
				vk::PresentInfoKHR{}
					.setWaitSemaphores(*frameSynchronization.prePresent)
					.setSwapchains(*Swapchain)
					.setImageIndices(NextImage),
				vk::SwapchainPresentFenceInfoEXT{}.setFences(*frameSynchronization.present)
			);

			// TODO: check VK_KHR_present_id
			// TODO: swapchain recreation
			PresentQueue.presentKHR(presentInfoChain.get<vk::PresentInfoKHR>());
		}

		++frameNumber;
	}

	// wait for present fences before shutdown
	{
		const auto fences = frameSynchronizations
			| std::views::transform([](const auto& frameSynchronization) { return *frameSynchronization.present; })
			| std::ranges::to<vku::small::vector<vk::Fence, MAX_PENDING_FRAMES>>(); // TODO: use std::inplace_vector C++26

		Device.waitForFences(fences, true, UINT64_MAX_VALUE);
	}

	Device.waitIdle();
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

	return EXIT_SUCCESS;
}

// TODO: add IWYU