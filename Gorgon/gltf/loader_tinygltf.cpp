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

vk::DeviceSize getElemSize(const tinygltf::Accessor& accessor)
{
	return tinygltf::GetComponentSizeInBytes(accessor.componentType) * tinygltf::GetNumComponentsInType(accessor.type);
}

inline std::optional<vk::Format> GltfToVkFormat(const tinygltf::Accessor& accessor) {
	using Pair = std::pair<int, int>;
	static const std::unordered_map<Pair, vk::Format> formatMap = {
		{{TINYGLTF_TYPE_VEC2, TINYGLTF_COMPONENT_TYPE_FLOAT}, vk::Format::eR32G32Sfloat},
		{{TINYGLTF_TYPE_VEC2, TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE}, vk::Format::eR8G8Unorm},
		{{TINYGLTF_TYPE_VEC2, TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT}, vk::Format::eR16G16Unorm},
		{{TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT}, vk::Format::eR32G32B32Sfloat},
		{{TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE}, vk::Format::eR8G8B8Unorm},
		{{TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT}, vk::Format::eR16G16B16Unorm},
		{{TINYGLTF_TYPE_VEC4, TINYGLTF_COMPONENT_TYPE_FLOAT}, vk::Format::eR32G32B32A32Sfloat},
		{{TINYGLTF_TYPE_VEC4, TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE}, vk::Format::eR8G8B8A8Unorm},
		{{TINYGLTF_TYPE_VEC4, TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT}, vk::Format::eR16G16B16A16Unorm},
	};

	if (auto it = formatMap.find({ accessor.type, accessor.componentType }); it != formatMap.end())
		return it->second;

	assert(false);
	return std::nullopt;
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

				auto stagingBuffer = vma.createBuffer(
					size,
					vk::BufferUsageFlagBits::eTransferSrc,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT // TODO:  VMA_ALLOCATION_CREATE_MAPPED_BIT?
				);

				auto deviceBuffer = vma.createBuffer(
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

			transferCommandBuffer.copyBuffer(*stagingBuffer, *deviceBuffer, copyRegion); // TODO: copyBuffer2
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

	// HACK
	static auto defaultSampler = [&]() {
		const auto createInfo = vk::SamplerCreateInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			//.anisotropyEnable = true, // TODO
		};

		return device.createSampler(createInfo);
	}();

	// TODO: move it into model and resuse on reload
	const auto createSampler = [&](const tinygltf::Sampler& sampler) {
		const auto magFilter = [&] {
			vk::Filter result;

			switch (sampler.magFilter) {
			case TINYGLTF_TEXTURE_FILTER_NEAREST: result = vk::Filter::eNearest; break;
			case -1:
			case TINYGLTF_TEXTURE_FILTER_LINEAR: result = vk::Filter::eLinear; break;
			default: assert(false);
			}

			return result;
			}();

		const auto minFilter = [&] {
			vk::Filter result;

			switch (sampler.minFilter) {
			case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
			case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
			case TINYGLTF_TEXTURE_FILTER_NEAREST: result = vk::Filter::eNearest; break;
			case -1:
			case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
			case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
			case TINYGLTF_TEXTURE_FILTER_LINEAR: result = vk::Filter::eLinear; break;
			default: assert(false);
			}

			return result;
			}();


		const auto samplerAddressMode = [](const int wrap) {
			vk::SamplerAddressMode result;

			switch (wrap) {
			case TINYGLTF_TEXTURE_WRAP_REPEAT: result = vk::SamplerAddressMode::eRepeat; break;
			case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE: result = vk::SamplerAddressMode::eClampToEdge; break;
			case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: result = vk::SamplerAddressMode::eMirroredRepeat; break;
			default: assert(false);
			}

			return result;
			};

		const auto createInfo = vk::SamplerCreateInfo{
			.pNext = nullptr,
			.magFilter = magFilter,
			.minFilter = minFilter,
			.addressModeU = samplerAddressMode(sampler.wrapS),
			.addressModeV = samplerAddressMode(sampler.wrapT),
			//.anisotropyEnable = true, // TODO
		};

		return Sampler{ .sampler = device.createSampler(createInfo) };
		};

	auto samplers = model.samplers
		| std::views::transform([&](const auto& sampler) { return createSampler(sampler); })
		| std::ranges::to<std::vector<Sampler>>();

	auto materials = [&] {
		const auto imageSize = [](const tinygltf::Image& image) {
			return image.image.size() * sizeof(decltype(image.image)::value_type);
		};

		const auto extent = [](const tinygltf::Image& image) {
			return vk::Extent3D{
				.width = static_cast<uint32_t>(image.width),
				.height = static_cast<uint32_t>(image.height),
				.depth = 1,
			};
		};

		struct ImageLoad {
			const tinygltf::Image& image;
			VmaBuffer src;
			vk::Image dst;
		};

		std::vector<ImageLoad> imageLoads;

		const auto createMaterial = [&](const tinygltf::Material& material) {
			const auto createTexture = [&](const tinygltf::TextureInfo& textureInfo) -> std::optional<Texture> {
				const auto createImage = [&](const tinygltf::Image& image, const vk::Format format) {
					const auto size = imageSize(image);

					auto stagingBuffer = vma.createBuffer(
						size,
						vk::BufferUsageFlagBits::eTransferSrc,
						VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
					);

					auto vmaImage = [&] {
						const auto createInfo = vk::ImageCreateInfo{
							.imageType = vk::ImageType::e2D,
							.format = format,
							.extent = extent(image),
							.mipLevels = 1,
							.arrayLayers = 1,
							.samples = vk::SampleCountFlagBits::e1,
							.tiling = vk::ImageTiling::eOptimal,
							.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
							.sharingMode = vk::SharingMode::eExclusive,
						};

						return vma.createImage(createInfo, 0);
					}();

					auto imageLoad = ImageLoad{
						.image = image,
						.src = std::move(stagingBuffer),
						.dst = *vmaImage,
					};

					imageLoads.push_back(std::move(imageLoad));

					return vmaImage;
				};

				if (textureInfo.index == -1) {
					return std::nullopt;
				}

				const auto& texture = model.textures[textureInfo.index];
				const auto& image = model.images[texture.source];
				const auto format = vk::Format::eR8G8B8A8Srgb;
				auto vmaImage = createImage(image, format);
				auto imageView = [&] {
					const auto createInfo = vk::ImageViewCreateInfo{
						.image = *vmaImage,
						.viewType = vk::ImageViewType::e2D,
						.format = format,
						.subresourceRange = COLOR_SUBRESOURCE_RANGE,
					};

					return device.createImageView(createInfo);
				}();

				return Texture{
					.image = std::move(vmaImage),
					.imageView = std::move(imageView),
					.sampler = texture.sampler == -1 ? *defaultSampler : *(samplers[texture.sampler].sampler),
					.uv = static_cast<uint32_t>(textureInfo.texCoord),
				};
			};

			return Material{
				.baseColorTexture = createTexture(material.pbrMetallicRoughness.baseColorTexture),
				.metallicRoughnessTexture = createTexture(material.pbrMetallicRoughness.metallicRoughnessTexture),
				.emissiveTexture = createTexture(material.emissiveTexture),
			};
		};

		auto materials = model.materials
			| std::views::transform([&](const auto& material) { return createMaterial(material); })
			| std::ranges::to<std::vector<Material>>();

		transferCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		for (const auto& [image, src, dst] : imageLoads)
		{
			const auto size = imageSize(image);

			const auto result = src.CopyMemoryToAllocation(
				image.image.data(),
				size
			);
			assert(result == vk::Result::eSuccess);

			// to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
			{
				const auto imageMemoryBarrier = vk::ImageMemoryBarrier2{
					.newLayout = vk::ImageLayout::eTransferDstOptimal,
					.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
					.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
					.image = dst,
					.subresourceRange = COLOR_SUBRESOURCE_RANGE,
				};

				const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(imageMemoryBarrier);
				transferCommandBuffer.pipelineBarrier2(dependencyInfo);
			}

			constexpr auto COLOR_SUBRESOURCE_LAYERS = vk::ImageSubresourceLayers{
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			};

			const auto copyRegion = vk::BufferImageCopy2{
				.bufferOffset = 0,
				.imageSubresource = COLOR_SUBRESOURCE_LAYERS,
				.imageExtent = extent(image),
			};

			const auto copyBufferToImageInfo = vk::CopyBufferToImageInfo2
			{
				.srcBuffer = *src,
				.dstImage = dst,
				.dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
			}.setRegions(copyRegion);

			transferCommandBuffer.copyBufferToImage2(copyBufferToImageInfo);

			// to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			{
				const auto imageMemoryBarrier = vk::ImageMemoryBarrier2{
					.oldLayout = vk::ImageLayout::eTransferDstOptimal,
					.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
					.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
					.image = dst,
					.subresourceRange = COLOR_SUBRESOURCE_RANGE,
				};

				const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(imageMemoryBarrier);
				transferCommandBuffer.pipelineBarrier2(dependencyInfo);
			}
		}

		transferCommandBuffer.end();

		const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *transferCommandBuffer };
		const auto submitInfo = vk::SubmitInfo2{}.setCommandBufferInfos(commandBufferInfo);

		transferQueue.submit2(submitInfo);
		transferQueue.waitIdle();

		return materials;
	}();

	const auto createPrimitive = [&](const tinygltf::Primitive& primitive) {
		const auto getPrimitiveMode = [&] {
			vk::PrimitiveTopology result;

			switch (primitive.mode) {
			case -1: // -1 is default == TINYGLTF_MODE_TRIANGLES //	TODO
			case TINYGLTF_MODE_TRIANGLES: result = vk::PrimitiveTopology::eTriangleList; break;
			case TINYGLTF_MODE_POINTS: result = vk::PrimitiveTopology::ePointList; break;
			case TINYGLTF_MODE_LINE_LOOP: /*assert(false); break;*/ // TODO: there is no line loop in Vulkan, implement as line strip?
			case TINYGLTF_MODE_LINE: result = vk::PrimitiveTopology::eLineList; break;
			case TINYGLTF_MODE_LINE_STRIP: result = vk::PrimitiveTopology::eLineStrip; break;
			case TINYGLTF_MODE_TRIANGLE_STRIP: result = vk::PrimitiveTopology::eTriangleStrip; break;
			case TINYGLTF_MODE_TRIANGLE_FAN: result = vk::PrimitiveTopology::eTriangleFan; break;
			default: assert(false);
			}

			return result;
		};

		auto vertexBindData = Primitive::VertexBindData();

		const auto getBindingData = [&](const tinygltf::Accessor& accessor) {
			const auto& bufferView = model.bufferViews[accessor.bufferView];
			const auto& buffer = buffers[bufferView.buffer].vmaBuffer;
			const auto offset = accessor.byteOffset + bufferView.byteOffset/* + buffer.offset()*/;
			const auto elemSize = getElemSize(accessor);
			const auto stride = std::max(bufferView.byteStride, elemSize);
			const auto size = accessor.count * stride - (stride - elemSize);

			return std::make_tuple(*buffer, offset, size, stride);
		};

		auto primitivePipelineInfo = PrimitivePipelineInfo{};

		const auto& accessors = model.accessors;

		auto count = uint32_t{};
		auto drawFunc = &Primitive::DrawNonIndexed;

		const auto position_l = [&](const tinygltf::Accessor& accessor) {
			count = accessor.count;
		};

		const auto normal_l = [&](const tinygltf::Accessor& accessor) {
			primitivePipelineInfo.hasNormal = true;
		};

		const auto tangent_l = [&](const tinygltf::Accessor& accessor) {
			primitivePipelineInfo.hasTangent = true;
		};

		const auto texcoord0_l = [&](const tinygltf::Accessor& accessor) {
			primitivePipelineInfo.texcoord0 = GltfToVkFormat(accessor).value();
		};

		const auto texcoord1_l = [&](const tinygltf::Accessor& accessor) {
			primitivePipelineInfo.texcoord1 = GltfToVkFormat(accessor).value();
		};

		const auto color0_l = [&](const tinygltf::Accessor& accessor) {
			primitivePipelineInfo.color0 = GltfToVkFormat(accessor).value();
		};

		using func_t = std::function<void(const tinygltf::Accessor&)>; // TODO: use std::function_ref c++26
		using pair_t = std::pair<std::string, func_t>;

		const std::initializer_list<pair_t> attributes = {
			{ "POSITION", position_l},
			{ "NORMAL", normal_l},
			{ "TANGENT", tangent_l},
			{ "TEXCOORD_0", texcoord0_l},
			{ "TEXCOORD_1", texcoord1_l},
			{ "COLOR_0", color0_l},
		};

		for (const auto& attribute : attributes)
		{
			if (const auto it = primitive.attributes.find(attribute.first); it != primitive.attributes.end())
			{
				const auto& accessor = accessors[it->second];
				const auto [buffer, offset, size, stride] = getBindingData(accessor);

				vertexBindData.add(buffer, offset, size, stride);

				attribute.second(accessor);
			}
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
					default: assert(false);
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

#if 1
	constexpr auto descriptorCount = 256u;
	const auto countInfo = vk::DescriptorSetVariableDescriptorCountAllocateInfo{
	}.setDescriptorCounts(descriptorCount);

	const auto allocateInfo = vk::DescriptorSetAllocateInfo{
		.pNext = &countInfo,
		.descriptorPool = pipelineLayoutData.descriptorPool,
	}.setSetLayouts(*pipelineLayoutData.descriptorSetLayout);

	auto descriptorSets = (*device).allocateDescriptorSets(allocateInfo);

	auto descriptorWrites = std::vector<vk::WriteDescriptorSet>();

	auto index = 0u;
	for (const auto& material: materials)
	{
		if (material.baseColorTexture)
		{
			const auto& texture = material.baseColorTexture.value();

			const auto descriptorImageInfo = vk::DescriptorImageInfo{
				.sampler = texture.sampler,
				.imageView = texture.imageView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			};

			const auto descriptorWrite = vk::WriteDescriptorSet{
				.dstSet = descriptorSets[0],
				.dstBinding = 0,
				.dstArrayElement = index++,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			}.setImageInfo(descriptorImageInfo);

			descriptorWrites.push_back(descriptorWrite);
		}

		if (material.metallicRoughnessTexture)
		{
			const auto& texture = material.metallicRoughnessTexture.value();

			const auto descriptorImageInfo = vk::DescriptorImageInfo{
				.sampler = texture.sampler,
				.imageView = texture.imageView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			};

			const auto descriptorWrite = vk::WriteDescriptorSet{
				.dstSet = descriptorSets[0],
				.dstBinding = 0,
				.dstArrayElement = index++,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			}.setImageInfo(descriptorImageInfo);

			descriptorWrites.push_back(descriptorWrite);
		}

		if (material.emissiveTexture)
		{
			const auto& texture = material.emissiveTexture.value();

			const auto descriptorImageInfo = vk::DescriptorImageInfo{
				.sampler = texture.sampler,
				.imageView = texture.imageView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			};

			const auto descriptorWrite = vk::WriteDescriptorSet{
				.dstSet = descriptorSets[0],
				.dstBinding = 0,
				.dstArrayElement = index++,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			}.setImageInfo(descriptorImageInfo);

			descriptorWrites.push_back(descriptorWrite);
		}
	}

	device.updateDescriptorSets(descriptorWrites, {});
#endif

	auto modelData = Model::Data
	{
		.buffers = std::move(buffers),
		.meshes = std::move(meshes),
		.scenes = std::move(scenes),
		.materials = std::move(materials),
		.samplers = std::move(samplers),
		.pipelineLayout = *pipelineLayoutData.pipelineLayout,
		.descriptorSets = std::move(descriptorSets),
		.device = device,
	};

	return Model(std::move(modelData));
}

}
