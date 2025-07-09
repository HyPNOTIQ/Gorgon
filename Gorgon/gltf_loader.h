#pragma once
#include "vma_raii.h"

class GltfBuffer
{
public:
	void BindAsVertex(const vk::raii::CommandBuffer& commandBuffer) const;
	void BindAsIndex(const vk::raii::CommandBuffer& commandBuffer) const;

	vmaBuffer buffer;
};

class GltfBufferView
{
public:
	void BindAsVertex(const vk::raii::CommandBuffer& commandBuffer) const;
	void BindAsIndex(const vk::raii::CommandBuffer& commandBuffer) const;

	//size_t ByteOffset() const { return byteOffset; }
	//size_t ByteStride() const { return byteStride; }

//private:
	const GltfBuffer& buffer;
	uint32_t byteOffset;
	uint32_t byteStride;
	uint32_t byteLength;
};

class GltfAccessors
{
public:
	void BindAsVertex(const vk::raii::CommandBuffer& commandBuffer) const;
	void BindAsIndex(const vk::raii::CommandBuffer& commandBuffer) const;

	GltfBufferView bufferView;
	uint32_t byteOffset;
	uint32_t count;
};

// move to pch.h
template<typename T>
using OptionalRef = std::optional<std::reference_wrapper<T>>;
constexpr auto maxAccessors = 11u;

class GltfPrimitive
{
public:
	struct DrawInfo
	{
		const vk::raii::CommandBuffer& commandBuffer;
	};

	void Draw(const DrawInfo& drawInfo) const;

	vku::small::vector<vk::VertexInputBindingDescription2EXT, maxAccessors> vertexBindingDescriptions;
	vku::small::vector<vk::VertexInputAttributeDescription2EXT, maxAccessors> vertexAttributeDescriptions;
	// TODO: ref?
	std::optional<const GltfAccessors> positionAccessor = std::nullopt;
	std::optional<const GltfAccessors> indicesAccessor = std::nullopt;
	vk::PrimitiveTopology topology;
};

class GltfMesh
{
public:
	struct DrawInfo
	{
		const vk::raii::CommandBuffer& commandBuffer;
	};

	void Draw(const DrawInfo& drawInfo) const;
	std::vector<GltfPrimitive> Primitives;
};

class GltfNode
{
public:
	struct DrawInfo
	{
		const glm::mat4& viewProj;
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::raii::PipelineLayout& pipelineLayout;
	};

	void Draw(const DrawInfo& drawInfo) const;
	glm::mat4 Transform = glm::identity<glm::mat4>();
	std::optional<GltfMesh> Mesh;
	std::vector<GltfNode> Children;
};

class GltfScene
{
public:
	struct DrawInfo
	{
		const glm::mat4& viewProj;
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::raii::PipelineLayout& pipelineLayout;
	};

	void Draw(const DrawInfo& drawInfo) const;
	std::vector<GltfNode> Nodes;
};

class GltfModel
{
public:
	struct DrawInfo
	{
		size_t sceneIndex;
		const glm::mat4& viewProj;
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::raii::PipelineLayout& pipelineLayout;
	};

	struct CreateInfo {
		const std::string_view& gltfFile;
		const VulkanMemoryAllocator& memoryAllocator;
		const vk::raii::CommandBuffer& transferCommandBuffer;
		const vk::raii::Queue& transferQueue;
		const vk::raii::Device& device;
	};

	GltfModel(const CreateInfo& createInfo);

	void Draw(const DrawInfo& drawInfo) const;

private:
	//tinygltf::Model model;
	std::vector<GltfScene> Scenes;
	std::vector<GltfMesh> Meshes;
	std::vector<GltfAccessors> Accessors;
	std::vector<GltfBufferView> BufferViews;
	std::vector<GltfBuffer> Buffers;
};
