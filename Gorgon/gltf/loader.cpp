#include "loader.h"
#include <shaders/shared.inl>

namespace
{

vk::raii::DescriptorPool createDescriptorPool(const vk::raii::Device& device)
{
	const auto poolSizes = {
		vk::DescriptorPoolSize{
			.type = vk::DescriptorType::eStorageBuffer,
			.descriptorCount = 256u, // TODO
		},
	};

	const auto createInfo = vk::DescriptorPoolCreateInfo{
		.maxSets = 256u, // TODO
		.poolSizeCount = 256u, // TODO
	}.setPoolSizes(poolSizes);

	return device.createDescriptorPool(createInfo);
}

vk::raii::DescriptorPool createBindlessDescriptorPool(const vk::raii::Device& device)
{
	const auto poolSize = vk::DescriptorPoolSize{
		.type = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 256u, // TODO
	};

	const auto createInfo = vk::DescriptorPoolCreateInfo{
		.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
		.maxSets = 256u, // TODO
		.poolSizeCount = 256u, // TODO
	}.setPoolSizes(poolSize);

	return device.createDescriptorPool(createInfo);
}

}

namespace gltf
{

Loader::Loader(const CreateInfo& info)
	: device(info.device)
	, vma(info.vma)
	, transferCommandBuffer(info.transferCommandBuffer)
	, transferQueue(info.transferQueue)
	, pipelineLayoutData(createPipelineLayoutData(info.device))
	, descriptorPool(createDescriptorPool(info.device))
	, bindlessDescriptorPool(createBindlessDescriptorPool(info.device))
	, shader(Shader(info.device, "shaders/combined.spv"))
	, surfaceFormat(info.surfaceFormat)
	, depthFormat(info.depthFormat)
{}

vk::Sampler Loader::getSampler(const vk::SamplerCreateInfo& info)
{
	auto it = samplers.find(info);
	if (it == samplers.end()) {
		const auto& [new_it, inserted] = samplers.emplace(info, device.createSampler(info));
		assert(inserted);

		return new_it->second;
	}
	return it->second;
}

vk::Pipeline Loader::getPipeline(const PrimitivePipelineInfo& info)
{
	const auto createPipeline = [&]
	{
		auto bindingDescriptions = vku::small::vector<vk::VertexInputBindingDescription, VERTEX_INPUT_NUM>();
		auto attributeDescriptions = vku::small::vector<vk::VertexInputAttributeDescription, VERTEX_INPUT_NUM>();

		auto addDescription = [&, bindingIndex = uint32_t(0)](const uint32_t location, const vk::Format format) mutable {
			const auto bindingDescription = vk::VertexInputBindingDescription{
				.binding = bindingIndex++,
				.inputRate = vk::VertexInputRate::eVertex,
			};

			const auto attributeDescription = vk::VertexInputAttributeDescription{
				.location = location,
				.binding = bindingDescription.binding,
				.format = format,
			};

			bindingDescriptions.emplace_back(bindingDescription);
			attributeDescriptions.emplace_back(attributeDescription);
			};

		// POSITION
		addDescription(0, vk::Format::eR32G32B32Sfloat);

		union {
			PrimitiveFlags data;
			PrimitiveFlagsInt packed = 0;

			static_assert(sizeof(packed) >= sizeof(data));
		} primitiveFlags;

		// NORMAL
		if (info.hasNormal)
		{
			addDescription(1, vk::Format::eR32G32B32Sfloat);
			primitiveFlags.data.normal = 1;
		}

		// TANGENT
		if (info.hasTangent)
		{
			addDescription(2, vk::Format::eR32G32B32A32Sfloat);
			primitiveFlags.data.tangent = 1;
		}

		const auto checkTexcoordFormat = [](const vk::Format val) {
			const auto formats = {
				vk::Format::eR32G32Sfloat,
				vk::Format::eR8G8Unorm,
				vk::Format::eR16G16Unorm,
			};

			for (const auto format : formats) {
				if (format == val)
				{
					return true;
				}
			}

			return false;
		};

		// TEXCOORD_0
		if (info.texcoord0)
		{
			const auto format = info.texcoord0.value();

			assert(checkTexcoordFormat(format));

			addDescription(3, format);
			primitiveFlags.data.texcoord_0 = 1;
		}

		// TEXCOORD_1
		if (info.texcoord1)
		{
			const auto format = info.texcoord1.value();

			assert(checkTexcoordFormat(format));

			addDescription(4, format);
			primitiveFlags.data.texcoord_1 = 1;
		}

		// COLOR3_0
		if (info.color0)
		{
			const auto format = info.color0.value();

			switch (format) {
			case vk::Format::eR32G32B32Sfloat:
			case vk::Format::eR8G8B8Unorm:
			case vk::Format::eR16G16B16Unorm: 
				addDescription(5, format);
				primitiveFlags.data.color_0 = 1;
				break;
			case vk::Format::eR32G32B32A32Sfloat:
			case vk::Format::eR8G8B8A8Unorm:
			case vk::Format::eR16G16B16A16Unorm:
				addDescription(6, format);
				primitiveFlags.data.color_0 = 2;
				break;
			default: assert(false);
			}
		}

		primitiveFlags.data.hasBaseColorTexture = info.hasBaseColorTexture;
		primitiveFlags.data.hasMetallicRoughnessTexture = info.hasMetallicRoughnessTexture;
		primitiveFlags.data.hasNormalTexture = info.hasNormalTexture;
		primitiveFlags.data.hasOcclusionTexture = info.hasOcclusionTexture;
		primitiveFlags.data.hasEmissiveTexture = info.hasEmissiveTexture;

		const auto vertexInputState = vk::PipelineVertexInputStateCreateInfo{}
			.setVertexBindingDescriptions(bindingDescriptions)
			.setVertexAttributeDescriptions(attributeDescriptions);

		// Input assembly
		const auto inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
			.topology = vk::PrimitiveTopology::eTriangleList,
			.primitiveRestartEnable = false
		};

		// Viewport state
		const auto viewportState = vk::PipelineViewportStateCreateInfo{
			.viewportCount = 1,
			.scissorCount = 1,
		};

		// Rasterization State
		const auto rasterizationState = vk::PipelineRasterizationStateCreateInfo{
			.polygonMode = vk::PolygonMode::eFill,
			.cullMode = vk::CullModeFlagBits::eBack,
			.lineWidth = 1.0f,
		};

		// Multisample state
		const auto multisampleState = vk::PipelineMultisampleStateCreateInfo{
			.rasterizationSamples = vk::SampleCountFlagBits::e1,
			.minSampleShading = 1.0f,
		};

		// Depth
		const auto depthStencilState = vk::PipelineDepthStencilStateCreateInfo{
			.depthTestEnable = true,
			.depthWriteEnable = true,
			.depthCompareOp = vk::CompareOp::eLess,
		};

		// Color blend state
		const auto colorBlendAttachmentState = vk::PipelineColorBlendAttachmentState{
			.colorWriteMask = ~vk::ColorComponentFlags(),
		};

		const auto colorBlendState = vk::PipelineColorBlendStateCreateInfo{
		}.setAttachments(colorBlendAttachmentState);

		// Dynamic state
		const auto dynamicStates = {
			vk::DynamicState::ePrimitiveTopology,
			vk::DynamicState::eVertexInputBindingStride,
			vk::DynamicState::eFrontFace,
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor,
		};

		const auto dynamicState = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamicStates);

