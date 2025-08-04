#pragma once
#include "vk/vma.h"

namespace gltf
{

class Sampler
{
public:
	vk::raii::Sampler sampler;
};

struct ImageData
{
	VmaImage image;
	vk::raii::ImageView imageView;
};

class Material2
{
public:
	struct TextureData
	{
		uint32_t texture;
		uint32_t sampler;
		uint32_t uv;
	};

	glm::vec4 baseColorFactor;
	std::optional<TextureData> baseColorTexture;
	std::optional<TextureData> metallicRoughnessTexture;
	std::optional<TextureData> normalTexture;
	std::optional<TextureData> occlusionTexture;
	std::optional<TextureData> emissiveTexture;
	//uint8_t baseColorTextureUV;
	//uint8_t metallicRoughnessTextureUV;
	//uint8_t normalTextureUV;
	//uint8_t occlusionTextureUV;
	//uint8_t emissiveTextureUV;
	float metallicFactor;
	float roughnessFactor;
	float normalTextureScale;
	float alphaCutoff;
};

class Buffer
{
public:
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
		vk::PipelineLayout pipelineLayout;
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

	uint32_t materialIndex;
	DrawFunc drawFunc;
};

class Mesh
{
public:
	struct DrawInfo
	{
		const vk::raii::CommandBuffer& commandBuffer;
		const vk::Extent2D& surfaceExtent;
		vk::PipelineLayout pipelineLayout;
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
		vk::PipelineLayout pipelineLayout;
	};

	void Draw(const DrawInfo& drawInfo) const;
	glm::mat4 transform;
	OptionalRef<Mesh> mesh;
	vk::FrontFace frontFace;
	
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
		vk::PipelineLayout pipelineLayout;
	};

	std::vector<Node> nodes;
private:
	void Draw(const DrawInfo& drawInfo) const;

	friend class Model;
};

class Model
{
public:
	~Model();

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
		//std::vector<Material> materials;
		Buffer materialsSSBO;
		std::vector<ImageData> imageData;
		vk::PipelineLayout pipelineLayout;
		std::vector<vk::raii::DescriptorSet> descriptorSetsRAII;
		std::vector<vk::DescriptorSet> descriptorSets;
		//vk::DescriptorPool descriptorPool;
		const vk::raii::Device& device;
	};

	Model(const Model&) = delete;
	Model(Data&& data) noexcept : data(std::move(data)) {}

	Data data;

	friend class Loader;
};

}