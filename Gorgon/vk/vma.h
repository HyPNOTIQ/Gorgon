#pragma once

class VmaImage
{
public:
	VmaImage(const VmaImage&) = delete;
	VmaImage(VmaImage&& rhs) noexcept;
	VmaImage& operator=(const VmaImage&) = delete;
	VmaImage& operator=(VmaImage&& rhs) noexcept;

	~VmaImage();

	//vk::DeviceSize offset() const;
	vk::Image operator*() const;

private:
	VmaImage(
		const VkImage image,
		const VmaAllocation allocation,
		const VmaAllocator allocator) noexcept;

	vk::Image image;
	VmaAllocation allocation;
	VmaAllocator allocator;

	friend class VulkanMemoryAllocator;
};

class VmaBuffer
{
public:
	VmaBuffer(const VmaBuffer&) = delete;
	VmaBuffer(VmaBuffer&& rhs) noexcept;
	VmaBuffer& operator=(const VmaBuffer&) = delete;
	VmaBuffer& operator=(VmaBuffer&& rhs) noexcept;

	~VmaBuffer();

	vk::Result CopyMemoryToAllocation(
		const void* const pSrcHostPointer,
		const vk::DeviceSize size) const;

	std::byte* MapMemory() const;
	void UnmapMemory() const;

	vk::DeviceSize size() const; // TODO
	vk::Buffer operator*() const;

private:
	VmaBuffer(
		const VkBuffer buffer,
		const VmaAllocation allocation,
		const VmaAllocator allocator) noexcept;

	vk::Buffer buffer;
	VmaAllocation allocation;
	VmaAllocator allocator;

	friend class VulkanMemoryAllocator;
};

class VulkanMemoryAllocator
{
public:
	struct CreateInfo
	{
		const vk::raii::Instance& instance;
		vk::PhysicalDevice physicalDevice;
		const vk::raii::Device& device;
	};

	VulkanMemoryAllocator(const CreateInfo& createInfo);
	~VulkanMemoryAllocator();
	VmaBuffer createBuffer(
		const vk::DeviceSize size,
		const vk::BufferUsageFlags usage,
		const VmaAllocationCreateFlags flags) const; // TODO

	VmaImage createImage(
		const vk::ImageCreateInfo& info,
		const VmaAllocationCreateFlags flags) const;

private:
	VmaAllocator allocator;
};
