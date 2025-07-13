#include "vma.h"

VmaBuffer::VmaBuffer(
	const VkBuffer buffer,
	const VmaAllocation allocation,
	const VmaAllocator allocator) noexcept
	: buffer(buffer)
	, allocation(allocation)
	, allocator(allocator)
{}

VmaBuffer::~VmaBuffer()
{
	if (buffer)
	{
		vmaDestroyBuffer(allocator, buffer, allocation);
	}
}

vk::Result VmaBuffer::CopyMemoryToAllocation(
	const void* pSrcHostPointer,
	const vk::DeviceSize size) const
{
	const auto result = vmaCopyMemoryToAllocation(
		allocator,
		pSrcHostPointer,
		allocation,
		0, // dstAllocationLocalOffset
		size
	);

	return static_cast<vk::Result>(result);
}

vk::DeviceSize VmaBuffer::offset() const
{
	VmaAllocationInfo pAllocationInfo;
	vmaGetAllocationInfo(allocator, allocation, &pAllocationInfo);
	return pAllocationInfo.offset;
}

vk::Buffer VmaBuffer::operator*() const
{
	return buffer;
}

VulkanMemoryAllocator::VulkanMemoryAllocator(const CreateInfo& createInfo)
{
	const auto& instance = createInfo.instance;
	const auto& device = createInfo.device;

	const auto vulkanFunctions = VmaVulkanFunctions{
		.vkGetInstanceProcAddr = instance.getDispatcher()->vkGetInstanceProcAddr,
		.vkGetDeviceProcAddr = device.getDispatcher()->vkGetDeviceProcAddr,
		.vkGetPhysicalDeviceProperties = instance.getDispatcher()->vkGetPhysicalDeviceProperties,
		.vkGetPhysicalDeviceMemoryProperties = instance.getDispatcher()->vkGetPhysicalDeviceMemoryProperties,
		.vkAllocateMemory = device.getDispatcher()->vkAllocateMemory,
		.vkFreeMemory = device.getDispatcher()->vkFreeMemory,
		.vkMapMemory = device.getDispatcher()->vkMapMemory,
		.vkUnmapMemory = device.getDispatcher()->vkUnmapMemory,
		.vkFlushMappedMemoryRanges = device.getDispatcher()->vkFlushMappedMemoryRanges,
		.vkInvalidateMappedMemoryRanges = device.getDispatcher()->vkInvalidateMappedMemoryRanges,
		.vkBindBufferMemory = device.getDispatcher()->vkBindBufferMemory,
		.vkBindImageMemory = device.getDispatcher()->vkBindImageMemory,
		.vkGetBufferMemoryRequirements = device.getDispatcher()->vkGetBufferMemoryRequirements,
		.vkGetImageMemoryRequirements = device.getDispatcher()->vkGetImageMemoryRequirements,
		.vkCreateBuffer = device.getDispatcher()->vkCreateBuffer,
		.vkDestroyBuffer = device.getDispatcher()->vkDestroyBuffer,
		.vkCreateImage = device.getDispatcher()->vkCreateImage,
		.vkDestroyImage = device.getDispatcher()->vkDestroyImage,
		.vkCmdCopyBuffer = device.getDispatcher()->vkCmdCopyBuffer,
#if VMA_VULKAN_VERSION >= 1001000
		.vkGetBufferMemoryRequirements2KHR = reinterpret_cast<PFN_vkGetBufferMemoryRequirements2>(device.getDispatcher()->vkGetBufferMemoryRequirements2), // TODO: why cast is needed?
		.vkGetImageMemoryRequirements2KHR = reinterpret_cast<PFN_vkGetImageMemoryRequirements2>(device.getDispatcher()->vkGetImageMemoryRequirements2),
		.vkBindBufferMemory2KHR = reinterpret_cast<PFN_vkBindBufferMemory2>(device.getDispatcher()->vkBindBufferMemory2),
		.vkBindImageMemory2KHR = reinterpret_cast<PFN_vkBindImageMemory2>(device.getDispatcher()->vkBindImageMemory2),
		.vkGetPhysicalDeviceMemoryProperties2KHR = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(instance.getDispatcher()->vkGetPhysicalDeviceMemoryProperties2),
#endif // VMA_VULKAN_VERSION >= 1001000
#if VMA_VULKAN_VERSION >= 1003000
		.vkGetDeviceBufferMemoryRequirements = device.getDispatcher()->vkGetDeviceBufferMemoryRequirements,
		.vkGetDeviceImageMemoryRequirements = device.getDispatcher()->vkGetDeviceImageMemoryRequirements,
#endif // VMA_VULKAN_VERSION >= 1003000
	};

	const auto vmaAllocatorCreateInfo = VmaAllocatorCreateInfo{
		.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
		.physicalDevice = createInfo.physicalDevice,
		.device = *device,
		.pVulkanFunctions = &vulkanFunctions,
		.instance = *instance,
		.vulkanApiVersion = VK_API_VERSION,
	};

	const auto result = vmaCreateAllocator(&vmaAllocatorCreateInfo, &allocator);
	assert(result == VK_SUCCESS);
}

