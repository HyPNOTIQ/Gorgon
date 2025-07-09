#include "vma_raii.h"

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

VulkanMemoryAllocator::VulkanMemoryAllocator(const VmaAllocatorCreateInfo& createInfo)
{
	const auto result = vmaCreateAllocator(&createInfo, &allocator);
	assert(result == VK_SUCCESS);
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

void vmaBuffer::BindAsVertex(const vk::raii::CommandBuffer& commandBuffer) const
{
	//commandBuffer.bindVertexBuffers(0, buffer);
}

void vmaBuffer::BindAsIndex(const vk::raii::CommandBuffer& commandBuffer) const
{
	//commandBuffer.bindIndexBuffer(buffer, 0, vk::IndexType::eUint32);
}

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"