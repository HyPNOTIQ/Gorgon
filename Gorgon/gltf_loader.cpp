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

void GltfBuffer::BindAsIndex(const vk::raii::CommandBuffer& commandBuffer) const
{
	const auto& buffer = this->buffer;
	buffer.BindAsIndex(commandBuffer);
}

void GltfBufferView::BindAsIndex(const vk::raii::CommandBuffer& commandBuffer) const
{
	const auto& buffer = this->buffer;
	buffer.BindAsIndex(commandBuffer);
}

void GltfAccessors::BindAsIndex(const vk::raii::CommandBuffer& commandBuffer) const
{
	assert(false);
	//const auto& buffer = this->buffer;
	//buffer.BindAsIndex(commandBuffer);
}

void GltfAccessors::BindAsVertex(const vk::raii::CommandBuffer& commandBuffer) const
{
	assert(false);
	//const auto& buffer = this->buffer;
	//buffer.BindAsIndex(commandBuffer);
}

void GltfPrimitive::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	if (not positionAccessor)
	{
		return; // no position data to draw
	}

	// bind pipeline

	//commandBuffer.setVertexInputEXT(vertexBindingDescriptions, vertexAttributeDescriptions);

	const auto& positionAccessor = this->positionAccessor.value();
	//positionAccessor.BindAsVertex(commandBuffer);

	commandBuffer.bindVertexBuffers2(
		0, // first binding
		{ *positionAccessor.bufferView.buffer.buffer }, // buffers
		{ positionAccessor.bufferView.byteOffset + positionAccessor.bufferView.buffer.buffer.Offset() }, // offsets
		{ positionAccessor.bufferView.byteLength },
		{ positionAccessor.bufferView.byteStride + sizeof(glm::vec3)} // strides
	);

	//commandBuffer.bindVertexBuffers(
	//	0, // first binding
	//	{ *positionAccessor.bufferView.buffer.buffer }, // buffers
	//	{ positionAccessor.bufferView.byteOffset + positionAccessor.byteOffset + positionAccessor.bufferView.buffer.buffer.Offset() } // offsets
	//);

	if (indicesAccessor)
	{
		const auto& accessor = indicesAccessor.value();
		//accessor.BindAsIndex(commandBuffer);

		const auto& bufferView = accessor.bufferView;
		const auto& buffer = bufferView.buffer;

		commandBuffer.bindIndexBuffer2(
			*buffer.buffer,
			buffer.buffer.Offset(),
			bufferView.byteLength,
			vk::IndexType::eUint16 // TODO: handle index type
		);

		commandBuffer.drawIndexed(
			accessor.count, // index count
			1, // instance count
			0, // first index
			0, // vertex offset
			0  // first instance
		);
	}
	else
	{
		commandBuffer.draw(
			positionAccessor.count, // vertex count
			1, // instance count
			0, // first vertex
			0  // first instance
		);
	}
}

