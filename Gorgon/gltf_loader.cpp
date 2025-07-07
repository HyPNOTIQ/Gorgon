#include "gltf_loader.h"

namespace
{

glm::mat4 getNodeMat4(const tinygltf::Node& node)
{
	if (node.matrix.empty())
	{
		const auto make_vec3 = [](const std::vector<double>& vec) {
			return glm::vec3(glm::make_vec3(vec.data()));
			};

		const auto make_quat = [](const std::vector<double>& vec) {
			return glm::quat(glm::make_quat(vec.data()));
			};

		const auto translation = node.translation.empty() ? glm::vec3(0) : make_vec3(node.translation);
		const auto rotation = node.rotation.empty() ? glm::identity<glm::quat>() : make_quat(node.rotation);
		const auto scale = node.scale.empty() ? glm::vec3(1) : make_vec3(node.scale);

		const auto T = glm::translate( glm::identity<glm::mat4>(), translation);
		const auto R = glm::mat4_cast(rotation);
		const auto S = glm::scale( glm::identity<glm::mat4>(), scale);

		return T * R * S;
	}
	else
	{
		return glm::make_mat4(node.matrix.data());
	}
}

}

GltfModel::GltfModel(const CreateInfo& createInfo)
{
	{
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;
		std::string filename(createInfo.gltfFile);

		const auto result = loader.LoadASCIIFromFile(
			&model,
			nullptr, // err
			nullptr, // warn
			filename
		);

		assert(result);
	}

	Scenes = model.scenes
		| std::views::transform([&](const auto& scene) { return GltfScene(model, scene); })
		| std::ranges::to<std::vector<GltfScene>>();

	const auto& memoryAllocator = createInfo.memoryAllocator;

	const auto buffers = [&] {
		const auto transform = [&](const tinygltf::Buffer& buffer) {
			auto stagingBuffer = memoryAllocator.CreateBuffer(
				buffer.data.size(),
				vk::BufferUsageFlagBits::eTransferSrc,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT // TODO:  VMA_ALLOCATION_CREATE_MAPPED_BIT?
			);
			
			auto deviceBuffer = memoryAllocator.CreateBuffer(
				buffer.data.size(),
				vk::BufferUsageFlagBits::eVertexBuffer |
				vk::BufferUsageFlagBits::eIndexBuffer |
				vk::BufferUsageFlagBits::eTransferDst,
				0 // TODO: VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT?  check VkMemoryDedicatedAllocateInfo
			);

			return std::make_tuple(std::ref(buffer), std::move(stagingBuffer), std::move(deviceBuffer));
		};

		return model.buffers |
			std::views::transform(transform) |
			std::ranges::to<std::vector<std::tuple<const tinygltf::Buffer&, vmaBuffer, vmaBuffer>>>();
	}();

	const auto& transferCommandBuffer = createInfo.transferCommandBuffer;

	transferCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	for (const auto& [dataBuffer, stagingBuffer, deviceBuffer] : buffers)
	{
		const auto result = stagingBuffer.CopyMemoryToAllocation(
			dataBuffer.data.data(),
			dataBuffer.data.size()
		);
		assert(result == vk::Result::eSuccess);

		const auto copyRegion = VulkanMemoryAllocator::createBufferCopy(
			stagingBuffer,
			deviceBuffer,
			dataBuffer.data.size()
		);

		transferCommandBuffer.copyBuffer(*stagingBuffer, *deviceBuffer, copyRegion);
	}
	transferCommandBuffer.end();

	const auto& transferQueue = createInfo.transferQueue;
	const auto fence = createInfo.device.createFence({});

	const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *transferCommandBuffer };
	const auto submitInfo = vk::SubmitInfo2{}.setCommandBufferInfos(commandBufferInfo);

	transferQueue.submit2({}, *fence);

	{
		const auto result = createInfo.device.waitForFences({ *fence }, true, UINT64_MAX_VALUE);
		assert(result == vk::Result::eSuccess);
	}
}

GltfPrimitive::GltfPrimitive(const tinygltf::Model& model)
{
}

void GltfPrimitive::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	// bind pipeline
	// cmd draw

}

GltfMesh::GltfMesh(const tinygltf::Model& model)
{
}

void GltfMesh::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	std::ranges::for_each(Primitives, [&](const auto& primitive) { primitive.Draw(commandBuffer); });
}

GltfNode::GltfNode(
	const tinygltf::Model& model,
	const tinygltf::Node& node,
	const glm::mat4& parentTransform)
	: Mesh(model)
	, Transform(parentTransform * getNodeMat4(node))
{
	Children = node.children
		| std::views::transform([&](const int childIndex) { return GltfNode(model, model.nodes[childIndex], Transform); })
		| std::ranges::to<std::vector<GltfNode>>();
}

void GltfNode::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	// TODO: set transform via push constants
	Mesh.Draw(commandBuffer);
	std::ranges::for_each(Children, [&](const auto& child) { child.Draw(commandBuffer); });
}

GltfScene::GltfScene(const tinygltf::Model& model, const tinygltf::Scene& scene)
{
	Nodes = scene.nodes
		| std::views::transform([&](const int nodeIndex) { return GltfNode(model, model.nodes[nodeIndex], glm::identity<glm::mat4>()); })
		| std::ranges::to<std::vector<GltfNode>>();
}

void GltfScene::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	std::ranges::for_each(Nodes, [&](const auto& node) { node.Draw(commandBuffer); });
}

void GltfModel::Draw(
	const size_t sceneIndex,
	const vk::raii::CommandBuffer& commandBuffer) const
{
	// TODO: bind buffers, textures, etc.
	const auto& scene = Scenes[sceneIndex];
	scene.Draw(commandBuffer);
}