		const auto SpecializationMapEntries = {
			vk::SpecializationMapEntry{
				.constantID = 0,
				.offset = 0,
				.size = sizeof(primitiveFlags.packed),
			},
		};

		const auto specializationInfo = vk::SpecializationInfo{
			.dataSize = sizeof(primitiveFlags.packed),
			.pData = &primitiveFlags.packed,
		}.setMapEntries(SpecializationMapEntries);
		//.setData(primitiveFlags.packed); TODO:

		const auto createPipelineShaderStageCreateInfo = [&](const vk::ShaderStageFlagBits stage) {
			return vk::PipelineShaderStageCreateInfo{
				.stage = stage,
				.module = *shader.getModule(),
				.pName = "main",
				.pSpecializationInfo = &specializationInfo,
			};
		};

		const auto stages = {
			createPipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment),
			createPipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex),
		};

		const auto pipelineRendering = vk::PipelineRenderingCreateInfo{}
		.setColorAttachmentFormats(surfaceFormat)
		.setDepthAttachmentFormat(depthFormat);

		const auto createInfo = vk::GraphicsPipelineCreateInfo{
			.pNext = &pipelineRendering,
			.pVertexInputState = &vertexInputState,
			.pInputAssemblyState = &inputAssemblyState,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizationState,
			.pMultisampleState = &multisampleState,
			.pDepthStencilState = &depthStencilState,
			.pColorBlendState = &colorBlendState,
			.pDynamicState = &dynamicState,
			.layout = *pipelineLayoutData.pipelineLayout,
		}.setStages(stages);

		return device.createGraphicsPipeline(nullptr, createInfo);
	};

	auto it = pipelines.find(info);
	if (it == pipelines.end()) {
		const auto& [new_it, inserted] = pipelines.emplace(info, createPipeline());
		assert(inserted);

		return new_it->second;
	}
	return it->second;
}

