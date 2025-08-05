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

std::optional<vk::Format> GltfToVkFormat(const tinygltf::Accessor& accessor) {
	using Key = std::pair<int, int>;
	static const std::unordered_map<Key, vk::Format> formatMap = {
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
	{
		return it->second;
	}

	assert(false);
	return std::nullopt;
}

std::optional<vk::Format> GltfImageToVkFormat(const tinygltf::Image& image, const bool unorm) {
	using Key = std::tuple<int, int, bool>;
	static const std::unordered_map<Key, vk::Format> formatMap = {
		{{4, 8, false}, vk::Format::eR8G8B8A8Srgb},
		{{4, 8, true}, vk::Format::eR8G8B8A8Unorm},
	};

	if (auto it = formatMap.find({ image.component, image.bits, unorm }); it != formatMap.end())
	{
		return it->second;
	}

	assert(false);
	return std::nullopt;
}

vk::SamplerCreateInfo GltfToVkSamplerInfo(const tinygltf::Sampler& sampler)
{
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

	return vk::SamplerCreateInfo{
		.magFilter = magFilter,
		.minFilter = minFilter,
		.addressModeU = samplerAddressMode(sampler.wrapS),
		.addressModeV = samplerAddressMode(sampler.wrapT),
		//.anisotropyEnable = true, // TODO
	};
}

vk::SamplerCreateInfo getDefaultSamplerInfo()
{
	static auto defaultSamplerInfo = vk::SamplerCreateInfo{
		.magFilter = vk::Filter::eLinear,
		.minFilter = vk::Filter::eLinear,
		.addressModeU = vk::SamplerAddressMode::eRepeat,
		.addressModeV = vk::SamplerAddressMode::eRepeat,
		//.anisotropyEnable = true, // TODO
	};

	return defaultSamplerInfo;
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

	auto buffers = [&] {
		const auto transform = [](const tinygltf::Buffer& buffer) {

			const auto size = buffer.data.size() * sizeof(decltype(buffer.data)::value_type);
			const auto data = reinterpret_cast<const std::byte*>(buffer.data.data());

			return std::span<const std::byte>(data, size);
		};

		const auto spans = model.buffers
			| std::views::transform(transform)
			| std::ranges::to<std::vector>();


		return loadBuffers(spans);
	}();

	auto samplers = std::vector<vk::Sampler>();
	auto imageInfos = std::vector<ImageInfo>();

	auto materials = [&] {
		const auto transform = [&](const tinygltf::Material& material){

			const auto getTextureData = [&](const auto& textureInfo, const bool unorm) -> std::optional<Material::TextureData> {
				if (textureInfo.index == -1)
				{
					return std::nullopt;
				}

				const auto& texture = model.textures[textureInfo.index];

				assert(texture.source != -1); // TODO
				const auto& image = model.images[texture.source];

				const auto getTextureIndex = [&] {

					const auto data = reinterpret_cast<const std::byte*>(image.image.data());
					const auto imageSize = image.image.size() * sizeof(decltype(image.image)::value_type);

					const auto extent = [&]() {
						return vk::Extent3D{
							.width = static_cast<uint32_t>(image.width),
							.height = static_cast<uint32_t>(image.height),
							.depth = 1,
						};
					};

					const auto imageInfo = ImageInfo{
						.imageBuffer = std::span<const std::byte>(data, imageSize),
						.format = GltfImageToVkFormat(image, unorm).value(),
						.extent = extent(),
					};

					auto it = std::ranges::find(imageInfos, imageInfo);

					if (it != imageInfos.end())
					{
						const auto result = std::distance(imageInfos.begin(), it);
						return static_cast<uint32_t>(result);
					}
					else
					{
						imageInfos.push_back(imageInfo);
						const auto result = imageInfos.size() - 1;
						return static_cast<uint32_t>(result);
					}
				};

				const auto getUV = [&]() {
					return static_cast<uint32_t>(textureInfo.texCoord);
				};

				const auto getSamplerIndex = [&] {
					const auto samplerInfo = texture.sampler != -1 ?
						GltfToVkSamplerInfo(model.samplers[texture.sampler]) :
						getDefaultSamplerInfo();

					const auto sampler = getSampler(samplerInfo);

					auto it = std::ranges::find(samplers, sampler);

					if (it != samplers.end())
					{
						const auto result = std::distance(samplers.begin(), it);
						return static_cast<uint32_t>(result);
					}
					else
					{
						samplers.push_back(sampler);
						const auto result = samplers.size() - 1;
						return static_cast<uint32_t>(result);
					}
				};

				const auto result = Material::TextureData{
					.texture = getTextureIndex(),
					.sampler = getSamplerIndex(),
					.uv = getUV(),
				};

				return std::make_optional(result);
			};

			return Material{
				.baseColorFactor = glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data()),
				.baseColorTexture = getTextureData(material.pbrMetallicRoughness.baseColorTexture, false),
				.metallicRoughnessTexture = getTextureData(material.pbrMetallicRoughness.metallicRoughnessTexture, true),
				.normalTexture = getTextureData(material.normalTexture, true),
				.occlusionTexture = getTextureData(material.occlusionTexture, true),
				.emissiveTexture = getTextureData(material.emissiveTexture, false),
				.metallicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor),
				.roughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor),
				.normalTextureScale = static_cast<float>(material.normalTexture.scale),
				.alphaCutoff = static_cast<float>(material.alphaCutoff),
			};
		};

		auto result = model.materials
			| std::views::transform(transform)
			| std::ranges::to<std::vector>();

		// default material
		result.push_back(transform(tinygltf::Material()));

		return result;
	}();

	auto materialsSSBO = createMaterialsSSBO(materials);
	auto imageData = createImages(imageInfos);

	const auto createPrimitive = [&](const tinygltf::Primitive& primitive) {
		const auto getPrimitiveMode = [&] {
			vk::PrimitiveTopology result;

			switch (primitive.mode) {
			case -1: // -1 is default == TINYGLTF_MODE_TRIANGLES //	TODO
			case TINYGLTF_MODE_TRIANGLES: result = vk::PrimitiveTopology::eTriangleList; break;
			case TINYGLTF_MODE_POINTS: result = vk::PrimitiveTopology::ePointList; break;
			case TINYGLTF_MODE_LINE: result = vk::PrimitiveTopology::eLineList; break;
			case TINYGLTF_MODE_LINE_LOOP: result = vk::PrimitiveTopology::eLineStrip; break; // TODO: Vulkan doesn't support line loop
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

		const auto materialIndex = [&] {
			return primitive.material != -1 ?
				static_cast<uint32_t>(primitive.material) :
				static_cast<uint32_t>(model.materials.size());
		}();

		const auto& material = materials[materialIndex];

		primitivePipelineInfo.hasBaseColorTexture = material.baseColorTexture.has_value();
		primitivePipelineInfo.hasMetallicRoughnessTexture = material.metallicRoughnessTexture.has_value();
		primitivePipelineInfo.hasNormalTexture = material.normalTexture.has_value();
		primitivePipelineInfo.hasOcclusionTexture = material.occlusionTexture.has_value();
		primitivePipelineInfo.hasEmissiveTexture = material.emissiveTexture.has_value();

		return Primitive{
			.vertexBindData = std::move(vertexBindData),
			.topology = getPrimitiveMode(),
			.pipeline = getPipeline(primitivePipelineInfo),
			.count = count,
			.indexedData = std::move(indexedData),
			.materialIndex = materialIndex,
			.drawFunc = drawFunc,
		};
	};

	const auto createMesh = [&](const tinygltf::Mesh& mesh) {
		auto primitives = mesh.primitives
			| std::views::transform([&](const auto& primitive) { return createPrimitive(primitive); })
			| std::ranges::to<std::vector>();

		return Mesh{
			.primitives = std::move(primitives),
		};
	};

	auto meshes = model.meshes
		| std::views::transform([&](const auto& mesh) { return createMesh(mesh); })
		| std::ranges::to<std::vector>();

	const auto createNode = [&](this auto self, const tinygltf::Node& node, const glm::mat4& parentTransform) -> Node {
		const auto modelMatix = parentTransform * getNodeMat4(node);

		auto children = node.children
			| std::views::transform([&](const auto& childIndex) { return self(model.nodes[childIndex], modelMatix); })
			| std::ranges::to<std::vector>();

		return Node{
			.modelMatix = modelMatix,
			.mesh = node.mesh == -1 ? std::nullopt : std::make_optional(std::ref(meshes[node.mesh])),
			.frontFace = glm::determinant(modelMatix) > 0.0f ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise,
			.children = std::move(children),
		};
	};

	const auto createScene = [&](const tinygltf::Scene& scene) {
		auto nodes = scene.nodes
			| std::views::transform([&](const auto& nodeIndex) { return createNode(model.nodes[nodeIndex], MAT4_IDENTITY); })
			| std::ranges::to<std::vector>();

		return Scene{
			.nodes = std::move(nodes)
		};
	};

	auto scenes = model.scenes
		| std::views::transform([&](const auto& scene) { return createScene(scene); })
		| std::ranges::to<std::vector>();

	auto descriptorSetsRAII = [&] {
		const auto allocateInfo = vk::DescriptorSetAllocateInfo{
			.descriptorPool = descriptorPool,
		}.setSetLayouts(*pipelineLayoutData.descriptorSetLayout);

		return device.allocateDescriptorSets(allocateInfo);
	}(); 

	auto bindlessDescriptorSetsRAII = [&] {
		constexpr auto descriptorCount = 256u;

		const auto countInfo = vk::DescriptorSetVariableDescriptorCountAllocateInfo{}
			.setDescriptorCounts(descriptorCount);

		const auto allocateInfo = vk::DescriptorSetAllocateInfo{
			.pNext = &countInfo,
			.descriptorPool = bindlessDescriptorPool,
		}.setSetLayouts(*pipelineLayoutData.bindlessDescriptorSetLayout);

		return device.allocateDescriptorSets(allocateInfo);
	}();

	auto descriptorSets = std::vector<vk::DescriptorSet>();
	descriptorSets.reserve(descriptorSetsRAII.size() + bindlessDescriptorSetsRAII.size());

	for (const auto& descriptorSet : descriptorSetsRAII)
	{
		descriptorSets.push_back(*descriptorSet);
	}

	for (auto& descriptorSet : bindlessDescriptorSetsRAII)
	{
		descriptorSets.push_back(*descriptorSet);
	}

	std::ranges::move(bindlessDescriptorSetsRAII, std::back_inserter(descriptorSetsRAII));
	bindlessDescriptorSetsRAII.clear();

	auto descriptorWrites = std::vector<vk::WriteDescriptorSet>();
	descriptorWrites.reserve(1 + imageData.size());

	{
		const auto descriptorBufferInfo = vk::DescriptorBufferInfo{
			.buffer = *materialsSSBO.vmaBuffer,
			.range = vk::WholeSize,
		};

		const auto descriptorWrite = vk::WriteDescriptorSet{
			.dstSet = descriptorSets[0],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorType = vk::DescriptorType::eStorageBuffer,
		}.setBufferInfo(descriptorBufferInfo);

		descriptorWrites.push_back(descriptorWrite);
	}

	const auto descriptorImageInfos = [&] {
		const auto transform = [&](const ImageData& data) {
			return vk::DescriptorImageInfo{
				.sampler = samplers[0], // TODO
				.imageView = data.imageView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			};
		};

		return imageData
			| std::views::transform(transform)
			| std::ranges::to<std::vector>();
	}();

	if(not descriptorImageInfos.empty())
	{
		const auto descriptorWrite = vk::WriteDescriptorSet{
			.dstSet = descriptorSets[1],
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		}.setImageInfo(descriptorImageInfos);

		descriptorWrites.push_back(descriptorWrite);
	}

	device.updateDescriptorSets(descriptorWrites, {});

	auto modelData = Model::Data
	{
		.buffers = std::move(buffers),
		.meshes = std::move(meshes),
		.scenes = std::move(scenes),
		.materialsSSBO = std::move(materialsSSBO),
		.imageData = std::move(imageData),
		.pipelineLayout = *pipelineLayoutData.pipelineLayout,
		.descriptorSetsRAII = std::move(descriptorSetsRAII),
		.descriptorSets = std::move(descriptorSets),
		.device = device,
	};

	return Model(std::move(modelData));
}

}
