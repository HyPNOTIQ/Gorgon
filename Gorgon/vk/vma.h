#pragma once

class VmaBuffer
{
public:
	VmaBuffer(const VmaBuffer&) = delete;
	VmaBuffer(VmaBuffer&& rhs) noexcept = default;
	VmaBuffer& operator=(const VmaBuffer&) = delete;
	VmaBuffer& operator=(VmaBuffer&& rhs) noexcept = default;

	~VmaBuffer();

	//vk::Result MapMemory(void** const ppData) const;
	vk::Result CopyMemoryToAllocation(
		const void* const pSrcHostPointer,
		const vk::DeviceSize size) const;

	//void UnmapMemory() const;

	vk::DeviceSize offset() const;
	vk::Buffer operator*() const;
	//{
	//	return buffer;
	//}

private:
	VmaBuffer(
		const VkBuffer buffer,
		const VmaAllocation allocation,
		const VmaAllocator allocator) noexcept;

	//	: buffer(buffer), allocation(allocation), allocator(allocator) {
	//}

	vk::Buffer buffer;
	VmaAllocation allocation;
	VmaAllocator allocator;

	friend class VulkanMemoryAllocator;
};

class VulkanMemoryAllocator
{
public:
	//static vk::BufferCopy createBufferCopy(
	//	const vmaBuffer& src,
	//	const vmaBuffer& dst,
	//	const vk::DeviceSize size);
	struct CreateInfo
	{
		const vk::raii::Instance& instance;
		vk::PhysicalDevice physicalDevice;
		const vk::raii::Device& device;
	};

	VulkanMemoryAllocator(const CreateInfo& createInfo);
	~VulkanMemoryAllocator();
	VmaBuffer CreateBuffer(
		const vk::DeviceSize size,
		const vk::BufferUsageFlags usage,
		const VmaAllocationCreateFlags flags) const;

private:
	VmaAllocator allocator;
};