std::vector<Buffer> Loader::loadBuffers(const std::vector<std::span<const std::byte>>& buffers)
{
	if (not buffers.size())
	{
		return {};
	}

	transferCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	auto buffers_temp = [&] {
		const auto transform = [&](const std::span<const std::byte>& buffer) {
			const auto size = buffer.size();

			auto stagingBuffer = vma.createBuffer(
				size,
				vk::BufferUsageFlagBits::eTransferSrc,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT // TODO:  VMA_ALLOCATION_CREATE_MAPPED_BIT?
			);

			const auto result = stagingBuffer.CopyMemoryToAllocation(
				buffer.data(),
				size
			);
			assert(result == vk::Result::eSuccess);

			constexpr auto usage = vk::BufferUsageFlagBits::eVertexBuffer
				| vk::BufferUsageFlagBits::eIndexBuffer
				| vk::BufferUsageFlagBits::eTransferDst;

			auto deviceBuffer = vma.createBuffer(
				size,
				usage,
				0
			);

			const auto copyRegion = vk::BufferCopy{ .size = size };

			transferCommandBuffer.copyBuffer(*stagingBuffer, *deviceBuffer, copyRegion);

			return std::make_pair(std::move(stagingBuffer), std::move(deviceBuffer));
		};

		return buffers |
			std::views::transform(transform) |
			std::ranges::to<std::vector<std::pair<VmaBuffer, VmaBuffer>>>();
	}();

	transferCommandBuffer.end();

	const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *transferCommandBuffer };
	const auto submitInfo = vk::SubmitInfo2{}.setCommandBufferInfos(commandBufferInfo);

	transferQueue.submit2(submitInfo);
	transferQueue.waitIdle();

	return buffers_temp
		| std::views::transform([](auto& pair) { return Buffer(std::move(pair.second)); })
		| std::ranges::to<std::vector<Buffer>>();
}

