#include "loader.h"

namespace
{

glm::mat4 getNodeMat4(const tinygltf::Node& node)
{
	if (not node.matrix.empty())
	{
		return glm::make_mat4(node.matrix.data());
	}

	const auto make_vec3 = [](const std::vector<double>& vec) {
		return glm::vec3(glm::make_vec3(vec.data()));
		};

	const auto make_quat = [](const std::vector<double>& vec) {
		return glm::quat(glm::make_quat(vec.data()));
		};

	const auto translation = node.translation.empty() ? glm::vec3(0) : make_vec3(node.translation);
	const auto rotation = node.rotation.empty() ? QUAT_IDENTITY : make_quat(node.rotation);
	const auto scale = node.scale.empty() ? glm::vec3(1) : make_vec3(node.scale);

	const auto T = glm::translate(MAT4_IDENTITY, translation);
	const auto R = glm::mat4_cast(rotation);
	const auto S = glm::scale(MAT4_IDENTITY, scale);

	return T * R * S;
}

struct LoadBuffersInfo
{
	const tinygltf::Model& model;
	const VulkanMemoryAllocator& vma;
};

vk::DeviceSize getElemSize(const tinygltf::Accessor& accessor)
{
	const auto componentType = accessor.componentType;
	const auto type = accessor.type;

	size_t componentSize;

	switch (componentType) {
	case TINYGLTF_COMPONENT_TYPE_BYTE:
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		componentSize = sizeof(uint8_t);
		break;
	case TINYGLTF_COMPONENT_TYPE_SHORT:
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		componentSize = sizeof(uint16_t);
		break;
	case TINYGLTF_COMPONENT_TYPE_INT:
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		componentSize = sizeof(uint32_t);
		break;
	case TINYGLTF_COMPONENT_TYPE_FLOAT:
		componentSize = sizeof(float);
		break;
	case TINYGLTF_COMPONENT_TYPE_DOUBLE:
		componentSize = sizeof(double);
		break;
	default:
		assert(false);
	}

	int numComponents = 0;

	switch (type) {
	case TINYGLTF_TYPE_SCALAR:
		numComponents = 1;
		break;
	case TINYGLTF_TYPE_VEC2:
		numComponents = 2;
		break;
	case TINYGLTF_TYPE_VEC3:
		numComponents = 3;
		break;
	case TINYGLTF_TYPE_VEC4:
	case TINYGLTF_TYPE_MAT2:
		numComponents = 4;
		break;
	case TINYGLTF_TYPE_MAT3:
		numComponents = 9;
		break;
	case TINYGLTF_TYPE_MAT4:
		numComponents = 16;
		break;
	default:
		assert(false);
	}

	return componentSize * numComponents;
}

}

