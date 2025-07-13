#pragma once
#include "vk/vma.h"

namespace gltf
{

class Buffer
{
public:
	Buffer(const Buffer&) = delete;
	Buffer(VmaBuffer&& vmaBuffer) : vmaBuffer(std::move(vmaBuffer)) {}
	Buffer(Buffer&& rhs) noexcept = default;
	Buffer& operator=(const Buffer&) = delete;
	Buffer& operator=(Buffer&& rhs) noexcept = default;

//private:
	VmaBuffer vmaBuffer;
};

class Primitive
{
public:
	struct DrawInfo
	{
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::Extent2D& surfaceExtent;
		vk::FrontFace frontFace;
	};

	using DrawFunc = void (Primitive::*)(const DrawInfo&) const;

	void Draw(const DrawInfo& info) const;
	void DrawIndexed(const DrawInfo& info) const;
	void DrawNonIndexed(const DrawInfo& info) const;

	struct VertexBindData
	{
		void add(
			const vk::Buffer buffer,
			const vk::DeviceSize offset,
			const vk::DeviceSize size,
			const vk::DeviceSize stride
		);

		vku::small::vector<vk::Buffer, VERTEX_INPUT_NUM> buffers;
		vku::small::vector<vk::DeviceSize, VERTEX_INPUT_NUM> offsets;
		vku::small::vector<vk::DeviceSize, VERTEX_INPUT_NUM> sizes;
		vku::small::vector<vk::DeviceSize, VERTEX_INPUT_NUM> strides;
	} vertexBindData;

	vk::PrimitiveTopology topology;
	vk::Pipeline pipeline;
	uint32_t count;

	struct IndexedData
	{
		vk::Buffer buffer;
		vk::DeviceSize offset;
		vk::DeviceSize size;
		vk::IndexType type;
	};

	std::optional<IndexedData> indexedData;

	DrawFunc drawFunc;
};

class Mesh
{
public:
	struct DrawInfo
	{
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::Extent2D& surfaceExtent;
	};

	void Draw(const DrawInfo& drawInfo) const;

	std::vector<Primitive> primitives;
};

class Node
{
public:
	struct DrawInfo
	{
		const glm::mat4& viewProj;
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::Extent2D& surfaceExtent;
	};

	void Draw(const DrawInfo& drawInfo) const;
	glm::mat4 transform;
	OptionalRef<Mesh> mesh;
	vk::FrontFace frontFace;
	vk::PipelineLayout pipelineLayout;
	std::vector<Node> children;
};

class Scene
{
public:
	struct DrawInfo
	{
		const glm::mat4& viewProj;
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::Extent2D& surfaceExtent;
	};

	void Draw(const DrawInfo& drawInfo) const;

	std::vector<Node> nodes;
};

class Model
{
public:
	struct DrawInfo
	{
		size_t sceneIndex;
		const glm::mat4& viewProj;
		const vk::raii::CommandBuffer& commandBuffer;
		vk::Extent2D surfaceExtent;
	};

	void Draw(const DrawInfo& drawInfo) const;

private:
	struct Data
	{
		std::vector<Buffer> buffers;
		std::vector<Mesh> meshes;
		std::vector<Scene> scenes;
	};

	Model(const Model&) = delete;
	Model(Data&& data) noexcept : data(std::move(data)) {}

	Data data;

	friend class Loader;
};

}