GltfModel::GltfModel(const CreateInfo& createInfo)
{
	tinygltf::Model model;
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

	auto buffers = [&] {
		const auto& memoryAllocator = createInfo.memoryAllocator;

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
			dataBuffer.data.size() // TODO: use sizeof
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
	const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *transferCommandBuffer };
	const auto submitInfo = vk::SubmitInfo2{}.setCommandBufferInfos(commandBufferInfo);

	transferQueue.submit2(submitInfo);
	const auto queueIdleGuard = gsl::finally([&] { transferQueue.waitIdle(); });

	Buffers = buffers
		| std::views::transform([](auto& tuple) { return GltfBuffer{ .buffer = std::move(std::get<2>(tuple)) }; })
		| std::ranges::to<std::vector<GltfBuffer>>();

	const auto createBufferView = [&](const tinygltf::BufferView& bufferView) {
		return GltfBufferView{
			.buffer = Buffers[bufferView.buffer],
			.byteOffset = static_cast<uint32_t>(bufferView.byteOffset),
			.byteStride = static_cast<uint32_t>(bufferView.byteStride),
			.byteLength = static_cast<uint32_t>(bufferView.byteLength),
		};
	};

	BufferViews = model.bufferViews
		| std::views::transform([&](const auto& bufferView) { return createBufferView(bufferView); })
		| std::ranges::to<std::vector<GltfBufferView>>();

	const auto createAccessor = [&](const tinygltf::Accessor& accessor) {
		return GltfAccessors{
			.bufferView = BufferViews[accessor.bufferView],
			.byteOffset = static_cast<uint32_t>(accessor.byteOffset),
			.count = static_cast<uint32_t>(accessor.count),
		};
	};

	Accessors = model.accessors
		| std::views::transform([&](const auto& accessor) { return createAccessor(accessor); })
		| std::ranges::to<std::vector<GltfAccessors>>();

	const auto createPrimitive = [&](const tinygltf::Primitive& primitive) {
		GltfPrimitive result;

		if(const auto it = primitive.attributes.find("POSITION"); it != primitive.attributes.end())
		{
			const auto& positionAccessor = Accessors[it->second];
			const auto vertexInputBinding = vk::VertexInputBindingDescription2EXT{
				.binding = 0,
				.stride = positionAccessor.bufferView.byteStride,
				.inputRate = vk::VertexInputRate::eVertex
			};

			result.vertexBindingDescriptions.emplace_back(vertexInputBinding);

			const auto vertexInputAttribute = vk::VertexInputAttributeDescription2EXT{
				.location = 0,
				.binding = 0,
				.format = vk::Format::eR32G32B32Sfloat, // assuming vec3
				.offset = positionAccessor.byteOffset
			};
			result.vertexAttributeDescriptions.emplace_back(vertexInputAttribute);

			result.positionAccessor.emplace(positionAccessor);
		}

		if (const auto indices = primitive.indices; indices != -1)
		{
			const auto& indicesAccessor = Accessors[indices];
			result.vertexBindingDescriptions.back() = vk::VertexInputBindingDescription2EXT{
				.binding = 0,
				.stride = indicesAccessor.bufferView.byteStride,
				.inputRate = vk::VertexInputRate::eVertex
			};

			result.vertexAttributeDescriptions.back() = vk::VertexInputAttributeDescription2EXT{
				.location = 0,
				.binding = 0,
				.format = vk::Format::eR32G32B32Sfloat, // assuming vec3
				.offset = indicesAccessor.byteOffset
			};

			result.indicesAccessor.emplace(indicesAccessor);
		}

		return result;
	};

	const auto createMesh = [&](const tinygltf::Mesh& mesh) {
		return GltfMesh{
			.Primitives = mesh.primitives
				| std::views::transform([&](const auto& primitive) { return createPrimitive(primitive); })
				| std::ranges::to<std::vector<GltfPrimitive>>()
		};
		};

	Meshes = model.meshes
		| std::views::transform([&](const auto& mesh) { return createMesh(mesh); })
		| std::ranges::to<std::vector<GltfMesh>>();

	const auto createNode = [&](this auto self, const tinygltf::Node& node, const glm::mat4& parentTransform) -> GltfNode {
		const auto nodeTransform = parentTransform * getNodeMat4(node);
		return GltfNode{
			.Transform = nodeTransform,
			.Mesh = node.mesh == -1 ? std::nullopt : std::make_optional(Meshes[node.mesh]),
			.Children = node.children
				| std::views::transform([&](const auto& childIndex) { return self(model.nodes[childIndex], nodeTransform); })
				| std::ranges::to<std::vector<GltfNode>>(),
		};
	};

	const auto createScene = [&](const tinygltf::Scene& scene) {
		auto nodes = scene.nodes
			| std::views::transform([&](const auto& nodeIndex) { return createNode(model.nodes[nodeIndex], glm::identity<glm::mat4>()); })
			| std::ranges::to<std::vector<GltfNode>>();

		return GltfScene{
			.Nodes = std::move(nodes)
		};
	};

	Scenes = model.scenes
		| std::views::transform([&](const auto& scene) { return createScene(scene); })
		| std::ranges::to<std::vector<GltfScene>>();
}

void GltfModel::Draw(const size_t sceneIndex, const vk::raii::CommandBuffer& commandBuffer) const
{
	if (sceneIndex >= Scenes.size())
	{
		return; // invalid scene index
	}
	const auto& scene = Scenes[sceneIndex];
	scene.Draw(commandBuffer);
}

void GltfScene::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	std::ranges::for_each(Nodes, [&](const GltfNode& node) { node.Draw(commandBuffer); });
}

void GltfNode::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	if (Mesh)
	{
		const auto& mesh = Mesh.value();
		mesh.Draw(commandBuffer);
	}
	std::ranges::for_each(Children, [&](const GltfNode& child) { child.Draw(commandBuffer); });
}

void GltfMesh::Draw(const vk::raii::CommandBuffer& commandBuffer) const
{
	std::ranges::for_each(Primitives, [&](const GltfPrimitive& primitive) { primitive.Draw(commandBuffer); });
}