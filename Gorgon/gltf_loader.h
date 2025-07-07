#pragma once
#include "vma_raii.h"

class GltfPrimitive
{
public:
	GltfPrimitive(const tinygltf::Model& model);
	void Draw(const vk::raii::CommandBuffer& commandBuffer) const;
};

class GltfMesh
{
public:
	GltfMesh(const tinygltf::Model& model);
	void Draw(const vk::raii::CommandBuffer& commandBuffer) const;
private:
	std::vector<GltfPrimitive> Primitives;
};

class GltfNode
{
public:
	GltfNode(
		const tinygltf::Model& model,
		const tinygltf::Node& node,
		const glm::mat4& parentTransform
	);

	void Draw(const vk::raii::CommandBuffer& commandBuffer) const;
private:
	std::vector<GltfNode> Children;
	GltfMesh Mesh;
	glm::mat4 Transform = glm::mat4(1.0f);
};

class GltfScene
{
public:
	GltfScene(const tinygltf::Model& model, const tinygltf::Scene& scene);
	void Draw(const vk::raii::CommandBuffer& commandBuffer) const;
private:
	std::vector<GltfNode> Nodes;
};

class GltfModel
{
public:
	struct CreateInfo {
		const std::string_view& gltfFile;
		const VulkanMemoryAllocator& memoryAllocator;
		const vk::raii::CommandBuffer& transferCommandBuffer;
		const vk::raii::Queue& transferQueue;
		const vk::raii::Device& device;
	};

	GltfModel(const CreateInfo& createInfo);

	void Draw(const size_t sceneIndex, const vk::raii::CommandBuffer& commandBuffer) const;

private:
	tinygltf::Model model;
	std::vector<GltfScene> Scenes;
	std::vector<vmaBuffer> Buffers;
};