VulkanMemoryAllocator::~VulkanMemoryAllocator()
{
	vmaDestroyAllocator(allocator);
}

VmaBuffer VulkanMemoryAllocator::CreateBuffer(
	const vk::DeviceSize size,
	const vk::BufferUsageFlags usage,
	const VmaAllocationCreateFlags flags) const
{
	const VkBufferCreateInfo bufferCreateInfo = vk::BufferCreateInfo{
		.size = size,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive,
	};

	const auto allocationCreateInfo = VmaAllocationCreateInfo{
		.flags = flags,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VkBuffer buffer;
	VmaAllocation allocation;
	const auto result = vmaCreateBuffer(
		allocator,
		&bufferCreateInfo,
		&allocationCreateInfo,
		&buffer,
		&allocation,
		nullptr
	);

	return VmaBuffer(buffer, allocation, allocator);
}

#if 0
vk::BufferCopy VulkanMemoryAllocator::createBufferCopy(
	const vmaBuffer& src,
	const vmaBuffer& dst,
	const vk::DeviceSize size)
{
	return vk::BufferCopy{
		.srcOffset = src.Offset(),
		.dstOffset = dst.Offset(),
		.size = size
	};
}



VulkanMemoryAllocator::~VulkanMemoryAllocator()
{
	vmaDestroyAllocator(allocator);
}

vmaBuffer VulkanMemoryAllocator::CreateBuffer(
	const vk::DeviceSize size,
	const vk::BufferUsageFlags usage,
	const VmaAllocationCreateFlags flags) const
{
	const VkBufferCreateInfo bufferCreateInfo = vk::BufferCreateInfo{
		.size = size,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive,
	};

	const auto allocationCreateInfo = VmaAllocationCreateInfo{
		.flags = flags,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VkBuffer buffer;
	VmaAllocation allocation;
	const auto result = vmaCreateBuffer(
		allocator,
		&bufferCreateInfo,
		&allocationCreateInfo,
		&buffer,
		&allocation,
		nullptr
	);

	return vmaBuffer(buffer, allocation, allocator);
}

vmaBuffer::~vmaBuffer()
{
	if (buffer)
	{
		vmaDestroyBuffer(allocator, buffer, allocation);
	}
}



vk::Result vmaBuffer::MapMemory(void** ppData) const
{
	const auto result = vmaMapMemory(allocator, allocation, ppData);
	return static_cast<vk::Result>(result);
}

vk::Result vmaBuffer::CopyMemoryToAllocation(
	const void* pSrcHostPointer,
	const vk::DeviceSize size) const
{
	const auto result = vmaCopyMemoryToAllocation(
		allocator,
		pSrcHostPointer,
		allocation,
		0,
		size
	);

	return static_cast<vk::Result>(result);
}

void vmaBuffer::UnmapMemory() const
{
	vmaUnmapMemory(allocator, allocation);
}

vk::DeviceSize vmaBuffer::Offset() const
{
	VmaAllocationInfo pAllocationInfo;
	vmaGetAllocationInfo(allocator, allocation, &pAllocationInfo);
	return pAllocationInfo.offset;
}
#endif

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"