namespace gltf
{

Model Loader::loadFromFile(const std::string_view& gltfFile)
{
	tinygltf::Model model;
	{
		auto loader = tinygltf::TinyGLTF();
		const auto filename = std::string(gltfFile);

		// TODO: handle errors and warnings
		const auto result = loader.LoadASCIIFromFile(
			&model,
			nullptr, // err
			nullptr, // warn
			filename
		);

		assert(result);
	}

	// Buffers
	// TODO: it sould be moved to loader.cpp with proper interface, so any loader can use it
	auto buffers = [&]
	{
		const auto bufferSize = [](const tinygltf::Buffer& buffer)
		{
			return buffer.data.size() * sizeof(decltype(buffer.data)::value_type);
		};

		auto buffers = [&] {
			const auto transform = [&](const tinygltf::Buffer& buffer) {
				const auto size = bufferSize(buffer);

				auto stagingBuffer = vma.CreateBuffer(
					size,
					vk::BufferUsageFlagBits::eTransferSrc,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT // TODO:  VMA_ALLOCATION_CREATE_MAPPED_BIT?
				);

				auto deviceBuffer = vma.CreateBuffer(
					size,
					vk::BufferUsageFlagBits::eVertexBuffer |
					vk::BufferUsageFlagBits::eIndexBuffer |
					vk::BufferUsageFlagBits::eTransferDst,
					0 // TODO: VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT?  check VkMemoryDedicatedAllocateInfo
				);

				return std::make_tuple(std::ref(buffer), std::move(stagingBuffer), std::move(deviceBuffer));
			};

			return model.buffers |
				std::views::transform(transform) |
				std::ranges::to<std::vector<std::tuple<const tinygltf::Buffer&, VmaBuffer, VmaBuffer>>>();
		}();

		transferCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		for (const auto& [dataBuffer, stagingBuffer, deviceBuffer] : buffers)
		{
			const auto size = bufferSize(dataBuffer);

			const auto result = stagingBuffer.CopyMemoryToAllocation(
				dataBuffer.data.data(),
				size
			);
			assert(result == vk::Result::eSuccess);


			const auto copyRegion = vk::BufferCopy{
				.srcOffset = stagingBuffer.offset(),
				.dstOffset = deviceBuffer.offset(),
				.size = size
			};

			transferCommandBuffer.copyBuffer(*stagingBuffer, *deviceBuffer, copyRegion);
		}

		transferCommandBuffer.end();

		const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *transferCommandBuffer };
		const auto submitInfo = vk::SubmitInfo2{}.setCommandBufferInfos(commandBufferInfo);

		transferQueue.submit2(submitInfo);
		transferQueue.waitIdle();

		return buffers
			| std::views::transform([](auto& tuple) { return Buffer(std::move(std::get<2>(tuple))); })
			| std::ranges::to<std::vector<Buffer>>();

	}();

	// Buffer views
	//const auto createBufferView = [&](const tinygltf::BufferView& bufferView) {
	//	return BufferView{
	//		.buffer = buffers[bufferView.buffer],
	//		.byteOffset = bufferView.byteOffset,
	//		.byteStride = bufferView.byteStride,
	//		.byteLength = bufferView.byteLength,
	//	};
	//};

	//auto bufferViews = model.bufferViews
	//	| std::views::transform([&](const auto& bufferView) { return createBufferView(bufferView); })
	//	| std::ranges::to<std::vector<BufferView>>();

	//const auto createAccessor = [&](const tinygltf::Accessor& accessor) {
	//	return Accessor{
	//		.bufferView = bufferViews[accessor.bufferView],
	//		.byteOffset = accessor.byteOffset,
	//		.count = accessor.count,
	//		.elemSize = getElemSize(accessor),
	//	};
	//};

	//auto accessors = model.accessors
	//	| std::views::transform([&](const auto& accessor) { return createAccessor(accessor); })
	//	| std::ranges::to<std::vector<Accessor>>();