Buffer Loader::createMaterialsSSBO(const std::vector<Material2>& materials)
{
	const auto& reflection = shader.getReflection();

	const auto materialFields = [&] -> std::optional<boost::json::array> {
		std::error_code ec;
		for (const auto& param : reflection.as_object().at("parameters").as_array())
		{
			if (param.as_object().at("name").as_string() == "materials")
			{
				const auto& result = param.find_pointer("/type/resultType/fields", ec)->as_array();
				assert(!ec);

				return result;
			}
		}

		return std::nullopt;
	}().value();

	const auto getOffsetAndSize = [](const boost::json::value& field) {
		const auto& binding = field.as_object().at("binding").as_object();
		const auto offset = binding.at("offset").as_int64();
		const auto size = binding.at("size").as_int64();

		return std::make_pair(offset , size);
	};

	const auto structSize = [&] {
		const auto lastField = materialFields.back();
		const auto [offset, size] = getOffsetAndSize(lastField);

		return offset + size;
	}();

	const auto bufferSize = structSize * materials.size();

	auto stagingBuffer = vma.createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferSrc,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
	);

	auto mapped = stagingBuffer.MapMemory();

	for (auto index = 0u; index < materials.size(); ++index, mapped += structSize)
	{
		const auto& material = materials[index];

		for (const auto& materialField: materialFields)
		{
			const auto setValue = [&](const auto& val, const boost::json::value& field, const size_t parentOffset = 0) {
				
				const auto [offset, size] = getOffsetAndSize(field);
				assert(sizeof(val) == size);

				auto ptr = reinterpret_cast<std::decay_t<decltype(val)>*>(mapped + parentOffset + offset);
				*ptr = val;

			};

			const auto setTexture = [&](const std::optional<Material2::TextureData>& val) {
				if (val)
				{
					const auto& value = val.value();

					std::error_code ec;
					const auto& textureFields = materialField.find_pointer("/type/fields", ec)->as_array();
					assert(!ec);

					const auto [offset, size] = getOffsetAndSize(materialField);
					for (const auto& textureField : textureFields)
					{
						const auto& name = textureField.as_object().at("name").as_string();

						if (name == "texture")
						{
							const auto textureIndex = glm::u32vec2(value.texture, 0u);
							setValue(textureIndex, textureField, offset);
						}
						else if (name == "samplerState")
						{
							const auto samplerIndex = glm::u32vec2(value.sampler, 0u);
							setValue(samplerIndex, textureField, offset);
						}
						else if (name == "uv")
						{
							setValue(value.uv, textureField, offset);
						}
						else
						{
							assert(false);
						}
					}
				}
			};

			const auto& name = materialField.as_object().at("name").as_string();

			// TODO: rework to unordered_map ?
			if (name == "baseColorFactor")
			{
				setValue(material.baseColorFactor, materialField);
			}
			else if (name == "baseColorTexture")
			{
				setTexture(material.baseColorTexture);
			}
			else if (name == "metallicRoughnessTexture")
			{
				setTexture(material.metallicRoughnessTexture);
			}
			else if (name == "normalTexture")
			{
				setTexture(material.normalTexture);
			}
			else if (name == "occlusionTexture")
			{
				setTexture(material.occlusionTexture);
			}
			else if (name == "emissiveTexture")
			{
				setTexture(material.emissiveTexture);
			}
			else if (name == "metallicFactor")
			{
				setValue(material.metallicFactor, materialField);
			}
			else if (name == "roughnessFactor")
			{
				setValue(material.roughnessFactor, materialField);
			}
			else if (name == "normalTextureScale")
			{
				setValue(material.normalTextureScale, materialField);
			}
			else if (name == "alphaCutoff")
			{
				setValue(material.alphaCutoff, materialField);
			}
			else
			{
				assert(false);
			}

		}
	}

	stagingBuffer.UnmapMemory();

	constexpr auto usage = vk::BufferUsageFlagBits::eStorageBuffer
		| vk::BufferUsageFlagBits::eTransferDst;

	auto deviceBuffer = vma.createBuffer(
		bufferSize,
		usage,
		0
	);

	transferCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	const auto copyRegion = vk::BufferCopy{ .size = bufferSize };

	transferCommandBuffer.copyBuffer(*stagingBuffer, *deviceBuffer, copyRegion);
	transferCommandBuffer.end();

	const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *transferCommandBuffer };
	const auto submitInfo = vk::SubmitInfo2{}.setCommandBufferInfos(commandBufferInfo);

	transferQueue.submit2(submitInfo);
	transferQueue.waitIdle();

	return Buffer{ .vmaBuffer = std::move(deviceBuffer) };
}

