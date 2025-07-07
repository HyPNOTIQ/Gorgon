#pragma once

class vmaBuffer
{
public:
	vmaBuffer(const vmaBuffer&) = delete;
	vmaBuffer(vmaBuffer&& rhs) noexcept = default;
	vmaBuffer& operator=(const vmaBuffer&) = delete;
	vmaBuffer& operator=(vmaBuffer&& rhs) noexcept = default;

	~vmaBuffer() {
		if (buffer)
		{
			vmaDestroyBuffer(allocator, buffer, allocation);
		}
	}

	vk::Result MapMemory(void** ppData) const;
	vk::Result CopyMemoryToAllocation(
		const void* pSrcHostPointer,
		const vk::DeviceSize size) const;

	void UnmapMemory() const;

	vk::DeviceSize Offset() const noexcept {
		VmaAllocationInfo pAllocationInfo;
		vmaGetAllocationInfo(allocator, allocation, &pAllocationInfo);
		return pAllocationInfo.offset;
	}

	vk::Buffer operator*() const noexcept {
		return buffer;
	}

private:
	vmaBuffer(VkBuffer buffer,
		VmaAllocation allocation,
		VmaAllocator allocator)
		: buffer(buffer), allocation(allocation), allocator(allocator) {
	}

	vk::Buffer buffer;
	VmaAllocation allocation;
	VmaAllocator allocator;

	friend class VulkanMemoryAllocator;
};

class VulkanMemoryAllocator
{
public:
	static vk::BufferCopy createBufferCopy(
		const vmaBuffer& src,
		const vmaBuffer& dst,
		const vk::DeviceSize size);

	VulkanMemoryAllocator(const VmaAllocatorCreateInfo& createInfo);
	~VulkanMemoryAllocator();
	vmaBuffer CreateBuffer(
		const vk::DeviceSize size,
		const vk::BufferUsageFlags usage,
		const VmaAllocationCreateFlags flags) const;

private:
	VmaAllocator allocator;
};