	const auto createPrimitive = [&](const tinygltf::Primitive& primitive) {
		const auto getPrimitiveMode = [&] {
			vk::PrimitiveTopology result;

			switch (primitive.mode) {
			case -1: // -1 is default == TINYGLTF_MODE_TRIANGLES
			case TINYGLTF_MODE_TRIANGLES: result = vk::PrimitiveTopology::eTriangleList; break;
			case TINYGLTF_MODE_POINTS: result = vk::PrimitiveTopology::ePointList; break;
			case TINYGLTF_MODE_LINE: result = vk::PrimitiveTopology::eLineList; break;
			case TINYGLTF_MODE_LINE_LOOP: assert(false); break; // TODO: there is no line loop in Vulkan, implement as line strip?
			case TINYGLTF_MODE_TRIANGLE_STRIP: result = vk::PrimitiveTopology::eTriangleStrip; break;
			case TINYGLTF_MODE_TRIANGLE_FAN: result = vk::PrimitiveTopology::eTriangleFan; break;
			default: assert(false); // unsupported primitive mode
			}

			return result;
		};

		auto vertexBindData = Primitive::VertexBindData();

		const auto getBindingData = [&](const tinygltf::Accessor& accessor) {
			const auto& bufferView = model.bufferViews[accessor.bufferView];
			const auto& buffer = buffers[bufferView.buffer].vmaBuffer;
			const auto offset = accessor.byteOffset + bufferView.byteOffset + buffer.offset();
			const auto elemSize = getElemSize(accessor);
			const auto stride = std::max(bufferView.byteStride, elemSize);
			const auto size = accessor.count * stride - (stride - elemSize);

			return std::make_tuple(*buffer, offset, size, stride);
		};

		auto primitivePipelineInfo = PrimitivePipelineInfo{};

		const auto& accessors = model.accessors;

		auto count = uint32_t{};

		auto drawFunc = &Primitive::DrawNonIndexed;
		if (const auto it = primitive.attributes.find("POSITION"); it != primitive.attributes.end())
		{
			const auto& accessor = accessors[it->second];
			const auto [buffer, offset, size, stride] = getBindingData(accessor);

			vertexBindData.add(buffer, offset, size, stride);

			count = accessor.count;
		}

		auto indexedData = [&] -> decltype(Primitive::indexedData) {
			if (const auto indices = primitive.indices; indices != -1)
			{
				const auto& accessor = accessors[indices];
				const auto [buffer, offset, size, stride] = getBindingData(accessor);

				const auto indexType = [&]
				{
					vk::IndexType result;

					switch (accessor.componentType) {
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: result = vk::IndexType::eUint8; break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: result = vk::IndexType::eUint16; break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: result = vk::IndexType::eUint32; break;
					default: assert(false); // unsupported
					}

					return result;
				}();

				auto result = Primitive::IndexedData
				{
					.buffer = buffer,
					.offset = offset,
					.size = size,
					.type = indexType,
				};

				count = accessor.count;
				drawFunc = &Primitive::DrawIndexed;
				return std::make_optional(std::move(result));
			}

			return std::nullopt;
		}();

		return Primitive{
			.vertexBindData = std::move(vertexBindData),
			.topology = getPrimitiveMode(),
			.pipeline = getPipeline(primitivePipelineInfo),
			.pipelineLayout = pipelineLayout,
			.count = count,
			.indexedData = std::move(indexedData),
			.drawFunc = drawFunc,
		};
	};

	const auto createMesh = [&](const tinygltf::Mesh& mesh) {
		auto primitives = mesh.primitives
			| std::views::transform([&](const auto& primitive) { return createPrimitive(primitive); })
			| std::ranges::to<std::vector<Primitive>>();

		return Mesh{ .primitives = std::move(primitives) };
	};

	auto meshes = model.meshes
		| std::views::transform([&](const auto& mesh) { return createMesh(mesh); })
		| std::ranges::to<std::vector<Mesh>>();

	const auto createNode = [&](this auto self, const tinygltf::Node& node, const glm::mat4& parentTransform) -> Node {
		const auto transform = parentTransform * getNodeMat4(node);

		auto children = node.children
			| std::views::transform([&](const auto& childIndex) { return self(model.nodes[childIndex], transform); })
			| std::ranges::to<std::vector<Node>>();

		return Node{
			.transform = transform,
			.mesh = node.mesh == -1 ? std::nullopt : std::make_optional(std::ref(meshes[node.mesh])),
			.frontFace = glm::determinant(transform) > 0.0f ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise,
			.children = std::move(children),
		};
	};

	const auto createScene = [&](const tinygltf::Scene& scene) {
		auto nodes = scene.nodes
			| std::views::transform([&](const auto& nodeIndex) { return createNode(model.nodes[nodeIndex], MAT4_IDENTITY); })
			| std::ranges::to<std::vector<Node>>();

		return Scene{
			.nodes = std::move(nodes)
		};
	};

	auto scenes = model.scenes
		| std::views::transform([&](const auto& scene) { return createScene(scene); })
		| std::ranges::to<std::vector<Scene>>();

	auto modelData = Model::Data
	{
		.buffers = std::move(buffers),
		//.bufferViews = std::move(bufferViews),
		//.accessors = std::move(accessors),
		.meshes = std::move(meshes),
		.scenes = std::move(scenes),
	};

	return Model(std::move(modelData));
}

}