std::vector<ImageData> Loader::createImages(const std::vector<ImageInfo>& imageInfos)
{
	if (not imageInfos.size())
	{
		return {};
	}

	auto imageData = std::vector<ImageData>();
	imageData.reserve(imageInfos.size());

	auto stagingBuffers = std::vector<VmaBuffer>();
	imageData.reserve(imageInfos.size());

	transferCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	for (const auto& info: imageInfos)
	{
		const auto size = info.imageBuffer.size();

		auto stagingBuffer = vma.createBuffer(
			size,
			vk::BufferUsageFlagBits::eTransferSrc,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);

		const auto result = stagingBuffer.CopyMemoryToAllocation(
			info.imageBuffer.data(),
			size
		);
		assert(result == vk::Result::eSuccess);

		auto vmaImage = [&] {
			const auto createInfo = vk::ImageCreateInfo{
				.imageType = vk::ImageType::e2D,
				.format = info.format,
				.extent = info.extent,
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = vk::SampleCountFlagBits::e1,
				.tiling = vk::ImageTiling::eOptimal,
				.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
				.sharingMode = vk::SharingMode::eExclusive,
			};

			return vma.createImage(createInfo, 0);
		}();

		// to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
		{
			const auto imageMemoryBarrier = vk::ImageMemoryBarrier2{
				.dstStageMask = vk::PipelineStageFlagBits2::eCopy,
				.dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
				.newLayout = vk::ImageLayout::eTransferDstOptimal,
				.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
				.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
				.image = *vmaImage,
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
			.imageExtent = info.extent,
		};

		const auto copyBufferToImageInfo = vk::CopyBufferToImageInfo2
		{
			.srcBuffer = *stagingBuffer,
			.dstImage = *vmaImage,
			.dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
		}.setRegions(copyRegion);

		transferCommandBuffer.copyBufferToImage2(copyBufferToImageInfo);

		// to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		{
			const auto imageMemoryBarrier = vk::ImageMemoryBarrier2{
				.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
				.srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
				.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
				.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
				.oldLayout = vk::ImageLayout::eTransferDstOptimal,
				.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
				.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
				.image = *vmaImage,
				.subresourceRange = COLOR_SUBRESOURCE_RANGE,
			};

			const auto dependencyInfo = vk::DependencyInfo{}.setImageMemoryBarriers(imageMemoryBarrier);
			transferCommandBuffer.pipelineBarrier2(dependencyInfo);
		}

		auto imageView = [&] {
			const auto createInfo = vk::ImageViewCreateInfo{
				.image = *vmaImage,
				.viewType = vk::ImageViewType::e2D,
				.format = info.format,
				.subresourceRange = COLOR_SUBRESOURCE_RANGE,
			};

			return device.createImageView(createInfo);
		}();

		stagingBuffers.push_back(std::move(stagingBuffer));
		imageData.push_back({ .image = std::move(vmaImage), .imageView = std::move(imageView) });
	}

	transferCommandBuffer.end();

	const auto commandBufferInfo = vk::CommandBufferSubmitInfo{ .commandBuffer = *transferCommandBuffer };
	const auto submitInfo = vk::SubmitInfo2{}.setCommandBufferInfos(commandBufferInfo);
	transferQueue.submit2(submitInfo);

	transferQueue.waitIdle();

	return imageData;
}

Loader::PipelineLayoutData Loader::createPipelineLayoutData(const vk::raii::Device& device)
{
	auto descriptorSetLayout0 = [&] {
		const auto descriptorSetLayoutBindings = {
			vk::DescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = vk::DescriptorType::eStorageBuffer,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
		};

		const auto descriptorSetLayoutCreateInfo = vk::DescriptorSetLayoutCreateInfo{
		}.setBindings(descriptorSetLayoutBindings);

		return device.createDescriptorSetLayout(descriptorSetLayoutCreateInfo);
	}();

	auto descriptorSetLayout1 = [&] {
		const auto descriptorSetLayoutBindings = {
			//vk::DescriptorSetLayoutBinding{
			//	.binding = 0,
			//	.descriptorType = vk::DescriptorType::eSampler,
			//	.descriptorCount = 1, // TODO
			//	.stageFlags = vk::ShaderStageFlagBits::eFragment,
			//},
			vk::DescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 256u, // TODO
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
			//vk::DescriptorSetLayoutBinding{
			//	.binding = 2,
			//	.descriptorType = vk::DescriptorType::eSampledImage,
			//	.descriptorCount = 1, // TODO
			//	.stageFlags = vk::ShaderStageFlagBits::eFragment,
			//},
		};

		const auto flags = vk::DescriptorBindingFlagBits::ePartiallyBound
			| vk::DescriptorBindingFlagBits::eVariableDescriptorCount
			| vk::DescriptorBindingFlagBits::eUpdateAfterBind;

		const auto flagsArr = {
			flags,
			//flags,
			//flags,
		};

		const auto flagsCreateInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo{}.setBindingFlags(flagsArr);

		const auto descriptorSetLayoutCreateInfo = vk::DescriptorSetLayoutCreateInfo{
			.pNext = &flagsCreateInfo,
			.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
		}.setBindings(descriptorSetLayoutBindings);

		return device.createDescriptorSetLayout(descriptorSetLayoutCreateInfo);
	}();

	constexpr auto pushConstantRange = vk::PushConstantRange{
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		.size = sizeof(PushConstants),
	};

	const auto setLayouts = {
		*descriptorSetLayout0,
		*descriptorSetLayout1,
	};

	const auto createInfo = vk::PipelineLayoutCreateInfo{}
		.setSetLayouts(setLayouts)
		.setPushConstantRanges(pushConstantRange);

	return PipelineLayoutData{
		.pipelineLayout = device.createPipelineLayout(createInfo),
		.descriptorSetLayout = std::move(descriptorSetLayout0),
		.bindlessDescriptorSetLayout = std::move(descriptorSetLayout1)
	};
}

}